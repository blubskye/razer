#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct razerd razerd_t;

razerd_t *razerd_open(void);
void      razerd_close(razerd_t *r);
int       razerd_errno(razerd_t *r);
int       razerd_get_mice(razerd_t *r, char ***mice_out, size_t *count_out);
void      razerd_free_mice(char **mice, size_t count);
