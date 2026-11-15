#include "heap.h"
#include "pages.h"

enum { HEAP_ALIGN = 16 };

struct block {
    struct block* next;
    unsigned int size;
    int used;
};

struct arena {
    struct arena* next;
} __attribute__((aligned(HEAP_ALIGN)));

_Static_assert(sizeof(struct block) % HEAP_ALIGN == 0, "Heap block alignment");
enum { MAX_BLOCK_SIZE = PAGE_SIZE - sizeof(struct arena) - sizeof(struct block) };
static struct arena* arenas;

static struct block* first_block(struct arena* arena)
{
    return (struct block*)(arena + 1);
}

void heap_init(void)
{
    arenas = 0;
}

static void* take_block(struct block* block, unsigned int size)
{
    if (block->size >= size + sizeof(struct block) + HEAP_ALIGN) {
        struct block* rest = (struct block*)((unsigned char*)(block + 1) + size);
        *rest = (struct block){ block->next, block->size - size - sizeof(*rest), 0 };
        block->size = size;
        block->next = rest;
    }
    block->used = 1;
    return block + 1;
}

void* kmalloc(unsigned int size)
{
    if (!size || size > MAX_BLOCK_SIZE)
        return 0;
    size = (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
    for (struct arena* arena = arenas; arena; arena = arena->next)
        for (struct block* block = first_block(arena); block; block = block->next)
            if (!block->used && block->size >= size)
                return take_block(block, size);

    struct arena* arena = page_alloc();
    if (!arena)
        return 0;
    arena->next = arenas;
    arenas = arena;
    struct block* block = first_block(arena);
    *block = (struct block){ 0, MAX_BLOCK_SIZE, 0 };
    return take_block(block, size);
}

static void merge_next(struct block* block)
{
    struct block* next = block->next;
    if (next && !next->used) {
        block->size += sizeof(*next) + next->size;
        block->next = next->next;
    }
}

void kfree(void* pointer)
{
    if (!pointer)
        return;
    for (struct arena** link = &arenas; *link; link = &(*link)->next) {
        struct arena* arena = *link;
        struct block* first = first_block(arena);
        struct block* previous = 0;
        for (struct block* block = first; block; block = block->next) {
            if (block + 1 != pointer) {
                previous = block;
                continue;
            }
            if (!block->used)
                return;
            block->used = 0;
            merge_next(block);
            if (previous && !previous->used)
                merge_next(previous);
            if (!first->used && !first->next) {
                *link = arena->next;
                page_free(arena);
            }
            return;
        }
    }
}

static void fill(volatile unsigned char* block, unsigned int size, unsigned char pattern)
{
    for (unsigned int i = 0; i < size; ++i)
        block[i] = pattern;
}

static int matches(const volatile unsigned char* block, unsigned int size,
    unsigned char pattern)
{
    for (unsigned int i = 0; i < size; ++i)
        if (block[i] != pattern)
            return 0;
    return 1;
}

int heap_self_test(void)
{
    const unsigned int initial = pages_free_count();
    /* The coalescing test needs a fresh arena, even with live shell blocks. */
    struct arena* const saved_arenas = arenas;
    arenas = 0;
    unsigned char* a = kmalloc(16);
    unsigned char* b = kmalloc(48);
    unsigned char* guard = kmalloc(37);
    unsigned char* joined = 0;
    int passed = 0;
    if (a && b && guard &&
        !(((unsigned long long)a | (unsigned long long)b |
            (unsigned long long)guard) & (HEAP_ALIGN - 1))) {
        fill(a, 16, 0xaa);
        fill(b, 48, 0x55);
        fill(guard, 37, 0x37);
        if (matches(a, 16, 0xaa) && matches(b, 48, 0x55) && matches(guard, 37, 0x37)) {
            const unsigned long long address = (unsigned long long)a;
            kfree(a);
            kfree(b);
            a = b = 0;
            joined = kmalloc(80);
            if ((unsigned long long)joined == address) {
                fill(joined, 80, 0xa5);
                passed = matches(joined, 80, 0xa5) && matches(guard, 37, 0x37);
            }
        }
    }
    kfree(joined);
    kfree(guard);
    kfree(b);
    kfree(a);
    const int released = !arenas && pages_free_count() == initial;
    arenas = saved_arenas;
    return passed && released;
}
