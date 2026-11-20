// Host test for the Phoenix 2.12 / 2.13 autoplay constructs of the PIUIO
// shim (libusb/autoplay.c). Each game executable is mapped at its link
// address, as the dynamic loader does, in a child process; then the real
// check_autoplay() / update_autoplay() run on it:
//   - the build is identified and autoplay is available, with the judgment
//     slots of that build (p1 = base + 0x20c58, p2 = p1 + 0x211f0);
//   - on: both slots get judgment 0; off: back to -1 once;
//   - one byte of the DEMO set-up changed: identified, no autoplay (a build
//     with the same strings but another layout is never poked).
// 32-bit (autoplay.c is i386 only), PIE so 0x08048000 is free:
//
//   i686-linux-gnu-gcc -std=gnu17 -Wall -I libusb -o /tmp/test_autoplay_phoenix tests/test_autoplay_phoenix.c libusb/autoplay.c
//   /tmp/test_autoplay_phoenix [piu 2.12] [piu 2.13]
//
// Default paths: $BASE_GAMEDATA/pki/phoenix/piu.phoenix.2.12.upxd.elf and
// piu.phoenix.2.13 (BASE_GAMEDATA=/media/Extended/Games/PIU/plugin); a
// missing file is skipped.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "autoplay.h"

extern unsigned int* player1_auto;
extern unsigned int* player2_auto;

static int fails = 0;
static void check(const char *what, int ok) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) fails++;
}

