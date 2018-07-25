#include "ht.h"

#include "libls/vector.h"
#include "libls/string_.h"

typedef uint_least32_t UIndex;

typedef struct {
    UIndex key_idx;
    UIndex nkey;
    Value value;
} Entry;

struct Ht {
    unsigned char rank;
    UIndex *buckets; /* nbuckets = 1 << rank */
    LS_VECTOR_OF(Entry) entries;
    LSString keys;
};

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

Ht *
ht_new(unsigned char rank)
{
    Ht *h = LS_XNEW(Ht, 1);
    *h = (Ht) {
        .rank = rank,
        .buckets = LS_XNEW(UIndex, ((UIndex) 1) << rank),
        .entries = LS_VECTOR_NEW(),
        .keys = LS_VECTOR_NEW(),
    };
    memset(h->buckets, (unsigned char) -1, sizeof(UIndex) << rank);
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

void
ht_put(Ht *h, const char *key, size_t nkey, Value value)
{
    // We will insert it anyway.
    value_ref(value);

    Entry *entries = h->entries.data;
    UIndex *buckets = h->buckets;
    char *keys = h->keys.data;

    const UIndex nbuckets = ((UIndex) 1) << h->rank;
    const UIndex mask = nbuckets - 1;
    const UIndex base = get_hash(key, nkey) & mask;

    for (UIndex bucket = base; ; ++bucket, bucket &= mask) {
        const UIndex index = buckets[bucket];
        if (index == (UIndex) -1) {
            buckets[bucket] = new_entry(h, key, nkey, value);
            grow_if_needed(h);
            return;
        } else {
            Entry e = entries[index];
            if (e.nkey == nkey && memcmp(keys + e.key_idx, key, nkey) == 0) {
                // Old value is going to get replaced.
                value_unref(e.value);
                entries[index].value = value;
                return;
            }
        }
    }
}

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
            if (e.nkey == nkey && memcmp(keys + e.key_idx, key, nkey) == 0) {
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

void
ht_destroy(Ht *h)
{
    free(h->buckets);
    for (size_t i = 0; i < h->entries.size; ++i) {
        value_unref(h->entries.data[i].value);
    }
    LS_VECTOR_FREE(h->entries);
    LS_VECTOR_FREE(h->keys);
    free(h);
}
