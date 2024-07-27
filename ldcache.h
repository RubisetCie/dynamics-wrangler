/*
 * Author: Matthieu Carteron <rubisetcie@gmail.com>
 * date:   2024-07-17
 *
 * Provides ld.so.cache reading functions.
 */

#ifndef LDCACHE_H_INCLUDED
#define LDCACHE_H_INCLUDED

#include <linux/limits.h>

typedef struct
{
    char name[NAME_MAX];
    char path[PATH_MAX];
} LD_Entry;

typedef struct
{
    LD_Entry *entries;
    size_t length;
} LD_Cache;

LD_Cache* ldcache_parse(const char *filename);
char* ldcache_search(const LD_Cache *cache, const char *name);
void ldcache_free(LD_Cache *cache);

#endif
