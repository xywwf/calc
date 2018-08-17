#ifndef runtime_h_
#define runtime_h_

#include "common.h"

#include "op.h"
#include "value.h"
#include "lexem.h"
#include "trie.h"
#include "lexer.h"
#include "parser.h"
#include "env.h"

typedef struct {
    Trie *ops;
    Lexer *lexer;
    Parser *parser;
    Env *env;
    bool dflag;
} Runtime;

typedef enum {
    ERR_KIND_OK,
    ERR_KIND_CTIME_HAS_POS,
    ERR_KIND_CTIME_NO_POS,
    ERR_KIND_RTIME_NO_POS,
} ExecErrorKind;

typedef struct {
    ExecErrorKind kind;
    Lexem pos;
    const char *msg;
} ExecError;

Runtime
runtime_new(void);

void
runtime_reg_op(Runtime r, const char *sym, Op op);

void
runtime_reg_ambig_op(Runtime r, const char *sym, Op prefix, Op infix);

void
runtime_put(Runtime r, const char *name, Value value);

ExecError
runtime_exec(Runtime r, const char *name, const char *buf, size_t nbuf);

void
runtime_destroy(Runtime r);

#endif
