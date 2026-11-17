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
    Gow3MenuPatched done = gow3_menu_patch(data, SIZE, arena, sizeof(arena), &used, GOW3_MENU_LABEL_EXIT_DESKTOP);
    assert(done.pause == 1 && done.lobby == 1 && used && used % 16 == 0);

    assert(gow3_menu_u64(data + PAUSE_ENTRY) == 4);
    unsigned char *pause = (unsigned char *)(uintptr_t)gow3_menu_u64(data + PAUSE_ENTRY + 8);
    assert(gow3_menu_u32(pause + 2 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_BUTTON_QUIT);
    assert(gow3_menu_u32(pause + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_EXIT_PAUSE);
    assert(gow3_menu_u32(pause + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_LABEL) == GOW3_MENU_LABEL_EXIT_DESKTOP);
    assert(!strcmp((const char *)(uintptr_t)gow3_menu_u64(pause + 3 * GOW3_MENU_ITEM_SIZE), "BUTTON_EXIT_DESKTOP"));

    assert(gow3_menu_u64(data + LOBBY_ENTRY) == 5);
    unsigned char *lobby = (unsigned char *)(uintptr_t)gow3_menu_u64(data + LOBBY_ENTRY + 8);
    assert(gow3_menu_u32(lobby + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_EXIT_LOBBY);
    assert(gow3_menu_u32(lobby + 4 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_KIND) == 1);

    assert(hash_at(PAUSE_ITEMS, 2) == GOW3_MENU_BUTTON_QUIT && hash_at(LOBBY_ITEMS, 3) == 0);

    const size_t before = used;
    done = gow3_menu_patch(data, SIZE, arena, sizeof(arena), &used, GOW3_MENU_LABEL_EXIT_DESKTOP);
    assert(!done.pause && !done.lobby && used == before);

    memset(arena, 0, sizeof(arena));
    entry(PAUSE_ENTRY, 3, PAUSE_ITEMS);
    entry(LOBBY_ENTRY, 4, LOBBY_ITEMS);
    used = sizeof(arena) - 16;
    done = gow3_menu_patch(data, SIZE, arena, sizeof(arena), &used, GOW3_MENU_LABEL_EXIT_DESKTOP);
    assert(!done.pause && !done.lobby);

    /* Stubs: fit the 512 bytes probe.c gives each, step over the red zone, keep rsp aligned. */
    static unsigned char stub[1024];
    static const unsigned char load_original[GOW3_MENU_LOAD_LENGTH] = {0x4c, 0x8b, 0x65, 0xb8, 0x49, 0x63, 0x48, 0x14};
    static const unsigned char click_original[GOW3_MENU_CLICK_LENGTH] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57};
    unsigned n = gow3_menu_build_load_stub(stub, 0x1111, 0x2222, load_original);
    assert(n <= 512);
    assert(!memcmp(stub, "\x48\x8d\x64\x24\x80\x9c", 6));
    assert(!memcmp(stub + n - 31, "\x9d\x48\x8d\xa4\x24\x80\x00\x00\x00", 9));
    assert(!memcmp(stub + n - 22, load_original, GOW3_MENU_LOAD_LENGTH));
    n = gow3_menu_build_click_stub(stub, 0x1111, 0x2222, click_original);
    assert(n <= 512);
    /* entry rsp = 8 mod 16; six pushes and 0x108 leave the call 16-byte aligned. */
    assert(!memcmp(stub + 8, "\x48\x81\xec\x08\x01\x00\x00", 7));
    assert(!memcmp(stub + n - 20, click_original, GOW3_MENU_CLICK_LENGTH));
    /* helper(menu, hash) takes the dispatcher's rdi and esi as they are: the call follows the saves. */
    assert(!memcmp(stub + 15 + 8 * 9, "\x48\xb8", 2));

    /* Text stub: our id jumps to the helper, any other runs the replayed entry. */
    static const unsigned char text_original[GOW3_MENU_TEXT_LENGTH] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57};
    unsigned original_at = 0;
    n = gow3_menu_build_text_stub(stub, 0x1111, 0x2222, text_original, &original_at);
    assert(n <= 64 && original_at == 20 && stub[6] == 0x75 && 8 + stub[7] == original_at);
    assert(gow3_menu_u32(stub + 2) == GOW3_MENU_LABEL_EXIT_DESKTOP);
    assert(!memcmp(stub + original_at, text_original, GOW3_MENU_TEXT_LENGTH));
    return 0;
}
