# block_allocator

Byte range suballocator in C or Odin, for suballocating GPU heaps, based on the ideas behind Sebastian Aaltonen's [OffsetAllocator](https://github.com/sebbbi/OffsetAllocator/blob/main/offsetAllocator.cpp).

These files are intended as reference implementations to be copied into your own codebase and modified / instrumented as you see fit. 
`test.c` and `test.odin` contain example code which partially tests the allocators for correctness, but they have not been validated
beyond that.

## Odin Usage

```odin
allocator := block_allocator_init(max(u32))
defer block_allocator_destroy(&allocator)
alloc, ok := block_alloc(&allocator, 512)
assert(ok)
block_free(&allocator, &alloc)
```

## C Usage

Include `block_allocator.h` everywhere it is needed, and `#define BLOCK_ALLOCATOR_IMPL` in _one_ compilation unit before including the
header to pull in the implementation.

```c
block_allocator_t allocator;
block_allocator_init(0xffffffff, &allocator);
block_allocator_allocation_t alloc;
int res = block_allocator_alloc(&allocator, size, &alloc);
assert(ok == BLOCK_ALLOCATOR_SUCCESS);
block_allocator_free(&allocator, &alloc);
block_allocator_destroy(&allocator);
```

## Visualising Heap Fragmentation

I also provide `block_allocator_head` and `block_allocator_next` functions, which can be used to traverse the heap in
memory order. This is useful for building visualisations of heap fragmentation, in conjuction with `block_allocator_is_used`
to determine if a memory blcok is currently allocated or available.