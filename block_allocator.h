#ifndef BLOCK_ALLOCATOR_H
#define BLOCK_ALLOCATOR_H
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

#include <stdint.h>
#include <stdlib.h>

#define BLOCK_ALLOCATOR_SUCCESS 0
#define BLOCK_ALLOCATOR_OUT_OF_MEMORY -1
#define BLOCK_ALLOCATOR_MAX_ALLOCS 131072 // 128 * 1024, you can change this if you like

typedef struct block_allocator_block_t {
        uint32_t offset; // Offset of the block in bytes
        uint32_t size; // Size of this block in bytes
        uint32_t bin_prev; // Previous block in a bin's linked-list of blocks, if any, or its bin index
        uint32_t bin_next; // Next block in a bin's linked-list of blocks, if any
        uint32_t mem_prev; // Previous block in memory, if any
        uint32_t mem_next; // Next block in memory, if any
} block_allocator_block_t;

typedef struct block_allocator_t {
        uint32_t top_bins; // Which of the top log2 bins are resident
        uint8_t bottom_bins[32]; // Which if the bottom linear bins are resident in each top bins
        uint32_t bin_lists[256]; // Start index for the linked-list of blocks in each bin
        block_allocator_block_t *blocks; // Pre-allocated array of block information
        uint32_t head_block; // Index of the block at the start of the memory heap
        uint32_t *free_blocks; // Free list of blocks
        uint32_t free_offset; // Current free list start index 
} block_allocator_t;

typedef struct block_allocator_allocation_t {
        uint32_t offset;
        uint32_t size;
        uint32_t metadata;
} block_allocator_allocation_t;

// Initialise a block allocator over size bytes of memory, returns OUT_OF_MEMORY if CPU allocation fails, success otherwise
int block_allocator_init(uint32_t size, block_allocator_t *out_allocator);

// Frees the backing memory for the allocator
void block_allocator_destroy(block_allocator_t *allocator);

// Allocates a sub-range of size bytes, returns OUT_OF_MEMORY if no such block exists larger than that size
int block_allocator_alloc(block_allocator_t *allocator, uint32_t size, block_allocator_allocation_t *out_alloc);

// Frees the byte range associated with this alloc
void block_allocator_free(block_allocator_t *allocator, block_allocator_allocation_t *alloc);

// Get the block at the start of the memory heap, useful for traversing to render heap fragmentation
void block_allocator_head(block_allocator_t *allocator, block_allocator_block_t *out_block);

// Get the next block in memory after this one, useful for traversing to render heap fragmentation.
// Returns BLOCK_ALLOCATOR_OUT_OF_MEMORY if we're at the end of the heap.
int block_allocator_next(block_allocator_t *allocator, block_allocator_block_t *block, block_allocator_block_t *out_block);

// Indicates whether a memory block is currently allocated, useful for traversing to render heap fragmentation
int block_allocator_is_used(block_allocator_block_t *block);

#ifdef BLOCK_ALLOCATOR_IMPL

#define BLOCK_ALLOCATOR_UNUSED 0xffffffff // Sentinel value used to denote the ends of linked lists
#define BLOCK_ALLOCATOR_HEAD_BITS 0xf0000000 // This could just be a single bit, but we have bits to spare
#define BLOCK_ALLOCATOR_HEAD_MASK 0x0fffffff // This is simply ~BLOCK_HEAD_BITS

uint32_t block_allocator_size_to_bin_index(uint32_t size, uint32_t *out_top, uint32_t *out_bottom) {
        uint32_t leading_zeros = __builtin_clz(size);
        *out_top = leading_zeros > 28 ? 0 : 28 - leading_zeros;
        *out_bottom = (size >> *out_top) & 0x7;
        return (*out_top << 3) | *out_bottom;
}

int block_allocator_is_used(block_allocator_block_t *block) {
        return (block->bin_next == BLOCK_ALLOCATOR_UNUSED) && (block->bin_prev == BLOCK_ALLOCATOR_UNUSED);
}

