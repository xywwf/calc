#include "ht.h"
#include "env.h"
#include "func.h"
#include "matrix.h"
#include "str.h"

#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include <assert.h>

#include <stdio.h>

#include "libls/vector.h"

typedef struct {
    const Instr *site;
    size_t stackpos;
    char *src;
} Callsite;

struct Env {
    LS_VECTOR_OF(Value) gs;
    Ht *gt;
    jmp_buf err_handler;
    char err[1024];
};

Env *
env_new(void)
{
    Env *e = LS_XNEW(Env, 1);
    *e = (Env) {
        .gs = LS_VECTOR_NEW(),
        .gt = ht_new(6),
    };
    return e;
}

void
env_put(Env *e, const char *name, size_t nname, Value value)
{
    value_ref(value);
    const HtValue res = ht_put(e->gt, name, nname, e->gs.size);
    if (res == e->gs.size) {
        LS_VECTOR_PUSH(e->gs, value);
    } else {
        value_unref(e->gs.data[res]);
        e->gs.data[res] = value;
    }
}

bool
env_eval(Env *e, const char *src, const Instr *const chunk, size_t nchunk)
{
    (void) nchunk;
    volatile bool ok = false;
    LS_VECTOR_OF(Value) stack = LS_VECTOR_NEW();
    LS_VECTOR_OF(Callsite) callstack = LS_VECTOR_NEW();

    Instr const *volatile ip = NULL;
    Value       *volatile data1;
    size_t       volatile size1;
    Callsite    *volatile data2;
    size_t       volatile size2;

#define FLUSH() \
    do { \
        ip    = site; \
        data1 = stack.data; \
        size1 = stack.size; \
        data2 = callstack.data; \
        size2 = callstack.size; \
    } while (0)

#define DONE() \
    do { \
        FLUSH(); \
        goto do_not_goto_me; /* this is OK: the "implementation" is allowed to goto this label */ \
    } while (0)

    if (setjmp(e->err_handler) != 0) {
        goto do_not_goto_me; // this is OK: the "implementation" is allowed to goto this label
    }

#define ERR(...) \
    do { \
        snprintf(e->err, sizeof(e->err), __VA_ARGS__); \
        DONE(); \
    } while (0)

#define PROTECT FLUSH

    for (const Instr *site = chunk; ;) {
        Instr in = *site;
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
            LS_VECTOR_PUSH(stack, MK_SCL(in.args.scalar));
            break;

        case CMD_LOAD_STR:
            {
                Str *s = str_new_unescape(in.args.str.start + 1, in.args.str.size - 2);
                LS_VECTOR_PUSH(stack, MK_STR(s));
            }
            break;

        case CMD_LOAD:
            {
                HtValue index = ht_get(e->gt, in.args.str.start, in.args.str.size);
                if (index == HT_NO_VALUE) {
                    ERR("undefined variable '%.*s'", (int) in.args.str.size, in.args.str.start);
                }
                Value value = e->gs.data[index];
                value_ref(value);
                LS_VECTOR_PUSH(stack, value);
            }
            break;

        case CMD_LOAD_FAST:
            {
                const size_t prev_pos = callstack.data[callstack.size - 1].stackpos;
                Value value = stack.data[prev_pos + in.args.index];

                value_ref(value);
                LS_VECTOR_PUSH(stack, value);
            }
            break;

        case CMD_STORE:
            {
                Value value = stack.data[--stack.size];
                const HtValue res = ht_put(e->gt, in.args.str.start, in.args.str.size, e->gs.size);
                if (res == e->gs.size) {
                    LS_VECTOR_PUSH(e->gs, value);
                } else {
                    value_unref(e->gs.data[res]);
                    e->gs.data[res] = value;
                }
            }
            break;

        case CMD_STORE_FAST:
            {
                const size_t prev_pos = callstack.data[callstack.size - 1].stackpos;
                const size_t index = prev_pos + in.args.index;
                value_unref(stack.data[index]);
                stack.data[index] = stack.data[--stack.size];
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
                Matrix *mat = AS_MAT(container);

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
                Matrix *mat = AS_MAT(container);

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
                Value func = ptr[0];
                switch (func.kind) {
                case VAL_KIND_CFUNC:
                    {
                        // <danger>
                        PROTECT();
                        Value result = AS_CFUNC(func)(e, ptr + 1, in.args.nargs);
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
                        Func *f = AS_FUNC(func);
                        if (in.args.nargs != f->nargs) {
                            ERR("wrong number of arguments");
                        }

                        LS_VECTOR_PUSH(callstack, ((Callsite) {
                            .site = site + 1,
                            .stackpos = stack.size - f->nargs,
                            .src = f->strdups,
                        }));

                        for (unsigned i = 0; i < f->nlocals; ++i) {
                            LS_VECTOR_PUSH(stack, MK_NIL());
                        }

                        site = f->chunk;
                    }
                    continue;

                default:
                    ERR("cannot call %s value", value_kindname(func.kind));
                }
            }
            break;

        case CMD_MATRIX:
            {
                const size_t nelems = (size_t) in.args.dims.height * in.args.dims.width;

                // <danger>
                PROTECT();
                Matrix *m = matrix_construct(
                    e,
                    stack.data + stack.size - nelems,
                    in.args.dims.height,
                    in.args.dims.width);
                // </danger>

                stack.size -= nelems;
                LS_VECTOR_PUSH(stack, MK_MAT(m));
            }
            break;

        case CMD_JUMP:
            site += in.args.offset;
            continue;

        case CMD_JUMP_UNLESS:
            {
                Value condition = stack.data[stack.size - 1];
                if (!value_is_truthy(condition)) {
                    site += in.args.offset;
                } else {
                    ++site;
                }

                --stack.size;
                value_unref(condition);
            }
            continue;

        case CMD_FUNCTION:
            {
                Func *f = func_new(
                    in.args.func.nargs,
                    in.args.func.nlocals,
                    callstack.size ? callstack.data[callstack.size - 1].src : src,
                    site + 1,
                    in.args.func.offset - 1);
                LS_VECTOR_PUSH(stack, MK_FUNC(f));
                site += in.args.func.offset;
            }
            continue;

        case CMD_EXIT:
            if (!callstack.size) {
                ok = true;
                DONE();
            }
            LS_VECTOR_PUSH(stack, MK_NIL());
            // fall through
        case CMD_RETURN:
            {
                Callsite prev = callstack.data[--callstack.size];

                Value result = stack.data[--stack.size];

                for (size_t i = prev.stackpos - 1; i < stack.size; ++i) {
                    value_unref(stack.data[i]);
                }

                stack.data[prev.stackpos - 1] = result;
                stack.size = prev.stackpos;

                site = prev.site;
            }
            continue;

        case CMD_QUARK:
            // do nothing
            break;
        }

        ++site;
    }

