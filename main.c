/*
 * Author: Matthieu Carteron <rubisetcie@gmail.com>
 * date:   2024-07-17
 *
 * Alter ELF needed dependencies information (shrink to remove version).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "dynamic.h"

static void usage(char *progname)
{
    printf("Usage: %s [<options>] <elf-file>\n\n\
Options:\n\
  -s,--soname         : Replace (or remove) the soname\n\
  -r,--rpath          : Replace (or remove) the run-time path\n\
  -n,--replace        : Replace needed dependency by one with another name\n\
     --repair-deps    : Perform repair on dependencies (don't run on system packages)\n\
     --priority-low   : Change the run-time path priority: system libs are above\n\
     --priority-high  : Change the run-time path priority: system libs are below \n\
  -d,--query-depends  : Query the dependencies needed (non-recursive)\n\
     --query-missing  : Query the missing dependencies\n\
     --query-soname   : Query the soname\n\
     --query-rpath    : Query the run-time path\n\
     --query-replace  : Query a potential replacement for a specified library name\n\
  -o,--output         : Output file\n\
  -h,--help           : Show help usage\n\n\
In order to replace needed dependency, supply two names:\n Example:\n\
  -n <old-name> <new-name>\n\n\
In order to remove soname or run-time path, don't supply a name after the parameter.\n", progname);
}

int main(int argc, char *const argv[])
{
    int i = 1, fix = 0;
    const char *output = NULL;
    const char *soname = NULL;
    const char *needOld = NULL;
    const char *needNew = NULL;
    const char *rpath = NULL;
    const char *filename = NULL;
    char *filenameSafe = NULL;
    LD_Cache *ldcache = NULL;
    Priority priority = PRI_UNCHANGED;
    Query query = QU_NOTHING;

    /* Checks the arguments */
    while (i < argc)
    {
        const char *arg = argv[i++];

        if (strcmp(arg, "-?") == 0 ||
            strcmp(arg, "-h") == 0 ||
            strcmp(arg, "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else if (strcmp(arg, "-s") == 0 ||
                 strcmp(arg, "--soname") == 0)
        {
            if (i >= argc || argv[i][0] == '-')
            {
                soname = REMOVAL;
                continue;
            }
            soname = argv[i++];
        }
        else if (strcmp(arg, "-r") == 0 ||
                 strcmp(arg, "--rpath") == 0)
        {
            if (i >= argc || argv[i][0] == '-')
            {
                rpath = REMOVAL;
                continue;
            }
            rpath = argv[i++];
        }
        else if (strcmp(arg, "-n") == 0 ||
                 strcmp(arg, "--replace") == 0)
        {
            if (i+1 >= argc || argv[i][0] == '-' || argv[i+1][0] == '-')
            {
                fputs("Missing two names after the parameter!\n", stderr);
                return 1;
            }
            needOld = argv[i++];
            needNew = argv[i++];
        }
        else if (strcmp(arg, "-o") == 0 ||
                 strcmp(arg, "--output") == 0)
        {
            if (i >= argc || argv[i][0] == '-')
            {
                fputs("Missing output after parameter!\n", stderr);
                return 1;
            }
            else
                output = argv[i++];
        }
        else if (strcmp(arg, "-d") == 0 ||
                 strcmp(arg, "--query-depends") == 0)
            query = QU_NEEDED;
        else if (strcmp(arg, "--query-missing") == 0)
            query = QU_MISSING;
        else if (strcmp(arg, "--query-soname") == 0)
            query = QU_SONAME;
        else if (strcmp(arg, "--query-rpath") == 0)
            query = QU_RPATH;
        else if (strcmp(arg, "--query-replace") == 0)
            query = QU_REPLACEMENT;
        else if (strcmp(arg, "--priority-low") == 0)
            priority = PRI_RUNPATH;
        else if (strcmp(arg, "--priority-high") == 0)
            priority = PRI_RPATH;
        else if (strcmp(arg, "--repair-deps") == 0)
            fix = 2;
        else if (arg[0] == '-')
        {
            fprintf(stderr, "Unrecognized parameter: %s\n", arg);
            return 1;
        }
        else
        {
            if (filename != NULL)
            {
                fputs("Only one file can be supplied!\n", stderr);
                return 1;
            }
            filename = arg;
        }
    }

    /* If no files are specified */
    if (filename == NULL)
    {
        usage(argv[0]);
        return 2;
    }

    /* Check the input and the output are not the same */
    if (output != NULL)
    {
        if (strcmp(filename, output) == 0)
        {
            fputs("The input and the output can't be the same!\n", stderr);
            return 2;
        }
    }

    /* Handle the case in which the filename is relative and doesn't contains slash */
    if (strchr(filename, '/') == NULL)
    {
        if ((filenameSafe = malloc(strlen(filename) + 3)) == NULL)
        {
            fprintf(stderr, "Failed to allocate memory for the filename: %s!\n", strerror(errno));
            return 3;
        }

        /* Prepend "./" as the filename has to contain at least one slash.
         * Or else the rpath replacement of $ORIGIN won't work.
         */
        strcpy(filenameSafe, "./");
        strcat(filenameSafe, filename);
        filename = filenameSafe;
    }

    /* Read the LD cache, to determine whether a library is found or not */
    if (needNew != NULL || query == QU_MISSING || query == QU_REPLACEMENT || fix != 0)
        ldcache = ldcache_parse("/etc/ld.so.cache");

    /* If a simple query is selected */
    if (query != QU_NOTHING)
        i = dynamics_query(ldcache, filename, query);
    else
        i = dynamics_process(ldcache, priority, filename, output, needOld, needNew, soname, rpath, fix);

    if (ldcache != NULL)
        ldcache_free(ldcache);

    free(filenameSafe);
    return i;
}
