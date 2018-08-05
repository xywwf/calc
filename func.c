#include "func.h"

#include <string.h>

Func *
func_new(unsigned nargs, const Instr *chunk, size_t nchunk)
{
    Func *f = ls_xmalloc(sizeof(Func) + nchunk * sizeof(Instr), 1);
    f->gchdr.nrefs = 1;
    f->nargs = nargs;
    LS_VECTOR_INIT(f->dups);
    f->nchunk = nchunk;
    memcpy(f->chunk, chunk, nchunk * sizeof(Instr));
    for (size_t i = 0; i < nchunk; ++i) {
        switch (f->chunk[i].cmd) {
            case CMD_LOAD:
            case CMD_STORE:
            case CMD_LOCAL:
                {
                    char *a;
                    f->chunk[i].args.varname.start = a = ls_xmemdup(
                        f->chunk[i].args.varname.start, f->chunk[i].args.varname.size);
                    LS_VECTOR_PUSH(f->dups, a);
                }
                break;
            case CMD_LOAD_STR:
                {
                    char *a;
                    f->chunk[i].args.str.start = a = ls_xmemdup(
                        f->chunk[i].args.str.start, f->chunk[i].args.str.size);
                    LS_VECTOR_PUSH(f->dups, a);
                }
                break;
            default:
                break;
        }
    }
    return f;
}

void
func_destroy(Func *f)
{
    for (size_t i = 0; i < f->dups.size; ++i) {
        free(f->dups.data[i]);
    }
    LS_VECTOR_FREE(f->dups);
}
