#include "texture_storage.h"

void
texture_storage_destroy(TextureStorage* storage)
{
  for (int i = 0; i < TEXTURE_COUNT; i++) {
    UnloadTexture(*storage->data[i]->texture);
  }
}

void
texture_storage_load(TextureStorage* storage,
                     const char* path,
                     TEXTURE_TYPE type,
                     Vector2 size)
{
  assert(storage != NULL);
  if (Vector2Equals(size, Vector2Zero())) {
    Texture tex = LoadTexture(path);
    *storage->data[type]->texture = tex;

    storage->data[type]->width = tex.width;
    storage->data[type]->height = tex.height;
    return;
  }

  TraceLog(LOG_INFO, "Loading texture %s", path);
  Image image = LoadImage(path);

  TraceLog(LOG_INFO, "Resizing texture %s to %f, %f", path, size.x, size.y);
  ImageResize(&image, (int)size.x, (int)size.y);

  TraceLog(LOG_INFO, "Loading texture %s", path);
  *storage->data[type]->texture = LoadTextureFromImage(image);

  storage->data[type]->width = image.width;
  storage->data[type]->height = image.height;

  TraceLog(LOG_INFO, "Unloading image %s", path);
  UnloadImage(image);
}

Texture2D*
texture_storage_get(TextureStorage* storage, TEXTURE_TYPE type)
{
  return storage->data[type]->texture;
}
