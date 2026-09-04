#include "test.h"
#include "elf_helpers.h"
#include "kmorph/elf.h"

static void a_static_executable_needs_no_interpreter(void)
{
	unsigned char elf[256];
	size_t len = fake_elf64(elf, 0);

	CHECK_EQ(elf_needs_interp(elf, len), 0);
}

static void a_dynamic_executable_is_detected_by_pt_interp(void)
{
	unsigned char elf[256];
	size_t len = fake_elf64(elf, 1);

	CHECK_EQ(elf_needs_interp(elf, len), 1);
}

static void non_elf_and_truncated_input_are_rejected(void)
{
	unsigned char elf[256];

	fake_elf64(elf, 1);
	CHECK_EQ(elf_needs_interp("#!/bin/sh\n", 10), -ENOEXEC);
	CHECK_EQ(elf_needs_interp(elf, 70), -ENOEXEC);
}

TEST_MAIN({
	RUN(a_static_executable_needs_no_interpreter);
	RUN(a_dynamic_executable_is_detected_by_pt_interp);
	RUN(non_elf_and_truncated_input_are_rejected);
})
