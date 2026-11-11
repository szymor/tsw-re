#include "map.h"

#include <stdio.h>
#include <string.h>

int map_floor_offset(int floor)
{
    if (floor < 0 || floor >= MAP_FLOOR_COUNT)
        return -1;
    return MAP_HEADER_SIZE + floor * MAP_FLOOR_STRIDE;
}

int map_stair_up_offset(int floor)
{
    if (floor < 1 || floor >= MAP_FLOOR_COUNT)
        return -1;
    return floor * MAP_FLOOR_STRIDE;
}

int map_stair_down_offset(int floor)
{
    int off = map_stair_up_offset(floor);
    return off < 0 ? -1 : off + 1;
}

bool map_parse(const uint8_t *bytes, size_t len, MapBlock *out,
               char *err, size_t errsz)
{
    if (!bytes || !out) {
        if (err && errsz) snprintf(err, errsz, "null map buffer");
        return false;
    }
    if (len < MAP_BLOCK_SIZE) {
        if (err && errsz)
            snprintf(err, errsz, "map block too short: %zu < %d",
                     len, MAP_BLOCK_SIZE);
        return false;
    }
    if (bytes[0] != 0x00 || bytes[1] != 0x01) {
        if (err && errsz)
            snprintf(err, errsz, "bad map header: %02x %02x",
                     bytes[0], bytes[1]);
        return false;
    }
    memcpy(out->bytes, bytes, MAP_BLOCK_SIZE);
    return true;
}

uint8_t map_tile(const MapBlock *map, int floor, int index)
{
    int off = map_floor_offset(floor);
    if (!map || off < 0 || index < 0 || index >= MAP_TILES)
        return 0;
    return map->bytes[off + index];
}

uint8_t map_stair_up(const MapBlock *map, int floor)
{
    int off = map_stair_up_offset(floor);
    return (!map || off < 0) ? 0 : map->bytes[off];
}

uint8_t map_stair_down(const MapBlock *map, int floor)
{
    int off = map_stair_down_offset(floor);
    return (!map || off < 0) ? 0 : map->bytes[off];
}

const char *map_tile_name(uint8_t raw)
{
    switch (raw) {
    case 0x01: return "yellow door";
    case 0x02: return "blue door";
    case 0x03: return "red door";
    case 0x04: case 0x05: return "open door";
    case 0x06: return "floor";
    case 0x07: case 0x08: case 0x09: case 0x0a: return "wall";
    case 0x0b: return "stairs up";
    case 0x0c: return "stairs down";
    case 0x0d: return "lava";
    case 0x0e: return "ice/sparkle floor";
    case 0x1d: return "yellow key";
    case 0x1e: return "red key";
    case 0x1f: return "blue key";
    case 0x20: return "life potion";
    case 0x21: return "large life potion";
    case 0x22: return "red crystal";
    case 0x23: return "blue crystal";
    default:
        if (raw >= 0x3d && raw <= 0x5a) return "monster";
        if (raw >= 0x25 && raw <= 0x32) return "special item";
        if (raw >= 0x33 && raw <= 0x37) return "sword";
        if (raw >= 0x38 && raw <= 0x3c) return "shield";
        return "event/special";
    }
}
