#include "env.h"
#include "ht.h"

#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include <assert.h>

#include "libls/vector.h"
#include "libls/sprintf_utils.h"

#define GCC_IS_RETARDED volatile

struct Env {
    Ht *ht;
    jmp_buf err_handler;
    char *err;
};

Env *
env_new(Ht *ht)
{
    Env *e = LS_XNEW(Env, 1);
    *e = (Env) {
        .ht = ht,
    };
    return e;
}

int
env_eval(Env *e, const Instr *chunk, size_t nchunk, Value *result)
{
    bool ok = true;
    LS_VECTOR_OF(Value) stack = LS_VECTOR_NEW();

#define ERR(...) \
    do { \
        e->err = ls_xasprintf(__VA_ARGS__); \
        ok = false; \
        goto done; \
    } while (0)

#define PROTECT() \
    do { \
        if (setjmp(e->err_handler) != 0) { \
            ok = false; \
            goto done; \
        } \
    } while (0)

    for (GCC_IS_RETARDED size_t i = 0; i < nchunk; ++i) {
        Instr in = chunk[i];
        switch (in.cmd) {
        case CMD_PUSH_SCALAR:
            LS_VECTOR_PUSH(stack, ((Value) {
                .kind = VAL_KIND_SCALAR,
                .as.scalar = in.args.scalar,
            }));
            break;

        case CMD_PUSH_VAR:
            {
                Value value;
                if (!ht_get(e->ht, in.args.varname.start, in.args.varname.size, &value)) {
                    ERR("undefined variable '%.*s'",
                        (int) in.args.varname.size, in.args.varname.start);
                }
                LS_VECTOR_PUSH(stack, value);
            }
            break;

        case CMD_ASSIGN:
            {
                Value value = stack.data[stack.size - 1];
                ht_put(e->ht, in.args.varname.start, in.args.varname.size, value);
                --stack.size;
                value_unref(value);
            }
            break;

        case CMD_OP_UNARY:
            {
                Value v = stack.data[stack.size - 1];

                // <danger>
                PROTECT();
                stack.data[stack.size - 1] = in.args.unary(e, v);
                // </danger>

                value_unref(v);
            }
            break;

        case CMD_OP_BINARY:
            {
                Value v = stack.data[stack.size - 2];
                Value w = stack.data[stack.size - 1];

                // <danger>
                PROTECT();
                stack.data[stack.size - 2] = in.args.binary(e, v, w);
                // </danger>

                --stack.size;
                value_unref(v);
                value_unref(w);
            }
            break;

        case CMD_CALL:
            {
                Value *ptr = stack.data + stack.size - in.args.nargs - 1;
                GCC_IS_RETARDED Value func = ptr[0];
                if (func.kind != VAL_KIND_CFUNC) {
                    ERR("cannot call %s value", value_kindname(func.kind));
                }

                // <danger>
                PROTECT();
                Value result = func.as.cfunc(e, ptr + 1, in.args.nargs);
                // </danger>

                for (size_t i = 0; i < in.args.nargs + 1; ++i) {
                    value_unref(ptr[i]);
                }
                *ptr = result;
                stack.size -= in.args.nargs;
            }
            break;

        case CMD_MATRIX:
            {
                const size_t nelems = in.args.dims.height * in.args.dims.width;

                // <danger>
                PROTECT();
                Matrix *m = matrix_construct(
                    e,
                    stack.data + stack.size - nelems,
                    in.args.dims.height,
                    in.args.dims.width);
                // </danger>

                stack.size -= nelems;
                LS_VECTOR_PUSH(stack, ((Value) {
                    .kind = VAL_KIND_MATRIX,
                    .as = {.gcobj = (GcObject *) m},
                }));
            }
            break;
        }
    }

done:
    (void) 0;
    int ret;
    if (ok) {
        if (stack.size) {
            assert(stack.size == 1);
            *result = stack.data[0];
            ret = 1;
        } else {
            ret = 0;
        }
    } else {
        for (size_t i = 0; i < stack.size; ++i) {
            value_unref(stack.data[i]);
        }
        ret = -1;
    }
    LS_VECTOR_FREE(stack);
    return ret;
#undef ERR
#undef PROTECT
}

void
env_throw(Env *e, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    e->err = ls_xvasprintf(fmt, vl);
    va_end(vl);
    longjmp(e->err_handler, 1);
}

const char *
env_last_error(Env *e)
{
    return e->err;
}

void
env_free_last_error(Env *e)
{
    free(e->err);
}

void
env_destroy(Env *e)
{
    free(e);
}
