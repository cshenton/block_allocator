package block_allocator

import "core:fmt"
import "core:math/rand"

assert_allocator_layout_good :: proc(allocator: ^Block_Allocator) {
	ok : bool
	block := block_allocator_head(allocator)
	for block.mem_next != BLOCK_UNUSED {
		assert(block_is_used(block) || block_is_used(allocator.blocks[block.mem_next]))
		assert(allocator.blocks[block.mem_next].offset == (block.offset + block.size))
		assert(block.offset < block.offset + block.size)
		block, _ = block_allocator_next(allocator, block)
	}
}

main :: proc() {
	ROUNDS_COUNT :: 1000
	ALLOCS_COUNT :: 1000
	ALLOC_MAX_SIZE :: 1024 * 1024 * 100

        fmt.println("Beginning validation of allocator")
	allocator := block_allocator_init(1024 * 1024 * 1024)
	defer block_allocator_destroy(&allocator)

	// Stack allocate an array to put allocation info into
	allocs: [ALLOCS_COUNT]Block_Allocation

	// Initialise the array with allocations
	for i in 0 ..< ALLOCS_COUNT {
		size := 256 * (1 + (rand.int31() % (ALLOC_MAX_SIZE / 256)))
		alloc, ok := block_alloc(&allocator, u32(size))
		assert_allocator_layout_good(&allocator)
		if ok {
			allocs[i] = alloc
		} else {
			allocs[i].metadata = BLOCK_UNUSED
		}
	}

	// Repeatedly free and allocate half of the allocations
	for r in 0 ..< ROUNDS_COUNT {
		for i in 0 ..< ALLOCS_COUNT / 2 {
			old_alloc := allocs[2 * i + (r % 2)]
			if old_alloc.metadata == BLOCK_UNUSED {continue}
			block_free(&allocator, old_alloc)
			allocs[2 * i + (r % 2)].metadata = BLOCK_UNUSED
			assert_allocator_layout_good(&allocator)
		}

		for i in 0 ..< ALLOCS_COUNT / 2 {
			size := 256 * (1 + (rand.int31() % (ALLOC_MAX_SIZE / 256)))
			new_alloc, ok := block_alloc(&allocator, u32(size))
			assert_allocator_layout_good(&allocator)
			if ok {
				allocs[2 * i + (r % 2)] = new_alloc
			} else {
				allocs[2 * i + (r % 2)].metadata = BLOCK_UNUSED
			}
		}
	}
	assert_allocator_layout_good(&allocator)
	fmt.println("Allocator integrity validated at all points")
}
