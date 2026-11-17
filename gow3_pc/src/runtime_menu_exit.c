#include "runtime_menu_exit.h"
#include "gpu/gow3gpu.h"
#include "menu_exit_hook.h"
#include <stdatomic.h>
#include <stdio.h>

#define DATA_MAGIC 0x44433030u
#define DATA_MIN 0x1000u
#define DATA_MAX 0x400000u
#define REGIONS 32

/* The new lists stay where they are written: a region is only written again for menu data loaded
 * at the same address (the file there was replaced, so nothing points at the old copy any more).
 * Other loads take new space; once the arena is full the button is left out of them. */
typedef struct { uintptr_t base; size_t offset, length; } Region;
static unsigned char *arena;
static size_t arena_size, arena_end;
static Region regions[REGIONS];
static unsigned region_count, full_reported;
static atomic_flag arena_lock = ATOMIC_FLAG_INIT;
static atomic_int asking, leaving;

void runtime_menu_exit_set_arena(unsigned char *memory, size_t size) {
    arena = memory;
    arena_size = size;
}

static Region *region_of(uintptr_t base) {
    for (unsigned i = 0; i < region_count; ++i)
        if (regions[i].base == base) return &regions[i];
    return NULL;
}

ABI void runtime_menu_exit_data_loaded(unsigned char *data) {
    if (!data || !arena || gow3_menu_u32(data) != DATA_MAGIC) return;
    const uint64_t size = gow3_menu_u32(data + 8);
    if (size < DATA_MIN || size > DATA_MAX) return;
    while (atomic_flag_test_and_set(&arena_lock)) {}
    Gow3MenuPatched done = {0, 0};
    int full = 0;
    Region *region = region_of((uintptr_t)data);
    if (region) {
        size_t used = 0;
        done = gow3_menu_patch(data, size, arena + region->offset, region->length, &used);
    }
    if (!done.lobby && !done.pause) {
        size_t used = arena_end;
        done = gow3_menu_patch(data, size, arena, arena_size, &used);
        if (done.lobby || done.pause) {
            if (!region && region_count < REGIONS) region = &regions[region_count++];
            if (region) *region = (Region){(uintptr_t)data, arena_end, used - arena_end};
            arena_end = used;
        } else if (arena_end + GOW3_MENU_NAME_SIZE + 16 + 2 * GOW3_MENU_ITEM_SIZE > arena_size && !full_reported) {
            full = full_reported = 1;
        }
    }
    atomic_flag_clear(&arena_lock);
    if (done.lobby || done.pause) printf("Menu: QUIT button added (lobby lists %u, pause lists %u)\n", done.lobby, done.pause);
    if (full) puts("Menu: QUIT button memory full, later menu loads go without it");
}

static void leave(void) {
    if (atomic_exchange(&leaving, 1)) return;
    puts("Menu: quit to desktop");
    gow3gpu_request_exit();
}

typedef ABI void (*ConfirmOpen)(void *menu, uint32_t title, uint32_t yes_label, uint32_t yes_button,
                                 uint32_t no_label, uint32_t no_button, uint32_t extra);
static ConfirmOpen confirm_open;

void runtime_menu_exit_set_confirm(void *open) {
    confirm_open = (ConfirmOpen)open;
}

/* Runs on the menu's thread, at the dispatcher's entry: the game's own opener is safe to call. */
ABI void runtime_menu_exit_button(void *menu, uint32_t hash) {
    if (hash == GOW3_MENU_EXIT_LOBBY || hash == GOW3_MENU_EXIT_CONFIRMED) {
        leave();
    } else if (hash == GOW3_MENU_EXIT_PAUSE && confirm_open && menu) {
        confirm_open(menu, GOW3_MENU_TEXT_LEAVE_GAME, GOW3_MENU_TEXT_YES, GOW3_MENU_EXIT_CONFIRMED,
                     GOW3_MENU_TEXT_NO, GOW3_MENU_CONFIRM_NO, 0);
    } else if (hash == GOW3_MENU_EXIT_PAUSE && !atomic_load(&asking)) {
        static const char *const items[] = {"Cancel", "Quit to desktop"};
        if (gow3gpu_choice_begin("Quit to desktop? Unsaved progress is lost.", items, NULL, 2, 0)) atomic_store(&asking, 1);
    }
}

void runtime_menu_exit_tick(void) {
    if (!atomic_load(&asking)) return;
    const int choice = gow3gpu_choice_poll();
    if (choice == -1) return;
    atomic_store(&asking, 0);
    if (choice == 1) leave();
}
