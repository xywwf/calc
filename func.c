#include "func.h"

#include <string.h>

Func *
func_new(unsigned nargs, const Instr *chunk, size_t nchunk)
{
    Func *f = ls_xmalloc(sizeof(Func) + nchunk * sizeof(Instr), 1);
    f->gchdr.nrefs = 1;
    f->nargs = nargs;
    f->nchunk = nchunk;
    memcpy(f->chunk, chunk, nchunk * sizeof(Instr));
    return f;
}
