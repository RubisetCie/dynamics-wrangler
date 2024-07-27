/*
 * Author: Matthieu Carteron <rubisetcie@gmail.com>
 * date:   2024-07-17
 *
 * Provides ld.so.cache reading functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "ldcache.h"

#define CACHE_MAGIC "glibc-ld.so.cache1.1"
#define FLAG_ELF 0x01

typedef struct
{
    char magic[sizeof(CACHE_MAGIC) - 1];
    uint32_t lib_count;
    uint32_t unused[6];
} Header;

typedef struct
{
    int32_t flags;
    uint32_t key;
    uint32_t value;
    uint32_t os_ver;
    uint64_t hwcap;
} Entry;

off_t align(int fd, size_t alignment)
{
    const off_t pos = lseek(fd, 0, SEEK_CUR);
    const size_t align = (size_t)pos % alignment;

    /* If already aligned, do nothing */
    if (align == 0)
        return pos;

    /* Else, align the position by advancing */
    return lseek(fd, alignment - align, SEEK_CUR);
}

size_t length(int fd)
{
    const off_t pos = lseek(fd, 0, SEEK_CUR);
    char c;
    size_t n = 0;

    while (read(fd, &c, sizeof(char)) == sizeof(char))
    {
        if (c == '\0' || n > PATH_MAX)
            break;

        n++;
    }

    lseek(fd, pos, SEEK_SET);
    return n;
}

LD_Cache* ldcache_parse(const char *filename)
{
    LD_Cache *cache = NULL;
    Header header;
    Entry entry;
    char magic[sizeof(CACHE_MAGIC) - 1];
    off_t strPos, curPos;
    uint32_t i;
    size_t n, l;
    int fd = -1;

    /* Open the cache */
    if ((fd = open(filename, O_RDONLY)) == -1)
    {
        fprintf(stderr, "Failed to open cache file: %s!\n", strerror(errno));
        return NULL;
    }

    /* Check the magic number */
    if (read(fd, magic, sizeof(CACHE_MAGIC) - 1) != sizeof(CACHE_MAGIC) - 1)
    {
        fprintf(stderr, "Failed to read the cache's magic number: %s!\n", strerror(errno));
        goto RET;
    }
    if (strncmp(magic, CACHE_MAGIC, sizeof(CACHE_MAGIC) - 1) != 0)
    {
        fprintf(stderr, "The cache's magic number doesn't compute: %s!\n", strerror(errno));
        goto RET;
    }

    /* Rewind to the beginning of the file */
    lseek(fd, 0, SEEK_SET);

    /* Read the header */
    strPos = align(fd, __alignof__(Header));
    if (read(fd, &header, sizeof(Header)) != sizeof(Header))
    {
        fprintf(stderr, "Failed to read the cache's header: %s!\n", strerror(errno));
        goto RET;
    }

    /* Allocate the cache object */
    if ((cache = malloc(sizeof(LD_Cache))) == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the cache: %s!\n", strerror(errno));
        goto RET;
    }

    /* Allocate the cache's entries */
    if ((cache->entries = malloc(sizeof(LD_Entry) * header.lib_count)) == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the cache's entries: %s!\n", strerror(errno));
        free(cache);
        goto RET;
    }

    /* Read the entries */
    for (i = 0, n = 0; i < header.lib_count; i++)
    {
        const size_t rd = read(fd, &entry, sizeof(Entry));
        if (rd != sizeof(Entry))
        {
            fprintf(stderr, "Failed to read the cache's entry: %s!\n", strerror(errno));
            continue;
        }

        if (!(entry.flags & FLAG_ELF))
            continue;

        /* Memorize the current offset */
        curPos = lseek(fd, 0, SEEK_CUR);

        /* Move to the key string offset */
        lseek(fd, strPos + entry.key, SEEK_SET);

        /* Read the key string */
        l = length(fd);
        if (read(fd, cache->entries[n].name, l) != l)
        {
            fprintf(stderr, "Failed to read the entry name: %s!\n", strerror(errno));
            continue;
        }

        /* Move to the value string offset */
        lseek(fd, strPos + entry.value, SEEK_SET);

        /* Read the value string */
        l = length(fd);
        if (read(fd, cache->entries[n].path, l) != l)
        {
            fprintf(stderr, "Failed to read the entry value: %s!\n", strerror(errno));
            continue;
        }

        /* Return to the saved offset */
        lseek(fd, curPos, SEEK_SET);
        n++;
    }

    /* Set the cache's length */
    cache->length = n;

  RET:
    /* Close the cache */
    close(fd);

    return cache;
}

char* ldcache_search(const LD_Cache *cache, const char *name)
{
    size_t i;

    /* Search for the name in the cache */
    for (i = 0; i < cache->length; i++)
    {
        if (strcmp(cache->entries[i].name, name) == 0)
            return cache->entries[i].path;
    }

    /* Return NULL if nothing was found */
    return NULL;
}

void ldcache_free(LD_Cache *cache)
{
    free(cache->entries);
    free(cache);
}
