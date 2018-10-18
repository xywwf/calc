#include "func.h"
#include "vector.h"

Func *
func_new(unsigned nargs, unsigned nlocals, const char *src, const Instr *chunk, size_t nchunk)
{
    Func *f = xmalloc(sizeof(Func) + nchunk * sizeof(Instr), 1);
    f->gchdr.nrefs = 1;
    f->nargs = nargs;
    f->nlocals = nlocals;
    f->src = src ? xstrdup(src) : NULL;
    f->nchunk = nchunk;
    memcpy(f->chunk, chunk, nchunk * sizeof(Instr));

    CharVector strdups = VECTOR_NEW();

    for (size_t i = 0; i < nchunk; ++i) {
        switch (f->chunk[i].cmd) {
        case CMD_LOAD:
        case CMD_STORE:
        case CMD_LOAD_STR:
            char_vector_append(&strdups, f->chunk[i].args.str.start, f->chunk[i].args.str.size);
            break;
        default:
            break;
        }
    }

    VECTOR_SHRINK(strdups);

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
