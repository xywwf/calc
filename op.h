#ifndef op_h_
#define op_h_

#include "common.h"

struct Env;
struct Value;

enum { OP_ASSOC_LEFT, OP_ASSOC_RIGHT };

typedef struct {
    unsigned char arity;
    unsigned char assoc;
    unsigned char priority;
    union {
        struct Value (*unary)(struct Env *e, struct Value arg);
        struct Value (*binary)(struct Env *e, struct Value arg1, struct Value arg2);
    } exec;
} Op;

typedef struct {
    Op *prefix;
    Op *infix;
} AmbigOp;

#endif
