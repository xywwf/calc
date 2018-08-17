#ifndef vm_h_
#define vm_h_

#include "common.h"
#include "value.h"

#define VM_NARGS_BITS 5

struct Env;

typedef enum {
    CMD_PRINT,
    CMD_LOAD_SCALAR,
    CMD_LOAD_STR,
    CMD_LOAD_FAST,
    CMD_LOAD,
    CMD_LOAD_AT,
    CMD_STORE_FAST,
    CMD_STORE,
    CMD_STORE_AT,
    CMD_OP_UNARY,
    CMD_OP_BINARY,
    CMD_CALL,
    CMD_MATRIX,
    CMD_JUMP,
    CMD_JUMP_UNLESS,
    CMD_FUNCTION,
    CMD_RETURN,
    CMD_EXIT,
    CMD_QUARK,
} Command;

typedef struct {
    Command cmd;
    union {
        // CMD_LOAD_SCALAR
        Scalar scalar;

        // CMD_LOAD_STR, CMD_LOAD, CMD_STORE
        struct {
            const char *start;
            size_t size;
        } str;

        // CMD_LOAD_FAST, CMD_STORE_FAST
        unsigned index;

        // CMD_LOAD_AT, CMD_STORE_AT
        unsigned nindices;

        // CMD_OP_UNARY
        Value (*unary)(struct Env *e, Value arg);

        // CMD_OP_BINARY
        Value (*binary)(struct Env *e, Value arg1, Value arg2);

        // CMD_CALL
        unsigned nargs;

        // CMD_MATRIX
        struct {
            unsigned height;
            unsigned width;
        } dims;

        // CMD_JUMP, CMD_JUMP_UNLESS
        int offset;

        // CMD_FUNCTION
        struct {
            int offset;
            unsigned nargs   : VM_NARGS_BITS;
            unsigned nlocals : (32 - VM_NARGS_BITS);
        } func;

        // CMD_QUARK
        unsigned nline;
    } args;
} Instr;

#endif
