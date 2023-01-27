#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define BLOCK_ALLOCATOR_IMPL
#include "block_allocator.h"

#define ROUNDS_COUNT 1000
#define ALLOCS_COUNT 1000
#define ALLOC_MAX_SIZE (256 * 256 * 16)

void assert_allocator_layout_good(block_allocator_t *allocator) {
        block_allocator_block_t block = allocator->blocks[0];
        while (block.mem_next != BLOCK_ALLOCATOR_UNUSED) {
                assert(allocator->blocks[block.mem_next].offset == (block.offset + block.size));
                assert(block_allocator_is_used(&block) || block_allocator_is_used(&allocator->blocks[block.mem_next]));
                assert(block.offset < block.offset + block.size);
                block = allocator->blocks[block.mem_next];
        }
}

int main() {
        block_allocator_t allocator;
        block_allocator_init(0xffffffff, &allocator);

        // Stack allocate an array to put allocation info into
        block_allocator_allocation_t allocs[1000];

	// Initialise the array with allocations
        for (int i=0; i < ALLOCS_COUNT; i++) {
                block_allocator_allocation_t alloc;
                uint32_t size = 256 * (1 + (rand() % (ALLOC_MAX_SIZE/256)));
                int res = block_allocator_alloc(&allocator, size, &alloc);
                allocs[i] = alloc;
        }
        assert_allocator_layout_good(&allocator);

	// Repeatedly free and allocate half of the allocations
        for (int r=0; r < ROUNDS_COUNT; r++) {
                printf("%d\n", r);
                for (int i=1; i < ALLOCS_COUNT / 2; i++) {
                        block_allocator_allocation_t old_alloc = allocs[2 * i + (r % 2)];
                        if (old_alloc.size == 0) { continue; }
                        block_allocator_free(&allocator, &old_alloc);
                        assert_allocator_layout_good(&allocator);
                }
                for (int i=1; i < ALLOCS_COUNT / 2; i++) {
                        block_allocator_allocation_t new_alloc;
                        uint32_t size = 256 * (1 + (rand() % (ALLOC_MAX_SIZE/256)));
                        int res = block_allocator_alloc(&allocator, size, &new_alloc);
                        assert_allocator_layout_good(&allocator);
                        if (res == BLOCK_ALLOCATOR_SUCCESS) {
				allocs[2 * i + (r % 2)] = new_alloc;
                        } else {
				allocs[2 * i + (r % 2)].size = 0;
                        }
                }
        }
        assert_allocator_layout_good(&allocator);
        block_allocator_destroy(&allocator);
        printf("Allocator integrity validated at all points\n");
}
