#include "librazerd.h"
#include <stdlib.h>

struct razerd {
    int placeholder;
};

razerd_t *razerd_open(void)    { return calloc(1, sizeof(razerd_t)); }
void      razerd_close(razerd_t *r) { free(r); }
int       razerd_errno(razerd_t *r) { (void)r; return 0; }
int       razerd_get_mice(razerd_t *r, char ***mice_out, size_t *count_out)
{
    (void)r;
    *mice_out  = NULL;
    *count_out = 0;
    return 0;
}
void razerd_free_mice(char **mice, size_t count) { (void)mice; (void)count; }
