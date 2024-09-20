/**
 * $Author David Kviloria
 * $Last Modified 2019
 */
#include <arpa/inet.h>
#include <fcntl.h>
#include <math.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arena.c"
#include "texture_storage.c"

#define PORT 8080
#define BUFLEN 512
#define MAX_GAMES 100
#define BOARD_SIZE 8
#define TILE_SIZE 60
#define ANIMATION_DURATION 0.05f

typedef enum
{
  EMPTY,
  T_RED,
  T_BLUE,
  T_GREEN,
  T_YELLOW,
  T_PURPLE,
  T_SPECIAL
} Tile;

struct GameState
{
  int32_t game_id;
  Tile board[BOARD_SIZE][BOARD_SIZE];
  int32_t current_turn;
  int32_t player1_score;
  int32_t player2_score;
  bool game_started;
  bool game_over;
};

typedef enum
{
  MAIN_MENU,
  IN_GAME
} GameScreen;

Font font = { 0 };
int sockfd;
struct sockaddr_in server_addr, client_addr;
int player_id = -1;
bool connected = false;

struct GameState game_state;
struct GameState previous_board;

GameScreen current_screen = MAIN_MENU;

Vector2 selected_tile = { -1, -1 };
Vector2 hover_tile = { -1, -1 };

float tile_offsets[BOARD_SIZE][BOARD_SIZE] = { 0 };
bool animating = false;
float animation_timer = 0;

const float SWAP_ANIMATION_DURATION = 0.05f;
bool animating_swap = false;
float swap_animation_timer = 0.0f;
Vector2 swap_from = { -1, -1 };
Vector2 swap_to = { -1, -1 };

void
die(const char* s)
{
  perror(s);
  exit(1);
}

void
set_socket_nonblocking(int sock)
{
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1) {
    die("fcntl F_GETFL failed");
  }
  if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
    die("fcntl F_SETFL failed");
  }
}

void
send_connect_request()
{
  char buffer[BUFLEN];
  strcpy(buffer, "CONNECT");
  if (sendto(sockfd,
             buffer,
             strlen(buffer),
             0,
             (struct sockaddr*)&server_addr,
             sizeof(server_addr)) == -1) {
    die("sendto() failed");
  }
}

void
send_disconnect_request()
{
  char buffer[BUFLEN];
  strcpy(buffer, "DISCONNECT");
  if (sendto(sockfd,
             buffer,
             strlen(buffer),
             0,
             (struct sockaddr*)&server_addr,
             sizeof(server_addr)) == -1) {
    die("sendto() failed");
  }
  connected = false;
  player_id = -1;
  current_screen = MAIN_MENU;
}

void
send_move(int fromX, int fromY, int toX, int toY)
{
  char buffer[BUFLEN];
  snprintf(buffer, BUFLEN, "%d %d %d %d %d", player_id, fromX, fromY, toX, toY);
  if (sendto(sockfd,
             buffer,
             strlen(buffer),
             0,
             (struct sockaddr*)&server_addr,
             sizeof(server_addr)) == -1) {
    die("sendto() failed");
  }
}

void
blit_text(Font* font,
          const char* str,
          Vector2 position,
          float size,
          Color color)
{
  DrawTextEx(*font,
             str,
             (Vector2){ position.x + 1.5f, position.y + 1.5f },
             size,
             1.0f,
             BLACK);
  DrawTextEx(*font, str, position, size, 1.0f, color);
}

bool
draw_button(const char* text, Rectangle bounds, Color color)
{

  Vector2 mousePoint = GetMousePosition();
  bool is_over = CheckCollisionPointRec(mousePoint, bounds);

  if (is_over) {
    color = Fade(color, 0.8f);
  }

  Vector2 size = MeasureTextEx(font, text, 30, 1.0);

  if (bounds.width < size.x) {
    bounds.width = size.x + 20;
  }

  DrawRectangleRounded(bounds, 0.2, 10, color);
  DrawRectangleRoundedLines(bounds, 0.2, 10, 2, BLACK);

  blit_text(
    &font,
    text,
    (Vector2){ bounds.x + 10, bounds.y + bounds.height / 2 - size.y / 2 },
    30,
    BLACK);

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    if (is_over) {
      return true;
    }
  }

  return false;
}

