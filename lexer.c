#include "lexer.h"

struct Lexer {
    const char *cur;
    const char *last;
    const char *mark;
    Trie *opreg;
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
    return x;
}

void
lexer_reset(Lexer *x, const char *buf, size_t nbuf)
{
    x->cur = buf;
    x->last = buf + nbuf;
}

Lexem
lexer_next(Lexer *x)
{
    while (1) {
        // skip whitespace
        while (1) {
            if (x->cur == x->last) {
                return (Lexem) {.kind = LEX_KIND_EOF, .start = x->cur, .size = 0};
            }
            if (!is_whitespace(*x->cur)) {
                break;
            }
            ++x->cur;
        }
        if (*x->cur == '\\') { // an escape?
            ++x->cur;
            if (x->cur == x->last) {
                return (Lexem) {
                    .kind = LEX_KIND_ERROR,
                    .data = "escape symbol at the end of input",
                    .start = x->cur - 1,
                    .size = 1,
                };
            }
            if (*x->cur != '\n') {
                return (Lexem) {
                    .kind = LEX_KIND_ERROR,
                    .data = "invalid escape (expected newline)",
                    .start = x->cur,
                    .size = 1,
                };
            }
            ++x->cur;
        } else if (*x->cur == '#') { // a comment?
            ++x->cur;
            while (x->cur != x->last && *x->cur++ != '\n') {}
        } else {
            break;
        }
    }

    // parse next token
    Lexem r = {.start = x->cur};
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

    } else if (c == ';' || c == '\n') {
        r.kind = LEX_KIND_SEMICOLON;
        ++x->cur;

    } else if (c == '[') {
        r.kind = LEX_KIND_LBRACKET;
        ++x->cur;

    } else if (c == ']') {
        r.kind = LEX_KIND_RBRACKET;
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
