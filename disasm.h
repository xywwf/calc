#ifndef disasm_h_
#define disasm_h_

#include "common.h"
#include "vm.h"

void
disasm_print(const Instr *chunk, size_t nchunk);

#endif
