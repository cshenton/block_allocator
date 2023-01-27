package block_allocator
// MIT License

// Copyright (c) 2023 Charlie Shenton

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Allocator based on Sebastian Aaltonen's Offset Allocator: 
// https://github.com/sebbbi/OffsetAllocator/blob/main/offsetAllocator.cpp

import "core:math/bits"

BLOCK_UNUSED :: 0xffffffff // Sentinel value used to denote the ends of linked lists
BLOCK_HEAD_BITS :: 0xf0000000 // This could just be a single bit, but this is readable and we have the bits to spare
BLOCK_HEAD_MASK :: 0x0fffffff // This is simply ~BLOCK_HEAD_BITS

Block_Allocator_Block :: struct {
	offset:   u32, // Offset of the block in bytes
	size:     u32, // Size of this block in bytes
	bin_prev: u32, // Previous block in a bin's linked-list of blocks, if any, or the bin index | BLOCK_HEAD_BITS if we're the first
	bin_next: u32, // Next block in a bin's linked-list of blocks, if any
	mem_prev: u32, // Previous block in memory, if any
	mem_next: u32, // Next block in memory, if any
}

Block_Allocator :: struct {
	top_bins:    u32, // Which of the top log2 bins are resident
	bottom_bins: [32]u8, // Which if the bottom linear bins are resident in each top bins
	bin_lists:   [256]u32, // Start index for the linked-list of blocks in each bin
	blocks:      []Block_Allocator_Block, // Pre-allocated array of block information
	head_block:  u32, // Index of the block at the start of the memory heap
	free_blocks: []u32, // Free list of blocks
	free_offset: u32, // Current free list start index 
}

Block_Allocation :: struct {
	offset:   u32,
	size:     u32,
	metadata: u32,
}

block_allocator_init :: proc(size: u32, max_allocs: u32 = 128 * 1024) -> (allocator: Block_Allocator) {
	allocator = Block_Allocator {
		top_bins    = 0,
		free_offset = 0,
	}

	for i in 0 ..< 32 {
		allocator.bottom_bins[i] = 0
	}

	for i in 0 ..< 256 {
		allocator.bin_lists[i] = BLOCK_UNUSED
	}

	allocator.blocks = make([]Block_Allocator_Block, max_allocs)
	allocator.free_blocks = make([]u32, max_allocs)

	for i in 0 ..< max_allocs {
		allocator.free_blocks[i] = i
	}

	insert_block(&allocator, 0, size, BLOCK_UNUSED, BLOCK_UNUSED)
	return
}

block_allocator_destroy :: proc(allocator: ^Block_Allocator) {
	delete(allocator.blocks)
	delete(allocator.free_blocks)
}

block_alloc :: proc(allocator: ^Block_Allocator, size: u32) -> (alloc: Block_Allocation, ok: bool) {
	// Get the indices of the bin that this size fits into
	index, top_index, bottom_index := size_to_bin_index(size)

	// Now find the smallest resident bin strictly larger than that
	bigger_bottoms := allocator.bottom_bins[top_index] & ~((1 << (bottom_index + 1)) - 1)
	if bigger_bottoms != 0 {
		// The top bin contains a large enough bottom bin, get its index
		bottom_index = u32(bits.count_trailing_zeros(bigger_bottoms))
	} else {
		// The top bin is empty, get the smallest resident top bin bigger
		bigger_tops := allocator.top_bins & ~((1 << (top_index + 1)) - 1)

		// Check if there's no large enough bin
		if bigger_tops == 0 {
			alloc = Block_Allocation{0, 0, 0}
			ok = false
			return
		}

		// Now get the top bin, and the smallest resident bin in that 
		top_index = bits.count_trailing_zeros(bigger_tops)
		bottom_index = u32(bits.count_trailing_zeros(allocator.bottom_bins[top_index]))
	}
	index = (top_index << 3) | bottom_index

	// Pop the top block off the bin
	block_index := allocator.bin_lists[index]
	block := &allocator.blocks[block_index]
	
	if block.size < size {
		ok = false
		return
		// fmt.println(block.size, size)
	}
	assert(block.size >= size)
	
	allocator.bin_lists[index] = block.bin_next
	if block.bin_next != BLOCK_UNUSED {
		// We have a new, valid, header block. So we need to link it to this bin
		allocator.blocks[block.bin_next].bin_prev = BLOCK_HEAD_BITS | index
	} else {
		// The bottom bin is now empty, zero its corresponding bit
		allocator.bottom_bins[top_index] &= ~(1 << bottom_index)

		// If all bottom bins are now empty, zero the corresponding top bit
		if allocator.bottom_bins[top_index] == 0 {
			allocator.top_bins &= ~(1 << top_index)
		}
	}
	block.bin_next = BLOCK_UNUSED
	block.bin_prev = BLOCK_UNUSED

	// Resize the block to just cover the allocation, and insert the rest as a new block
	remaining_size := block.size - size
	if remaining_size > 0 {
		ok = insert_block(allocator, block.offset + size, remaining_size, block_index, block.mem_next)
		if !ok {
			alloc = Block_Allocation{0, 0, 0}
			return
		}
	}
	block.size = size
	alloc = Block_Allocation{block.offset, block.size, block_index}
	return
}

