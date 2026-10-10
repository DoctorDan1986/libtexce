#ifndef LIBTEXCE_TEST_CHECK_H
#define LIBTEXCE_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

// Test operations and checks must execute with or without NDEBUG.
#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
			abort(); \
		} \
	} while (0)

#endif
