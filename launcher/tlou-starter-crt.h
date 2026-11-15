/* The little the starters (tlou-launcher.c, tlou-game.c) need from a C library, so they link
 * without one (build-starters.sh: -nostdlib, kernel32 and user32 only). The MinGW C runtime's
 * start-up code brings TLS callbacks, VirtualProtect/VirtualQuery (pseudo-relocations) and an
 * unhandled-exception filter: in a small unsigned program that starts a hidden interpreter,
 * antivirus heuristics read those as a packed malware loader (issues #88, #112, #148:
 * Trojan:Win32/Wacatac.B!ml, Win64/GenKryptik, VirusTotal's "obfuscated" and
 * "detect-debug-environment" tags). Include it once per program, after <windows.h>. */
#ifndef TLOU_STARTER_CRT_H
#define TLOU_STARTER_CRT_H

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>

/* The compiler may still call these for struct initialisation and copies. */
void *memset(void *destination, int value, size_t size)
{
    volatile unsigned char *out = destination;
    while (size--)
        *out++ = (unsigned char)value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t size)
{
    volatile unsigned char *out = destination;
    const unsigned char *in = source;
    while (size--)
        *out++ = *in++;
    return destination;
}

/* swprintf with only %ls (and %%): the starters only join paths and command lines. Always
 * terminated; cut at size - 1 characters. */
static inline int format(wchar_t *out, size_t size, const wchar_t *pattern, ...)
{
    va_list values;
    va_start(values, pattern);
    size_t length = 0;
    for (const wchar_t *p = pattern; *p; p++) {
        const wchar_t *text = NULL, single[2] = {*p, 0};
        if (p[0] == L'%' && p[1] == L'l' && p[2] == L's') {
            text = va_arg(values, const wchar_t *);
            p += 2;
        } else if (p[0] == L'%' && p[1] == L'%') {
            text = L"%";
            p++;
        } else
            text = single;
        for (; *text && length + 1 < size; text++)
            out[length++] = *text;
    }
    va_end(values);
    if (size)
        out[length] = 0;
    return (int)length;
}

static inline wchar_t *last_char(wchar_t *text, wchar_t wanted)
{
    wchar_t *found = NULL;
    for (; *text; text++)
        if (*text == wanted)
            found = text;
    return found;
}

static inline void copy_text(wchar_t *out, const wchar_t *text)
{
    while ((*out++ = *text++))
        ;
}

static inline const char *find_char(const char *text, char wanted)
{
    for (; *text; text++)
        if (*text == wanted)
            return text;
    return NULL;
}

static inline int same_start(const char *a, const char *b, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 1;
}

static inline const char *find_text(const char *text, const char *wanted)
{
    size_t length = 0;
    while (wanted[length])
        length++;
    for (; *text; text++)
        if (same_start(text, wanted, length))
            return text;
    return NULL;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show);

/* The program's entry point (build-starters.sh: -e starter_entry): wWinMain's arguments are the
 * command line after the program's own name, as the C runtime gives them. */
void starter_entry(void)
{
    wchar_t *line = GetCommandLineW();
    if (*line == L'"') {
        for (line++; *line && *line != L'"'; line++)
            ;
        if (*line)
            line++;
    } else {
        while (*line && *line != L' ' && *line != L'\t')
            line++;
    }
    while (*line == L' ' || *line == L'\t')
        line++;
    ExitProcess((UINT)wWinMain(GetModuleHandleW(NULL), NULL, line, SW_SHOWDEFAULT));
}

#endif
