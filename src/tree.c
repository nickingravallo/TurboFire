#include "tree.h"
#include <stdio.h>

void arena_init(Arena* a, size_t size) {
	a->memory = (uint8_t*)malloc(size);
	if (a->memory) {
		printf("cant allocate %zu bytes for arena\n", size);
		exit(1);
	}
	a->capacity = size;
	a->offset = 0;
}

void* arena_alloc(Arena* a, size_t size) {
	size_t aligned_size = (size + 31) & ~31; //force 32b align for simd

	if (a->offset + aligned_size > a->capacity) {
		printf("Arena out of memory! Tree too large\n");
		exit(1);
	}

	void* ptr = a->memory + a->offset;
	a->offset +=aligned_size;
	return ptr;
}