block_free :: proc(allocator: ^Block_Allocator, alloc: Block_Allocation) {
	// Free the backing entry for the block, and get its data
	// There's no need to remove it, since it's allocated, so not part of any bin lists
	block := allocator.blocks[alloc.metadata]
	allocator.free_offset -= 1
	allocator.free_blocks[allocator.free_offset] = alloc.metadata

	// Remove the mem_prev neighbour and claim its memory if its unused
	if block.mem_prev != BLOCK_UNUSED && !block_is_used(allocator.blocks[block.mem_prev]) {
		prev_block := allocator.blocks[block.mem_prev]
		block.offset = prev_block.offset
		block.size += prev_block.size
		remove_block(allocator, block.mem_prev)
		block.mem_prev = prev_block.mem_prev
	}

	// Remove the mem_next neighbour and claim its memory if its unused
	if block.mem_next != BLOCK_UNUSED && !block_is_used(allocator.blocks[block.mem_next]) {
		next_block := allocator.blocks[block.mem_next]
		block.size += next_block.size
		remove_block(allocator, block.mem_next)
		block.mem_next = next_block.mem_next
	}

	// Insert a fresh block covering the new range
	insert_block(allocator, block.offset, block.size, block.mem_prev, block.mem_next)
}

block_is_used :: proc(block: Block_Allocator_Block) -> bool {
	// A block is used only if it is not part of a bin's linked list, i.e. if bin_next and bin_prev are both BLOCK_UNUSED
	return (block.bin_next == BLOCK_UNUSED) && (block.bin_prev == BLOCK_UNUSED)
}

block_allocator_head :: proc(allocator: ^Block_Allocator) -> (block: Block_Allocator_Block) {
	return allocator.blocks[allocator.head_block];
}

block_allocator_next :: proc(allocator: ^Block_Allocator, block: Block_Allocator_Block) -> (Block_Allocator_Block, bool) {
	if block.mem_next == BLOCK_UNUSED {
		return {}, false
	}
	return allocator.blocks[block.mem_next], true
}

@(private = "file")
size_to_bin_index :: proc(size: u32) -> (index, top_index, bottom_index: u32) {
	// Top index is 1 less than the smallest power of two which contains size
	leading_zeros := bits.count_leading_zeros(size)
	top_index = leading_zeros > 28 ? 0 : 28 - leading_zeros
	bottom_index = (size >> top_index) & 0x7
	index = (top_index << 3) | bottom_index
	return
}

@(private = "file")
insert_block :: proc(allocator: ^Block_Allocator, offset, size, mem_prev, mem_next: u32) -> bool {
	// Get the indices of the smallest bin that fully contains a block of this size
	index, top_index, bottom_index := size_to_bin_index(size)

	// Flip the corresponding bin bits to indicate we have block/s in this bin
	allocator.top_bins |= (1 << top_index)
	allocator.bottom_bins[top_index] |= (1 << bottom_index)

	// Now, allocate a backing entry for the block, fill it out, and insert it into the bin list
	if allocator.free_offset == u32(len(allocator.free_blocks)) {return false}
	block_index := allocator.free_blocks[allocator.free_offset]
	allocator.free_offset += 1
	head_block_index := allocator.bin_lists[index]
	allocator.blocks[block_index] = {offset, size, BLOCK_HEAD_BITS | index, head_block_index, mem_prev, mem_next}
	if head_block_index != BLOCK_UNUSED {allocator.blocks[head_block_index].bin_prev = block_index}
	if mem_prev != BLOCK_UNUSED {allocator.blocks[mem_prev].mem_next = block_index}
	if mem_next != BLOCK_UNUSED {allocator.blocks[mem_next].mem_prev = block_index}
	allocator.bin_lists[index] = block_index
	if offset == 0 { allocator.head_block = block_index }
	return true
}

@(private = "file")
remove_block :: proc(allocator: ^Block_Allocator, block_index: u32) {
	// Free the backing entry for the block
	block := allocator.blocks[block_index]
	allocator.free_offset -= 1
	allocator.free_blocks[allocator.free_offset] = block_index
	is_head := (block.bin_prev & BLOCK_HEAD_BITS) != 0

	if !is_head {
		// We have a valid previous block, simply link it to the next
		allocator.blocks[block.bin_prev].bin_next = block.bin_next
		if block.bin_next != BLOCK_UNUSED {allocator.blocks[block.bin_next].bin_prev = block.bin_prev}
		return
	}

	index := block.bin_prev & BLOCK_HEAD_MASK
	top_index := index >> 3
	bottom_index := index & 0x7
	allocator.bin_lists[index] = block.bin_next

	if block.bin_next != BLOCK_UNUSED {
		// We have a new, valid, header block. So we need to link it to this bin.
		allocator.blocks[block.bin_next].bin_prev = block.bin_prev
		return
	}

	// The bin is now empty, zero its corresponding bottom bin bit
	allocator.bottom_bins[top_index] &= ~(1 << bottom_index)

	// If all bottom bins are now empty, zero the corresponding top bit
	if allocator.bottom_bins[top_index] == 0 {
		allocator.top_bins &= ~(1 << top_index)
	}
}
