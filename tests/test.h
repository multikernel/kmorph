#ifndef KMORPH_TEST_H
#define KMORPH_TEST_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		test_failures++; \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	long long _a = (long long)(a), _b = (long long)(b); \
	if (_a != _b) { \
		fprintf(stderr, "%s:%d: CHECK_EQ failed: %s = %lld, %s = %lld\n", \
			__FILE__, __LINE__, #a, _a, #b, _b); \
		test_failures++; \
	} \
} while (0)

#define CHECK_STREQ(a, b) do { \
	const char *_a = (a), *_b = (b); \
	if (!_a || !_b || strcmp(_a, _b)) { \
		fprintf(stderr, "%s:%d: CHECK_STREQ failed: %s = \"%s\", %s = \"%s\"\n", \
			__FILE__, __LINE__, #a, _a ? _a : "(null)", #b, _b ? _b : "(null)"); \
		test_failures++; \
	} \
} while (0)

#define RUN(fn) do { fprintf(stderr, "  %s\n", #fn); fn(); } while (0)

#define TEST_MAIN(body) \
int main(void) \
{ \
	body; \
	if (test_failures) { \
		fprintf(stderr, "%d check(s) failed\n", test_failures); \
		return 1; \
	} \
	return 0; \
}

#endif
