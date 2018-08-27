#include "lexer.h"

struct Lexer {
    const char *cur;
    const char *last;
    const char *mark;
    Trie *opreg;
    unsigned line;
};

static inline
bool
is_whitespace(char c)
{
    switch (c) {
    case ' ':
    case '\t':
        return true;
    default:
        return false;
    }
}

static inline
bool
is_number_start(char c)
{
    return '0' <= c && c <= '9';
}

static inline
bool
is_number_part(char c)
{
    return is_number_start(c) || c == '.';
}

static inline
bool
is_ident_start(char c)
{
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

static inline
bool
is_ident_part(char c)
{
    return is_ident_start(c) || ('0' <= c && c <= '9');
}

Lexer *
lexer_new(Trie *opreg)
{
    Lexer *x = LS_XNEW(Lexer, 1);
    *x = (Lexer) {
        .opreg = opreg,
    };

    trie_insert(opreg, "=",  LEX_KIND_EQ,       NULL);
    trie_insert(opreg, ":=", LEX_KIND_COLON_EQ, NULL);
    trie_insert(opreg, "|",  LEX_KIND_BAR,      NULL);

    return x;
}

void
lexer_reset(Lexer *x, const char *buf, size_t nbuf)
{
    x->cur = buf;
    x->last = buf + nbuf;
    x->line = 1;
}

Lexem
lexer_next(Lexer *x)
{
#define RET(Kind_, Sz_, Data_) \
    return (Lexem) { \
        .kind = Kind_, \
        .line = x->line, \
        .data = Data_, \
        .start = x->cur, \
        .size = Sz_, \
    }

    while (1) {
        // skip whitespace
        while (1) {
            if (x->cur == x->last) {
                RET(LEX_KIND_EOF, 0, NULL);
            }
            if (!is_whitespace(*x->cur)) {
                break;
            }
            ++x->cur;
        }
        if (*x->cur == '\\') { // an escape?
            ++x->cur;
            if (x->cur == x->last) {
                RET(LEX_KIND_ERROR, 1, "escape symbol at the end of input");
            }
            if (*x->cur != '\n') {
                RET(LEX_KIND_ERROR, 1, "invalid escape (expected newline)");
            }
            ++x->line;
            ++x->cur;
        } else if (*x->cur == '#') { // a comment?
            do {
                ++x->cur;
            } while (x->cur != x->last && *x->cur != '\n');
        } else {
            break;
        }
    }

    // parse next token
    Lexem r = {.line = x->line, .start = x->cur};
    char c = *x->cur;

    if (c == '(') {
        r.kind = LEX_KIND_LBRACE;
        ++x->cur;

    } else if (c == ')') {
        r.kind = LEX_KIND_RBRACE;
        ++x->cur;

    } else if (c == ',') {
        r.kind = LEX_KIND_COMMA;
        ++x->cur;

    } else if (c == ';') {
        r.kind = LEX_KIND_SEMICOLON;
        ++x->cur;

    } else if (c == '\n') {
        r.kind = LEX_KIND_SEMICOLON;
        ++x->line;
        ++x->cur;

    } else if (c == '[') {
        r.kind = LEX_KIND_LBRACKET;
        ++x->cur;

    } else if (c == ']') {
        r.kind = LEX_KIND_RBRACKET;
        ++x->cur;

    } else if (c == '"') {
        r.kind = LEX_KIND_STR;
        do {
            ++x->cur;
            if (x->cur == x->last || *x->cur == '\n') {
                RET(LEX_KIND_ERROR, 0, "unterminated string");
            }
        } while (*x->cur != '"');
        ++x->cur;

    } else if (is_number_start(c)) {
        r.kind = LEX_KIND_NUM;
        do {
            ++x->cur;
        } while (x->cur != x->last && is_number_part(*x->cur));

    } else if (is_ident_start(c)) {
        r.kind = LEX_KIND_IDENT;
        do {
            ++x->cur;
        } while (x->cur != x->last && is_ident_part(*x->cur));

        const size_t n = x->cur - r.start;

#define KEYWORD(Lit_, LexKind_) \
        do { \
            if (n == sizeof(Lit_) - 1 && memcmp(r.start, Lit_, n) == 0) { \
                r.kind = LexKind_; \
            } \
        } while (0)

        KEYWORD("if",           LEX_KIND_IF);
        KEYWORD("then",         LEX_KIND_THEN);
        KEYWORD("elif",         LEX_KIND_ELIF);
        KEYWORD("else",         LEX_KIND_ELSE);
        KEYWORD("while",        LEX_KIND_WHILE);
        KEYWORD("for",          LEX_KIND_FOR);
        KEYWORD("do",           LEX_KIND_DO);
        KEYWORD("break",        LEX_KIND_BREAK);
        KEYWORD("continue",     LEX_KIND_CONTINUE);
        KEYWORD("fu",           LEX_KIND_FU);
        KEYWORD("return",       LEX_KIND_RETURN);
        KEYWORD("exit",         LEX_KIND_EXIT);
        KEYWORD("end",          LEX_KIND_END);

#undef KEYWORD

    } else {
        size_t len;
        r.kind = trie_greedy_lookup(x->opreg, x->cur, x->last - x->cur, &r.data, &len);
        if (r.kind == LEX_KIND_ERROR) {
            r.data = "invalid character";
            ++x->cur;
        } else {
            x->cur += len;
        }
    }

    r.size = x->cur - r.start;
    return r;
#undef RET
}

void
lexer_mark(Lexer *x)
{
    x->mark = x->cur;
}

void
lexer_rollback(Lexer *x)
{
    x->cur = x->mark;
}

void
lexer_destroy(Lexer *x)
{
    free(x);
}
