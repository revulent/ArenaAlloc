#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

typedef struct Arena_t {
	//pointer to the current position in the arena stack
	uintptr_t ptr;
	//pointer to the start of the arena
	uintptr_t start_ptr;
	//pointer to the end of the Arena
	uintptr_t end_ptr;
	//total size of the arena in bytes
	size_t size;
	//If this Arena is used to hold a single type, set the element size to this variable
	size_t elem_size;
	//pointer to the free_list arena to be used if the Arena is of a single type and if you call ArenaPop() on an element of the Arena. This enables ArenaPush to replace the slot that was pop'd with ArenaPop
	struct Arena_t* free_list;
	//pointer to the next free slot if ArenaPop was used
	void** to_free;
	//alignment of the arena. Can be changed with ArenaSetAlignment
	uint alignment;
	//Boolean that determines if the Arena is meant to hold a single type or not. You must set this if you want the extra features of a single type Arena
	bool one_type;
} Arena;


Arena* ArenaAlloc (unsigned pages) {
	// get system page size
	size_t page_size = getpagesize();
	if (page_size == -1) {
		perror("Could not get page size");
		exit(EXIT_FAILURE);
	}
	#ifdef CONFIG_MPROTECT_ARENA
	//allocate an extra page for mprotect in debug mode
	size_t alloc = (pages+1) * page_size;
	#else
	size_t alloc = (pages) * page_size;
	#endif
	Arena* arena;
	arena = malloc(sizeof(Arena));
	assert(arena != NULL);
	arena->ptr = (uintptr_t) mmap(NULL, alloc, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (arena->ptr == (uintptr_t)MAP_FAILED) {
		perror("couldn't allocate arena");
		exit(EXIT_FAILURE);
	}
	arena->start_ptr = arena->ptr;
	arena->alignment = 16;
	arena->size = alloc;
	arena->end_ptr = arena->ptr + arena->size;
	arena->free_list = NULL;
	arena->one_type = false;
	arena->elem_size = 0;
	#ifdef CONFIG_MPROTECT_ARENA
	arena->end_ptr = arena->end_ptr - page_size;
	arena->size = arena->size - page_size;
	if(mprotect((void*)arena->end_ptr+16, page_size, PROT_NONE) != 0){
		return NULL;
	}
	#endif
	return arena;
}
int ArenaRelease(Arena* arena) {
	if (!arena) {
		return -1;
	}
	//release the free list first
	if (!(arena->free_list)) {
		ArenaRelease(arena->free_list);
		arena->free_list = NULL;
	}
	#ifdef CONFIG_MPTROTECT_ARENA
	int result = munmap((void*)arena->start_ptr, arena->size + getpagesize());
	#else
	int result = munmap((void*)arena->start_ptr, arena->size);
	#endif
	free(arena);
	return result;
}

//returns -1 if the alignment specified is not possible
int ArenaSetAlignment(Arena* arena, size_t new_alignment) {
	assert(arena->ptr < arena->end_ptr);
	if (new_alignment % 16 || new_alignment < 16 || arena->ptr + new_alignment >= arena->end_ptr)
		return -1;
	arena->alignment = new_alignment;
	arena->ptr = (arena->ptr + (arena->alignment -1)) & ~(arena->alignment -1);
	return 0;
}


void ArenaDrop (Arena* arena, void* ptr); 

//pushes a new element to the Arena. If the Arena is of a single type and ArenaPop was called, it will insert the newest element into the last hole left by ArenaDrop()
void* ArenaPush(Arena* arena, size_t size) {
	assert(arena->ptr < arena->end_ptr);
	if (!arena || size == 0 || (arena->ptr + size + arena->alignment) >=  arena->end_ptr){
		fprintf(stderr, "Something went wrong with the ArenaPush().\n arena = %p\n size to push = %ld\n arena->alignment = %d\n arena->ptr = %ld\n arena->start_ptr = %ld\n arena->end_ptr = %ld\n", arena, size, arena->alignment, arena->ptr, arena->start_ptr, arena->end_ptr);
		return NULL;
	}
	void* newptr;
	newptr = NULL;
	if (arena->free_list && arena->to_free && (arena->free_list->start_ptr != arena->free_list->ptr)) {
		//if a free list exists, it must already be of one_type unless something went horribly wrong
		assert(arena->one_type == true);
		assert(arena->elem_size > 0);
		newptr = *arena->to_free; 
		memset(newptr, 0, arena->elem_size);
		ArenaDrop(arena->free_list, arena->to_free);
		if (arena->free_list->start_ptr == arena->free_list->ptr) {
			arena->to_free = NULL;
		} else {
			arena->to_free = (void**) ArenaPush(arena->free_list, sizeof(void*));
		}
	} else {
		newptr = (void*) arena->ptr;
	}

	//if the size of the push is not aligned with the arena, this aligns the pointer
	arena->ptr = (arena->ptr + size + (arena->alignment -1)) & ~(arena->alignment -1);
	return newptr;
}



void ArenaPopTo (Arena* arena, void* pos) {
	assert(arena->ptr < arena->end_ptr);
	if (!arena || !pos || 
		(uintptr_t)pos > arena->end_ptr || 
		(uintptr_t)pos < arena->start_ptr) {
		fprintf(stderr,"Something went wrong calling ArenaPopTo() arena = %p\n ArenaPopTo position = %p\n arena->start_ptr = %ld \n arena->end_ptr = %ld \n", arena, pos, arena->start_ptr, arena->end_ptr);
		return;
	}

	arena->ptr = ((uintptr_t)pos + (arena->alignment -1)) & ~(arena->alignment -1);
	if (arena->ptr == arena->start_ptr) {
		arena->to_free = NULL;
	}

}

//this function only works for Arenas of a single type. It will "free" the location in memory provided by the pointer and add that address to the free list so that ArenaPush can use it next time
void ArenaDrop (Arena* arena, void* ptr) {
	assert(arena->ptr < arena->end_ptr);
	assert(arena->one_type == true);
	assert(arena->elem_size > 0);
	assert(arena->ptr >= arena->start_ptr);

	if( arena->ptr == arena->start_ptr) {
		fprintf(stderr, "Tried to ArenaDrop() on an Arena that hasn't had anything added to it or has been ArenaPopTo()'d the start of the Arena\n");
		arena->to_free = NULL;
	}
	
	//if ptr is the top element in the Arena stack, just ArenaPopTo() the ptr
	if (arena->ptr - arena->elem_size == (uintptr_t) ptr) {
	} else {
		//if there is no free_list, make one
		if (!(arena->free_list)) {
			arena->free_list = ArenaAlloc(arena->size / getpagesize());
			arena->free_list->one_type = true;
			arena->free_list->elem_size = sizeof(void*);
		}
		//this is just in case the free_list ArenaAlloc() fails
		assert(arena->free_list != NULL);
		memset(ptr, 0, arena->elem_size);
		arena->to_free = (void**)ArenaPush(arena->free_list, sizeof(void*));
		if(arena->to_free){
			*(arena->to_free) = ptr;
		} else {
			fprintf(stderr, "Failed to set up void** to_free for Arena\n");
		}
	}
}

//Just like ArenaPop, but 0's the memory indicated by the ptr
void ArenaPop (Arena* arena, void* ptr) {
	ArenaDrop(arena, ptr);
	memset(ptr, 0, arena->elem_size);
}

//swaps two elements of an Arena. Only works for Arenas of a single type
void ArenaSwap(Arena* arena, void* elem1, void* elem2) {
	assert(arena->one_type == true);
	assert(arena->elem_size > 0);
	uint page_size = getpagesize();
	Arena* scratch = ArenaAlloc((arena->elem_size + page_size -1) / page_size);
	void* buffer = ArenaPush(scratch, arena->elem_size);
	memcpy(buffer, elem1, arena->elem_size);
	memcpy(elem1, elem2, arena->elem_size);
	memcpy(elem2, buffer, arena->elem_size);
	int release = ArenaRelease(scratch);
	assert(release == 0);
}
