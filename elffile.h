/*
 * Author: Matthieu Carteron <rubisetcie@gmail.com>
 * date:   2024-07-17
 *
 * Provides ELF files reading functions.
 */

#ifndef ELFFILE_H_INCLUDED
#define ELFFILE_H_INCLUDED

#include <byteswap.h>
#include <elf.h>

/* Determine the endianness */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define ELFDATA2 ELFDATA2MSB
#else
#define ELFDATA2 ELFDATA2LSB
#endif

typedef union
{
    unsigned char id[EI_NIDENT];
    Elf32_Ehdr e32;
    Elf64_Ehdr e64;
} Elf_Header;

typedef union
{
    Elf32_Shdr e32;
    Elf64_Shdr e64;
} Elf_Section;

typedef union
{
    Elf32_Phdr e32;
    Elf64_Phdr e64;
} Elf_Program;

int is_e32(void);
int swap_bytes(void);

#define DO_SWAPU16(x) ( !swap_bytes() ? x : (uint16_t)bswap_16(x) )
#define DO_SWAPU32(x) ( !swap_bytes() ? x : (uint32_t)bswap_32(x) )
#define DO_SWAPU64(x) ( !swap_bytes() ? x : (uint64_t)bswap_64(x) )
#define DO_SWAPS16(x) ( !swap_bytes() ? x : (int16_t)bswap_16(x) )
#define DO_SWAPS32(x) ( !swap_bytes() ? x : (int32_t)bswap_32(x) )
#define DO_SWAPS64(x) ( !swap_bytes() ? x : (int64_t)bswap_64(x) )

#define HDRWS(o, x) (is_e32() ? DO_SWAPS32(o.e32.x) : DO_SWAPS64(o.e64.x))
#define HDRHS(o, x) (is_e32() ? DO_SWAPS16(o.e32.x) : DO_SWAPS16(o.e64.x))
#define HDRWU(o, x) (is_e32() ? DO_SWAPU32(o.e32.x) : DO_SWAPU64(o.e64.x))
#define HDRHU(o, x) (is_e32() ? DO_SWAPU16(o.e32.x) : DO_SWAPU16(o.e64.x))

#define PHDR(x)   (is_e32() ? DO_SWAPU32(phdr.e32.x) : DO_SWAPU64(phdr.e64.x))
#define SHDR_W(x) (is_e32() ? DO_SWAPU32(shdr.e32.x) : DO_SWAPU32(shdr.e64.x))
#define SHDR_O(x) (is_e32() ? DO_SWAPU32(shdr.e32.x) : DO_SWAPU64(shdr.e64.x))

#define SWAPU(x)  (is_e32() ? ( !swap_bytes() ? (*((uint32_t*)x)) : (uint32_t)bswap_32(*((uint32_t*)x))) : ( !swap_bytes() ? (*((uint64_t*)x)) : (uint64_t)bswap_64(*((uint64_t*)x))))
#define SWAPS(x)  (is_e32() ? ( !swap_bytes() ? (*((int32_t*)x)) : (int32_t)bswap_32(*((int32_t*)x))) : ( !swap_bytes() ? (*((int64_t*)x)) : (int64_t)bswap_64(*((int64_t*)x))))
#define ADV(i, x) (is_e32() ? (i += (sizeof(int32_t) * x)) : (i += (sizeof(int64_t) * x)))

int elf_open(const char *filename, int flags, Elf_Header *ehdr);
int elf_find_program(int fd, uint32_t type, const Elf_Header *ehdr, Elf_Program *phdr);
int elf_find_section(int fd, uint32_t type, const Elf_Header *ehdr, Elf_Section *shdr);
void elf_close(int fd);

#endif
