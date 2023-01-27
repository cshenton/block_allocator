package block_allocator

import "core:fmt"
import "core:math/rand"

assert_allocator_layout_good :: proc(allocator: ^Block_Allocator) {
	block := allocator.blocks[0]
	for block.mem_next != BLOCK_UNUSED {
		assert(allocator.blocks[block.mem_next].offset == (block.offset + block.size))
		assert(block_is_used(block) || block_is_used(allocator.blocks[block.mem_next]))
		assert(block.offset < block.offset + block.size)
		block = allocator.blocks[block.mem_next]
	}
}

main :: proc() {
	ROUNDS_COUNT :: 1000
	ALLOCS_COUNT :: 1000
	ALLOC_MAX_SIZE :: 256 * 256 * 16

        fmt.println("Beginning validation of allocator")
	allocator := block_allocator_init(max(u32))
	defer block_allocator_destroy(&allocator)

	// Stack allocate an array to put allocation info into
	allocs: [ALLOCS_COUNT]Block_Allocation

	// Initialise the array with allocations
	for i in 0 ..< ALLOCS_COUNT {
		alloc, ok := block_alloc(&allocator, 1 + u32(rand.int31_max(ALLOC_MAX_SIZE)))
		allocs[i] = alloc
	}

	// Repeatedly free and allocate half of the allocations
	for r in 0 ..< ROUNDS_COUNT {
		fmt.println(r)
		for i in 1 ..< ALLOCS_COUNT / 2 {
			old_alloc := allocs[2 * i + (r % 2)]
			if old_alloc.size == 0 {continue}
			block_free(&allocator, old_alloc)
			assert_allocator_layout_good(&allocator)
		}

		for i in 1 ..< ALLOCS_COUNT / 2 {
			new_alloc, ok := block_alloc(&allocator, 1 + u32(rand.int31_max(ALLOC_MAX_SIZE)))
			assert_allocator_layout_good(&allocator)
			if ok {
				allocs[2 * i + (r % 2)] = new_alloc
			} else {
				allocs[2 * i + (r % 2)].size = 0
			}
		}
	}
	assert_allocator_layout_good(&allocator)
	fmt.println("Allocator integrity validated at all points")
}
