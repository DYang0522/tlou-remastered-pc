/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GOW3_MENU_EXIT_HOOK_H
#define GOW3_MENU_EXIT_HOOK_H
#include <stdint.h>
#include <string.h>
#include "signature.h"

/* The menu data (dc1/menu.bin, "00CD") keeps each page's buttons as an array of 0x70-byte items,
 * listed in a table of 24-byte entries {count, items pointer, 0, 0xdd3834e0}. The data loader
 * calls the first hook once the file's pointers are fixed up (r8 is the data's base); the button
 * dispatcher (hash in esi) calls the second. */
#define GOW3_MENU_LIST_TYPE 0xdd3834e0u
#define GOW3_MENU_BUTTON_QUIT 0xfdcd24dfu
#define GOW3_MENU_BUTTON_OPTIONS 0xfa872aa5u
#define GOW3_MENU_BUTTON_STORY 0xe789553eu
#define GOW3_MENU_LABEL_QUIT 0xe5f5e373u
#define GOW3_MENU_EXIT_LOBBY 0x3c9e51a7u
#define GOW3_MENU_EXIT_PAUSE 0x3c9e51a8u
enum {
    GOW3_MENU_ITEM_SIZE = 0x70, GOW3_MENU_ENTRY_SIZE = 24, GOW3_MENU_MAX_ITEMS = 63,
    GOW3_MENU_ITEM_HASH = 8, GOW3_MENU_ITEM_KIND = 0x10, GOW3_MENU_ITEM_LABEL = 0x28,
    GOW3_MENU_KIND_BUTTON = 2, GOW3_MENU_KIND_DOWNLOADS = 0x10,
    GOW3_MENU_NAME_SIZE = 32
};

static const Gow3Signature gow3_menu_load_site = {
    "4c 8b 65 b8 49 63 48 14 48 85 c9 41 89 4c 24 30 49 8b 40 18 49 89 44 24 38 7e 22 31 d2 48 89 c6", 0};
static const Gow3Signature gow3_menu_click_site = {
    "55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec d8 01 00 00 89 95 2c fe ff ff 49 89 f8 41 89 f4", 0};
enum { GOW3_MENU_LOAD_LENGTH = 8, GOW3_MENU_CLICK_LENGTH = 6 };

