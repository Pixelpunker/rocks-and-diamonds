#include <string>
#include <string.h>
#include <memory>
#include <cstdlib>

#include "rocks-and-diamonds.hpp"

using namespace blit;

#define PLAYER_TOP (player.position.y)
#define PLAYER_BOTTOM (player.position.y + player.size.y)
#define PLAYER_RIGHT (player.position.x + player.size.x)
#define PLAYER_LEFT (player.position.x)

const Point P_LEFT(-1, 0);
const Point P_RIGHT(1, 0);
const Point P_BELOW(0, 1);
const Point P_ABOVE(0, -1);

const Point P_ALEFT = P_LEFT + P_ABOVE;
const Point P_ARIGHT = P_RIGHT + P_ABOVE;
const Point P_BLEFT = P_LEFT + P_BELOW;
const Point P_BRIGHT = P_RIGHT + P_BELOW;

const Size LEVEL_SIZE(64, 64);

const void* levels[] = {
  &asset_assets_level00_tmx,
  &asset_assets_level01_tmx,
  &asset_assets_level02_tmx,
  &asset_assets_level03_tmx
};

uint8_t *level_data;

Timer timer_level_update;
Timer timer_level_animate;
TileMap* level;

struct Feedback {
  bool rock_thunk;
} feedback;

struct Player {
  Point start;
  Point position;
  Point screen_location;
  uint32_t score;
  Vec2 camera;
  Point size = Point(1, 1);
  bool facing = true;
  bool has_key;
  uint32_t level;
  bool dead;
} player;

struct GameSaveData {
    uint8_t currentLevel;
} gameSaveData;

struct LastButtonPress {
  uint32_t DPAD_UP;
  uint32_t DPAD_DOWN;
  uint32_t DPAD_LEFT;
  uint32_t DPAD_RIGHT;
  uint32_t A;
  uint32_t B;
  uint32_t X;
  uint32_t Y;
} lastButtonPress;

struct CurrentButtonPress {
  uint32_t DPAD_UP;
  uint32_t DPAD_DOWN;
  uint32_t DPAD_LEFT;
  uint32_t DPAD_RIGHT;
  uint32_t A;
  uint32_t B;
  uint32_t X;
  uint32_t Y;
} currentButtonPress;

uint32_t repeat_time = 150; // in ms
uint32_t hold_time = 250; // in ms

auto leveltick = 0;

Mat3 camera;

enum entityType {
  // Terrain
  NOTHING = 0x00,
  DIRT = 0x01,
  WALL = 0x02,
  STAIRS = 0x03,
  LOCKED_STAIRS = 0x04,

  // Rocks and diamonds
  ROCK = 0x10,
  DIAMOND = 0x11,

  // Player animations... or lack thereof
  PLAYER = 0x30,
  PLAYER_FL = 0x31,
  PLAYER_SQUASHED = 0x3e,
  PLAYER_DEAD = 0x3f,

  // Collectable entities that aren't diamonds
  KEY_SILVER = 0x20,
  KEY_GOLD = 0x21,

  // Animations, ho boy this is ugly!
  DIRT_ANIM_1 = 0x50,
  DIRT_ANIM_2 = 0x51,
  DIRT_ANIM_3 = 0x52,
  DIRT_ANIM_4 = 0x53,

  // So ugly, oh boy!
  BOMB_ANIM_1 = 0x60,
  BOMB_ANIM_2 = 0x61,
  BOMB_ANIM_3 = 0x62,
  BOMB_ANIM_4 = 0x63,
  BOMB_ANIM_5 = 0x64,
  BOMB_ANIM_6 = 0x65,
};

struct FallingObject {
  Point level_position;
  entityType type;
  uint8_t tick;
};

std::vector<FallingObject> fallingObjects;

// Line-interrupt callback for level->draw that applies our camera transformation
// This can be expanded to add effects like camera shake, wavy dream-like stuff, all the fun!
Mat3 level_line_interrupt_callback(uint8_t y) {
  (void)y; // Camera is updated elsewhere and is scanline-independent
  return camera;
};