void
reset_game_state()
{
  connected = false;
  player_id = -1;
  memset(&game_state, 0, sizeof(struct GameState));
  current_screen = MAIN_MENU;
}

void
update_animation(float delta_time)
{
  bool any_animating = false;

  for (int y = 0; y < BOARD_SIZE; y++) {
    for (int x = 0; x < BOARD_SIZE; x++) {
      if (tile_offsets[y][x] != 0) {
        any_animating = true;

        float progress = animation_timer / ANIMATION_DURATION;
        if (progress > 1.0f)
          progress = 1.0f;

        float eased_progress = 1.0f - (1.0f - progress) * (1.0f - progress);
        tile_offsets[y][x] = -TILE_SIZE + eased_progress * TILE_SIZE;

        animation_timer += delta_time;

        if (animation_timer >= ANIMATION_DURATION) {
          tile_offsets[y][x] = 0;
          animation_timer = 0;
        }
      }
    }
  }

  animating = any_animating;

  if (animating_swap) {
    swap_animation_timer += delta_time;
    if (swap_animation_timer >= SWAP_ANIMATION_DURATION) {
      animating_swap = false;
      swap_animation_timer = 0.0f;

      // Swap tiles in the game_state
      // Tile temp = game_state.board[(int)swap_from.y][(int)swap_from.x];
      // game_state.board[(int)swap_from.y][(int)swap_from.x] =
      //   game_state.board[(int)swap_to.y][(int)swap_to.x];
      // game_state.board[(int)swap_to.y][(int)swap_to.x] = temp;

      swap_from = (Vector2){ -1, -1 };
      swap_to = (Vector2){ -1, -1 };
    }
  }
}

void
start_animation()
{
  animating = true;
  animation_timer = 0;
  for (int i = 0; i < BOARD_SIZE; i++) {
    for (int j = 0; j < BOARD_SIZE; j++) {
      if (game_state.board[i][j] != EMPTY) {
        tile_offsets[i][j] = -TILE_SIZE * (i + 1);
      }
    }
  }
}

void
receive_server_message()
{
  char buffer[BUFLEN];
  socklen_t slen = sizeof(server_addr);
  int recv_len =
    recvfrom(sockfd, buffer, BUFLEN, 0, (struct sockaddr*)&server_addr, &slen);
  if (recv_len == -1) {
    return;
  }

  if (strncmp(buffer, "PLAYER_ID:", 10) == 0) {
    sscanf(buffer, "PLAYER_ID:%d", &player_id);
    printf("Assigned Player ID: %d\n", player_id);
    connected = true;
    current_screen = IN_GAME;
  } else {
    struct GameState new_state;
    memcpy(&new_state, buffer, sizeof(struct GameState));

    for (int y = 0; y < BOARD_SIZE; y++) {
      for (int x = 0; x < BOARD_SIZE; x++) {
        if (previous_board.board[y][x] == EMPTY &&
            new_state.board[y][x] != EMPTY) {
          tile_offsets[y][x] = -TILE_SIZE;
        }
      }
    }

    start_animation();

    memcpy(&previous_board, &game_state, sizeof(struct GameState));
    memcpy(&game_state, &new_state, sizeof(struct GameState));

    if (!game_state.game_over && new_state.game_over) {
      printf("Game Over! Player 1 Score: %d, Player 2 Score: %d\n",
             new_state.player1_score,
             new_state.player2_score);
    }
  }
}

Vector2
tile_to_sprite_coord(Tile tile)
{
  switch (tile) {
    case T_RED:
      return (Vector2){ 0, 3 };
      break;

    case T_BLUE:
      return (Vector2){ 0, 2 };
      break;

    case T_GREEN:
      return (Vector2){ 0, 4 };
      break;

    case T_YELLOW:
      return (Vector2){ 0, 0 };
      break;

    case T_PURPLE:
      return (Vector2){ 0, 6 };
      break;

    case T_SPECIAL:
      return (Vector2){ 0, 7 };
      break;

    default:
      break;
  }
}