do_not_goto_me:
    if (ok) {
        assert(!size1);
        assert(!size2);
    } else {
        do {
            --ip;
        } while (ip->cmd != CMD_QUARK);
        fprintf(stderr, "  Error in %s at line %u\n", data2[size2 - 1].src, ip->args.nline);

        for (size_t i = size2 - 1; i; --i) {
            const Instr *ptr = data2[i].site;
            do {
                --ptr;
            } while (ptr->cmd != CMD_QUARK);
            fprintf(stderr, " called by %s at line %u\n", data2[i - 1].src, ptr->args.nline);
        }
    }
    for (size_t i = 0; i < size1; ++i) {
        value_unref(data1[i]);
    }
    free(data1);
    free(data2);
    return ok;
#undef ERR
#undef DONE
#undef FLUSH
#undef PROTECT
}

void
env_throw(Env *e, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    vsnprintf(e->err, sizeof(e->err), fmt, vl);
    va_end(vl);
    longjmp(e->err_handler, 1);
}

const char *
env_last_error(Env *e)
{
    return e->err;
}

void
env_destroy(Env *e)
{
    ht_destroy(e->gt);
    for (size_t i = 0; i < e->gs.size; ++i) {
        value_unref(e->gs.data[i]);
    }
    LS_VECTOR_FREE(e->gs);
    free(e);
}
