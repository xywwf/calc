#ifndef parser_h_
#define parser_h_

#include "common.h"
#include "lexem.h"
#include "lexer.h"
#include "vm.h"

typedef struct {
    bool has_pos;
    Lexem pos;
    const char *msg;
} ParserError;

typedef struct Parser Parser;

Parser *
parser_new(Lexer *lex);

void
parser_reset(Parser *p);

bool
parser_parse(Parser *p);

const Instr *
parser_last_chunk(Parser *p, size_t *nchunk);

ParserError
parser_last_error(Parser *p);

void
parser_destroy(Parser *p);

#endif