void
draw_sprite_frame(Texture* texture,
                  Vector2 size,
                  Vector2 coords,
                  Vector2 position,
                  float scale,
                  Color color)
{
  int32_t spire_sheet_width = texture->width;
  int32_t spire_sheet_height = texture->height;

  Vector2 sprite_size = { size.x, size.y };

  Vector2 sprite_pos = {
    (sprite_size.x * coords.x),
    (sprite_size.y * coords.y),
  };

  Rectangle source = {
    sprite_pos.x, sprite_pos.y, sprite_size.x, sprite_size.y
  };
  Rectangle dest = {
    position.x, position.y, sprite_size.x * scale, sprite_size.y * scale
  };

  Vector2 origin = { 0.0f, 0.0f };
  DrawTexturePro(*texture, source, dest, origin, 0.0f, color);
}

void
draw_board(Texture* sprite_sheet)
{

  static float frame_timer[BOARD_SIZE][BOARD_SIZE] = { 0.0f };
  static int32_t coordx[BOARD_SIZE][BOARD_SIZE] = { 0 };

  const int32_t max_frames = 19;
  const float frame_duration = 0.15f;

  for (int y = 0; y < BOARD_SIZE; y++) {
    for (int x = 0; x < BOARD_SIZE; x++) {
      bool is_selected = (x == (int)selected_tile.x && y == (int)selected_tile.y);
      bool is_hovered = (x == (int)hover_tile.x && y == (int)hover_tile.y);
      bool is_adjacent = false;

      if (selected_tile.x != -1 && !is_selected) {
        if ((abs(x - selected_tile.x) == 1 && y == (int)selected_tile.y) ||
            (abs(y - selected_tile.y) == 1 && x == (int)selected_tile.x)) {
          is_adjacent = true;
        }
      }

      Tile display_tile = game_state.board[y][x];

      float pos_x = 100 + x * TILE_SIZE;
      float pos_y = 100 + y * TILE_SIZE;

      if (animating_swap) {
        if ((x == (int)swap_from.x && y == (int)swap_from.y) ||
            (x == (int)swap_to.x && y == (int)swap_to.y)) {
          float t = swap_animation_timer / SWAP_ANIMATION_DURATION;
          if (t > 1.0f) t = 1.0f;

          float dx = (swap_to.x - swap_from.x) * TILE_SIZE * t;
          float dy = (swap_to.y - swap_from.y) * TILE_SIZE * t;

          if (x == (int)swap_from.x && y == (int)swap_from.y) {
            pos_x += dx;
            pos_y += dy;
          } else if (x == (int)swap_to.x && y == (int)swap_to.y) {
            pos_x -= dx;
            pos_y -= dy;
          }
        }
      }

      pos_y += tile_offsets[y][x];

      Rectangle tileRect = {
        pos_x + 5.0f, pos_y + 5.0f, TILE_SIZE - 10.0f, TILE_SIZE - 10.0f
      };
      Color tileColor;

      switch (display_tile) {
        case T_RED:
          tileColor = RED;
          break;
        case T_BLUE:
          tileColor = BLUE;
          break;
        case T_GREEN:
          tileColor = GREEN;
          break;
        case T_YELLOW:
          tileColor = YELLOW;
          break;
        case T_PURPLE:
          tileColor = PURPLE;
          break;
        default:
          tileColor = LIGHTGRAY;
          break;
      }

      DrawRectangleRounded(tileRect, 0.2f, 10, BROWN);
      DrawRectangleRoundedLines(tileRect, 0.2f, 10, 2, BLACK);

      frame_timer[y][x] += GetFrameTime();

      if (frame_timer[y][x] >= frame_duration) {
        frame_timer[y][x] = 0.0f;
        coordx[y][x]++;

        if (coordx[y][x] > max_frames) {
          coordx[y][x] = 0;
        }
      }

      Vector2 coords = tile_to_sprite_coord(display_tile);
      Vector2 sprite_coords = {
        coords.x, coords.y
      };

      if (display_tile == T_SPECIAL || is_selected) {
        sprite_coords.x = coordx[y][x] * 84;
      }

      draw_sprite_frame(sprite_sheet,
                        (Vector2){ 84.0f, 84.0f },
                        sprite_coords,
                        (Vector2){ tileRect.x, tileRect.y },
                        0.6f,
                        WHITE);

      if (is_selected) {
        DrawRectangleRoundedLines(tileRect, 0.2f, 10, 4, WHITE);
      } else if (is_adjacent) {
        DrawRectangleRoundedLines(tileRect, 0.2f, 10, 2, LIGHTGRAY);
      } else if (is_hovered) {
        DrawRectangleRoundedLines(tileRect, 0.2f, 10, 2, DARKGRAY);
      }
    }
  }
}

