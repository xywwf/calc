#include "func.h"

#include <string.h>

Func *
func_new(unsigned nargs, unsigned nlocals, const Instr *chunk, size_t nchunk)
{
    Func *f = ls_xmalloc(sizeof(Func) + nchunk * sizeof(Instr), 1);
    f->gchdr.nrefs = 1;
    f->nargs = nargs;
    f->nlocals = nlocals;
    f->nchunk = nchunk;
    memcpy(f->chunk, chunk, nchunk * sizeof(Instr));

    LSString strdups = LS_VECTOR_NEW();

#define XPAND_FOR_STR() \
    for (size_t i = 0; i < nchunk; ++i) { \
        switch (f->chunk[i].cmd) { \
            case CMD_LOAD: \
            case CMD_STORE: \
            case CMD_LOAD_STR: \
                X(f->chunk[i].args.str.start, f->chunk[i].args.str.size); \
                break; \
            default: \
                break; \
        } \
    }

#define X(S_, NS_) ls_string_append_b(&strdups, S_, NS_)
    XPAND_FOR_STR()
#undef X

    LS_VECTOR_SHRINK(strdups);

    size_t offset = 0;
#define X(S_, NS_) S_ = strdups.data + offset, offset += NS_
    XPAND_FOR_STR()
#undef X

    f->strdups = strdups.data;
    return f;
#undef XPAND_FOR_STR
}

void
func_destroy(Func *f)
{
    free(f->strdups);
}
