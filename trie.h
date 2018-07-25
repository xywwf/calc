#ifndef trie_h_
#define trie_h_

#include "common.h"
#include "lexem.h"

typedef struct Trie Trie;

Trie *
trie_new(size_t nreserve);

void
trie_insert(Trie *t, const char *key, LexemKind kind, void *data);

LexemKind
trie_greedy_lookup(Trie *t, const char *buf, size_t nbuf, void **data, size_t *len);

LexemKind
trie_fixed_lookup(Trie *t, const char *key, size_t nkey, void **data);

void
trie_destroy(Trie *t);

#endif
