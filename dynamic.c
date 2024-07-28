/*
 * Author: Matthieu Carteron <rubisetcie@gmail.com>
 * date:   2024-07-17
 *
 * Alter ELF needed dependencies information (shrink to remove version).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "dynamic.h"
#include "elffile.h"

static size_t available_length(const char *str, const size_t len)
{
    size_t i = 0;
    char lc = 1;

    /* Walk through the string to detect the end of it */
    while (i < len)
    {
        if (str[i] != '\0' && lc == '\0')
            return i - 1;

        /* Save the last character */
        lc = str[i];
        i++;
    }

    return 0;
}

static void write_type(char *data, int type)
{
    char buffer[8] = { 0 };
    if (is_e32())
    {
        int32_t value = type;
        memcpy(buffer, &value, sizeof(int32_t));

        if (!swap_bytes())
            (*data) = (*((int32_t*)buffer));
        else
            (*data) = bswap_32(*((int32_t*)buffer));
    }
    else
    {
        int64_t value = type;
        memcpy(buffer, &value, sizeof(int64_t));

        if (!swap_bytes())
            (*data) = (*((int64_t*)buffer));
        else
            (*data) = bswap_64(*((int64_t*)buffer));
    }
}

static void write_string(char *str, const char *name, const size_t len, const size_t available)
{
    /* Write the string */
    memcpy(str, name, len);

    /* Add zeros padding */
    memset(str + len, 0, available - len);
}

static int write_input_to_output_until(int in, int out, long offset)
{
    char buffer[1024];
    long i = 0, l;

    /* Write by chunks of 1024, until the offset is near */
    while (i + 1024 < offset)
    {
        if (read(in, buffer, sizeof(buffer)) != sizeof(buffer))
        {
            fprintf(stderr, "Failed to read from the input file: %s!\n", strerror(errno));
            return 0;
        }
        if (write(out, buffer, sizeof(buffer)) != sizeof(buffer))
        {
            fprintf(stderr, "Failed to write to the output file: %s!\n", strerror(errno));
            return 0;
        }
        i += 1024;
    }

    /* Compute the remaining length */
    l = offset - i;

    if (read(in, buffer, l) != l)
    {
        fprintf(stderr, "Failed to read from the input file: %s!\n", strerror(errno));
        return 0;
    }
    if (write(out, buffer, l) != l)
    {
        fprintf(stderr, "Failed to write to the output file: %s!\n", strerror(errno));
        return 0;
    }

    return 1;
}

static int write_input_to_output_end(int in, int out)
{
    char buffer[1024];
    long i, l, end;

    /* Get the end position of the file */
    i = lseek(in, 0, SEEK_CUR);
    end = lseek(in, 0, SEEK_END);
    lseek(in, i, SEEK_SET);

    /* Write by chunks of 1024, until the end */
    while (i + 1024 < end)
    {
        if (read(in, buffer, sizeof(buffer)) != sizeof(buffer))
        {
            fprintf(stderr, "Failed to read from the input file: %s!\n", strerror(errno));
            return 0;
        }
        if (write(out, buffer, sizeof(buffer)) != sizeof(buffer))
        {
            fprintf(stderr, "Failed to write to the output file: %s!\n", strerror(errno));
            return 0;
        }
        i += 1024;
    }

    /* Compute the remaining length */
    l = end - i;

    if (read(in, buffer, l) != l)
    {
        fprintf(stderr, "Failed to read from the input file: %s!\n", strerror(errno));
        return 0;
    }
    if (write(out, buffer, l) != l)
    {
        fprintf(stderr, "Failed to write to the output file: %s!\n", strerror(errno));
        return 0;
    }

    return 1;
}

