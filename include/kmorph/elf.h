#ifndef KMORPH_ELF_H
#define KMORPH_ELF_H

#include <stddef.h>

/* 0: a static ELF64 executable; 1: it needs an interpreter; -ENOEXEC otherwise. */
int elf_needs_interp(const void *buf, size_t len);

#endif
