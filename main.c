#include <stdio.h>
#include <string.h>
#include "arena.c"


/* -----------------------------------------------------------------------------
 * Test functions
 * -----------------------------------------------------------------------------*/

/* Test basic ArenaAlloc and ArenaRelease. */
static void test_ArenaAlloc_and_Release(void) {
	printf("Running test_ArenaAlloc_and_Release...\n");
	Arena* arena = ArenaAlloc(2);
	assert(arena != NULL);
	// We expect the arena pointers to be nonzero and in the expected order.
	assert(arena->ptr >= arena->start_ptr);
	int result = ArenaRelease(arena);
	assert(result == 0);
}

/* Test ArenaSetAlignment with both valid and invalid alignments. */
static void test_ArenaSetAlignment(void) {
	printf("Running test_ArenaSetAlignment...\n");
	Arena* arena = ArenaAlloc(2);
	// Set a valid alignment of 32 bytes.
	int res = ArenaSetAlignment(arena, 32);
	assert(res == 0);
	assert(arena->ptr % 32 == 0);
	assert(arena->first_ptr % 32 == 0);

	// Test an invalid alignment (not a power of 2).
	res = ArenaSetAlignment(arena, 20);
	assert(res == -1);


	// Test a huge alignment that would leave no room.
	res = ArenaSetAlignment(arena, arena->end_ptr - arena->ptr + 16);
	assert(res == -1);

	ArenaRelease(arena);
}

/* Test basic push functionality.
 * We simply push two blocks and verify that the returned pointers are distinct.
 */
static void test_ArenaPush_basic(void) {
	printf("Running test_ArenaPush_basic...\n");
	Arena* arena = ArenaAlloc(4);
	size_t block_size = 64;
	void* p1 = ArenaPush(arena, block_size);
	assert(p1 != NULL);
	void* p2 = ArenaPush(arena, block_size);
	assert(p2 != NULL);
	assert(p1 != p2);
	ArenaRelease(arena);
}

/* Test ArenaDropTo.
 * We push two blocks and then drop back to the first block.
 */
static void test_ArenaDropTo(void) {
	printf("Running test_ArenaDropTo...\n");
	Arena* arena = ArenaAlloc(4);
	void* first = ArenaPush(arena, 64);
	void* second = ArenaPush(arena, 64);
	(void)second; // unused after drop
	// Drop back to the first block so that the second push is undone.
	ArenaDropTo(arena, first);
	// After drop, arena->ptr should be equal to the aligned version of first.
	assert(arena->ptr ==
				 ((((uintptr_t)first) + (arena->alignment - 1)) &
					 ~(arena->alignment - 1)));
	ArenaRelease(arena);
}

/* Test ArenaPop in one_type mode.
 * We push an integer element, store a value and then pop it.
 */
static void test_ArenaPop(void) {
	printf("Running test_ArenaPop...\n");
	Arena* arena = ArenaAlloc(4);
	arena->one_type = true;
	arena->elem_size = sizeof(int);
	ArenaSetAlignment(arena, sizeof(int));
	int* value = (int*)ArenaPush(arena, arena->elem_size);
	assert(value != NULL);
	*value = 12345;
	ArenaPop(arena, value); // This should drop the element and zero out its memory.
	assert(*value == 0);
	ArenaRelease(arena);
}

/* Test ArenaSwap in one_type mode.
 * We push two integer elements, assign them distinct values, swap them,
 * and check that their values were exchanged.
 */
static void test_ArenaSwap(void) {
	printf("Running test_ArenaSwap...\n");
	Arena* arena = ArenaAlloc(4);
	arena->one_type = true;
	arena->elem_size = sizeof(int);

	int* a = (int*)ArenaPush(arena, arena->elem_size);
	int* b = (int*)ArenaPush(arena, arena->elem_size);
	assert(a != NULL && b != NULL);
	*a = 42;
	*b = 99;
	ArenaSwap(arena, a, b);
	assert(*a == 99);
	assert(*b == 42);
	ArenaRelease(arena);
}
void test_ArenaDefrag(void) {
		printf("Running test_ArenaDefrag...\n");

		// Create a one-type arena for integers.
		Arena* arena = ArenaAlloc(1);
		arena->one_type = true;
		arena->elem_size = sizeof(long);
		ArenaSetAlignment(arena, sizeof(long));
	printf("%ld\n",sizeof(long));

		// Push four integer elements with known values.
		long *a = (long*)ArenaPush(arena, arena->elem_size);
		long *b = (long*)ArenaPush(arena, arena->elem_size);
		long *c = (long*)ArenaPush(arena, arena->elem_size);
		long *d = (long*)ArenaPush(arena, arena->elem_size);
		*a = 10;
		*b = 20;
		*c = 30;
		*d = 40;

		// Free one element to create a 'hole'. We free element 'b'.
		ArenaPop(arena, b);
		// The popped element should now be zero.
		assert(*b == 0);
		// The arena's free list should now be active.
		assert(arena->to_free != NULL);

		/* Call ArenaDefrag(). According to our intended algorithm, this will:
			 - Take the element at the top of the arena (which is 'd' with value 40),
				 move it into the free slot (the one formerly occupied by 'b'),
			 - And then remove the duplicate occurrence from the top.
		*/
		ArenaDefrag(arena);

		// After defrag, the free list should be empty.
		assert(arena->to_free == NULL);

		// Compute how many elements remain in the arena.
		printf("arena->ptr = %ld\narena->first_ptr = %ld\nDifference = %ld\n num_elements = %ld\n",arena->ptr,arena->first_ptr,arena->ptr - arena->first_ptr, (arena->ptr - arena->first_ptr) / arena->elem_size);
		int num_elements = (arena->ptr - arena->first_ptr) / arena->elem_size;
		// Since one element was popped then replaced by the top element,
		// we expect 3 valid elements.
		assert(num_elements == 3);

		// Check that there are no zeroed-out (“hole”) elements.
		long sum = 0, count_zero = 0;
		long* p = (long*)arena->first_ptr;
		for (int i = 0; i < num_elements; i++) {
				sum += p[i];
				if (p[i] == 0)
						count_zero++;
		}
		// Originally, valid values were 10, (20 removed), 30, 40.
		// After defrag, the free slot should be filled with 40 (the prior top).
		// Thus, expect valid elements: 10, 40, 30 with a total sum of 80.
		//assert(sum == 80);
		assert(count_zero == 0);

		ArenaRelease(arena);
}

/* Main function to run all tests */
int main(void) {
	test_ArenaAlloc_and_Release();
	test_ArenaSetAlignment();
	test_ArenaPush_basic();
	test_ArenaDropTo();
	test_ArenaPop();
	test_ArenaSwap();
	test_ArenaDefrag();
	printf("All tests passed successfully.\n");
	return 0;
}
