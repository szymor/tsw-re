#ifndef TSW_MAP_H
#define TSW_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAP_BLOCK_SIZE 6273
#define MAP_HEADER_SIZE 2
#define MAP_FLOOR_STRIDE 123
#define MAP_TILES 121
#define MAP_TILE_COLS 11
#define MAP_TILE_ROWS 11
#define MAP_FLOOR_COUNT 51

typedef struct {
    uint8_t bytes[MAP_BLOCK_SIZE];
} MapBlock;

/* Parse the 6273-byte save map block (2-byte header + 51 floor records). */
bool map_parse(const uint8_t *bytes, size_t len, MapBlock *out,
               char *err, size_t errsz);

/* Floor numbers are 1..50.  Floor 0 is the unused dummy map. */
int map_floor_offset(int floor);
int map_stair_up_offset(int floor);
int map_stair_down_offset(int floor);

uint8_t map_tile(const MapBlock *map, int floor, int index);
uint8_t map_stair_up(const MapBlock *map, int floor);
uint8_t map_stair_down(const MapBlock *map, int floor);

const char *map_tile_name(uint8_t raw);

#endif
