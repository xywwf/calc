#include "common.h"

#include <stdint.h>

typedef uint_least32_t HtValue;

static const HtValue HT_NO_VALUE = (HtValue) -1;

typedef struct Ht Ht;

Ht *
ht_new(unsigned char order);

HtValue
ht_put(Ht *h, const char *key, size_t nkey, HtValue value);

HtValue
ht_get(Ht *h, const char *key, size_t nkey);

size_t
ht_size(Ht *h);

void
ht_destroy(Ht *h);