void update_camera(uint32_t time) {
  (void)time;

  static uint32_t thunk_a_bunch = 0;
  // Create a camera transform that centers around the player's position
  if(player.camera.x < player.position.x) {
    player.camera.x += 0.1f;
  }
  if(player.camera.x > player.position.x) {
    player.camera.x -= 0.1f;
  }
  if(player.camera.y < player.position.y) {
    player.camera.y += 0.1f;
  }
  if(player.camera.y > player.position.y) {
    player.camera.y -= 0.1f;
  }

  if(feedback.rock_thunk) {
    thunk_a_bunch = 10;
    feedback.rock_thunk = 0;
  }

  camera = Mat3::identity();
  camera *= Mat3::translation(Vec2(player.camera.x * 8.0f, player.camera.y * 8.0f)); // offset to middle of world
  camera *= Mat3::translation(Vec2(-screen.bounds.w / 2, -screen.bounds.h / 2)); // transform to centre of framebuffer

  if(thunk_a_bunch){
    camera *= Mat3::translation(Vec2(
      float(blit::random() & 0x03) - 1.5f,
      float(blit::random() & 0x03) - 1.5f
    ));
    thunk_a_bunch--;
    vibration = thunk_a_bunch / 10.0f;
  }
}

Point level_first(entityType entity) {
  for(auto x = 0; x < LEVEL_SIZE.w; x++) {
    for(auto y = 0; y < LEVEL_SIZE.h; y++) {
      if (level_data[y * LEVEL_SIZE.w + x] == entity) {
        return Point(x, y);
      } 
    }
  }
  return Point(-1, -1);
}

void level_set(Point location, entityType entity) {
  level_data[location.y * LEVEL_SIZE.w + location.x] = entity;
}

bool player_at(Point location) {
  return (player.position.x == location.x && player.position.y == location.y);
}

entityType level_get(Point location) {
  static Rect level_bounds(Point(0, 0), LEVEL_SIZE);
  if(!level_bounds.contains(location)) {
    return WALL;
  }
  entityType entity = (entityType)level_data[location.y * LEVEL_SIZE.w + location.x];
  if(entity == NOTHING && player_at(location)) {
    entity = PLAYER;
  }
  return entity;
}

// get element at location without CHANGING the damn level
entityType level_peek(Point location) {
  uint8_t char1copy = level_data[location.y * LEVEL_SIZE.w + location.x];
  uint8_t char2copy = level_data[(location.y + 1) * LEVEL_SIZE.w + location.x];

  entityType entity1 = (entityType)char1copy;
  entityType entity2 = (entityType)char2copy;

  if (entity2 == entityType::NOTHING && player_at(location + Point(0, 1))) {
    return entityType::NOTHING;
  }

  if (entity2 == entityType::NOTHING && (entity1 == entityType::ROCK || entity1 == entityType::DIAMOND)) {
    return entity1;
  } else {
    return entityType::NOTHING;
  }
}

void level_set(Point location, entityType entity, bool not_nothing) {
  if(not_nothing) {
    if(level_get(location) != NOTHING) {
      level_set(location, entity);
    }
  } else {
    level_set(location, entity);
  }
}

