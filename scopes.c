#include "scopes.h"

#include "libls/vector.h"
#include "libls/string_.h"

typedef uint_least32_t UIndex;

typedef struct {
    UIndex key_idx;
    UIndex nkey;
    Value value;
} Entry;

typedef struct {
    unsigned char rank;
    UIndex *buckets; /* nbuckets = 1 << rank */
    LS_VECTOR_OF(Entry) entries;
    LSString keys;
} Ht;

static
UIndex
get_hash(const char *key, size_t nkey)
{
    // DJBX33A
    UIndex ret = 5381;
    for (size_t i = 0; i < nkey; ++i) {
        ret = ret * 33 + (unsigned char) key[i];
    }
    return ret;
}

static
Ht
ht_new(unsigned char rank)
{
    Ht h = {
        .rank = rank,
        .buckets = LS_XNEW(UIndex, ((UIndex) 1) << rank),
        .entries = LS_VECTOR_NEW(),
        .keys = LS_VECTOR_NEW(),
    };
    memset(h.buckets, (unsigned char) -1, sizeof(UIndex) << rank);
    return h;
}

static inline
void
grow_if_needed(Ht *h)
{
    const size_t nentries = h->entries.size;
    const size_t old_nbuckets = ((size_t) 1) << h->rank;
    if (nentries * 3 < old_nbuckets * 2) {
        return;
    }

    const unsigned char rank = ++h->rank;
    const UIndex nbuckets = ((UIndex) 1) << rank;
    const UIndex mask = nbuckets - 1;
    UIndex *buckets = h->buckets = ls_xrealloc(h->buckets, nbuckets, sizeof(UIndex));
    memset(buckets, (unsigned char) -1, sizeof(UIndex) << rank);

    Entry *entries = h->entries.data;
    char *keys = h->keys.data;

    for (size_t i = 0; i < nentries; ++i) {
        // re-insert
        Entry e = entries[i];
        const UIndex base = get_hash(keys + e.key_idx, e.nkey) & mask;
        for (UIndex bucket = base; ; ++bucket, bucket &= mask) {
            if (buckets[bucket] == (UIndex) -1) {
                buckets[bucket] = i;
                break;
            }
        }
    }
}

static inline
UIndex
new_entry(Ht *h, const char *key, size_t nkey, Value value)
{
    const UIndex prev_sz = h->keys.size;
    ls_string_append_b(&h->keys, key, nkey);
    LS_VECTOR_PUSH(h->entries, ((Entry) {
        .key_idx = prev_sz,
        .nkey = nkey,
        .value = value,
    }));
    return h->entries.size - 1;
}

static
bool
ht_put(Ht *h, const char *key, size_t nkey, Value value, bool add_new)
{
    Entry *entries = h->entries.data;
    UIndex *buckets = h->buckets;
    char *keys = h->keys.data;

    const UIndex nbuckets = ((UIndex) 1) << h->rank;
    const UIndex mask = nbuckets - 1;
    const UIndex base = get_hash(key, nkey) & mask;

    for (UIndex bucket = base; ; ++bucket, bucket &= mask) {
        const UIndex index = buckets[bucket];
        if (index == (UIndex) -1) {
            if (!add_new) {
                return false;
            }
            value_ref(value);
            buckets[bucket] = new_entry(h, key, nkey, value);
            grow_if_needed(h);
            return true;
        } else {
            Entry e = entries[index];
            if (e.nkey == nkey && nkey && memcmp(keys + e.key_idx, key, nkey) == 0) {
                value_unref(e.value);
                value_ref(value);
                entries[index].value = value;
                return true;
            }
        }
    }
}

static
bool
ht_get(Ht *h, const char *key, size_t nkey, Value *result)
{
    Entry *entries = h->entries.data;
    UIndex *buckets = h->buckets;
    char *keys = h->keys.data;

    const UIndex nbuckets = ((UIndex) 1) << h->rank;
    const UIndex mask = nbuckets - 1;
    const UIndex base = get_hash(key, nkey) & mask;

    UIndex bucket = base;
    do {
        const UIndex index = buckets[bucket];
        if (index == (UIndex) -1) {
            return false;
        } else {
            Entry e = entries[index];
            if (e.nkey == nkey && nkey && memcmp(keys + e.key_idx, key, nkey) == 0) {
                // New copy is made
                value_ref(e.value);
                *result = e.value;
                return true;
            }
        }
        ++bucket;
        bucket &= mask;
    } while (bucket != base);

    return false;
}

static
void
ht_destroy(Ht *h)
{
    free(h->buckets);
    for (size_t i = 0; i < h->entries.size; ++i) {
        value_unref(h->entries.data[i].value);
    }
    LS_VECTOR_FREE(h->entries);
    LS_VECTOR_FREE(h->keys);
}

struct Scopes {
    LS_VECTOR_OF(Ht) hts;
};

Scopes *
scopes_new(void)
{
    Scopes *s = LS_XNEW(Scopes, 1);
    *s = (Scopes) {
        .hts = LS_VECTOR_NEW(),
    };
    LS_VECTOR_PUSH(s->hts, ht_new(6));
    return s;
}

void
scopes_push(Scopes *s)
{
    LS_VECTOR_PUSH(s->hts, ht_new(2));
}

void
scopes_pop(Scopes *s)
{
    ht_destroy(&s->hts.data[--s->hts.size]);
    assert(s->hts.size); // it is illegal to pop the global scope
}

void
scopes_put_local(Scopes *s, const char *key, size_t nkey, Value value)
{
    ht_put(&s->hts.data[s->hts.size - 1], key, nkey, value, true);
}

void
scopes_put(Scopes *s, const char *key, size_t nkey, Value value)
{
    for (size_t i = s->hts.size - 1; i; --i) {
        if (ht_put(&s->hts.data[i], key, nkey, value, false)) {
            return;
        }
    }
    ht_put(&s->hts.data[0], key, nkey, value, true);
}

bool
scopes_get(Scopes *s, const char *key, size_t nkey, Value *result)
{
    for (size_t i = s->hts.size - 1; i != (size_t) -1; --i) {
        if (ht_get(&s->hts.data[i], key, nkey, result)) {
            return true;
        }
    }
    return false;
}

void
scopes_destroy(Scopes *s)
{
    for (size_t i = 0; i < s->hts.size; ++i) {
        ht_destroy(&s->hts.data[i]);
    }
    LS_VECTOR_FREE(s->hts);
    free(s);
}
