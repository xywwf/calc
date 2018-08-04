#ifndef vm_h_
#define vm_h_

#include "common.h"
#include "value.h"

struct Env;

typedef enum {
    CMD_PRINT,
    CMD_LOAD_SCALAR,
    CMD_LOAD,
    CMD_STORE,
    CMD_LOAD_AT,
    CMD_STORE_AT,
    CMD_OP_UNARY,
    CMD_OP_BINARY,
    CMD_CALL,
    CMD_MATRIX,
    CMD_JUMP,
    CMD_JUMP_UNLESS,
    CMD_HALT,
} Command;

typedef struct {
    Command cmd;
    union {
        // CMD_LOAD_SCALAR
        Scalar scalar;

        // CMD_LOAD, CMD_STORE
        struct {
            const char *start;
            size_t size;
        } varname;

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
        size_t pos;
    } args;
} Instr;

#endif
