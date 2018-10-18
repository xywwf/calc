#ifndef env_h_
#define env_h_

#include "common.h"
#include "lexem.h"
#include "value.h"
#include "vm.h"

typedef struct Env Env;

Env *
env_new(void *userdata);

void *
env_userdata(Env *e);

void
env_put(Env *e, const char *name, size_t nname, Value value);

bool
env_exec(Env *e, const char *src, const Instr *chunk, size_t nchunk);

ATTR_NORETURN ATTR_PRINTF(2, 3)
void
env_throw(Env *e, const char *fmt, ...);

void
env_destroy(Env *e);

#endif
