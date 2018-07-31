#include "env.h"
#include "ht.h"

#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include <assert.h>

#include "libls/vector.h"
#include "libls/sprintf_utils.h"

#if defined(__GNUC_PATCHLEVEL__) && !defined(__clang_patchlevel__)
#   define USE_RETARDED_PRAGMAS 1
#else
#   define USE_RETARDED_PRAGMAS 0
#endif

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

#if USE_RETARDED_PRAGMAS
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wclobbered"
#endif
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

    for (size_t i = 0; i < nchunk; ++i) {
        Instr in = chunk[i];
        switch (in.cmd) {
        case CMD_PUSH_SCALAR:
            DEBUG("\tCMD_PUSH_SCALAR\t%.15g\n", in.args.scalar);
            LS_VECTOR_PUSH(stack, ((Value) {
                .kind = VAL_KIND_SCALAR,
                .as.scalar = in.args.scalar,
            }));
            break;

        case CMD_PUSH_VAR:
            DEBUG("\tCMD_PUSH_VAR\t%.*s\n", (int) in.args.varname.size, in.args.varname.start);
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
            DEBUG("\tCMD_ASSIGN\t%.*s\n", (int) in.args.varname.size, in.args.varname.start);
            {
                Value value = stack.data[stack.size - 1];
                ht_put(e->ht, in.args.varname.start, in.args.varname.size, value);
                --stack.size;
                value_unref(value);
            }
            break;

        case CMD_GET_AT:
            DEBUG("\tCMD_GET_AT\t%u\n", in.args.nindices);
            {
                const unsigned nindices = in.args.nindices;
                Value *ptr = stack.data + stack.size - nindices - 1;
                Value container = ptr[0];
                if (container.kind != VAL_KIND_MATRIX) {
                    ERR("cannot index %s value", value_kindname(container.kind));
                }
                if (nindices > 2) {
                    ERR("number of indices is greater than 2");
                }
                Matrix *mat = (Matrix *) container.as.gcobj;

                // <danger>
                PROTECT();
                Value result = nindices == 1
                    ? matrix_get1(e, mat, ptr[1])
                    : matrix_get2(e, mat, ptr[1], ptr[2]);
                // </danger>

                for (size_t i = 0; i < nindices + 1; ++i) {
                    value_unref(ptr[i]);
                }
                *ptr = result;
                stack.size -= nindices;
            }
            break;

        case CMD_SET_AT:
            DEBUG("\tCMD_SET_AT\t%u\n", in.args.nindices);
            {
                const unsigned nindices = in.args.nindices;
                Value *ptr = stack.data + stack.size - nindices - 2;
                Value container = ptr[0];
                if (container.kind != VAL_KIND_MATRIX) {
                    ERR("cannot index %s value", value_kindname(container.kind));
                }
                if (nindices > 2) {
                    ERR("number of indices is greater than 2");
                }
                Matrix *mat = (Matrix *) container.as.gcobj;

                // <danger>
                PROTECT();
                if (nindices == 1) {
                    matrix_set1(e, mat, ptr[1], ptr[2]);
                } else {
                    matrix_set2(e, mat, ptr[1], ptr[2], ptr[3]);
                }
                // </danger>

                for (size_t i = 0; i < nindices + 2; ++i) {
                    value_unref(ptr[i]);
                }
                stack.size -= nindices + 2;
            }
            break;

        case CMD_OP_UNARY:
            DEBUG("\tCMD_OP_UNARY\t<...>\n");
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
            DEBUG("\tCMD_OP_BINARY\t<...>\n");
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
            DEBUG("\tCMD_CALL\t%u\n", in.args.nargs);
            {
                Value *ptr = stack.data + stack.size - in.args.nargs - 1;

#if USE_RETARDED_PRAGMAS
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
                Value func = ptr[0];
#if USE_RETARDED_PRAGMAS
#   pragma GCC diagnostic pop
#endif

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
            DEBUG("\tCMD_MATRIX\t%u %u\n", in.args.dims.height, in.args.dims.width);
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
#if LS_HAS_BUILTIN_UNREACHABLE
        default:
            __builtin_unreachable();
#endif
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
#if USE_RETARDED_PRAGMAS
#   pragma GCC diagnostic pop
#endif

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
