#include "disasm.h"

#include <stdio.h>

void
disasm_print(const Instr *chunk, size_t nchunk)
{
#define CMDFMT "%-16s"
    for (size_t i = 0; i < nchunk; ++i) {
        Instr in = chunk[i];
        printf("%8zu | ", i);
        switch (in.cmd) {
        case CMD_PRINT:
            printf(CMDFMT "\n", "print");
            break;
        case CMD_PUSH_SCALAR:
            printf(CMDFMT "%g\n", "push_scalar", in.args.scalar);
            break;
        case CMD_LOAD:
            printf(CMDFMT "\"%.*s\"\n", "load", (int) in.args.varname.size, in.args.varname.start);
            break;
        case CMD_STORE:
            printf(CMDFMT "\"%.*s\"\n", "store", (int) in.args.varname.size, in.args.varname.start);
            break;
        case CMD_LOAD_AT:
            printf(CMDFMT "%u\n", "load_at", in.args.nindices);
            break;
        case CMD_STORE_AT:
            printf(CMDFMT "%u\n", "store_at", in.args.nindices);
            break;
        case CMD_OP_UNARY:
            printf(CMDFMT "%p\n", "unary", *(void **) &in.args.unary);
            break;
        case CMD_OP_BINARY:
            printf(CMDFMT "%p\n", "binary", *(void **) &in.args.binary);
            break;
        case CMD_CALL:
            printf(CMDFMT "%u\n", "call", in.args.nargs);
            break;
        case CMD_MATRIX:
            printf(CMDFMT "%u, %u\n", "matrix", in.args.dims.height, in.args.dims.width);
            break;
        case CMD_JUMP:
            printf(CMDFMT "%zu\n", "jump", in.args.pos);
            break;
        case CMD_JUMP_UNLESS:
            printf(CMDFMT "%zu\n", "jump_unless", in.args.pos);
            break;
        case CMD_HALT:
            printf(CMDFMT "\n", "halt");
        }
    }
#undef CMDFMT
}