static inline uint32_t gow3_menu_u32(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t gow3_menu_u64(const unsigned char *p) { uint64_t v; memcpy(&v, p, 8); return v; }

typedef struct { unsigned lobby, pause; } Gow3MenuPatched;

/* Appends the exit button to the pause menu's lists (the last item is Quit to Main Menu) and puts
 * one after Downloadables in the lobby's list (the one with the story button). Each changed list
 * is copied into the arena and its table entry repointed; the file's own arrays stay as they are.
 * A list that does not look as expected is left alone. */
static inline Gow3MenuPatched gow3_menu_patch(unsigned char *data, uint64_t size, unsigned char *arena,
                                              size_t arena_size, size_t *used) {
    Gow3MenuPatched done = {0, 0};
    const uint64_t first = (uint64_t)(uintptr_t)data, last = first + size;
    for (uint64_t at = 0; at + GOW3_MENU_ENTRY_SIZE <= size; at += 8) {
        unsigned char *entry = data + at;
        if (gow3_menu_u32(entry + 20) != GOW3_MENU_LIST_TYPE) continue;
        const uint64_t count = gow3_menu_u64(entry), items = gow3_menu_u64(entry + 8);
        if (count < 2 || count > GOW3_MENU_MAX_ITEMS || items < first || items % 8 ||
            items + count * GOW3_MENU_ITEM_SIZE > last) continue;
        unsigned char *list = (unsigned char *)(uintptr_t)items;
        int story = 0, options = -1, downloads = -1, ours = 0;
        for (uint64_t i = 0; i < count; ++i) {
            const unsigned char *item = list + i * GOW3_MENU_ITEM_SIZE;
            const uint32_t hash = gow3_menu_u32(item + GOW3_MENU_ITEM_HASH), kind = gow3_menu_u32(item + GOW3_MENU_ITEM_KIND);
            if (hash == GOW3_MENU_EXIT_LOBBY || hash == GOW3_MENU_EXIT_PAUSE) ours = 1;
            if (hash == GOW3_MENU_BUTTON_STORY) story = 1;
            if (hash == GOW3_MENU_BUTTON_OPTIONS && options < 0) options = (int)i;
            if (!hash && kind == GOW3_MENU_KIND_DOWNLOADS && downloads < 0) downloads = (int)i;
        }
        const unsigned char *last_item = list + (count - 1) * GOW3_MENU_ITEM_SIZE;
        const int quit = gow3_menu_u32(last_item + GOW3_MENU_ITEM_HASH) == GOW3_MENU_BUTTON_QUIT &&
                         gow3_menu_u32(last_item + GOW3_MENU_ITEM_KIND) == GOW3_MENU_KIND_BUTTON;
        if (ours || (!quit && !(story && options >= 0 && downloads >= 0))) continue;
        const uint64_t insert = quit ? count : (uint64_t)downloads + 1;
        const unsigned char *model = quit ? last_item : list + (uint64_t)options * GOW3_MENU_ITEM_SIZE;
        const size_t need = GOW3_MENU_NAME_SIZE + (count + 1) * GOW3_MENU_ITEM_SIZE + 16;
        if (*used + need > arena_size) continue;
        unsigned char *name = arena + *used;
        unsigned char *copy = (unsigned char *)(((uintptr_t)name + GOW3_MENU_NAME_SIZE + 15) & ~(uintptr_t)15);
        memset(name, 0, GOW3_MENU_NAME_SIZE);
        memcpy(name, "BUTTON_EXIT_DESKTOP", 20);
        memcpy(copy, list, insert * GOW3_MENU_ITEM_SIZE);
        memcpy(copy + (insert + 1) * GOW3_MENU_ITEM_SIZE, list + insert * GOW3_MENU_ITEM_SIZE,
               (count - insert) * GOW3_MENU_ITEM_SIZE);
        unsigned char *item = copy + insert * GOW3_MENU_ITEM_SIZE;
        memcpy(item, model, GOW3_MENU_ITEM_SIZE);
        const uint64_t name_pointer = (uint64_t)(uintptr_t)name;
        const uint32_t hash = quit ? GOW3_MENU_EXIT_PAUSE : GOW3_MENU_EXIT_LOBBY, label = GOW3_MENU_LABEL_QUIT;
        memcpy(item, &name_pointer, 8);
        memcpy(item + GOW3_MENU_ITEM_HASH, &hash, 4);
        memcpy(item + GOW3_MENU_ITEM_LABEL, &label, 4);
        *used = (size_t)(copy + (count + 1) * GOW3_MENU_ITEM_SIZE - arena);
        const uint64_t new_count = count + 1, new_items = (uint64_t)(uintptr_t)copy;
        memcpy(entry, &new_count, 8);
        memcpy(entry + 8, &new_items, 8);
        if (quit) ++done.pause; else ++done.lobby;
    }
    return done;
}

static inline unsigned gow3_menu_emit_ymm(unsigned char *out, unsigned load) {
    unsigned k = 0;
    for (unsigned n = 0; n < 16; ++n) {
        const int32_t disp = (int32_t)(n * 32);
        out[k++] = 0xc5;
        out[k++] = n < 8 ? 0xfc : 0x7c;
        out[k++] = load ? 0x10 : 0x11;
        out[k++] = (unsigned char)(0x84 | ((n & 7) << 3));
        out[k++] = 0x24;
        memcpy(out + k, &disp, 4);
        k += 4;
    }
    return k;
}

/* The loader's hook: every register and vector register comes back as it was; helper(base)
 * runs in between, then the replaced bytes and a jump back. The size of the stub. */
static inline unsigned gow3_menu_build_load_stub(unsigned char *out, uintptr_t helper, uintptr_t resume,
                                                 const unsigned char original[GOW3_MENU_LOAD_LENGTH]) {
    static const unsigned char save[] = {
        0x9c, 0x50, 0x51, 0x52, 0x56, 0x57, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, /* pushfq, rax..r11 */
        0x55, 0x48, 0x89, 0xe5,                                                             /* push rbp; mov rbp, rsp */
        0x48, 0x83, 0xe4, 0xe0,                                                             /* and rsp, -32 */
        0x48, 0x81, 0xec, 0x00, 0x02, 0x00, 0x00                                            /* sub rsp, 0x200 */
    };
    static const unsigned char restore[] = {
        0x48, 0x89, 0xec, 0x5d, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, /* mov rsp, rbp; pop rbp; pop r11..r8 */
        0x5f, 0x5e, 0x5a, 0x59, 0x58, 0x9d                                      /* pop rdi, rsi, rdx, rcx, rax; popfq */
    };
    unsigned k = 0;
    memcpy(out + k, save, sizeof(save)); k += sizeof(save);
    k += gow3_menu_emit_ymm(out + k, 0);
    out[k++] = 0x4c; out[k++] = 0x89; out[k++] = 0xc7; /* mov rdi, r8 */
    out[k++] = 0x48; out[k++] = 0xb8;                   /* mov rax, helper */
    memcpy(out + k, &helper, 8); k += 8;
    out[k++] = 0xff; out[k++] = 0xd0;                   /* call rax */
    k += gow3_menu_emit_ymm(out + k, 1);
    memcpy(out + k, restore, sizeof(restore)); k += sizeof(restore);
    memcpy(out + k, original, GOW3_MENU_LOAD_LENGTH); k += GOW3_MENU_LOAD_LENGTH;
    static const unsigned char jump[] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00}; /* jmp [rip+0] */
    memcpy(out + k, jump, sizeof(jump)); k += sizeof(jump);
    memcpy(out + k, &resume, 8); k += 8;
    return k;
}

/* The dispatcher's hook: helper(hash) is told of every button, then the dispatcher runs as it
 * would have (the arguments and the stack as at its entry). */
static inline unsigned gow3_menu_build_click_stub(unsigned char *out, uintptr_t helper, uintptr_t resume,
                                                  const unsigned char original[GOW3_MENU_CLICK_LENGTH]) {
    static const unsigned char save[] = {
        0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51, /* push rdi, rsi, rdx, rcx, r8, r9 */
        0x48, 0x83, 0xec, 0x08,                         /* sub rsp, 8 */
        0x89, 0xf7,                                     /* mov edi, esi */
        0x48, 0xb8                                      /* mov rax, helper */
    };
    static const unsigned char restore[] = {
        0x48, 0x83, 0xc4, 0x08, 0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f /* add rsp, 8; pop r9..rdi */
    };
    unsigned k = 0;
    memcpy(out + k, save, sizeof(save)); k += sizeof(save);
    memcpy(out + k, &helper, 8); k += 8;
    out[k++] = 0xff; out[k++] = 0xd0; /* call rax */
    memcpy(out + k, restore, sizeof(restore)); k += sizeof(restore);
    memcpy(out + k, original, GOW3_MENU_CLICK_LENGTH); k += GOW3_MENU_CLICK_LENGTH;
    static const unsigned char jump[] = {0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
    memcpy(out + k, jump, sizeof(jump)); k += sizeof(jump);
    memcpy(out + k, &resume, 8); k += 8;
    return k;
}
#endif