void update_rocks(Timer &timer) {
  if (leveltick == 0) { // find objects that are about to fall
    Point location = Point(0, 0);
    for(location.y = LEVEL_SIZE.h - 1; location.y > -1; location.y--) {
      for(location.x = 0; location.x < LEVEL_SIZE.w + 1; location.x++) {
        entityType current = level_peek(location);
        if (current == ROCK || current == DIAMOND) {
          FallingObject fallingObject;
          fallingObject.level_position = location;
          fallingObject.type = current;
          fallingObject.tick = leveltick + 1;
          fallingObjects.push_back(fallingObject);
          // now remove it from map
          level_set(location, entityType::NOTHING);
        }
      }
    }
  } else if (leveltick == 7) { // resume normal level logic
    for (auto& fallingObject : fallingObjects) {
            level_set(fallingObject.level_position + Point(0, 1), fallingObject.type);
            if(fallingObject.type == entityType::ROCK) {
              // Add a little *THUNK* effect for rocks falling directly down
              Point location_land = fallingObject.level_position + Point(0, 2);
              switch (level_get(location_land)) {
                case WALL:
                  feedback.rock_thunk = true;
                  break;
                case PLAYER:
                  player.dead = true;
                  feedback.rock_thunk = true;
                  level_set(location_land, PLAYER_SQUASHED);
                default:
                  break;
              }
            }
        }
    fallingObjects.clear();
    update_level(timer);
  } else { // update falling objects
    for (auto& fallingObject : fallingObjects) {
      fallingObject.tick = leveltick + 1;
    }
  }
  leveltick++;
  if (leveltick > 7) {
    leveltick = 0;
  }
}

void animate_level(Timer &timer) {
  (void)timer;

  Point location = Point(0, 0);
  for(location.y = LEVEL_SIZE.h - 1; location.y > -1; location.y--) {
    for(location.x = 0; location.x < LEVEL_SIZE.w + 1; location.x++) {
      entityType current = level_get(location);

      if(current == DIRT_ANIM_4) {
        level_set(location, NOTHING);
      } else if(current == DIRT_ANIM_3) {
        level_set(location, DIRT_ANIM_4);
      } else if(current == DIRT_ANIM_2) {
        level_set(location, DIRT_ANIM_3);
      } else if(current == DIRT_ANIM_1) {
        level_set(location, DIRT_ANIM_2);
      }

      if(current == BOMB_ANIM_1) {
        level_set(location, BOMB_ANIM_2);
      } else if(current == BOMB_ANIM_2) {
        level_set(location, BOMB_ANIM_3);
      } else if(current == BOMB_ANIM_3) {
        level_set(location, BOMB_ANIM_4);
      } else if(current == BOMB_ANIM_4) {
        level_set(location, BOMB_ANIM_5);
      } else if(current == BOMB_ANIM_5) {
        level_set(location, BOMB_ANIM_6);
      } else if(current == BOMB_ANIM_6) {
        level_set(location, NOTHING);
        level_set(location + P_BELOW, DIRT_ANIM_1, true);
        level_set(location + P_ABOVE, DIRT_ANIM_1, true);
        level_set(location + P_RIGHT, DIRT_ANIM_1, true);
        level_set(location + P_LEFT, DIRT_ANIM_1, true);
        level_set(location + P_BRIGHT, DIRT_ANIM_1, true);
        level_set(location + P_ARIGHT, DIRT_ANIM_1, true);
        level_set(location + P_BLEFT, DIRT_ANIM_1, true);
        level_set(location + P_ALEFT, DIRT_ANIM_1, true);
      }
    }
  }
}

void update_level(Timer &timer) {
  (void)timer;

  Point location = Point(0, 0);
  for(location.y = LEVEL_SIZE.h - 1; location.y > 0; location.y--) {
    for(location.x = 0; location.x < LEVEL_SIZE.w; location.x++) {
      Point location_below = location + P_BELOW;
      entityType current = level_get(location);
      entityType below = level_get(location_below);

      for(entityType check_entity : {ROCK, DIAMOND}) {
        if(current == check_entity) {
          if (below == PLAYER_SQUASHED && check_entity == ROCK) {
            level_set(location, NOTHING);
            level_set(location_below, PLAYER_DEAD);
          } else if (below == ROCK || below == DIAMOND) {
            // If the space below is a rock or a diamond, check to the left/right
            // and "roll" down the stack
            entityType left = level_get(location + P_LEFT);
            entityType below_left = level_get(location + P_BLEFT);
            entityType right = level_get(location + P_RIGHT);
            entityType below_right = level_get(location + P_BRIGHT);

            if(left == NOTHING && below_left == NOTHING){
              level_set(location, NOTHING);
              level_set(location + Point(-1, 1), check_entity);
            } else if(right == NOTHING && below_right == NOTHING){
              level_set(location, NOTHING);
              level_set(location + Point(1, 1), check_entity);
            }
          }
        }
      }

    }
  }
}

