/* saugns: Memory pool module.
 * Copyright (c) 2014, 2018-2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "mempool.h"
#ifndef SAU_MEM_DEBUG
/*
 * Debug-friendly memory handling? (Slower.)
 *
 * Enable to simply calloc every allocation.
 */
# define SAU_MEM_DEBUG 0
#endif
#include <stdlib.h>
#include <string.h>

/* Not important enough to query page size for; use large-enough default. */
#define DEFAULT_BLOCK_SIZE \
	(4096 * 2)

#define ALIGN_BYTES \
	sizeof(void*)

#define ALIGN_SIZE(size) \
	(((size) + (ALIGN_BYTES - 1)) & ~(ALIGN_BYTES - 1))

typedef struct BlockEntry {
	size_t free;
	void *mem;
} BlockEntry;

typedef struct BlockArr {
	BlockEntry *a;
	size_t count, first_i;
	size_t asize;
} BlockArr;

/*
 * Extend memory block array.
 *
 * \return true, or false on allocation failure
 */
static bool BlockArr_upsize(BlockArr *restrict o) {
	size_t new_asize = (o->asize > 0) ? (o->asize << 1) : 1;
	BlockEntry *new_a = realloc(o->a, sizeof(BlockEntry) * new_asize);
	if (!new_a)
		return false;
	o->a = new_a;
	o->asize = new_asize;
	return true;
}

/*
 * Allocate new memory block,
 * initialized to zero bytes.
 *
 * \return allocated memory, or NULL on allocation failure
 */
static void *BlockArr_add(BlockArr *restrict o,
		size_t size_alloc, size_t size_used) {
	if (o->count == o->asize && !BlockArr_upsize(o))
		return NULL;
	void *mem = calloc(1, size_alloc);
	if (!mem)
		return NULL;
	BlockEntry *b = &o->a[o->count++];
	b->free = size_alloc - size_used;
	b->mem = mem;
#if !SAU_MEM_DEBUG
	/*
	 * Skip fully used blocks in binary searches.
	 */
	while (o->first_i < o->count) {
		if (!o->a[o->first_i].free) ++o->first_i;
		else break;
	}
#endif
	return mem;
}

/*
 * Free all memory blocks.
 */
static void BlockArr_clear(BlockArr *restrict o) {
	for (size_t i = 0; i < o->count; ++i) {
		free(o->a[i].mem);
	}
	free(o->a);
}

#if !SAU_MEM_DEBUG
/*
 * Locate the first block with the smallest size into which \p size fits,
 * using binary search. If found, \p id will be set to the id.
 *
 * \return true if found, false if not
 */
static bool BlockArr_first_smallest(const BlockArr *restrict o,
		size_t size, size_t *restrict id) {
	size_t i;
	ptrdiff_t min = o->first_i;
	ptrdiff_t max = o->count - 1;
	for (;;) {
		i = ((size_t)(min + max) >> 1);
		if (o->a[i].free < size) {
			min = i + 1;
			if (max < min) {
				++i;
				break;
			}
		} else {
			max = i - 1;
			if (max < min) {
				break;
			}
		}
	}
	if (i < o->count && o->a[i].free >= size) {
		*id = i;
		return true;
	}
	return false;
}

/*
 * Locate the first block with the smallest size greater than \p size,
 * using binary search. If found, \p id will be set to the id.
 *
 * \return true if found, false if not
 */
#define BlockArr_first_greater(o, size, id) \
	BlockArr_first_smallest((o), (size) + 1, (id))

/*
 * Copy the blocks from \p from to \p to upwards one step.
 *
 * Technically, only the first block of each successive size is overwritten,
 * by the previous such block, until finally the last such block overwrites
 * the block at \p to.
 */
