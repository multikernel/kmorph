#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/file.h"
#include "kmorph/iomem.h"

static const char iomem[] =
	"00000000-00000fff : Reserved\n"
	"00001000-0009fbff : System RAM\n"
	"000a0000-000bffff : PCI Bus 0000:00\n"
	"00100000-7ffdbfff : System RAM\n"
	"  01000000-02000000 : Kernel code\n"
	"7ffdc000-7fffffff : Reserved\n"
	"80000000-8fffffff : Multikernel pool\n"
	"  80000000-87ffffff : Instance web\n"
	"fed00000-fed003ff : HPET 0\n";

static void collects_system_ram_only(void)
{
	char path[] = "/tmp/kmorph-test-iomem-XXXXXX";
	struct rangeset ram = RANGESET_INIT;
	int fd = mkstemp(path);

	CHECK(fd >= 0);
	close(fd);
	CHECK_EQ(file_write(path, iomem, sizeof(iomem) - 1), 0);

	CHECK_EQ(iomem_system_ram(path, &ram), 0);
	CHECK_EQ(ram.count, 2);
	CHECK_EQ(ram.r[0].base, 0x1000);
	CHECK_EQ(ram.r[0].size, 0x9ec00);
	CHECK_EQ(ram.r[1].base, 0x100000);
	CHECK_EQ(ram.r[1].size, 0x7ffdc000 - 0x100000);

	rangeset_free(&ram);
	unlink(path);
}

static void missing_file_is_an_error(void)
{
	struct rangeset ram = RANGESET_INIT;

	CHECK(iomem_system_ram("/nonexistent/iomem", &ram) < 0);
}

TEST_MAIN({
	RUN(collects_system_ram_only);
	RUN(missing_file_is_an_error);
})
