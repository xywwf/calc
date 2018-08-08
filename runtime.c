#include "runtime.h"

#include "disasm.h"

#include <string.h>

Runtime
runtime_new(void)
{
    Runtime r;
    r.ops = trie_new(TRIE_NRESERVE_DEFAULT);
    r.lexer = lexer_new(r.ops);
    r.parser = parser_new(r.lexer);
    r.scopes = scopes_new();
    r.env = env_new(r.scopes);
    return r;
}

void
runtime_reg_op(Runtime r, const char *sym, Op op)
{
    trie_insert(r.ops, sym, LEX_KIND_OP, ls_xmemdup(&op, sizeof(op)));
}

void
runtime_reg_ambig_op(Runtime r, const char *sym, Op prefix, Op infix)
{
    AmbigOp amb_op = {
        .prefix = ls_xmemdup(&prefix, sizeof(prefix)),
        .infix  = ls_xmemdup(&infix, sizeof(infix)),
    };
    trie_insert(r.ops, sym, LEX_KIND_AMBIG_OP, ls_xmemdup(&amb_op, sizeof(amb_op)));
}

void
runtime_put(Runtime r, const char *name, Value value)
{
    scopes_put(r.scopes, name, strlen(name), value);
}

ExecError
runtime_exec(Runtime r, const char *buf, size_t nbuf)
{
    lexer_reset(r.lexer, buf, nbuf);
    parser_reset(r.parser);
    if (!parser_parse(r.parser)) {
        ParserError err = parser_last_error(r.parser);
        return (ExecError) {
            .kind = err.has_pos ? ERR_KIND_CTIME_HAS_POS : ERR_KIND_CTIME_NO_POS,
            .pos = err.pos,
            .msg = err.msg,
        };
    }
    size_t nchunk;
    const Instr *chunk = parser_last_chunk(r.parser, &nchunk);
    if (r.dflag) {
        disasm_print(chunk, nchunk);
    } else {
        if (!env_eval(r.env, chunk, nchunk)) {
            return (ExecError) {
                .kind = ERR_KIND_RTIME_NO_POS,
                .msg = env_last_error(r.env),
            };
        }
    }
    return (ExecError) {.kind = ERR_KIND_OK};
}

static
void
destroy_op(void *userdata, LexemKind kind, void *data)
{
    (void) userdata;
    switch (kind) {
    case LEX_KIND_OP:
        free(data);
        break;
    case LEX_KIND_AMBIG_OP:
        {
            AmbigOp *amb = data;
            free(amb->prefix);
            free(amb->infix);
            free(amb);
        }
        break;
    default:
        break;
    }
}

void
runtime_destroy(Runtime r)
{
    trie_traverse(r.ops, destroy_op, NULL);
    trie_destroy(r.ops);
    lexer_destroy(r.lexer);
    parser_destroy(r.parser);
    scopes_destroy(r.scopes);
    env_destroy(r.env);
}