int
main()
{

  SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);

  InitWindow(680, 720, "Bejeweled PvP");
  SetTargetFPS(60);

  font.baseSize = 120;
  font.glyphCount = 95;

  uint32_t fileSize = 0;
  uint8_t* fileData = LoadFileData("res/fonts/times.ttf", &fileSize);
  uint32_t fontSize = 128;

  font.glyphs = LoadFontData(fileData, fileSize, fontSize, 0, 0, FONT_SDF);
  Image atlas =
    GenImageFontAtlas(font.glyphs, &font.recs, font.glyphCount, fontSize, 0, 1);
  font.texture = LoadTextureFromImage(atlas);

  UnloadImage(atlas);
  UnloadFileData(fileData);
  SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

  ArenaAllocator* arena = MakeArenaAllocator((1024 << 2));

  TextureStorage* texture_storage;
  texture_storage = (TextureStorage*)ArenaAlloc(arena, sizeof(TextureStorage));
  for (uint32_t i = 0; i < TEXTURE_COUNT; i++) {
    texture_storage->data[i] =
      (TextureStorageEntry*)ArenaAlloc(arena, sizeof(TextureStorageEntry));
    texture_storage->data[i]->texture =
      (Texture*)ArenaAlloc(arena, sizeof(Texture));
  }

  texture_storage_load(
    texture_storage, "./res/spritesheet.png", TEXTURE0, Vector2Zero());

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    die("socket");
  }

  memset((char*)&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  set_socket_nonblocking(sockfd);

  Rectangle connectButton = {
    GetScreenWidth() / 2 - 250 / 2, GetScreenHeight() / 2, 200, 50
  };
  Rectangle disconnectButton = { GetScreenWidth() - (100 + 185), 20, 180, 40 };

  memset(&previous_board, 0, sizeof(struct GameState));
  Texture* sprite_sheet = texture_storage_get(texture_storage, TEXTURE0);

  while (!WindowShouldClose()) {
    float delta_time = GetFrameTime();
    update_animation(delta_time);
    receive_server_message();

    BeginDrawing();
    ClearBackground((Color){ 30, 39, 46, 255 });

    switch (current_screen) {
      case MAIN_MENU:
        DrawTexture(*sprite_sheet, 0, 0, WHITE);
        DrawRectangleRec(
          (Rectangle){ 0, 0, GetScreenWidth(), GetScreenHeight() },
          Fade(BLACK, 0.7f));

        float title_font_size = 90.0f;
        const char* title = "Bejeweled PvP";
        Vector2 title_size = MeasureTextEx(font, title, title_font_size, 1.0f);
        Vector2 title_pos = {
          GetScreenWidth() * 0.5f - title_size.x * 0.5f,
          200.0f,
        };

        blit_text(&font, title, title_pos, title_font_size, WHITE);
        if (draw_button("Connect To Server", connectButton, BLUE)) {
          send_connect_request();
        }

        break;

      case IN_GAME:
        if (!connected) {
          blit_text(&font,
                    "Connecting to the server...",
                    (Vector2){ 190, 200 },
                    20,
                    LIGHTGRAY);
        } else if (!game_state.game_started) {
          blit_text(&font,
                    "Waiting for another player...",
                    (Vector2){ 190, 200 },
                    20,
                    LIGHTGRAY);

        } else {

          blit_text(&font,
                    TextFormat("P1: %d", game_state.player1_score),
                    (Vector2){ 100, 20 },
                    20,
                    BLUE);
          blit_text(&font,
                    TextFormat("P2: %d", game_state.player2_score),
                    (Vector2){ 100, 50 },
                    20,
                    RED);

          draw_board(sprite_sheet);

          if (draw_button("Disconnect", disconnectButton, PINK)) {
            send_disconnect_request();
            reset_game_state();
          }
          if (game_state.game_over) {

            const char* result;
            if (game_state.player1_score == game_state.player2_score) {
              result = "It's a Tie!";
            } else if ((player_id == 0 &&
                        game_state.player1_score > game_state.player2_score) ||
                       (player_id == 1 &&
                        game_state.player2_score > game_state.player1_score)) {
              result = "You Won!";
            } else {
              result = "You Lost!";
            }

            Vector2 text_size = MeasureTextEx(font, result, 40, 1);
            float center_x = (GetScreenWidth() - text_size.x) / 2;

            float padding = 20;
            float bg_width = text_size.x + padding * 2;
            float bg_height = text_size.y + padding * 2;
            float bg_x = center_x - padding;
            float bg_y = 300 - padding;

            DrawRectangleRounded((Rectangle){ bg_x, bg_y, bg_width, bg_height },
                                 0.3,
                                 10,
                                 DARKGRAY);
            blit_text(&font, result, (Vector2){ center_x, 300 }, 40, GREEN);

            const char* instruction = "Press Space to return to main menu";
            Vector2 instruction_size = MeasureTextEx(font, instruction, 20, 1);
            float instruction_center_x =
              (GetScreenWidth() - instruction_size.x) / 2;

            bg_width = instruction_size.x + padding * 2;
            bg_height = instruction_size.y + padding * 2;
            bg_x = instruction_center_x - padding;
            bg_y = 400 - padding;

            DrawRectangleRounded((Rectangle){ bg_x, bg_y, bg_width, bg_height },
                                 0.3,
                                 10,
                                 DARKGRAY);
            blit_text(&font,
                      instruction,
                      (Vector2){ instruction_center_x, 400 },
                      20,
                      RAYWHITE);

            if (IsKeyPressed(KEY_SPACE)) {
              reset_game_state();
            }

          } else if (game_state.current_turn == player_id && !animating &&
                     !animating_swap) {

            float font_size = 30.0f;
            const char* str = "Your turn!";
            Vector2 size = MeasureTextEx(font, str, font_size, 1.0f);
            blit_text(&font,
                      str,
                      (Vector2){ GetScreenWidth() / 2 - size.x / 2,
                                 GetScreenHeight() - size.y * 4 },
                      font_size,
                      GREEN);

            Vector2 mousePoint = GetMousePosition();
            int hoverX = (mousePoint.x - 100) / TILE_SIZE;
            int hoverY = (mousePoint.y - 100) / TILE_SIZE;

            hover_tile = (Vector2){ -1, -1 };
            if (hoverX >= 0 && hoverX < BOARD_SIZE && hoverY >= 0 &&
                hoverY < BOARD_SIZE) {
              hover_tile = (Vector2){ hoverX, hoverY };
            }

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
              if (hover_tile.x != -1) {
                if (selected_tile.x == -1) {
                  selected_tile = hover_tile;
                } else {
                  if ((abs(hover_tile.x - selected_tile.x) == 1 && hover_tile.y == selected_tile.y) ||
                      (abs(hover_tile.y - selected_tile.y) == 1 && hover_tile.x == selected_tile.x)) {
                    send_move(selected_tile.x, selected_tile.y, hover_tile.x, hover_tile.y);
                    animating_swap = true;
                    swap_animation_timer = 0.0f;

                    swap_from = selected_tile;
                    swap_to = hover_tile;

                    selected_tile = (Vector2){ -1, -1 };
                  } else if (hover_tile.x == selected_tile.x && hover_tile.y == selected_tile.y) {
                    selected_tile = (Vector2){ -1, -1 };
                  } else {
                    selected_tile = hover_tile;
                  }
                }
              } else {
                selected_tile = (Vector2){ -1, -1 };
              }
            }

          } else {
            float font_size = 30.0f;
            const char* str = "Opponent's Turn!";
            Vector2 size = MeasureTextEx(font, str, font_size, 1.0f);
            blit_text(&font,
                      str,
                      (Vector2){ GetScreenWidth() / 2 - size.x / 2,
                                 GetScreenHeight() - size.y * 4 },
                      font_size,
                      RED);
          }
        }
        break;
    }

    EndDrawing();
  }

  close(sockfd);
  CloseWindow();
  return 0;
}
