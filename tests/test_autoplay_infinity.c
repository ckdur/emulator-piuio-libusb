// Host test of the PIUIO shim's Infinity 1.10.0 autoplay (libusb/autoplay.c):
// the controller write and the three "autoplay counts nothing" branches
// (IMPER-167). The game's pages are mapped at their link addresses and filled
// from the real executable when one is given.
//
// 32-bit (autoplay.c is i386 only), PIE so 0x08048000 is free:
//   gcc -m32 -fPIE -pie -std=gnu17 -Wall -I libusb -o /tmp/t tests/test_autoplay_infinity.c libusb/autoplay.c
//   /tmp/t <pki/infinity/piu>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "autoplay.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static int fails;
#define CHECK(what, ok) do { int _ok = (ok); printf("%s %s\n", _ok ? "PASS" : "FAIL", what); if(!_ok) fails++; } while(0)

static const uintptr_t sites[3] = { 0x083693acu, 0x0836eaa1u, 0x0836fb6eu };

static unsigned char* map_page(uintptr_t at) {
    void* m = mmap((void*)(at & ~0xfffu), 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if(m == MAP_FAILED) { perror("mmap"); exit(2); }
    return m;
}

/* the pages of the three branches, from the executable (non-PIE at 0x08048000), read + exec */
static void load(const char* exe) {
    FILE* f = fopen(exe, "rb");
    if(!f) { perror(exe); exit(2); }
    for(int i = 0; i < 3; i++) {
        unsigned char* p = map_page(sites[i]);
        if(fseek(f, (long)((sites[i] & ~0xfffu) - 0x08048000u), SEEK_SET) || fread(p, 1, 0x1000, f) != 0x1000) exit(2);
        mprotect(p, 0x1000, PROT_READ | PROT_EXEC);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    if(argc != 2) { fprintf(stderr, "usage: %s <infinity piu>\n", argv[0]); return 2; }
    load(argv[1]);
    unsigned char before[3][16];
    for(int i = 0; i < 3; i++) memcpy(before[i], (void*)sites[i], 16);

    /* GAMESTATE -> two PlayerStates */
    static unsigned char gs[0x400], ps[2][0x900];
    uint32_t* gsp = (uint32_t*)map_page(0x086f59b0u) + (0x9b0 / 4);
    *gsp = (uint32_t)(uintptr_t)gs;
    for(int s = 0; s < 2; s++) *(uint32_t*)(gs + 0x36c + 4 * s) = (uint32_t)(uintptr_t)ps[s];

    extern struct autoplay_construct all_contstructs[];
    extern int size_all_constructs;
    struct autoplay_construct* inf = NULL;
    for(int i = 0; i < size_all_constructs; i++)
        if(all_contstructs[i].version == 6) inf = &all_contstructs[i];
    CHECK("the table has the Infinity entry", inf != NULL);
    if(!inf) return 1;
    void report_autoplay(struct autoplay_construct* con);
    report_autoplay(inf);
    CHECK("autoplay available", auto_available);

    update_autoplay();
    int same = 1;
    for(int i = 0; i < 3; i++) same &= memcmp(before[i], (void*)sites[i], 16) == 0;
    CHECK("off from the start: no code written", same);

    auto_1 = 0;
    update_autoplay();
    CHECK("P1 on: P1's controller is autoplay, P2's untouched", *(int*)(ps[0] + 0x8c4) == 1 && *(int*)(ps[1] + 0x8c4) == 0);
    const unsigned char* a = (const unsigned char*)sites[0];
    const unsigned char* b = (const unsigned char*)sites[1];
    const unsigned char* c = (const unsigned char*)sites[2];
    CHECK("on: judgement branch falls through", a[7] == 0x0f && a[8] == 0x84 && !a[9] && !a[10] && !a[11] && !a[12]);
    CHECK("on: tap-row and hold score branches fall through", b[7] == 0x74 && b[8] == 0 && c[7] == 0x74 && c[8] == 0);
    CHECK("on: nothing else changed", memcmp(before[0], a, 9) == 0 && memcmp(before[0] + 13, a + 13, 3) == 0 &&
          memcmp(before[1], b, 8) == 0 && memcmp(before[1] + 9, b + 9, 7) == 0 &&
          memcmp(before[2], c, 8) == 0 && memcmp(before[2] + 9, c + 9, 7) == 0);
    update_autoplay();
    CHECK("polled again while on: still patched", b[8] == 0);

    auto_1 = -1;
    update_autoplay();
    same = 1;
    for(int i = 0; i < 3; i++) same &= memcmp(before[i], (void*)sites[i], 16) == 0;
    CHECK("off: the code is the original again", same);
    CHECK("off: P1 is human again", *(int*)(ps[0] + 0x8c4) == 0);

    /* another build: one branch differs -> none is touched */
    mprotect((void*)(sites[2] & ~0xfffu), 0x1000, PROT_READ | PROT_WRITE);
    ((unsigned char*)sites[2])[8] = 0x11;
    mprotect((void*)(sites[2] & ~0xfffu), 0x1000, PROT_READ | PROT_EXEC);
    auto_1 = 0;
    update_autoplay();
    CHECK("unexpected code: no branch patched", memcmp(before[0], a, 16) == 0 && memcmp(before[1], b, 16) == 0 && c[8] == 0x11);

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
