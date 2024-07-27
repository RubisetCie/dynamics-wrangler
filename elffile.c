/*
 * Author: Matthieu Carteron <rubisetcie@gmail.com>
 * date:   2024-07-17
 *
 * Provides ELF files reading functions.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <elf.h>
#include <errno.h>
#include "elffile.h"

#define EHDR_PWS(x) (is_e32() ? DO_SWAPS32(ehdr->e32.x) : DO_SWAPS64(ehdr->e64.x))
#define EHDR_PHS(x) (is_e32() ? DO_SWAPS16(ehdr->e32.x) : DO_SWAPS16(ehdr->e64.x))
#define PHDR_PWS(x) (is_e32() ? DO_SWAPS32(phdr->e32.x) : DO_SWAPS64(phdr->e64.x))
#define EHDR_PWU(x) (is_e32() ? DO_SWAPU32(ehdr->e32.x) : DO_SWAPU64(ehdr->e64.x))
#define EHDR_PHU(x) (is_e32() ? DO_SWAPU16(ehdr->e32.x) : DO_SWAPU16(ehdr->e64.x))
#define PHDR_PWU(x) (is_e32() ? DO_SWAPU32(phdr->e32.x) : DO_SWAPU32(phdr->e64.x))
#define PHDR_POU(x) (is_e32() ? DO_SWAPU32(phdr->e32.x) : DO_SWAPU64(phdr->e64.x))
#define SHDR_POU(x) (is_e32() ? DO_SWAPU32(shdr->e32.x) : DO_SWAPU32(shdr->e64.x))

static int is_e32_flag;
static int swap_bytes_flag;

int is_e32(void)
{
    return is_e32_flag;
}

int swap_bytes(void)
{
    return swap_bytes_flag;
}

int elf_open(const char *filename, int flags, Elf_Header *ehdr)
{
    int fd;
    size_t headerSize;
    size_t partSize;

    /* Open the file */
    if ((fd = open(filename, flags)) == -1)
    {
        fprintf(stderr, "Failed to open file: %s!\n", strerror(errno));
        return -1;
    }

    /* Read the identifier */
    if (read(fd, ehdr, EI_NIDENT) != EI_NIDENT)
    {
        fprintf(stderr, "Failed to read ELF header: %s!\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* Verify the magick number and other ELF characteristics */
    if (memcmp(ehdr->id, ELFMAG, SELFMAG) != 0 ||
       (ehdr->id[EI_CLASS] != ELFCLASS32 &&
        ehdr->id[EI_CLASS] != ELFCLASS64) ||
       (ehdr->id[EI_DATA]  != ELFDATA2LSB &&
        ehdr->id[EI_DATA]  != ELFDATA2MSB) ||
        ehdr->id[EI_VERSION] != EV_CURRENT)
    {
        fprintf(stderr, "File %s probably isn't an ELF file.\n", filename);
        close(fd);
        return -1;
    }

    /* Set the flags */
    is_e32_flag = ehdr->id[EI_CLASS] == ELFCLASS32;
    swap_bytes_flag = ehdr->id[EI_DATA] != ELFDATA2;

    /* Read the ELF header */
    headerSize = is_e32() ? sizeof(Elf32_Ehdr) : sizeof(Elf64_Ehdr);
    if (read(fd, ((char*)ehdr) + EI_NIDENT, headerSize - EI_NIDENT) != (ssize_t)(headerSize - EI_NIDENT))
    {
        fprintf(stderr, "Failed to read full ELF header: %s!\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* Read the parts header */
    partSize = is_e32() ? sizeof(Elf32_Phdr) : sizeof(Elf64_Phdr);
    if ((size_t)EHDR_PHS(e_phentsize) != partSize)
    {
        fprintf(stderr, "section size was read as %zd, not %zd!\n", (size_t)EHDR_PHS(e_phentsize), partSize);
        close(fd);
        return -1;
    }

    return fd;
}

int elf_find_program(int fd, uint32_t type, const Elf_Header *ehdr, Elf_Program *phdr)
{
    int i;
    const size_t prgSize = is_e32() ? sizeof(Elf32_Phdr) : sizeof(Elf64_Phdr);

    /* Position for sections */
    if (lseek(fd, EHDR_PWU(e_phoff), SEEK_SET) == -1)
    {
        fprintf(stderr, "Failed to position for sections: %s!\n", strerror(errno));
        return 1;
    }

    /* Iterate through sections */
    for (i = 0; i < EHDR_PHS(e_phnum); i++)
    {
        if (read(fd, phdr, prgSize) != (ssize_t)prgSize)
        {
            fprintf(stderr, "Failed to read section header: %s!\n", strerror(errno));
            return 1;
        }

        /* Stop at the the chosen section */
        if (PHDR_PWU(p_type) == type)
            break;
    }

    if (EHDR_PHS(e_phnum) == i)
    {
        fputs("No section found.\n", stderr);
        return 2;
    }

    if (PHDR_POU(p_filesz) == 0)
    {
        fputs("Length of section is zero.\n", stderr);
        return 3;
    }

    return 0;
}

int elf_find_section(int fd, uint32_t type, const Elf_Header *ehdr, Elf_Section *shdr)
{
    int i;
    const size_t secSize = is_e32() ? sizeof(Elf32_Shdr) : sizeof(Elf64_Shdr);

    /* Position for sections */
    if (lseek(fd, EHDR_PWU(e_shoff), SEEK_SET) == -1)
    {
        fprintf(stderr, "Failed to position for sections: %s!\n", strerror(errno));
        return 1;
    }

    /* Iterate through sections */
    for (i = 0; i < EHDR_PHU(e_shnum); i++)
    {
        if (read(fd, shdr, secSize) != (ssize_t)secSize)
        {
            fprintf(stderr, "Failed to read section header: %s!\n", strerror(errno));
            return 1;
        }

        /* Stop at the the chosen section */
        if (SHDR_POU(sh_type) == type)
            break;
    }

    if (EHDR_PHU(e_shnum) == i)
    {
        fputs("No section found.\n", stderr);
        return 2;
    }

    if (SHDR_POU(sh_size) == 0)
    {
        fputs("Length of section is zero.\n", stderr);
        return 3;
    }

    return 0;
}

void elf_close(int fd)
{
    close(fd);
}
