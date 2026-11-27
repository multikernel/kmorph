#include "test.h"
#include "kmorph/mksys.h"

static void kexec_flags_encode_id_and_initrd(void)
{
	CHECK_EQ(mksys_kexec_flags(7, true), 0x80000000 | (7 << 16));
	CHECK_EQ(mksys_kexec_flags(7, false), 0x80000000 | (7 << 16) | 0x4);
	CHECK_EQ(mksys_kexec_flags(32767, true), 0x80000000 | 0x7fff0000);
	CHECK_EQ(mksys_kexec_flags(32768, true), 0x80000000);
	CHECK_EQ(mksys_kexec_unload_flags(7), 0x1 | 0x80000000 | (7 << 16));
	CHECK_EQ(mksys_kexec_open_flags(7), 0x40 | 0x80000000 | (7 << 16));
}

TEST_MAIN({
	RUN(kexec_flags_encode_id_and_initrd);
})
