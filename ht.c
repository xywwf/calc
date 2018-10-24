#include "ht.h"
#include "vector.h"

typedef uint32_t UIndex;

typedef struct {
    UIndex key_idx;
    UIndex nkey;
    HtValue value;
} Entry;

struct Ht {
    unsigned char rank;
    UIndex *buckets; /* nbuckets = 1 << rank */
    VECTOR_OF(Entry) entries;
    CharVector keys;
};

static inline
size_t
zu_scalbn(size_t a, unsigned char b)
{
    return a << b;
}

static
uint32_t
get_hash(const char *key, size_t nkey)
{
    // 32-bit FNV-1a
    uint32_t ret = 2166136261u;
    for (size_t i = 0; i < nkey; ++i) {
        ret ^= (unsigned char) key[i];
        ret *= 16777619;
    }
    return ret;
}

Ht *
ht_new(unsigned char rank)
{
    Ht *h = XNEW(Ht, 1);
    *h = (Ht) {
        .rank = rank,
        .buckets = XNEW(UIndex, zu_scalbn(1, rank)),
        .entries = VECTOR_NEW(),
        .keys = VECTOR_NEW(),
    };
    memset(h->buckets, (unsigned char) -1, zu_scalbn(sizeof(UIndex), rank));
    return h;
}

static inline
void
grow_if_needed(Ht *h)
{
    const size_t nentries = h->entries.size;
    const size_t old_nbuckets = zu_scalbn(1, h->rank);
    if (nentries * 3 < old_nbuckets * 2) {
        return;
    }

    const unsigned char rank = ++h->rank;
    const UIndex nbuckets = zu_scalbn(1, rank);
    const UIndex mask = nbuckets - 1;
    UIndex *buckets = h->buckets = xrealloc(h->buckets, nbuckets, sizeof(UIndex));
    memset(buckets, (unsigned char) -1, zu_scalbn(sizeof(UIndex), rank));

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
new_entry(Ht *h, const char *key, size_t nkey, HtValue value)
{
    const UIndex prev_sz = h->keys.size;
    char_vector_append(&h->keys, key, nkey);
    VECTOR_PUSH(h->entries, ((Entry) {
        .key_idx = prev_sz,
        .nkey = nkey,
        .value = value,
    }));
    return h->entries.size - 1;
}

HtValue
ht_put(Ht *h, const char *key, size_t nkey, HtValue value)
{
    Entry *entries = h->entries.data;
    UIndex *buckets = h->buckets;
    char *keys = h->keys.data;

    const UIndex nbuckets = zu_scalbn(1, h->rank);
    const UIndex mask = nbuckets - 1;
    const UIndex base = get_hash(key, nkey) & mask;

    for (UIndex bucket = base; ; ++bucket, bucket &= mask) {
        const UIndex index = buckets[bucket];
        if (index == (UIndex) -1) {
            buckets[bucket] = new_entry(h, key, nkey, value);
            grow_if_needed(h);
            return value;
        } else {
            Entry e = entries[index];
            if (e.nkey == nkey && nkey && memcmp(keys + e.key_idx, key, nkey) == 0) {
                return e.value;
            }
        }
    }
}

HtValue
ht_get(Ht *h, const char *key, size_t nkey)
{
    Entry *entries = h->entries.data;
    UIndex *buckets = h->buckets;
    char *keys = h->keys.data;

    const UIndex nbuckets = zu_scalbn(1, h->rank);
    const UIndex mask = nbuckets - 1;
    const UIndex base = get_hash(key, nkey) & mask;

    UIndex bucket = base;
    do {
        const UIndex index = buckets[bucket];
        if (index == (UIndex) -1) {
            return HT_NO_VALUE;
        } else {
            Entry e = entries[index];
            if (e.nkey == nkey && nkey && memcmp(keys + e.key_idx, key, nkey) == 0) {
                return e.value;
            }
        }
        ++bucket;
        bucket &= mask;
    } while (bucket != base);

    return HT_NO_VALUE;
}

size_t
ht_size(Ht *h)
{
    return h->entries.size;
}

void
ht_destroy(Ht *h)
{
    free(h->buckets);
    VECTOR_FREE(h->entries);
    VECTOR_FREE(h->keys);
    free(h);
}
