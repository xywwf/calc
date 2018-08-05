#include "env.h"
#include "scopes.h"
#include "func.h"
#include "matrix.h"
#include "str.h"

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
    Scopes *scopes;
    jmp_buf err_handler;
    char *err;
};

Env *
env_new(Scopes *scopes)
{
    Env *e = LS_XNEW(Env, 1);
    *e = (Env) {
        .scopes = scopes,
    };
    return e;
}

#if USE_RETARDED_PRAGMAS
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wclobbered"
#endif
bool
env_eval(Env *e, const Instr *chunk, size_t nchunk)
{
    (void) nchunk;
    Scopes *scopes = e->scopes;
    bool ok = true;
    LS_VECTOR_OF(Value) stack = LS_VECTOR_NEW();
    LS_VECTOR_OF(const Instr *) callstack = LS_VECTOR_NEW();

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

    (void) nchunk;
    while (1) {
        Instr in = *chunk;
        switch (in.cmd) {
        case CMD_PRINT:
            {
                Value v = stack.data[stack.size - 1];
                value_print(v);

                value_unref(v);
                --stack.size;
            }
            break;

        case CMD_LOAD_SCALAR:
            LS_VECTOR_PUSH(stack, ((Value) {
                .kind = VAL_KIND_SCALAR,
                .as.scalar = in.args.scalar,
            }));
            break;

        case CMD_LOAD_STR:
            {
                Str *s = str_new(in.args.str.start + 1, in.args.str.size - 2);
                LS_VECTOR_PUSH(stack, ((Value) {
                    .kind = VAL_KIND_STR,
                    .as.gcobj = (GcObject *) s,
                }));
            }
            break;

        case CMD_LOAD:
            {
                Value value;
                if (!scopes_get(scopes, in.args.varname.start, in.args.varname.size, &value)) {
                    ERR("undefined variable '%.*s'",
                        (int) in.args.varname.size, in.args.varname.start);
                }
                LS_VECTOR_PUSH(stack, value);
            }
            break;

        case CMD_STORE:
            {
                Value value = stack.data[stack.size - 1];
                scopes_put(scopes, in.args.varname.start, in.args.varname.size, value);
                --stack.size;
                value_unref(value);
            }
            break;

        case CMD_LOCAL:
            {
                Value value = stack.data[stack.size - 1];
                scopes_put_local(scopes, in.args.varname.start, in.args.varname.size, value);
                --stack.size;
                value_unref(value);
            }
            break;

        case CMD_LOAD_AT:
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

        case CMD_STORE_AT:
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

#if USE_RETARDED_PRAGMAS
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
                Value func = ptr[0];
#if USE_RETARDED_PRAGMAS
#   pragma GCC diagnostic pop
#endif

                switch (func.kind) {
                case VAL_KIND_CFUNC:
                    {
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

                case VAL_KIND_FUNC:
                    {
                        Func *f = (Func *) func.as.gcobj;
                        if (in.args.nargs != f->nargs) {
                            ERR("wrong # of arguments");
                        }
                        scopes_push(scopes);
                        LS_VECTOR_PUSH(callstack, chunk);
                        chunk = f->chunk;
                    }
                    continue;

                default:
                    ERR("cannot call %s value", value_kindname(func.kind));
                }
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

        case CMD_JUMP:
            chunk += in.args.offset;
            continue;

        case CMD_JUMP_UNLESS:
            {
                Value condition = stack.data[stack.size - 1];
                if (!value_is_truthy(condition)) {
                    chunk += in.args.offset;
                } else {
                    ++chunk;
                }

                --stack.size;
                value_unref(condition);
            }
            continue;

        case CMD_FUNCTION:
            {
                Func *f = func_new(
                    in.args.func.nargs,
                    chunk + 1,
                    in.args.func.offset - 1);

                LS_VECTOR_PUSH(stack, ((Value) {
                    .kind = VAL_KIND_FUNC,
                    .as = {.gcobj = (GcObject *) f},
                }));

                chunk += in.args.func.offset;
            }
            continue;

        case CMD_RETURN:
            {
                scopes_pop(scopes);
                value_unref(stack.data[stack.size - 2]);
                stack.data[stack.size - 2] = stack.data[stack.size - 1];
                --stack.size;
                chunk = callstack.data[callstack.size - 1] + 1;
                --callstack.size;
            }
            continue;

        case CMD_EXIT:
            {
                if (callstack.size) {
                    LS_VECTOR_PUSH(stack, ((Value) {
                        .kind = VAL_KIND_SCALAR,
                        .as = {.scalar = 0},
                    }));
                    scopes_pop(scopes);
                    value_unref(stack.data[stack.size - 2]);
                    stack.data[stack.size - 2] = stack.data[stack.size - 1];
                    --stack.size;
                    chunk = callstack.data[callstack.size - 1] + 1;
                    --callstack.size;
                } else {
                    goto done;
                }
            }
            continue;
        }

        ++chunk;
    }

done:
    for (size_t i = 0; i < stack.size; ++i) {
        value_unref(stack.data[i]);
    }
    LS_VECTOR_FREE(stack);
    LS_VECTOR_FREE(callstack);
    return ok;
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
