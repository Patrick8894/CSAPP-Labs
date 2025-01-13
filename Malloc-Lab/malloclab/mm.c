/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Reuse_block this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {

    /* Team name */
    "xxx",
    /* First member's full name */
    "Bo-Hao Wu",
    /* First member's email address */
    "bohaowu2@illinois.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* Constants and macros */
#define ALIGNMENT 8
#define WSIZE 4 /* Word & header/footer size in Byte */
#define CHUNKSIZE (1<<12) /* Chunk size in Byte */

/* Global variables */
static char *sg_free_list[13];

/* Helper functions for block management */
static void mm_put(char *ptr, unsigned int val)
{
    *(unsigned int *)ptr = val;
}

static unsigned int mm_get(char *ptr)
{
    return *(unsigned int *)ptr;
}

static unsigned int build_header_footer(size_t size, int alloc)
{
    return size | alloc;
}

static size_t get_size(char *ptr)
{
    return mm_get(ptr) & ~0x7;
}

static size_t get_block_size(char *block)
{
    return get_size(block - WSIZE);
}

static int get_alloc(char *block)
{
    return mm_get(block - WSIZE) & 0x1;
}

static char *get_next_block(char *block)
{
    return block + get_block_size(block);
}

static char *get_prev_block(char *block)
{
    return block - get_size(block - WSIZE * 2);
}

static char *get_next_free_addr(char *block)
{
    return block + WSIZE;
}

static char *get_prev_free_addr(char *block)
{
    return block;
}

static char *get_next_free(char *block)
{
    return *(char**)(get_next_free_addr(block));
}

static char *get_prev_free(char *block)
{
    return *(char**)(get_prev_free_addr(block));
}

static void set_next_free(char *block, char *next)
{
    mm_put(get_next_free_addr(block), (unsigned int)next);
}

static void set_prev_free(char *block, char *prev)
{
    mm_put(get_prev_free_addr(block), (unsigned int)prev);
}

static void *get_header_addr(char *block)
{
    return block - WSIZE;
}

static void *get_footer_addr(char *block)
{
    return block + get_block_size(block) - WSIZE * 2;
}

/* Helper functions for free list management */
static void init_free_list()
{
    for (int i = 0; i < 13; i++)
        sg_free_list[i] = NULL;
}

static int find_list_index(size_t size)
{
    int index = 0;
    while (size > 1 && index < 12)
    {
        size >>= 1;
        index++;
    }
    return index;
}

static void insert_free_block(char *block)
{
    size_t size = get_block_size(block);
    int index = find_list_index(size);
    char *root = sg_free_list[index];
    char *prev = NULL;
    char *next = root;
    while (next)
    {
        if (size < get_block_size(next))
            break;
        prev = next;
        next = get_next_free(next);
    }

    if (next == root)
        sg_free_list[index] = block;

    set_prev_free(block, prev);
    set_next_free(block, next);

    if (prev)
        set_next_free(prev, block);

    if (next)
        set_prev_free(next, block);
}

static void remove_free_block(char *block)
{
    size_t size = get_block_size(block);
    int index = find_list_index(size);
    char *prev = get_prev_free(block);
    char *next = get_next_free(block);

    if (prev)
        set_next_free(prev, next);
    else
        sg_free_list[index] = next;

    if (next)
        set_prev_free(next, prev);
}

static char *find_fit(size_t size)
{
    int index = find_list_index(size);
    for (int i = index; i < 13; i++)
    {
        char *block = sg_free_list[i];
        while (block)
        {
            if (get_block_size(block) >= size)
                return block;
            block = get_next_free(block);
        }
    }
    return NULL;
}

static void use_block(void *block, size_t size) {

    size_t block_size = get_block_size(block);
    size_t remain_size = block_size - size;

    remove_free_block(block);

    if (remain_size >= WSIZE * 4)
    {
        mm_put(get_header_addr(block), build_header_footer(size, 1));
        mm_put(get_footer_addr(block), build_header_footer(size, 1));

        char *next_block = get_next_block(block);
        mm_put(get_header_addr(next_block), build_header_footer(remain_size, 0));
        mm_put(get_footer_addr(next_block), build_header_footer(remain_size, 0));

        insert_free_block(next_block);
    }
    else
    {
        mm_put(get_header_addr(block), build_header_footer(block_size, 1));
        mm_put(get_footer_addr(block), build_header_footer(block_size, 1));
    }
}

/* Helper functions for heap management */
static char *coalesce(char *block){

    char *prev_block = get_prev_block(block);
    char *next_block = get_next_block(block);
    size_t prev_alloc = get_alloc(prev_block);
    size_t next_alloc = get_alloc(next_block);
    size_t size = get_block_size(block);
    size_t new_size = size;

    char *new_header_addr = prev_alloc ? get_header_addr(block) : get_header_addr(prev_block);
    char *new_footer_addr = next_alloc ? get_footer_addr(block) : get_footer_addr(next_block);

    if (!prev_alloc)
    {
        new_size += get_block_size(prev_block);
        remove_free_block(prev_block);
    }

    if (!next_alloc)
    {
        new_size += get_block_size(next_block);
        remove_free_block(next_block);
    }

    mm_put(new_header_addr, build_header_footer(new_size, 0));
    mm_put(new_footer_addr, build_header_footer(new_size, 0));

    insert_free_block(new_header_addr + WSIZE);
    return new_header_addr + WSIZE;
}

static char *extend_heap(size_t words)
{
    size_t size;
    void *block;

    size = words % 8 == 0 ? words : (words + 8 - (words % 8));
    if ((block = mem_sbrk(size)) == (void *)-1)
        return NULL;

    mm_put(get_header_addr(block), build_header_footer(size, 0));
    mm_put(get_footer_addr(block), build_header_footer(size, 0));
    mm_put(get_header_addr(get_next_block(block)), build_header_footer(0, 1));

    return coalesce(block);
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{   
    init_free_list();

    char *block = mem_sbrk(4 * WSIZE);
    if (block == (void *)-1)
        return -1;

    mm_put(block, 0);
    mm_put(block + WSIZE, build_header_footer(WSIZE * 2, 1));
    mm_put(block + WSIZE * 2, build_header_footer(WSIZE * 2, 1));
    mm_put(block + WSIZE * 3, build_header_footer(0, 1));

    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;

    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t block_size;
    size_t extend_size;
    char *block;

    if (size == 0)
        return NULL;

    block_size = size % 8 == 0 ? size : (size + 8 - (size % 8));
    block_size += WSIZE * 2;

    if ((block = find_fit(block_size)) != NULL)
    {
        use_block(block, block_size);
        return block;
    }

    extend_size = block_size > CHUNKSIZE ? block_size : CHUNKSIZE;
    if ((block = extend_heap(extend_size)) == NULL)
        return NULL;

    use_block(block, block_size);
    return block;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    mm_put(get_header_addr(bp), build_header_footer(get_block_size(bp), 0));
    mm_put(get_footer_addr(bp), build_header_footer(get_block_size(bp), 0));

    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *newptr;
    size_t copySize;

    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }

    if (ptr == NULL)
        return mm_malloc(size);

    if ((newptr = mm_malloc(size)) == NULL)
        return NULL;

    copySize = get_size(ptr) - WSIZE * 2;
    if (size < copySize)
        copySize = size;

    memcpy(newptr, ptr, copySize);
    mm_free(ptr);

    return newptr;
}