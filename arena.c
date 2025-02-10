#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

typedef struct Arena_t {
	uintptr_t ptr;
	uintptr_t start_ptr;
	size_t alignment;
	uintptr_t end_ptr;
	size_t size;
} Arena;


Arena* ArenaAlloc (unsigned pages) {
	// get system page size
	size_t page_size = getpagesize();
	if (page_size == -1) {
		perror("Could not get page size");
		exit(EXIT_FAILURE);
	}
	#ifdef DEBUG_BUILD
	//allocate an extra page for mprotect in debug mode
	size_t alloc = (pages+1) * page_size;
	#else
	size_t alloc = (pages) * page_size;
	#endif
	Arena* arena;
	arena = malloc(sizeof(Arena));
	arena->ptr = (uintptr_t) mmap(NULL, alloc, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (arena->ptr == (uintptr_t)MAP_FAILED) {
		perror("couldn't allocate arena");
		exit(EXIT_FAILURE);
	}
	arena->start_ptr = arena->ptr;
	arena->alignment = 16;
	arena->size = alloc;
	arena->end_ptr = arena->ptr + arena->size;
	//mprotect in debug builds
	#ifdef DEBUG_BUILD
	arena->end_ptr = arena->end_ptr - page_size;
	arena->size = arena->size - page_size;
	if(mprotect((void*)arena->end_ptr, page_size, PROT_NONE) != 0){
		return NULL;
	}
	#endif
	return arena;
}
int ArenaRelease(Arena* arena) {
	if (!arena) {
		return -1;
	}
	int result = munmap((void*)arena->start_ptr, arena->size);
	free(arena);
	return result;
}

int ArenaSetAlignment(Arena* arena, size_t new_alignment) {
	assert(arena->ptr < arena->end_ptr);
	if (new_alignment % 16 || new_alignment < 16 || arena->ptr + new_alignment >= arena->end_ptr)
		return -1;
	arena->alignment = new_alignment;
	arena->ptr = (arena->ptr + (arena->alignment -1)) & ~(arena->alignment -1);
	return 0;
}



void* ArenaPush(Arena* arena, size_t size) {
	assert(arena->ptr < arena->end_ptr);
	if (!arena || size == 0 || (arena->ptr + size + arena->alignment) >=  arena->end_ptr){
		return NULL;
	}
	uintptr_t newptr;
	newptr = arena->ptr;
	arena->ptr = (arena->ptr + size + (arena->alignment -1)) & ~(arena->alignment -1);

	return (void*) newptr;
}


void* ArenaPopTo (Arena* arena, void* pos) {
	assert(arena->ptr < arena->end_ptr);
	if (!arena || !pos || 
		(uintptr_t)pos > arena->end_ptr || 
		(uintptr_t)pos < arena->start_ptr) {
		return NULL;
	}
	arena->ptr = ((uintptr_t)pos + (arena->alignment -1)) & ~(arena->alignment -1);
	return (void*)arena->ptr;

}