// map the PT_LOAD segments of an ELF32 ET_EXEC at their addresses
static int map_exe(const char* path) {
    int fd = open(path, O_RDONLY);
    struct stat st;
    if(fd < 0 || fstat(fd, &st) != 0) return 0;
    unsigned char* f = malloc(st.st_size);
    if(read(fd, f, st.st_size) != st.st_size) return 0;
    close(fd);
    uint32_t phoff = *(uint32_t*)(f + 0x1c);
    uint16_t phnum = *(uint16_t*)(f + 0x2c);
    for(int i = 0; i < phnum; i++) {
        uint32_t* ph = (uint32_t*)(f + phoff + i * 0x20);
        if(ph[0] != 1) continue;                 // PT_LOAD
        uint32_t off = ph[1], va = ph[2], fsz = ph[4], msz = ph[5];
        uint32_t lo = va & ~0xfffu, hi = (va + msz + 0xfff) & ~0xfffu;
        void* m = mmap((void*)(uintptr_t)lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if(m != (void*)(uintptr_t)lo) { perror("mmap"); return 0; }
        memcpy((void*)(uintptr_t)va, f + off, fsz);   // the rest (.bss) stays 0
    }
    free(f);
    return 1;
}

struct expect {
    const char* file;
    const char* ver;
    uint32_t p1, p2, demo_set;
};

// one build, in a child (the mappings and the shim's globals start clean)
static int run(const struct expect* e, int corrupt) {
    pid_t pid = fork();
    if(pid == 0) {
        fails = 0;
        if(!map_exe(e->file)) { printf("FAIL: cannot map %s\n", e->file); _exit(1); }
        if(corrupt) ((unsigned char*)(uintptr_t)e->demo_set)[corrupt] ^= 0x40;
        // the game's own state before anybody forces anything
        *(uint32_t*)(uintptr_t)e->p1 = 0xffffffff;
        *(uint32_t*)(uintptr_t)e->p2 = 0xffffffff;
        check_autoplay();
        char what[128];
        if(corrupt) {
            snprintf(what, sizeof(what), "%s, DEMO set-up byte %d changed: no autoplay", e->ver, corrupt);
            check(what, !auto_available);
            snprintf(what, sizeof(what), "%s, DEMO set-up changed: still identified", e->ver);
            check(what, strcmp(game_ver, e->ver) == 0);
            auto_1 = auto_2 = 0;
            update_autoplay();
            check("  nothing poked", *(uint32_t*)(uintptr_t)e->p1 == 0xffffffff);
            _exit(fails ? 1 : 0);
        }
        snprintf(what, sizeof(what), "%s: identified, autoplay available", e->ver);
        check(what, auto_available && strcmp(game_ver, e->ver) == 0 &&
                    strcmp(game_name, "IT UP: PHOENIX") == 0);
        check("  judgment slots of this build",
              player1_auto == (unsigned int*)(uintptr_t)e->p1 &&
              player2_auto == (unsigned int*)(uintptr_t)e->p2);
        if(!auto_available) _exit(1);
        update_autoplay();
        check("  off at start: untouched", *player1_auto == 0xffffffff && *player2_auto == 0xffffffff);
        auto_1 = auto_2 = 0;                    // twitch.c `req 9`
        update_autoplay();
        check("  on: both players judged 0", *player1_auto == 0 && *player2_auto == 0);
        *player1_auto = 5;                      // the game writes the slot itself
        update_autoplay();
        check("  on: forced again every poll", *player1_auto == 0);
        auto_1 = auto_2 = -1;                   // `req 10`
        update_autoplay();
        check("  off: back to -1", *player1_auto == 0xffffffff && *player2_auto == 0xffffffff);
        *player1_auto = 3;
        update_autoplay();
        check("  off: the game's own value is left alone", *player1_auto == 3);
        _exit(fails ? 1 : 0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int main(int argc, char** argv) {
    const char* gd = getenv("BASE_GAMEDATA");
    char f12[512], f13[512];
    if(!gd) gd = "/media/Extended/Games/PIU/plugin";
    snprintf(f12, sizeof(f12), "%s", argc > 1 ? argv[1] : "");
    snprintf(f13, sizeof(f13), "%s", argc > 2 ? argv[2] : "");
    if(!f12[0]) snprintf(f12, sizeof(f12), "%s/pki/phoenix/piu.phoenix.2.12.upxd.elf", gd);
    if(!f13[0]) snprintf(f13, sizeof(f13), "%s/pki/phoenix/piu.phoenix.2.13", gd);
    struct expect ex[] = {
        { f12, "v2.12.0", 0x96baa18, 0x96dbc08, 0x80dca10 },
        { f13, "v2.13.0", 0x96b99f8, 0x96dabe8, 0x80dbd1a },
    };
    int bad = 0, ran = 0;
    setvbuf(stdout, NULL, _IONBF, 0);       // the children _exit
    for(unsigned i = 0; i < sizeof(ex) / sizeof(ex[0]); i++) {
        if(access(ex[i].file, R_OK) != 0) { printf("skip: %s (no file)\n", ex[i].file); continue; }
        ran++;
        fflush(stdout);
        bad += run(&ex[i], 0) != 0;
        fflush(stdout);
        bad += run(&ex[i], 4) != 0;            // the demo_var address of the set-up
        fflush(stdout);
        bad += run(&ex[i], 21) != 0;           // the P2 slot address
        fflush(stdout);
    }
    // the bytes check_autoplay expects, built from the construct
    struct autoplay_construct c;
    unsigned char b[27];
    memset(&c, 0, sizeof(c));
    c.demo_var = (void*)0x979e7a4; c.p1_a = (void*)0x96baa18; c.p2_a = (void*)0x96dbc08;
    autoplay_demo_set_bytes(&c, b);
    static const unsigned char want[27] = {
        0xc6,0x05,0xa4,0xe7,0x79,0x09,0x01, 0xc7,0x05,0x14,0xaa,0x6b,0x09,0,0,0,0,
        0xc7,0x05,0x04,0xbc,0x6d,0x09,0,0,0,0 };
    check("DEMO set-up bytes of the 2.12 construct", memcmp(b, want, 27) == 0);
    bad += fails;
    printf("%s (%d build(s))\n", bad ? "FAILED" : "all passed", ran);
    return bad ? 1 : 0;
}
