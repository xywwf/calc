#include "ht.h"
#include "env.h"
#include "func.h"
#include "matrix.h"
#include "str.h"
#include "vector.h"

typedef struct {
    const Instr *site;
    size_t stackpos;
    char *src;
} Callsite;

struct Env {
    VECTOR_OF(Value) gs;    // globals storage
    Ht *gt;                 // globals table
    jmp_buf err_handler;
    char err[1024];
    void *userdata;
};

Env *
env_new(void *userdata)
{
    Env *e = XNEW(Env, 1);
    VECTOR_INIT(e->gs);
    e->gt = ht_new(6);
    e->userdata = userdata;
    return e;
}

void *
env_userdata(Env *e)
{
    return e->userdata;
}

void
env_put(Env *e, const char *name, size_t nname, Value value)
{
    value_ref(value);
    const HtValue res = ht_put(e->gt, name, nname, e->gs.size);
    if (res == e->gs.size) {
        VECTOR_PUSH(e->gs, value);
    } else {
        value_unref(e->gs.data[res]);
        e->gs.data[res] = value;
    }
}

static
void
print_stackframe(const Instr *ip, const char *src, bool first)
{
    if (!src) {
        return;
    }
    do {
        --ip;
    } while (ip->cmd != CMD_QUARK);
    fprintf(stderr, "\t%s %s at line %u\n",
            first ? "in" : "by",
            src,
            ip->args.nline);
}

bool
env_exec(Env *e, const char *src, const Instr *const chunk, size_t nchunk)
{
    (void) nchunk;
    volatile bool ok = false;
    VECTOR_OF(Value) stack = VECTOR_NEW();
    VECTOR_OF(Callsite) callstack = VECTOR_NEW();

    struct {
        Instr const *volatile ip;
        Value       *volatile stack_data;
        size_t       volatile stack_size;
        Callsite    *volatile callstack_data;
        size_t       volatile callstack_size;
    } flushed;

#define FLUSH() \
    do { \
        flushed.ip             = site; \
        flushed.stack_data     = stack.data; \
        flushed.stack_size     = stack.size; \
        flushed.callstack_data = callstack.data; \
        flushed.callstack_size = callstack.size; \
    } while (0)

    if (setjmp(e->err_handler) != 0) {
        goto stop;
    }

#define DONE() \
    do { \
        ok = true; \
        FLUSH(); \
        goto stop; \
    } while (0)

#define ERR(...) \
    do { \
        snprintf(e->err, sizeof(e->err), __VA_ARGS__); \
        FLUSH(); \
        goto stop; \
    } while (0)

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
            VECTOR_PUSH(stack, MK_SCL(in.args.scalar));
            break;

        case CMD_LOAD_STR:
            {
                Str *s = str_new_unescape(in.args.str.start + 1, in.args.str.size - 2);
                VECTOR_PUSH(stack, MK_STR(s));
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
                VECTOR_PUSH(stack, value);
            }
            break;

        case CMD_LOAD_FAST:
            {
                const size_t prev_pos = callstack.data[callstack.size - 1].stackpos;
                Value value = stack.data[prev_pos + in.args.index];

                value_ref(value);
                VECTOR_PUSH(stack, value);
            }
            break;

        case CMD_STORE:
            {
                Value value = VECTOR_POP(stack);
                const HtValue res = ht_put(e->gt, in.args.str.start, in.args.str.size, e->gs.size);
                if (res == e->gs.size) {
                    VECTOR_PUSH(e->gs, value);
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
                stack.data[index] = VECTOR_POP(stack);
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
                FLUSH();
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
                FLUSH();
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
                FLUSH();
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
                FLUSH();
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
                        FLUSH();
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

                        VECTOR_PUSH(callstack, ((Callsite) {
                            .site = site + 1,
                            .stackpos = stack.size - f->nargs,
                            .src = f->src,
                        }));

                        for (unsigned i = 0; i < f->nlocals; ++i) {
                            VECTOR_PUSH(stack, MK_NIL());
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
                const size_t nelems = xmul_mat_dims(in.args.dims.height, in.args.dims.width);

                // <danger>
                FLUSH();
                Matrix *m = matrix_construct(
                    e,
                    stack.data + stack.size - nelems,
                    in.args.dims.height,
                    in.args.dims.width);
                // </danger>

                stack.size -= nelems;
                VECTOR_PUSH(stack, MK_MAT(m));
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
                VECTOR_PUSH(stack, MK_FUNC(f));
                site += in.args.func.offset;
            }
            continue;

        case CMD_EXIT:
            if (!callstack.size) {
                ok = true;
                DONE();
            }
            VECTOR_PUSH(stack, MK_NIL());
            // fall through
        case CMD_RETURN:
            {
                Callsite prev = VECTOR_POP(callstack);
                Value result = VECTOR_POP(stack);

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

stop:
    if (ok) {
        assert(!flushed.stack_size);
        assert(!flushed.callstack_size);
    } else {
        fprintf(stderr, "Error: %s\n", e->err);

        print_stackframe(
            flushed.ip,
            flushed.callstack_data[flushed.callstack_size - 1].src,
            true);

        for (size_t i = flushed.callstack_size - 1; i; --i) {
            print_stackframe(
                flushed.callstack_data[i].site,
                flushed.callstack_data[i - 1].src,
                false);
        }
    }
    for (size_t i = 0; i < flushed.stack_size; ++i) {
        value_unref(flushed.stack_data[i]);
    }
    free(flushed.stack_data);
    free(flushed.callstack_data);
    return ok;
#undef ERR
#undef DONE
#undef FLUSH
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

void
env_destroy(Env *e)
{
    ht_destroy(e->gt);
    for (size_t i = 0; i < e->gs.size; ++i) {
        value_unref(e->gs.data[i]);
    }
    VECTOR_FREE(e->gs);
    free(e);
}