int dynamics_process(const LD_Cache *ldcache, const Priority priority, const char *filename, const char *output, const char *needOld, const char *needNew, const char *soname, const char *rpath)
{
    Elf_Header ehdr;
    Elf_Section shdr;
    Elf_Program phdr;
    size_t phdrlen, shdrlen, len, available, slotnum = 0, slots[2], slotstr[2], slotlen[2] = { 0 };
    char *dyns = NULL, *strtab = NULL, *name;
    int in = -1, out = -1, rv = 0, dynsmod = 0, needmod = 0, somod = 0, rmod = 0, i, j, l, last;

    /* Open the output file */
    if (output && (needOld || soname || rpath))
    {
        if ((out = open(output, O_WRONLY | O_CREAT | O_TRUNC)) == -1)
        {
            fprintf(stderr, "Failed to open the output file: %s!\n", strerror(errno));
            rv = 4; goto RET;
        }
    }

    /* Open the input ELF file */
    if ((in = elf_open(filename, out == -1 ? O_RDWR : O_RDONLY, &ehdr)) == -1)
        return 3;

    /* Set the output file permissions */
    if (out != -1)
    {
        struct stat stats;

        if (fstat(in, &stats) != 0)
        {
            fprintf(stderr, "Failed to read the stats of the input file: %s!\n", strerror(errno));
            rv = 3; goto RET;
        }
        if (fchmod(out, stats.st_mode) != 0)
        {
            fprintf(stderr, "Failed to change the permissions of the output file: %s!\n", strerror(errno));
            rv = 3; goto RET;
        }
    }

    /* Find the dynamic section */
    if (elf_find_program(in, PT_DYNAMIC, &ehdr, &phdr) != 0)
    {
        rv = 3;
        goto RET;
    }

    /* Allocate the dynamic section */
    phdrlen = HDRWU(phdr, p_filesz);
    if ((dyns = malloc(phdrlen)) == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for dynamic section: %s!\n", strerror(errno));
        rv = 3; goto RET;
    }

    /* Read the dynamic section */
    memset(dyns, 0, phdrlen);
    if (lseek(in, HDRWU(phdr, p_offset), SEEK_SET) == -1
      || read(in, dyns, phdrlen) != (ssize_t)phdrlen)
    {
        fprintf(stderr, "Failed to read dynamic section: %s!\n", strerror(errno));
        rv = 3; goto RET;
    }

    /* Find the string table section */
    if (elf_find_section(in, SHT_STRTAB, &ehdr, &shdr) != 0)
    {
        rv = 3;
        goto RET;
    }

    /* Allocate the dynamic section */
    shdrlen = HDRWU(shdr, sh_size);
    if ((strtab = malloc(shdrlen)) == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for string table: %s!\n", strerror(errno));
        rv = 3; goto RET;
    }

    /* Read the dynamic section */
    memset(strtab, 0, shdrlen);
    if (lseek(in, HDRWU(shdr, sh_offset), SEEK_SET) == -1
      || read(in, strtab, shdrlen) != (ssize_t)shdrlen)
    {
        fprintf(stderr, "Failed to read string table: %s!\n", strerror(errno));
        rv = 3; goto RET;
    }

    /* If no modifications have to be done, just print infos about the dynamics */
    if (needOld || soname || rpath)
    {
        printf("Processing file: %s\n", filename);
        last = -2;
    }
    else
    {
        printf("[ELF dynamic table informations]\n  File: %s\n", filename);
        last = -1;
    }

    /* The 'removed' dynamic entries are actually turned into type DT_DEBUG.
     * Since the debug sections are vastly unused.
     * Do a first pass to list all debug sections.
     */
    i = 0;
    while (i < phdrlen)
    {
        switch (SWAPS(&dyns[i]))
        {
            case DT_DEBUG:
                /* Check if the value points outside of the string table (which may be) */
                if (SWAPU(&dyns[i]) > shdrlen)
                    goto DEFAULT;

                /* Pick the slot with the minimum current length */
                l = slotlen[0] > slotlen[1] ? 1 : 0;
                slots[l] = i;
                ADV(i, 1);
                slotstr[l] = SWAPU(&dyns[i]);
                name = &strtab[SWAPU(&dyns[i])];
                ADV(i, 1);

                /* Compute the available length of the slot */
                slotlen[l] = available_length(name, shdrlen - (name - strtab));
                slotnum++;
                break;
          DEFAULT:
            default:
                ADV(i, 2);
                break;
        }
    }

    /* Process the dynamic entries */
    i = 0;
    while (i < phdrlen)
    {
        switch (SWAPS(&dyns[i]))
        {
            case DT_NEEDED:
                /* Retrieve the library name */
                ADV(i, 1);
                name = &strtab[SWAPU(&dyns[i])];
                ADV(i, 1);

                if (last > -2)
                {
                    if (last != 0)
                        puts("");
                    last = 0;
                    printf("· Needed: %s\n", name);
                    break;
                }

                if (needOld != NULL)
                {
                    /* Check if this is the name to be replaced */
                    if (strcmp(name, needOld) != 0)
                        break;

                    needmod = 1;

                    /* Compute the available length in the string table */
                    len = strlen(needNew);
                    available = available_length(name, shdrlen - (name - strtab));
                    if (len > available)
                    {
                        fputs("The new name is too big to fit!\n", stderr);
                        break;
                    }

                    printf("Replacing needed: %s => %s...\n", needOld, needNew);

                    /* Check if the new name is in the cache */
                    if (ldcache != NULL)
                    {
                        if (ldcache_search(ldcache, needNew) == NULL)
                            fprintf(stderr, "Warning! The library name %s is not found in the cache!\nYou may want to run `ldconfig`.\n", needNew);
                    }

                    /* Write in the string table */
                    write_string(name, needNew, len, available);
                }
                break;
            case DT_SONAME:
                /* Save the offset of the type */
                j = i;

                /* Retrieve the SO name */
                ADV(i, 1);
                name = &strtab[SWAPU(&dyns[i])];
                ADV(i, 1);

                if (last > -2)
                {
                    if (last != 1)
                        puts("");
                    last = 1;
                    printf("· Soname: %s\n", name);
                    break;
                }

                if (soname != NULL)
                {
                    somod = 1;

                    /* If to be removed, replace the type with DT_DEBUG */
                    if (soname == REMOVAL)
                    {
                        puts("Removing soname entry...");
                        write_type(&dyns[j], DT_DEBUG);
                        dynsmod = 1;
                        break;
                    }

                    /* Compute the available length in the string table */
                    len = strlen(soname);
                    available = available_length(name, shdrlen - (name - strtab));
                    if (len > available)
                    {
                        fputs("The new soname is too big to fit!\n", stderr);
                        break;
                    }

                    printf("Setting soname: %s...\n", soname);

                    /* Write in the string table */
                    write_string(name, soname, len, available);
                }
                break;
            case DT_RPATH:
                /* Change the type if priority doesn't match */
                if (priority == PRI_RUNPATH)
                {
                    fputs("Changing run-time priority to low...\n", stderr);
                    write_type(&dyns[i], DT_RUNPATH);
                    dynsmod = 1;
                }
            case DT_RUNPATH:
                /* Change the type if priority doesn't match */
                if (priority == PRI_RPATH)
                {
                    fputs("Changing run-time priority to high...\n", stderr);
                    write_type(&dyns[i], DT_RPATH);
                    dynsmod = 1;
                }

                /* Save the offset of the type */
                j = i;

                /* Retrieve the run-time path */
                ADV(i, 1);
                name = &strtab[SWAPU(&dyns[i])];
                ADV(i, 1);

                if (last > -2)
                {
                    if (last != 2)
                        puts("");
                    last = 2;
                    printf("· Run-time path: %s\n", name);
                    break;
                }

                if (rpath != NULL)
                {
                    rmod = 1;

                    /* If to be removed, replace the type with DT_DEBUG */
                    if (rpath == REMOVAL)
                    {
                        puts("Removing run-time path entry...");
                        write_type(&dyns[j], DT_DEBUG);
                        dynsmod = 1;
                        break;
                    }

                    /* Compute the available length in the string table */
                    len = strlen(rpath);
                    available = available_length(name, shdrlen - (name - strtab));
                    if (len > available)
                    {
                        fputs("The new run-time path is too big to fit!\n", stderr);
                        break;
                    }

                    printf("Setting run-time path: %s...\n", rpath);

                    /* Write in the string table */
                    write_string(name, rpath, len, available);
                }
                break;
            default:
                ADV(i, 2);
                break;
        }
    }

    /* If no concerned sections have been found yet, try the DT_DEBUG ones */
    while (slotnum > 0)
    {
        if (soname > REMOVAL && !somod)
        {
            somod = 1;
            slotnum--;

            /* Pick the slot with the maximum length */
            l = slotlen[0] < slotlen[1] ? 1 : 0;

            /* Compute the available length in the string table */
            len = strlen(soname);
            available = slotlen[l];
            if (len > available)
                fputs("The new run-time path is too big to fit!\n", stderr);
            else
            {
                printf("Adding soname: %s...\n", soname);
                write_type(&dyns[slots[l]], DT_SONAME);
                dynsmod = 1;

                /* Write in the string table */
                name = &strtab[slotstr[l]];
                write_string(name, soname, len, available);
            }
        }
        else if (rpath > REMOVAL && !rmod)
        {
            rmod = 1;
            slotnum--;

            /* Pick the slot with the maximum length */
            l = slotlen[0] < slotlen[1] ? 1 : 0;

            /* Compute the available length in the string table */
            len = strlen(rpath);
            available = slotlen[l];
            if (len > available)
                fputs("The new run-time path is too big to fit!\n", stderr);
            else
            {
                printf("Adding run-time path: %s...\n", rpath);
                write_type(&dyns[slots[l]], priority == PRI_RUNPATH ? DT_RUNPATH : DT_RPATH);
                dynsmod = 1;

                /* Write in the string table */
                name = &strtab[slotstr[l]];
                write_string(name, rpath, len, available);
            }
        }
    }

    /* Write the output file */
    if ((needOld && needmod) || (soname && somod) || (rpath && rmod))
    {
        if (out == -1)
        {
            /* Position to the offset of the string table section */
            if (lseek(in, HDRWU(shdr, sh_offset), SEEK_SET) == -1)
            {
                fprintf(stderr, "Failed to position to the string table: %s!\n", strerror(errno));
                rv = 4; goto RET;
            }

            /* Write the modified string table */
            if (write(in, strtab, shdrlen) != shdrlen)
            {
                fprintf(stderr, "Failed to write to the string table: %s!\n", strerror(errno));
                rv = 4; goto RET;
            }

            if (dynsmod)
            {
                /* Position to the offset of the dynamic section */
                if (lseek(in, HDRWU(phdr, p_offset), SEEK_SET) == -1)
                {
                    fprintf(stderr, "Failed to position to the dynamic section: %s!\n", strerror(errno));
                    rv = 4; goto RET;
                }

                /* Write the modified dynamic section */
                if (write(in, dyns, phdrlen) != phdrlen)
                {
                    fprintf(stderr, "Failed to write to the dynamic section: %s!\n", strerror(errno));
                    rv = 4; goto RET;
                }
            }
        }
        else
        {
            /* Position to the beginning of the input file */
            lseek(in, 0, SEEK_SET);

            /* Copy the data until the string table section */
            if (!write_input_to_output_until(in, out, HDRWU(shdr, sh_offset)))
            {
                rv = 4;
                goto RET;
            }

            /* Write the string table section */
            if (write(out, strtab, shdrlen) != shdrlen)
            {
                fprintf(stderr, "Failed to write to the string table: %s!\n", strerror(errno));
                rv = 4; goto RET;
            }

            /* Advance the input until the end of the string table section */
            if (lseek(in, shdrlen, SEEK_CUR) == -1)
            {
                fprintf(stderr, "Failed to advance in the input file: %s!\n", strerror(errno));
                rv = 4; goto RET;
            }

            if (dynsmod)
            {
                /* Copy the data until the dynamic section */
                if (!write_input_to_output_until(in, out, HDRWU(phdr, p_offset) - lseek(in, 0, SEEK_CUR)))
                {
                    rv = 4;
                    goto RET;
                }

                /* Write the dynamic section */
                if (write(out, dyns, phdrlen) != phdrlen)
                {
                    fprintf(stderr, "Failed to write to the dynamic section: %s!\n", strerror(errno));
                    rv = 4; goto RET;
                }

                /* Advance the input until the end of the dynamic section */
                if (lseek(in, phdrlen, SEEK_CUR) == -1)
                {
                    fprintf(stderr, "Failed to advance in the input file: %s!\n", strerror(errno));
                    rv = 4; goto RET;
                }
            }

            /* Copy the rest of the data */
            if (!write_input_to_output_end(in, out))
            {
                rv = 4;
                goto RET;
            }
        }
    }

    /* Warn if something wanted to be done, but nothing actually was */
    if (needOld && !needmod)
        fprintf(stderr, "Warning! No needed library with name %s was found.\n", needOld);
    if (soname && !somod)
        fputs("Warning! No available section was found to modify soname.\n", stderr);
    if (rpath && !rmod)
        fputs("Warning! No available section was found to modify run-time path.\n", stderr);

  RET:
    if (in != -1)
        elf_close(in);

    if (out != -1)
        close(out);

    free(strtab);
    free(dyns);

    return rv;
}
