// Patching test (32-bit, run in the build container): a fake game in memory,
// a getter and two `call getter` sites, one of each kind; the real patch and
// hooks of freeplay_attract.c are applied to it.
//
//   gcc -m32 -std=gnu99 -Wall -I . -I libusb -o /tmp/test_fpa_patch tests/test_freeplay_attract_patch.c -lpthread -lrt && /tmp/test_fpa_patch
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "freeplay_attract.c"   // the statics (fpa_patch, fpa_hook ...)

unsigned char bytes_f[16], bytes_fb[2];   // what piuio_emu.c owns

#define BASE 0x60000000u
static int fails = 0;
static void check(const char* what, int ok) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if(!ok) fails++; }

int main(void)
{
    unsigned char* m = mmap((void*)BASE, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    static struct fpa_getter getters[3];
    static struct fpa_site sites[2];
    static struct fpa_build b;
    unsigned char *code = m, *data = m + 0x1000;
    uint32_t rel;
    int (*att)(void*), (*sta)(void*);
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    if(m != (unsigned char*)BASE) { puts("mmap failed"); return 2; }
    // getter at BASE: mov $99,%eax; ret   (free play answers 99; here always)
    memcpy(code, "\xb8\x63\x00\x00\x00\xc3", 6);
    // site 1 at BASE+0x100: call getter; ret ; site 2 at BASE+0x200
    for(i = 1; i <= 2; i++) {
        unsigned char* s = code + 0x100 * i;
        rel = (uint32_t)(BASE - (BASE + 0x100 * i + 5));
        s[0] = 0xe8; memcpy(s + 1, &rel, 4); s[5] = 0xc3;
    }
    getters[0].at = BASE; memcpy(getters[0].sig, code, 6);
    sites[0].at = BASE + 0x100; sites[0].getter = 0; sites[0].kind = FPA_ATTRACT;
    sites[1].at = BASE + 0x200; sites[1].getter = 0; sites[1].kind = FPA_START;
    b.name = "fake"; b.getters = getters; b.sites = sites; b.nsites = 2;
    b.cpc = BASE + 0x1000; b.event = BASE + 0x1001; b.flags = BASE + 0x1004;
    memset(data, 0, 16);

    mprotect(m + 0x1000, 0x1000, PROT_READ | PROT_WRITE);   // data page: patching must leave it alone
    check("fake build matches", fpa_build_match(&b, fpa_mem_read, NULL));
    att = (int (*)(void*))(BASE + 0x100);
    sta = (int (*)(void*))(BASE + 0x200);
    // an unpatched call is the game's: 99
    check("before the patch: 99", att(NULL) == 99 && sta(NULL) == 99);
    check("patch applies", fpa_patch(&b) == 0);
    // the sites are now `call hook`, not `call getter`
    memcpy(&rel, code + 0x101, 4);
    check("rel32 of site 1 points to the attract hook", (uint32_t)(BASE + 0x105 + rel) == (uint32_t)(uintptr_t)fpa_hook_a0);
    memcpy(&rel, code + 0x201, 4);
    check("rel32 of site 2 points to the start hook", (uint32_t)(BASE + 0x205 + rel) == (uint32_t)(uintptr_t)fpa_hook_s0);

    data[0] = 5;   // coins per credit 5: not free play
    check("coin mode: the game's own answer", att(NULL) == 99 && sta(NULL) == 99);
    data[0] = 0;   // free play
    check("free play, attract: 0", att(NULL) == 0);
    check("free play, start: 0", sta(NULL) == 0);
    bytes_f[0] = 0xff; for(i = 0; i < 8; i++) bytes_f[i] = 0xff;
    bytes_fb[0] = 0xff;
    bytes_f[2] = 0xfb;  // a centre step (bit 2 low)
    fpa_gave_up = 0;
    freeplay_attract_poll(0);
    check("centre step: attract answers 99, start still 0", att(NULL) == 99 && sta(NULL) == 0);
    bytes_f[2] = 0xff;
    freeplay_attract_poll(0);
    check("panel released: start answers 99", sta(NULL) == 99);
    data[4] = 1;   // a player joined
    freeplay_attract_poll(0);
    data[4] = 0;
    check("joined then over: latch cleared, attract is back", att(NULL) == 0 && sta(NULL) == 0);
    data[0] = 5; data[1] = 1;   // event mode
    data[0] = 5;
    check("event mode counts as free play", att(NULL) == 0);

    // ---- the inline family: a fake game with `cmpb $0,cpc ; sete %al ; ret` and the event one
    {
        static struct fpa_isite isites[4];
        static struct fpa_ibuild ib;
        int (*f[4])(void);
        unsigned char *cpcb = data + 0x20, *evb = data + 0x21, *fl = data + 0x24;
        unsigned char op[4][7];
        uint32_t a;
        mprotect(m, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);   // the first patch left the text read-only
        fpa_b = NULL; fpa_ib = NULL; memset((void*)&fpa_l, 0, sizeof(fpa_l)); fpa_gave_up = 0; fpa_start_ms = 0;
        memset(data, 0, 0x40);
        for(i = 0; i < 4; i++) {
            unsigned char* c = code + 0x300 + 0x10 * i;
            a = (uint32_t)(uintptr_t)(i % 2 ? evb : cpcb);
            c[0] = 0x80; c[1] = 0x3d; memcpy(c + 2, &a, 4); c[6] = i % 2;   // cmpb $0/$1, addr
            memcpy(c + 7, "\x0f\x94\xc0\x0f\xb6\xc0\xc3", 7);            // sete %al ; movzbl %al,%eax ; ret
            isites[i].at = BASE + 0x300 + 0x10 * i;
            isites[i].which = i % 2;
            isites[i].kind = i < 2 ? FPA_ATTRACT : FPA_START;
            memcpy(op[i], c, 7);
            f[i] = (int (*)(void))(BASE + 0x300 + 0x10 * i);
        }
        ib.name = "fake inline"; ib.sites = isites; ib.nsites = 4;
        ib.cpc = (uintptr_t)cpcb; ib.event = (uintptr_t)evb; ib.flags = (uintptr_t)fl;
        ib.anchor = BASE + 0x300; memcpy(ib.sig, code + 0x300, FPA_SIG_LEN);
        *cpcb = 0; *evb = 0;   // free play
        check("inline: fake build matches", fpa_ibuild_match(&ib, fpa_mem_read, NULL));
        check("inline: before the patch the game sees free play", f[0]() == 1 && f[2]() == 1);
        check("inline: patch applies", fpa_ipatch(&ib) == 0);
        check("inline: operands point to the shadow bytes", memcmp(code + 0x300 + 2, &(uint32_t){ (uint32_t)(uintptr_t)&fpa_shadow[0] }, 4) == 0
              && memcmp(code + 0x310 + 2, &(uint32_t){ (uint32_t)(uintptr_t)&fpa_shadow[1] }, 4) == 0
              && memcmp(code + 0x320 + 2, &(uint32_t){ (uint32_t)(uintptr_t)&fpa_shadow[2] }, 4) == 0);
        check("inline: opcode and immediate untouched", code[0x300] == 0x80 && code[0x306] == op[0][6] && code[0x316] == 1);
        check("inline: free play, unarmed: attract sees coin mode, start too", f[0]() == 0 && f[1]() == 0 && f[2]() == 0);
        bytes_f[2] = 0xfb;   // a centre step
        freeplay_attract_poll(0);
        check("inline: centre step: attract sees free play, start still not", f[0]() == 1 && f[2]() == 0);
        bytes_f[2] = 0xff;
        freeplay_attract_poll(0);
        check("inline: released: start sees free play", f[2]() == 1);
        *fl = 1;   // a player joined
        freeplay_attract_poll(0);
        check("inline: joined: the real values", f[0]() == 1 && f[2]() == 1);
        *fl = 0;
        freeplay_attract_poll(0);
        check("inline: after the game the attract is back", f[0]() == 0 && f[2]() == 0);
        *cpcb = 5; *evb = 1;   // coin mode but event
        freeplay_attract_poll(0);
        check("inline: event mode hidden until armed", f[1]() == 0);
        *cpcb = 5; *evb = 0;
        freeplay_attract_poll(0);
        check("inline: coin mode: real values (event 0, cpc 5)", f[0]() == 0 && f[1]() == 0);
        *cpcb = 0;
        bytes_f[2] = 0xfb; freeplay_attract_poll(0); bytes_f[2] = 0xff; freeplay_attract_poll(0);
        check("inline: armed again", f[0]() == 1);
    }
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