void new_game(uint32_t level) {
  // Get a pointer to the map header
  TMX *tmx = (TMX *)levels[level];

  // Bail if the map is oversized
  if(tmx->width > LEVEL_SIZE.w) return;
  if(tmx->height > LEVEL_SIZE.h) return;

  // Clear the level data
  memset(level_data, 0, LEVEL_SIZE.area());

  // Load the level data from the map memory
  for(auto x = 0u; x < tmx->width; x++) {
    for(auto y = 0u; y < tmx->height; y++) {
      auto src = y * tmx->width + x;
      auto dst = y * LEVEL_SIZE.w + x;
      level_data[dst] = tmx->data[src];
    }
  }

  player.start = level_first(PLAYER);
  level_set(player.start, NOTHING);
  player.position = player.start;
  player.camera = Vec2(player.position);
  player.has_key = false;
  player.score = 0;
  player.dead = false;

  player.screen_location = Point(screen.bounds.w / 2, screen.bounds.h / 2);
  player.screen_location += Point(1, 1);

  gameSaveData.currentLevel = level;
  write_save(gameSaveData);
}

void init() {
  bool success = read_save(gameSaveData);
  if (success) {
    if (gameSaveData.currentLevel > sizeof(levels) -1) { // cheater!
      gameSaveData.currentLevel = 0;
    }
    player.level = gameSaveData.currentLevel;
  }

  set_screen_mode(ScreenMode::lores);

  // Load the spritesheet from the linked binary blob
  screen.sprites = Surface::load(asset_sprites);

  // Allocate memory for the writeable copy of the level
  level_data = (uint8_t *)malloc(LEVEL_SIZE.area());

  // Load our level data into the TileMap
  level = new TileMap((uint8_t *)level_data, nullptr, Size(LEVEL_SIZE.w, LEVEL_SIZE.h), screen.sprites);
  
  timer_level_update.init(update_rocks, 32, -1);
  timer_level_update.start();
  
  timer_level_animate.init(animate_level, 100, -1);
  timer_level_animate.start();

  new_game(player.level);
}

void render(uint32_t time) {
  (void)time;

  screen.pen = Pen(0, 0, 0);
  screen.clear();

  // Draw our level
  level->draw(&screen, Rect(0, 0, screen.bounds.w, screen.bounds.h), level_line_interrupt_callback);
  
  // draw falling objects if any
  // Point offset = player.screen_location;
  // player.screen_location does not take into account all the camera movement
  Point offset = player.position * 8;
  offset.x -= floor(camera.v02);
  offset.y -= round(camera.v12);
  for (auto fallingObject : fallingObjects) {
    Point pos = fallingObject.level_position - player.position;
    screen.pen = {255, 0, 0};
    // screen.rectangle(Rect(offset + pos * 8, Size(8, 8)));
    screen.sprite(fallingObject.type, offset + (pos * 8) + Point(0, fallingObject.tick));
  }

  // Draw our character sprite
  if(!player.dead) {
    screen.sprite(player.facing ? entityType::PLAYER : entityType::PLAYER_FL, player.screen_location);
  }

  // Draw the header bar
  screen.pen = Pen(255, 255, 255);
  screen.rectangle(Rect(0, 0, screen.bounds.w, 10));
  screen.pen = Pen(0, 0, 0);
  screen.text("Level: " + std::to_string(player.level) + " Score: " + std::to_string(player.score), minimal_font, Point(2, 2));

  if(player.has_key) {
    screen.sprite(entityType::KEY_SILVER, Point(screen.bounds.w - 10, 1));
  }
  // screen.text(std::to_string(player.position.x), minimal_font, Point(0, 0));
  // screen.text(std::to_string(player.position.y), minimal_font, Point(0, 10));
}

