#include "func.h"

#include <string.h>

Func *
func_new(unsigned nargs, unsigned nlocals, const char *src, const Instr *chunk, size_t nchunk)
{
    Func *f = ls_xmalloc(sizeof(Func) + nchunk * sizeof(Instr), 1);
    f->gchdr.nrefs = 1;
    f->nargs = nargs;
    f->nlocals = nlocals;
    f->src = src ? ls_xstrdup(src) : NULL;
    f->nchunk = nchunk;
    memcpy(f->chunk, chunk, nchunk * sizeof(Instr));

    LSString strdups = LS_VECTOR_NEW();

    for (size_t i = 0; i < nchunk; ++i) {
        switch (f->chunk[i].cmd) {
            case CMD_LOAD:
            case CMD_STORE:
            case CMD_LOAD_STR:
                ls_string_append_b(&strdups, f->chunk[i].args.str.start, f->chunk[i].args.str.size);
                break;
            default:
                break;
        }
    }

    LS_VECTOR_SHRINK(strdups);

    size_t offset = 0;

    for (size_t i = 0; i < nchunk; ++i) {
        switch (f->chunk[i].cmd) {
            case CMD_LOAD:
            case CMD_STORE:
            case CMD_LOAD_STR:
                f->chunk[i].args.str.start = strdups.data + offset;
                offset += f->chunk[i].args.str.size;
                break;
            default:
                break;
        }
    }

    f->strdups = strdups.data;
    return f;
}

void
func_destroy(Func *f)
{
    free(f->src);
    free(f->strdups);
}
