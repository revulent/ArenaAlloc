#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

typedef struct Arena_t {
	void* ptr;
	void* start_ptr;
	size_t alignment;
	void* end_ptr;
	size_t size;
} Arena;

Arena* ArenaAlloc (size_t capacity) {
	// get system page size
	long page_size = getpagesize();
	if (page_size == -1) {
		perror("Could not get page size");
		exit(EXIT_FAILURE);
	}
	if (capacity < page_size) {
		capacity = page_size;
	}
	size_t alloc;
	if (capacity%page_size != 0) {
		//align alloc to page_size
		alloc = capacity - (capacity % page_size) + page_size;
	} else {alloc = capacity;}
	Arena* arena;
	arena = malloc(sizeof(Arena));
	arena->ptr = mmap(NULL, alloc, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (arena->ptr == MAP_FAILED) {
		perror("couldn't allocate arena");
		exit(EXIT_FAILURE);
	}
	arena->start_ptr = arena->ptr;
	arena->alignment = 16;
	arena->size = alloc;
	arena->end_ptr = arena->ptr + arena->size;
	return arena;
}

int ArenaRelease(Arena* arena) {
	return munmap(arena->start_ptr, arena->size);
}

void* ArenaPush(Arena* arena, size_t size) {
	if (size%arena->alignment != 0) {
		size = size - (size % arena->alignment) + arena->alignment;
	}
	if ((arena->ptr + size) >= arena->end_ptr) {
		perror("Arena too small for allocation. Next time make arena bigger or fix your memory leaks. Unused virtual memory is free!");
		return NULL;
	}
	void* newptr;
	newptr = arena->ptr;
	arena->ptr = arena->ptr + size;
	return newptr;
}
