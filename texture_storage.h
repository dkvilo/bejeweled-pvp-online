#ifndef __ME_TEX_STORAGE
#define __ME_TEX_STORAGE

#include <raylib.h>
#include <raymath.h>
#include <string.h>

#include "arena.h"

typedef enum TEXTURE_STORAGE
{
  TEXTURE0,
  TEXTURE1,
  TEXTURE2,
  TEXTURE3,
  TEXTURE4,
  TEXTURE5,
  TEXTURE6,
  TEXTURE7,
  TEXTURE8,
  TEXTURE9,
  TEXTURE_COUNT,
} TEXTURE_TYPE;

typedef struct TextureStorageEntry {
  Texture *texture;
  int32_t width;
  int32_t height;
} TextureStorageEntry;

typedef struct TextureStorage
{
  TextureStorageEntry *data[TEXTURE_COUNT];
} TextureStorage;

void
texture_storage_destroy(TextureStorage* storage);

void
texture_storage_load(TextureStorage* storage,
                     const char* path,
                     TEXTURE_TYPE type,
                     Vector2 size);


Texture2D*
texture_storage_get(TextureStorage* storage, TEXTURE_TYPE type);

#endif // __ME_TEX_STORAGE