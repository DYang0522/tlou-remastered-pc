/* The game menus' "QUIT" button (lobby and pause menu): added to the menu data as it loads, and
 * answered when pressed (src/menu_exit_hook.h). */
#ifndef GOW3_RUNTIME_MENU_EXIT_H
#define GOW3_RUNTIME_MENU_EXIT_H
#include <stddef.h>
#include <stdint.h>
#include "runtime.h"

/* Memory the new button lists live in (readable by the guest, never freed). */
void runtime_menu_exit_set_arena(unsigned char *arena, size_t size);
/* Called by the hooks, from guest code. */
ABI void runtime_menu_exit_data_loaded(unsigned char *data);
ABI void runtime_menu_exit_button(uint32_t hash);
/* From the pad's sampling: the pause menu's confirmation answer. */
void runtime_menu_exit_tick(void);
#endif
