#ifndef lexer_h_
#define lexer_h_

#include "common.h"
#include "lexem.h"
#include "trie.h"

typedef struct Lexer Lexer;

Lexer *
lexer_new(Trie *opreg);

void
lexer_reset(Lexer *x, const char *buf, size_t nbuf);

Lexem
lexer_next(Lexer *x);

void
lexer_mark(Lexer *x);

void
lexer_rollback(Lexer *x);

void
lexer_destroy(Lexer *x);

#endif
