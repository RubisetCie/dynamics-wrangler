/*
 * Author: Matthieu Carteron <rubisetcie@gmail.com>
 * date:   2024-07-17
 *
 * Alter ELF needed dependencies information (shrink to remove version).
 */

#ifndef DYNAMIC_H_INCLUDED
#define DYNAMIC_H_INCLUDED

#include "ldcache.h"

/* Special value to mean the removal of the property */
#define REMOVAL (char*)1

typedef enum
{
    PRI_UNCHANGED,
    PRI_RUNPATH,
    PRI_RPATH
} Priority;

typedef enum
{
    QU_NOTHING,
    QU_NEEDED,
    QU_MISSING,
    QU_SONAME,
    QU_RPATH,
    QU_REPLACEMENT
} Query;

int dynamics_process(LD_Cache *ldcache, const Priority priority, const char *filename, const char *output, const char *needOld, const char *needNew, const char *soname, const char *rpath, int fix);
int dynamics_query(LD_Cache *ldcache, const char *filename, const Query query);

#endif
