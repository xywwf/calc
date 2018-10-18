#include "disasm.h"

void
disasm_print(const Instr *chunk, size_t nchunk)
{
#define CMDFMT "%-16s"
#define JMPFMT "%+d \t(-> %zu)"
#define JMPARG(D_) D_, i + (D_)

    for (size_t i = 0; i < nchunk; ++i) {
        Instr in = chunk[i];
        printf("%8zu | ", i);
        switch (in.cmd) {
        case CMD_PRINT:
            printf(CMDFMT "\n", "print");
            break;
        case CMD_LOAD_SCALAR:
            printf(CMDFMT "%g\n", "load_scalar", in.args.scalar);
            break;
        case CMD_LOAD_STR:
            printf(CMDFMT "%.*s\n", "load_str", (int) in.args.str.size, in.args.str.start);
            break;
        case CMD_LOAD:
            printf(CMDFMT "\"%.*s\"\n", "load", (int) in.args.str.size, in.args.str.start);
            break;
        case CMD_STORE:
            printf(CMDFMT "\"%.*s\"\n", "store", (int) in.args.str.size, in.args.str.start);
            break;
        case CMD_LOAD_FAST:
            printf(CMDFMT "%u\n", "load_fast", in.args.index);
            break;
        case CMD_STORE_FAST:
            printf(CMDFMT "%u\n", "store_fast", in.args.index);
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
            printf(CMDFMT JMPFMT "\n", "jump", JMPARG(in.args.offset));
            break;
        case CMD_JUMP_UNLESS:
            printf(CMDFMT JMPFMT "\n", "jump_unless", JMPARG(in.args.offset));
            break;
        case CMD_FUNCTION:
            printf(CMDFMT "nargs=%u, nlocals=%u, " JMPFMT "\n", "function",
                   in.args.func.nargs, in.args.func.nlocals, JMPARG(in.args.func.offset));
            break;
        case CMD_RETURN:
            printf(CMDFMT "\n", "return");
            break;
        case CMD_EXIT:
            printf(CMDFMT "\n", "exit");
            break;
        case CMD_QUARK:
            printf(CMDFMT "%u\n", "quark", in.args.nline);
            break;
        }
    }
#undef CMDFMT
}
