#include "runtime_menu_exit.h"
#include "gpu/gow3gpu.h"
#include "menu_exit_hook.h"
#include <stdatomic.h>
#include <stdio.h>

#define DATA_MAGIC 0x44433030u
#define DATA_MIN 0x1000u
#define DATA_MAX 0x400000u
#define SLOT_SIZE 0x2000u

static unsigned char *arena;
static size_t arena_size, arena_next;
static atomic_flag arena_lock = ATOMIC_FLAG_INIT;
static atomic_int asking, leaving;

void runtime_menu_exit_set_arena(unsigned char *memory, size_t size) {
    arena = memory;
    arena_size = size;
}

ABI void runtime_menu_exit_data_loaded(unsigned char *data) {
    if (!data || !arena || gow3_menu_u32(data) != DATA_MAGIC) return;
    const uint64_t size = gow3_menu_u32(data + 8);
    if (size < DATA_MIN || size > DATA_MAX) return;
    const size_t slots = arena_size / SLOT_SIZE;
    if (!slots) return;
    while (atomic_flag_test_and_set(&arena_lock)) {}
    size_t used = 0;
    const Gow3MenuPatched done = gow3_menu_patch(data, size, arena + arena_next % slots * SLOT_SIZE, SLOT_SIZE, &used);
    if (done.lobby || done.pause) ++arena_next;
    atomic_flag_clear(&arena_lock);
    if (done.lobby || done.pause) printf("Menu: QUIT button added (lobby lists %u, pause lists %u)\n", done.lobby, done.pause);
}

static void leave(void) {
    if (atomic_exchange(&leaving, 1)) return;
    puts("Menu: quit to desktop");
    gow3gpu_request_exit();
}

ABI void runtime_menu_exit_button(uint32_t hash) {
    if (hash == GOW3_MENU_EXIT_LOBBY) {
        leave();
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
