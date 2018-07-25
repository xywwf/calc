#ifndef ht_h_
#define ht_h_

#include "common.h"
#include "value.h"

typedef struct Ht Ht;

Ht *
ht_new(unsigned char rank);

void
ht_put(Ht *h, const char *key, size_t nkey, Value value);

bool
ht_get(Ht *h, const char *key, size_t nkey, Value *result);

void
ht_destroy(Ht *h);

#endif
