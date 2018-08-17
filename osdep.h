#ifndef osdep_h_
#define osdep_h_

#include "common.h"

void *
osdep_rng_new(void);

bool
osdep_rng_fill(void *handle, void *buf, size_t nbuf);

void
osdep_rng_destroy(void *handle);

#endif