void update(uint32_t time) {
  (void)time;

  Point movement = Point(0, 0);

  // Restart level
  if(buttons.pressed & Button::B) {
    lastButtonPress.B = currentButtonPress.B; currentButtonPress.B = time;
    new_game(player.level);
  }

  // Restart game
  if(buttons & Button::B) {
    if ((time - currentButtonPress.B) == hold_time) {
      player.level = 0;
      new_game(player.level);
    }
  }

  if(!player.dead) {
    // Throw bomb
    if(buttons.pressed & Button::A) {
      if(level_get(player.position + Point(0, 1)) == NOTHING) {
        level_set(player.position + Point(0, 1), BOMB_ANIM_1);
      }
    }
    if(buttons.pressed & Button::DPAD_UP) {
      lastButtonPress.DPAD_UP = currentButtonPress.DPAD_UP; currentButtonPress.DPAD_UP = time;
      movement.y = -1;
    }
    if(buttons.pressed & Button::DPAD_DOWN) {
      lastButtonPress.DPAD_DOWN = currentButtonPress.DPAD_DOWN; currentButtonPress.DPAD_DOWN = time;
      movement.y = 1;
    }
    if(buttons.pressed & Button::DPAD_LEFT) {
      lastButtonPress.DPAD_LEFT = currentButtonPress.DPAD_LEFT; currentButtonPress.DPAD_LEFT = time;
      player.facing = false;
      movement.x = -1;
    }
    if(buttons.pressed & Button::DPAD_RIGHT) {
      lastButtonPress.DPAD_RIGHT = currentButtonPress.DPAD_RIGHT; currentButtonPress.DPAD_RIGHT =time;
      player.facing = true;
      movement.x = 1;
    }

    // repeat à la classic boulder dash
    if(buttons & Button::DPAD_UP) {
      if ((time - currentButtonPress.DPAD_UP) % repeat_time == 0) {
        movement.y = -1;
      }
    }
    if(buttons & Button::DPAD_DOWN) {
      if ((time - currentButtonPress.DPAD_DOWN) % repeat_time == 0) {
        movement.y = 1;
      }
    }
    if(buttons & Button::DPAD_LEFT) {
      if ((time - currentButtonPress.DPAD_LEFT) % repeat_time == 0) {
        player.facing = false;
        movement.x = -1;
      }
    }
    if(buttons & Button::DPAD_RIGHT) {
      if ((time - currentButtonPress.DPAD_RIGHT) % repeat_time == 0) {
        player.facing = true;
        movement.x = 1;
      }      
    }

    player.position += movement;

    entityType standing_on = level_get(player.position);

    switch(standing_on) {
      case WALL:
        player.position -= movement;
        break;
      case ROCK:
        // Push rocks if there's an empty space the other side of them
        if(movement.x > 0 && level_get(player.position + Point(1, 0)) == NOTHING){
          level_set(player.position + Point(1, 0), ROCK);
          level_set(player.position, NOTHING);
        }
        else if(movement.x < 0 && level_get(player.position + Point(-1, 0)) == NOTHING){
          level_set(player.position + Point(-1, 0), ROCK);
          level_set(player.position, NOTHING);
        }
        else {
          player.position -= movement;
        }
        break;
      case DIAMOND:
        // Collect diamonds!
        player.score += 1;
        level_set(player.position, NOTHING);
        break;
      case DIRT:
        // Dig dirt!
        level_set(player.position, DIRT_ANIM_1);
        break;
      case KEY_SILVER:
        player.has_key = true;
        level_set(player.position, NOTHING);
        break;
      case LOCKED_STAIRS:
        if(player.has_key) {
          level_set(player.position, STAIRS);
        }
        player.position -= movement;
        break;
      case STAIRS:
        player.level++;
        new_game(player.level);
        break;
      default:
        break;
    }
  }

  update_camera(time);
}