static void BlockArr_copy_up_one(BlockArr *restrict o,
		size_t to, size_t from) {
	if (from == (to - 1) || o->a[from].free == o->a[to - 1].free) {
		/*
		 * Either there are no blocks in-between, or they all have
		 * the same free space as the first; simply set the last to
		 * the first.
		 */
		o->a[to] = o->a[from];
	} else {
		/*
		 * Find the first block of the next larger size and recurse.
		 * Afterwards that block is overwritten by the original
		 * first block of this call.
		 */
		size_t higher_from;
		BlockArr_first_greater(o, o->a[from].free, &higher_from);
		BlockArr_copy_up_one(o, to, higher_from);
		o->a[higher_from] = o->a[from];
	}
}
#endif

struct SAU_MemPool {
	BlockArr blocks;
	size_t block_size;
};

/**
 * Create instance.
 *
 * \p block_size specifies the requested size of each memory block managed by
 * the memory pool. If 0 is passed, the default is used: 8KB, meant to take
 * up at least one memory page on the system. Performance measurement is
 * advised if a custom size is used.
 *
 * Allocations larger than the requested block size can still be made;
 * these are given individual blocks sized according to need.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_MemPool *SAU_create_MemPool(size_t block_size) {
	SAU_MemPool *o = calloc(1, sizeof(SAU_MemPool));
	if (!o)
		return NULL;
	o->block_size = (block_size > 0) ?
		ALIGN_SIZE(block_size) :
		DEFAULT_BLOCK_SIZE;
	return o;
}

/**
 * Destroy instance.
 */
void SAU_destroy_MemPool(SAU_MemPool *restrict o) {
	if (!o)
		return;
	BlockArr_clear(&o->blocks);
	free(o);
}

/**
 * Allocate block of \p size within the memory pool,
 * initialized to zero bytes.
 *
 * \return allocated memory, or NULL on allocation failure
 */
void *SAU_MemPool_alloc(SAU_MemPool *restrict o, size_t size) {
#if !SAU_MEM_DEBUG
	size_t i = o->blocks.count;
	void *mem;
	size = ALIGN_SIZE(size);
	/*
	 * If blocks exist and the most spacious can hold the size,
	 * pick least-free-space best fit using binary search.
	 * Otherwise, use a new block.
	 */
	if ((i > 0 && size <= o->blocks.a[i - 1].free) &&
			BlockArr_first_smallest(&o->blocks, size, &i)) {
		size_t offset = o->block_size - o->blocks.a[i].free;
		mem = &((char*)o->blocks.a[i].mem)[offset];
		o->blocks.a[i].free -= size;
	} else {
		size_t alloc_size = (size > o->block_size) ?
			size :
			o->block_size;
		mem = BlockArr_add(&o->blocks, alloc_size, size);
		if (!mem)
			return NULL;
	}
	/*
	 * Sort blocks after allocation so that binary search will work.
	 */
	if (i > 0) {
		/*
		 * The free space of the block at i is temporarily
		 * fudged in order for binary search to work reliably.
		 */
		size_t j;
		size_t i_free = o->blocks.a[i].free;
		o->blocks.a[i].free = o->blocks.a[i - 1].free;
		if (BlockArr_first_greater(&o->blocks, i_free, &j) && j < i) {
			/*
			 * Copy blocks upwards, then
			 * set the one at j to the one originally at i
			 * (and give it the restored free space value).
			 */
			BlockEntry tmp = o->blocks.a[i];
			tmp.free = i_free;
			BlockArr_copy_up_one(&o->blocks, i, j);
			o->blocks.a[j] = tmp;
		} else {
			o->blocks.a[i].free = i_free;
		}
	}
	return mem;
#else /* SAU_MEM_DEBUG */
	return BlockArr_add(&o->blocks, size, size);
#endif
}

/**
 * Allocate block of \p size within the memory pool,
 * copied from \p src if not NULL, otherwise
 * initialized to zero bytes.
 *
 * \return allocated memory, or NULL on allocation failure
 */
void *SAU_MemPool_memdup(SAU_MemPool *restrict o,
		const void *restrict src, size_t size) {
	void *mem = SAU_MemPool_alloc(o, size);
	if (!mem)
		return NULL;
	if (src != NULL)
		memcpy(mem, src, size);
	return mem;
}
