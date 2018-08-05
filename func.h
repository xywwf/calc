#ifndef func_h_
#define func_h_

#include "common.h"
#include "value.h"
#include "vm.h"

#include "libls/string_.h"

typedef struct {
    GcObject gchdr;
    unsigned nargs;
    char * strdups;
    size_t nchunk;
    Instr chunk[];
} Func;

Func *
func_new(unsigned nargs, const Instr *chunk, size_t nchunk);

void
func_destroy(Func *f);

#endif
