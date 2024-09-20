#if !defined(__ALLOCATOR_H)
#define __ALLOCATOR_H

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KB(x) ((size_t)(x) << 10)
#define MB(x) ((size_t)(x) << 20)
#define GB(x) ((size_t)(x) << 30)

typedef struct ArenaAllocator
{
  char* data;
  size_t capacity;
  size_t size;
} ArenaAllocator;

ArenaAllocator*
MakeArenaAllocator(size_t capacity);

void*
ArenaAlloc(ArenaAllocator* arena, size_t size);

void
ArenaFree(ArenaAllocator* arena);

int
GetTotalMemory(ArenaAllocator* arena);

int
GetFreeMemory(ArenaAllocator* arena);

void
export_memory_arena(ArenaAllocator* arena, const char* path);

#endif