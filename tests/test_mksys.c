#include "test.h"
#include "kmorph/mksys.h"

static void kexec_flags_encode_id_and_initrd(void)
{
	CHECK_EQ(mksys_kexec_flags(7, true), 0x80000000 | (7 << 16));
	CHECK_EQ(mksys_kexec_flags(7, false), 0x80000000 | (7 << 16) | 0x4);
	CHECK_EQ(mksys_kexec_flags(32767, true), 0x80000000 | 0x7fff0000);
	CHECK_EQ(mksys_kexec_flags(32768, true), 0x80000000);
	CHECK_EQ(mksys_kexec_unload_flags(7), 0x1 | 0x80000000 | (7 << 16));
}

static void reboot_commands_match_the_kernel(void)
{
	CHECK_EQ(MK_REBOOT_CMD_EXEC, 0x4D4B4C49);
	CHECK_EQ(MK_REBOOT_CMD_HALT, 0x4D4B4C48);
	CHECK_EQ(MK_REBOOT_CMD_HALT_FORCE, 0x4D4B4C46);
}

TEST_MAIN({
	RUN(kexec_flags_encode_id_and_initrd);
	RUN(reboot_commands_match_the_kernel);
})