int block_allocator_insert(block_allocator_t *allocator, uint32_t offset, uint32_t size, uint32_t mem_prev, uint32_t mem_next) {
        if (allocator->free_offset == BLOCK_ALLOCATOR_MAX_ALLOCS) { return BLOCK_ALLOCATOR_OUT_OF_MEMORY; }
        uint32_t top_index, bottom_index;
        uint32_t index = block_allocator_size_to_bin_index(size, &top_index, &bottom_index);
        allocator->top_bins |= (1 << top_index);
        allocator->bottom_bins[top_index] |= (1 << bottom_index);
        uint32_t block_index = allocator->free_blocks[allocator->free_offset];
        allocator->free_offset += 1;
        uint32_t head_block_index = allocator->bin_lists[index];
        block_allocator_block_t block = {offset, size, BLOCK_ALLOCATOR_HEAD_BITS | index, head_block_index, mem_prev, mem_next};
        allocator->blocks[block_index] = block;
        if (head_block_index != BLOCK_ALLOCATOR_UNUSED) { allocator->blocks[head_block_index].bin_prev = block_index; }
        if (mem_prev != BLOCK_ALLOCATOR_UNUSED) { allocator->blocks[mem_prev].mem_next = block_index; }
        if (mem_next != BLOCK_ALLOCATOR_UNUSED) { allocator->blocks[mem_next].mem_prev = block_index; }
        allocator->bin_lists[index] = block_index;
        if (offset == 0) { allocator->head_block = block_index; }
        return BLOCK_ALLOCATOR_SUCCESS;
}

void block_allocator_remove(block_allocator_t *allocator, uint32_t block_index) {
        block_allocator_block_t block = allocator->blocks[block_index];
        allocator->free_offset -= 1;
        allocator->free_blocks[allocator->free_offset] = block_index;
        int is_head = (block.bin_prev & BLOCK_ALLOCATOR_HEAD_BITS) != 0;

        if (!is_head) {
                allocator->blocks[block.bin_prev].bin_next = block.bin_next;
                if (block.bin_next != BLOCK_ALLOCATOR_UNUSED) { allocator->blocks[block.bin_next].bin_prev = block.bin_prev; }
                return;
        }

        uint32_t index = block.bin_prev & BLOCK_ALLOCATOR_HEAD_MASK;
        uint32_t top_index = index >> 3;
        uint32_t bottom_index = index & 0x7;
        allocator->bin_lists[index] = block.bin_next;
        if (block.bin_next != BLOCK_ALLOCATOR_UNUSED) {
                allocator->blocks[block.bin_next].bin_prev = block.bin_prev;
                return;
        }
        allocator->bottom_bins[top_index] &= ~(1 << bottom_index);
        if (allocator->bottom_bins[top_index] == 0) { allocator->top_bins &= ~(1 << top_index); }
}

int block_allocator_init(uint32_t size, block_allocator_t *out_allocator) {
        out_allocator->top_bins = 0;
        out_allocator->free_offset = 0;
        out_allocator->head_block = 0;
        for (int i=0; i < 32; i++) {
                out_allocator->bottom_bins[i] = 0;
        }
        for (int i=0; i < 256; i++) {
                out_allocator->bin_lists[i] = BLOCK_ALLOCATOR_UNUSED;
        }
        out_allocator->blocks = (block_allocator_block_t*)malloc(sizeof(block_allocator_block_t) * BLOCK_ALLOCATOR_MAX_ALLOCS);
        out_allocator->free_blocks = (uint32_t*)malloc(sizeof(uint32_t) * BLOCK_ALLOCATOR_MAX_ALLOCS);
        if (out_allocator->blocks == NULL || out_allocator->free_blocks == NULL) { return BLOCK_ALLOCATOR_OUT_OF_MEMORY; }
        for (int i=0; i < BLOCK_ALLOCATOR_MAX_ALLOCS; i++) {
                out_allocator->free_blocks[i] = i;
        }
        block_allocator_insert(out_allocator, 0, size, BLOCK_ALLOCATOR_UNUSED, BLOCK_ALLOCATOR_UNUSED);
        return BLOCK_ALLOCATOR_SUCCESS;
}

void block_allocator_destroy(block_allocator_t *allocator) {
        free(allocator->blocks);
        free(allocator->free_blocks);
}

