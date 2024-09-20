#include "arena.h"

ArenaAllocator*
MakeArenaAllocator(size_t capacity)
{
  ArenaAllocator* arena = (ArenaAllocator*)malloc(sizeof(ArenaAllocator));
  arena->data = (char*)malloc(capacity);
  arena->capacity = capacity;
  arena->size = 0;
  return arena;
}

void*
ArenaAlloc(ArenaAllocator* arena, size_t size)
{
  assert(arena->size + size < arena->capacity);
  void* ptr = arena->data + arena->size;
  arena->size += size;
  return ptr;
}

void
ArenaFree(ArenaAllocator* arena)
{
  free(arena->data);
  free(arena);
}

int
GetTotalMemory(ArenaAllocator* arena)
{
  return arena->capacity;
}

int
GetFreeMemory(ArenaAllocator* arena)
{
  return arena->capacity - arena->size;
}
