#undef NDEBUG
#include "../src/menu_exit_hook.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

enum { SIZE = 0x2000, PAUSE_ITEMS = 0x400, LOBBY_ITEMS = 0x1000, PAUSE_ENTRY = 0x100, LOBBY_ENTRY = 0x118 };
static _Alignas(16) unsigned char data[SIZE];
static _Alignas(16) unsigned char arena[0x1000];

static void item(uint64_t list, unsigned index, uint32_t hash, uint32_t kind) {
    unsigned char *p = data + list + index * GOW3_MENU_ITEM_SIZE;
    memcpy(p + GOW3_MENU_ITEM_HASH, &hash, 4);
    memcpy(p + GOW3_MENU_ITEM_KIND, &kind, 4);
    const uint64_t name = hash ? (uint64_t)(uintptr_t)data : 0;
    memcpy(p, &name, 8);
}
static void entry(uint64_t at, uint64_t count, uint64_t list) {
    const uint64_t pointer = (uint64_t)(uintptr_t)data + list;
    const uint32_t type = GOW3_MENU_LIST_TYPE;
    memcpy(data + at, &count, 8);
    memcpy(data + at + 8, &pointer, 8);
    memcpy(data + at + 20, &type, 4);
}
static uint32_t hash_at(uint64_t list, unsigned index) {
    return gow3_menu_u32(data + list + index * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH);
}

int main(void) {
    item(PAUSE_ITEMS, 0, GOW3_MENU_BUTTON_OPTIONS, 2);
    item(PAUSE_ITEMS, 1, 0, 1);
    item(PAUSE_ITEMS, 2, GOW3_MENU_BUTTON_QUIT, GOW3_MENU_KIND_BUTTON);
    entry(PAUSE_ENTRY, 3, PAUSE_ITEMS);
    item(LOBBY_ITEMS, 0, GOW3_MENU_BUTTON_STORY, 2);
    item(LOBBY_ITEMS, 1, GOW3_MENU_BUTTON_OPTIONS, 2);
    item(LOBBY_ITEMS, 2, 0, GOW3_MENU_KIND_DOWNLOADS);
    item(LOBBY_ITEMS, 3, 0, 1);
    entry(LOBBY_ENTRY, 4, LOBBY_ITEMS);

    size_t used = 0;
    Gow3MenuPatched done = gow3_menu_patch(data, SIZE, arena, sizeof(arena), &used);
    assert(done.pause == 1 && done.lobby == 1 && used && used % 16 == 0);

    assert(gow3_menu_u64(data + PAUSE_ENTRY) == 4);
    unsigned char *pause = (unsigned char *)(uintptr_t)gow3_menu_u64(data + PAUSE_ENTRY + 8);
    assert(gow3_menu_u32(pause + 2 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_BUTTON_QUIT);
    assert(gow3_menu_u32(pause + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_EXIT_PAUSE);
    assert(gow3_menu_u32(pause + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_LABEL) == GOW3_MENU_LABEL_QUIT);
    assert(!strcmp((const char *)(uintptr_t)gow3_menu_u64(pause + 3 * GOW3_MENU_ITEM_SIZE), "BUTTON_EXIT_DESKTOP"));

    assert(gow3_menu_u64(data + LOBBY_ENTRY) == 5);
    unsigned char *lobby = (unsigned char *)(uintptr_t)gow3_menu_u64(data + LOBBY_ENTRY + 8);
    assert(gow3_menu_u32(lobby + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_EXIT_LOBBY);
    assert(gow3_menu_u32(lobby + 4 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_KIND) == 1);

    assert(hash_at(PAUSE_ITEMS, 2) == GOW3_MENU_BUTTON_QUIT && hash_at(LOBBY_ITEMS, 3) == 0);

    const size_t before = used;
    done = gow3_menu_patch(data, SIZE, arena, sizeof(arena), &used);
    assert(!done.pause && !done.lobby && used == before);

    memset(arena, 0, sizeof(arena));
    entry(PAUSE_ENTRY, 3, PAUSE_ITEMS);
    entry(LOBBY_ENTRY, 4, LOBBY_ITEMS);
    used = sizeof(arena) - 16;
    done = gow3_menu_patch(data, SIZE, arena, sizeof(arena), &used);
    assert(!done.pause && !done.lobby);
    return 0;
}
