#ifndef env_h_
#define env_h_

#include "common.h"
#include "lexem.h"
#include "value.h"
#include "vm.h"
#include "scopes.h"

typedef struct Env Env;

Env *
env_new(Scopes *s);

bool
env_eval(Env *e, const Instr *chunk, size_t nchunk);

const char *
env_last_error(Env *e);

void
env_free_last_error(Env *e);

LS_ATTR_NORETURN LS_ATTR_PRINTF(2, 3)
void
env_throw(Env *e, const char *fmt, ...);

void
env_destroy(Env *e);

#endif