int block_allocator_alloc(block_allocator_t *allocator, uint32_t size, block_allocator_allocation_t *out_alloc) {
        if (size == 0) { return BLOCK_ALLOCATOR_OUT_OF_MEMORY; }
        uint32_t top_index, bottom_index;
        uint32_t index = block_allocator_size_to_bin_index(size, &top_index, &bottom_index);
        uint8_t bigger_bottoms;
        if (bottom_index == 7) {
                bigger_bottoms = 0;
        } else {
                bigger_bottoms = allocator->bottom_bins[top_index] & ~((1 << (bottom_index + 1)) - 1);
        }
        if (bigger_bottoms != 0) {
                bottom_index = __builtin_ctz(bigger_bottoms);
        } else {
                if (top_index == 31) { return BLOCK_ALLOCATOR_OUT_OF_MEMORY; }
                uint32_t bigger_tops = allocator->top_bins & ~((1 << (top_index + 1)) - 1);
                if (bigger_tops == 0) { return BLOCK_ALLOCATOR_OUT_OF_MEMORY; }
                top_index = __builtin_ctz(bigger_tops);
                bottom_index = __builtin_ctz(allocator->bottom_bins[top_index]);
        }
        index = (top_index << 3) | bottom_index;
        uint32_t block_index = allocator->bin_lists[index];
        block_allocator_block_t *block = &allocator->blocks[block_index];
        allocator->bin_lists[index] = block->bin_next;
        if (block->bin_next != BLOCK_ALLOCATOR_UNUSED) {
                allocator->blocks[block->bin_next].bin_prev = BLOCK_ALLOCATOR_HEAD_BITS | index;
        } else {
                allocator->bottom_bins[top_index] &= ~(1 << bottom_index);
                if (allocator->bottom_bins[top_index] == 0) { allocator->top_bins &= ~(1 << top_index); }
        }
        block->bin_prev = BLOCK_ALLOCATOR_UNUSED;
        block->bin_next = BLOCK_ALLOCATOR_UNUSED;
        uint32_t remaining_size = block->size - size;
        if (remaining_size > 0) {
                int res = block_allocator_insert(allocator, block->offset + size, remaining_size, block_index, block->mem_next);
                if (res == BLOCK_ALLOCATOR_OUT_OF_MEMORY) { return res; }
        }
        block->size = size;
        block_allocator_allocation_t alloc = {block->offset, block->size, block_index};
        *out_alloc = alloc;
        return BLOCK_ALLOCATOR_SUCCESS;
}

void block_allocator_free(block_allocator_t *allocator, block_allocator_allocation_t *alloc) {
        if (alloc->size == 0) { return; }
        block_allocator_block_t block = allocator->blocks[alloc->metadata];
        allocator->free_offset -= 1;
        allocator->free_blocks[allocator->free_offset] = alloc->metadata;
        if (block.mem_prev != BLOCK_ALLOCATOR_UNUSED && !block_allocator_is_used(&allocator->blocks[block.mem_prev])) {
                block_allocator_block_t prev_block = allocator->blocks[block.mem_prev];
                block.offset = prev_block.offset;
                block.size += prev_block.size;
                block_allocator_remove(allocator, block.mem_prev);
                block.mem_prev = prev_block.mem_prev;
        }
        if (block.mem_next != BLOCK_ALLOCATOR_UNUSED && !block_allocator_is_used(&allocator->blocks[block.mem_next])) {
                block_allocator_block_t next_block = allocator->blocks[block.mem_next];
                block.size += next_block.size;
                block_allocator_remove(allocator, block.mem_next);
                block.mem_next = next_block.mem_next;
        }
        block_allocator_insert(allocator, block.offset, block.size, block.mem_prev, block.mem_next);
}

void block_allocator_head(block_allocator_t *allocator, block_allocator_block_t *out_block) {
        *out_block = allocator->blocks[allocator->head_block];
}

int block_allocator_next(block_allocator_t *allocator, block_allocator_block_t *block, block_allocator_block_t *out_block) {
        if (block->mem_next == BLOCK_ALLOCATOR_UNUSED) { return BLOCK_ALLOCATOR_OUT_OF_MEMORY; }
        *out_block = allocator->blocks[block->mem_next];
        return BLOCK_ALLOCATOR_SUCCESS;
}


#endif // BLOCK_ALLOCATOR_IMPL

#endif // BLOCK_ALLOCATOR_H