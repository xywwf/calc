#ifndef osdep_h_
#define osdep_h_

#include "common.h"

extern int OSDEP_UTF8_READY;

bool
osdep_is_interactive(void);

void *
osdep_rng_new(void);

bool
osdep_rng_fill(void *handle, void *buf, size_t nbuf);

void
osdep_rng_destroy(void *handle);

#endif
