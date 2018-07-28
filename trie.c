#include "trie.h"

typedef uint_least32_t UIndex;

typedef struct {
    UIndex children[128];
    LexemKind kind;
    void *data;
} TrieNode;

struct Trie {
    TrieNode *nodes;
    size_t size;
    size_t capacity;
};

static inline
size_t
add_node(Trie *t)
{
    if (t->size == t->capacity) {
        t->nodes = ls_x2realloc0(t->nodes, &t->capacity, sizeof(TrieNode));
    }
    return t->size++;
}

Trie *
trie_new(size_t nreserve)
{
    Trie *t = LS_XNEW(Trie, 1);
    *t = (Trie) {
        .nodes = LS_XNEW0(TrieNode, nreserve),
        .size = 0,
        .capacity = nreserve,
    };
    (void) add_node(t);
    return t;
}

void
trie_insert(Trie *t, const char *key, LexemKind kind, void *data)
{
    if (!key[0]) {
        LS_PANIC("empty key");
    }
    UIndex p = 0;
    for (size_t i = 0; key[i]; ++i) {
        unsigned char c = key[i];
        if (c >= 128) {
            LS_PANIC("non-ASCII character in key");
        }
        UIndex q = t->nodes[p].children[c];
        if (!q) {
            q = add_node(t);
            t->nodes[p].children[c] = q;
        }
        p = q;
    }
    t->nodes[p].kind = kind;
    t->nodes[p].data = data;
}

LexemKind
trie_greedy_lookup(Trie *t, const char *buf, size_t nbuf, void **data, size_t *len)
{
    LexemKind kind = LEX_KIND_ERROR;
    UIndex p = 0;
    // Warning for copy-pasters: the case of empty string-key is not handled here.
    for (size_t i = 0; i < nbuf; ++i) {
        const unsigned char c = buf[i];
        if (c >= 128) {
            break;
        }
        p = t->nodes[p].children[c];
        if (!p) {
            break;
        }
        LexemKind cur_kind = t->nodes[p].kind;
        if (cur_kind != LEX_KIND_ERROR) {
            kind = cur_kind;
            *data = t->nodes[p].data;
            *len = i + 1;
        }
    }
    return kind;
}

LexemKind
trie_fixed_lookup(Trie *t, const char *key, size_t nkey, void **data)
{
    UIndex p = 0;
    for (size_t i = 0; i < nkey; ++i) {
        const unsigned char c = key[i];
        if (c >= 128) {
            return LEX_KIND_ERROR;
        }
        p = t->nodes[p].children[c];
        if (!p) {
            return LEX_KIND_ERROR;
        }
    }
    const LexemKind kind = t->nodes[p].kind;
    if (kind != LEX_KIND_ERROR) {
        *data = t->nodes[p].data;
    }
    return kind;
}

void
trie_traverse(Trie *t, void (*on_elem)(void *userdata, LexemKind kind, void *data), void *userdata)
{
    for (size_t i = 0; i < t->size; ++i) {
        TrieNode node = t->nodes[i];
        if (node.kind != LEX_KIND_ERROR) {
            on_elem(userdata, node.kind, node.data);
        }
    }
}

void
trie_destroy(Trie *t)
{
    free(t->nodes);
    free(t);
}
