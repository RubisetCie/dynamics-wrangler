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
    char path[PATH_MAX];
} LD_Path;

typedef struct
{
    char name[NAME_MAX];
    /*char path[PATH_MAX];*/
} LD_Entry;

typedef struct
{
    LD_Entry *entries;
    LD_Path *paths;
    size_t length;
    size_t pathlen;
} LD_Cache;

LD_Cache* ldcache_parse(const char *filename);
const char* ldcache_replacement(const LD_Cache *cache, const char *name);
int ldcache_search(const LD_Cache *cache, const char *name);
int ldcache_setpath(LD_Cache *cache, const char *path, const char *filename);
void ldcache_free(LD_Cache *cache);

#endif
