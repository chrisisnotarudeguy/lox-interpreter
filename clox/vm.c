#include "vm.h"

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include "compiler.h"
#include "disassemble.h"
#include "object.h"
#include "memory.h"

#define DEBUG_TRACE_EXECUTION

VM vm;

static void print_stack()
{
    printf("stack: ");
    for (Value *p = vm.stack; p < vm.sp; p++) {
        printf("[");
        value_print(*p);
        printf("]");
    }
    printf("\n");
}

static Value peek(int dist)
{
    return vm.sp[-1 - dist];
}

static bool is_falsey(Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concat()
{
    ObjString *b = AS_STRING(vm_pop());
    ObjString *a = AS_STRING(vm_pop());
    size_t len = a->len + b->len;
    char *data = ALLOCATE(char, len+1);
    memcpy(data,          a->data, a->len);
    memcpy(data + a->len, b->data, b->len);
    data[len] = '\0';
    ObjString *result = take_string(data, len);
    vm_push(VALUE_MKOBJ(result));
}

static void reset_stack()
{
    vm.sp = vm.stack;
}

static void runtime_error(const char *fmt, ...)
{
    size_t instr = vm.ip - vm.chunk->code - 1;
    int line = vm.chunk->lines[instr];
    fprintf(stderr, "%s:%d: runtime error: ", vm.filename, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputs("\n", stderr);
    reset_stack();
}

static VMResult run()
{
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(value_type, op) \
    do {                    \
        if (!IS_NUM(peek(0)) || !IS_NUM(peek(1))) { \
            runtime_error("operands must be numbers"); \
            return VM_RUNTIME_ERROR; \
        } \
        double b = AS_NUM(vm_pop()); \
        double a = AS_NUM(vm_pop()); \
        vm_push(value_type(a op b));    \
    } while (0)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        print_stack();
        disassemble_opcode(vm.chunk, (size_t)(vm.ip - vm.chunk->code));
        printf("\n");
#endif
        u8 instr = READ_BYTE();
        switch (instr) {
        case OP_CONSTANT: {
            Value constant = READ_CONSTANT();
            vm_push(constant);
            break;
        }
        case OP_NIL:    vm_push(VALUE_MKNIL());       break;
        case OP_TRUE:   vm_push(VALUE_MKBOOL(true));  break;
        case OP_FALSE:  vm_push(VALUE_MKBOOL(false));     break;
        case OP_EQ: {
            Value b = vm_pop();
            Value a = vm_pop();
            vm_push(VALUE_MKBOOL(value_equal(a, b)));
            break;
        }
        case OP_GREATER: BINARY_OP(VALUE_MKBOOL, >); break;
        case OP_ADD:
            if (IS_STRING(peek(0)) && IS_STRING(peek(1)))
                concat();
            else if (IS_NUM(peek(0)) && IS_NUM(peek(1))) {
                double b = AS_NUM(vm_pop());
                double a = AS_NUM(vm_pop());
                vm_push(VALUE_MKNUM(a + b));
            } else {
                runtime_error("operands must be two numbers or two strings");
                return VM_RUNTIME_ERROR;
            }
            break;
        case OP_SUB:    BINARY_OP(VALUE_MKNUM, -); break;
        case OP_MUL:    BINARY_OP(VALUE_MKNUM, *); break;
        case OP_DIV:    BINARY_OP(VALUE_MKNUM, /); break;
        case OP_NOT:
            vm_push(VALUE_MKBOOL(is_falsey(vm_pop())));
            break;
        case OP_NEGATE:
            if (!IS_NUM(peek(0))) {
                runtime_error("operand must be a number");
                return VM_RUNTIME_ERROR;
            }
            vm_push(VALUE_MKNUM(-AS_NUM(vm_pop())));
            break;
        case OP_RETURN:
            value_print(vm_pop());
            printf("\n");
            return VM_OK;
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

void vm_init()
{
    reset_stack();
    vm.objects = NULL;
}

void vm_free()
{
    free_objects(vm.objects);
}

VMResult vm_interpret(const char *src, const char *filename)
{
    Chunk chunk;
    chunk_init(&chunk);
    if (!compile(src, &chunk, filename)) {
        chunk_free(&chunk);
        return VM_COMPILE_ERROR;
    }
    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;
    vm.filename = filename;
    printf("=== running VM ===\n");
    VMResult result = run();
    chunk_free(&chunk);
    return result;
}

void vm_push(Value value)
{
    *vm.sp++ = value;
}

Value vm_pop()
{
    return *--vm.sp;
}

