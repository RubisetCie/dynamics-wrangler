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

static off_t align(int fd, size_t alignment)
{
    const off_t pos = lseek(fd, 0, SEEK_CUR);
    const size_t align = (size_t)pos % alignment;

    /* If already aligned, do nothing */
    if (align == 0)
        return pos;

    /* Else, align the position by advancing */
    return lseek(fd, alignment - align, SEEK_CUR);
}

static size_t length(int fd)
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

static size_t base(const char *str)
{
    size_t i = 0;
    const size_t len = strlen(str);

    /* Extract the "main" part ot the library name */
    for (; i < len; i++)
    {
        if (str[i] == '.')
            break;
    }
    return i;
}

static const char* occurence(const char *str, const char *find)
{
    char c;
    const char sc = *find;
    const size_t l = strlen(find);

    do
    {
        if ((c = *str) == sc)
        {
            if (strncmp(str, find, l) == 0)
                return str;
        }
        str++;
    } while (c != 0 && c != ':');

    return NULL;
}

static void rpath_origin(const char *origin, const char *path, char *dest, const size_t len)
{
    const char *sub = occurence(path, "$ORIGIN");
    size_t fi = 0, si = 0;

    if (sub != NULL)
    {
        const size_t origlen = sub - path;
        const size_t baselen = strrchr(origin, '/') - origin; /* The origin has to have a '/', since we handled that case in main */

        /* Replace the special variable $ORIGIN with the location of the file */
        memcpy(dest, path, origlen); fi += origlen; si += origlen;
        memcpy(dest + fi, origin, baselen); fi += baselen; si += 7;
        memcpy(dest + fi, sub + 7, len - si); fi += len - si;
    }
    else
    {
        memcpy(dest, path, len);
        fi += len;
    }

    dest[fi] = 0;
}

static int search_file_dir(const char *path, const char *name)
{
    char fullpath[PATH_MAX];
    strcpy(fullpath, path);
    strcat(fullpath, "/");
    strcat(fullpath, name);
    return access(fullpath, F_OK) == 0;
}

LD_Cache* ldcache_parse(const char *filename)
{
    LD_Cache *cache = NULL;
    Header header;
    Entry entry;
    char magic[sizeof(CACHE_MAGIC) - 1], path[PATH_MAX];
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
    cache->paths = NULL;

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

        /* Read the value string.
         * Storing it in the cache is useless since we only care about the library name.
         */
        l = length(fd);
        if (read(fd, path, l) != l)
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

const char* ldcache_replacement(const LD_Cache *cache, const char *name)
{
    size_t i;

    /* Extract the "main" part ot the name */
    const size_t s = base(name);

    /* Search for a name in the cache, that is the close to the original */
    for (i = 0; i < cache->length; i++)
    {
        const char *cacheName = cache->entries[i].name;
        if (s != base(cacheName))
            continue;
        if (strncmp(cacheName, name, s) != 0)
            continue;
        if (strcmp(cacheName, name) == 0)
            continue;
        return cacheName;
    }

    /* No replacement have been found */
    return NULL;
}

int ldcache_search(const LD_Cache *cache, const char *name)
{
    size_t i;

    /* Firstly, search for the library file in the system directories */
#if defined(SYSTEM_LIBS_3)
    if (search_file_dir(SYSTEM_LIBS_1, name))
        return 1;
    if (search_file_dir(SYSTEM_LIBS_2, name))
        return 1;
    if (search_file_dir(SYSTEM_LIBS_3, name))
        return 1;
#elif defined(SYSTEM_LIBS_2)
    if (search_file_dir(SYSTEM_LIBS_1, name))
        return 1;
    if (search_file_dir(SYSTEM_LIBS_2, name))
        return 1;
#elif defined(SYSTEM_LIBS_1)
    if (search_file_dir(SYSTEM_LIBS_1, name))
        return 1;
#endif

    /* Then, search it in the saved paths */
    if (cache->paths != NULL)
    {
        for (i = 0; i < cache->pathlen; i++)
        {
            if (search_file_dir(cache->paths[i].path, name))
                return 1;
        }
    }

    /* Finally, search for the name in the cache.
     *
     * Note: Binary search would have been more efficient.
     * However, I didn't find the guarantee that the ldcache would always be sorted...
     *
     * So it's a classic linear search.
     */
    for (i = 0; i < cache->length; i++)
    {
        if (strcmp(cache->entries[i].name, name) == 0)
            return 1;
    }

    /* Return zero if nothing was found */
    return 0;
}

int ldcache_setpath(LD_Cache *cache, const char *path, const char *filename)
{
    size_t i, j, count = 1;
    const size_t len = strlen(path);

    /* Count the number of entries in the path (separated by colons) */
    for (i = 0; i < len; i++)
    {
        if (path[i] == ':')
            count++;
    }

    /* Allocate the paths, assuming the path is not already allocated! */
    if ((cache->paths = malloc(sizeof(LD_Path) * count)) == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the paths: %s!\n", strerror(errno));
        return 0;
    }

    /* Set the cache's number of paths */
    cache->pathlen = count;

    /* Process the paths */
    count = j = 0;
    for (i = 0; i < len; i++)
    {
        if (path[i] == ':')
        {
            rpath_origin(filename, path + count, cache->paths[j++].path, i);
            count = ++i;
        }
    }

    rpath_origin(filename, path + count, cache->paths[j++].path, i);

    return 1;
}

void ldcache_free(LD_Cache *cache)
{
    free(cache->entries);
    free(cache->paths);
    free(cache);
}
