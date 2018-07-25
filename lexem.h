#ifndef lexem_h_
#define lexem_h_

#include "common.h"

typedef enum {
    LEX_KIND_ERROR = 0, // required for Trie
    LEX_KIND_EOF,

    LEX_KIND_OP,
    LEX_KIND_AMBIG_OP,
    LEX_KIND_NUM,
    LEX_KIND_IDENT,
    LEX_KIND_LBRACE,
    LEX_KIND_RBRACE,
    LEX_KIND_LBRACKET,
    LEX_KIND_RBRACKET,
    LEX_KIND_COMMA,
    LEX_KIND_SEMICOLON,
    LEX_KIND_EQ,
} LexemKind;

typedef struct {
    LexemKind kind;
    void *data;
    const char *start;
    size_t size;
} Lexem;

#endif
