#ifndef vm_h_
#define vm_h_

#include "common.h"
#include "value.h"

struct Env;

typedef enum {
    CMD_PUSH_SCALAR,
    CMD_PUSH_VAR,
    CMD_ASSIGN,
    CMD_GET_AT,
    CMD_SET_AT,
    CMD_OP_UNARY,
    CMD_OP_BINARY,
    CMD_CALL,
    CMD_MATRIX,
} Command;

typedef struct {
    Command cmd;
    union {
        // CMD_PUSH_SCALAR
        Scalar scalar;

        // CMD_PUSH_VAR, CMD_ASSIGN
        struct {
            const char *start;
            size_t size;
        } varname;

        // CMD_GET_AT, CMD_SET_AT
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
    } args;
} Instr;

#endif
