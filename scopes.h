#ifndef scopes_h_
#define scopes_h_

#include "common.h"
#include "value.h"

typedef struct Scopes Scopes;

Scopes *
scopes_new(void);

void
scopes_push(Scopes *s);

void
scopes_pop(Scopes *s);

void
scopes_put_local(Scopes *s, const char *key, size_t nkey, Value value);

void
scopes_put(Scopes *s, const char *key, size_t nkey, Value value);

bool
scopes_get(Scopes *s, const char *key, size_t nkey, Value *result);

void
scopes_destroy(Scopes *s);

#endif
