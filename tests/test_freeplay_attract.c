// Host test for free play with attract in the older games (IMPER-119):
// the latch, the answer a hooked call gives, and that every build in the
// table matches the real executable (every getter signature, every call
// site is E8 + a rel32 to its getter) and nothing else.
//
//   gcc -std=gnu17 -Wall -Wextra -DFPA_LOGIC_ONLY -I libusb -o /tmp/test_fpa tests/test_freeplay_attract.c libusb/freeplay_attract.c
//   /tmp/test_fpa [dir with the executables: zero nx1 nx2 nx2cn nxa nxacn fiesta_120 fex_121 f2_161 JE_1_17_0/piu-PRIMEJE117] [prime piu]
// (default dir: /media/Extended/Games/PIU/plugin/exes_1_1; skipped when absent)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freeplay_attract.h"

static int fails = 0;
static void check(const char* what, int ok)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) fails++;
}

// ---- A game's file as its memory (32-bit ELF, PT_LOAD segments) --------------
struct img { unsigned char* d; size_t n; };
static uint32_t le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int img_read(uintptr_t addr, void* buf, size_t n, void* ctx)
{
    const struct img* im = ctx;
    uint32_t phoff = le32(im->d + 28);
    unsigned phnum = im->d[44] | im->d[45] << 8, i;
    for(i = 0; i < phnum; i++) {
        const unsigned char* ph = im->d + phoff + 32 * i;
        uint32_t type = le32(ph), off = le32(ph + 4), va = le32(ph + 8), fs = le32(ph + 16);
        if(type != 1) continue;
        if(addr >= va && addr + n <= (uintptr_t)va + fs) {
            memcpy(buf, im->d + off + (addr - va), n);
            return 0;
        }
    }
    return -1;
}

static int load(const char* path, struct img* im)
{
    FILE* f = fopen(path, "rb");
    if(!f) return -1;
    fseek(f, 0, SEEK_END);
    im->n = ftell(f);
    rewind(f);
    im->d = malloc(im->n);
    if(fread(im->d, 1, im->n, f) != im->n) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static const char* file_of(const char* name)
{
    static const char* m[][2] = { { "Zero", "zero" }, { "NX (NX1)", "nx1" }, { "NX2", "nx2" },
        { "NX2 CN", "nx2cn" }, { "NXA", "nxa" }, { "NXA CN", "nxacn" },
        { "Fiesta", "fiesta_120" }, { "Fiesta EX", "fex_121" }, { "Fiesta 2", "f2_161" },
        { "Prime", "../pki_prime" }, { "Prime JE", "JE_1_17_0/piu-PRIMEJE117" } };
    unsigned i;
    for(i = 0; i < sizeof(m) / sizeof(m[0]); i++)
        if(strcmp(m[i][0], name) == 0) return m[i][1];
    return NULL;
}

int main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "/media/Extended/Games/PIU/plugin/exes_1_1";
    const char* prime = argc > 2 ? argv[2] : "/home/ckdur/Documents/PIU/love-attack/build.multiloader.release/pki/prime/piu";
    struct fpa_latch l = { 0, 0 };
    int i, j;

    // ---- the latch
    fpa_latch_step(&l, 0, 0);
    check("boot: not armed", !l.armed);
    fpa_latch_step(&l, 0, 1);
    check("centre step arms", l.armed && !l.released);
    fpa_latch_step(&l, 0, 1);
    check("held centre: still not released", l.armed && !l.released);
    fpa_latch_step(&l, 0, 0);
    check("panel up: released", l.armed && l.released);
    fpa_latch_step(&l, 0, 1);
    check("a second step keeps it armed and released", l.armed && l.released);
    fpa_latch_step(&l, 1, 1);
    check("a player joined: cleared", !l.armed && !l.released);
    fpa_latch_step(&l, 1, 0);
    check("still playing: stays cleared", !l.armed && !l.released);
    fpa_latch_step(&l, 0, 1);
    check("after the game: arms again with a step", l.armed);

    // ---- what a hooked call answers
    {
        struct fpa_latch z = { 0, 0 }, a = { 1, 0 }, ar = { 1, 1 };
        check("not free play: the game's own answer (attract)", fpa_answer(&z, FPA_ATTRACT, 0, 0, 3) == 3);
        check("not free play: the game's own answer (start)", fpa_answer(&z, FPA_START, 0, 0, 3) == 3);
        check("free play, attract, not armed: 0", fpa_answer(&z, FPA_ATTRACT, 1, 0, 99) == 0);
        check("free play, attract, armed: 99", fpa_answer(&a, FPA_ATTRACT, 1, 0, 99) == 99);
        check("free play, start, not armed: 0", fpa_answer(&z, FPA_START, 1, 0, 99) == 0);
        check("free play, start, armed but panel held: 0", fpa_answer(&a, FPA_START, 1, 0, 99) == 0);
        check("free play, start, armed and released: 99", fpa_answer(&ar, FPA_START, 1, 0, 99) == 99);
        check("free play, a player joined: passes through", fpa_answer(&z, FPA_ATTRACT, 1, 1, 99) == 99);
        check("a bool answer passes unchanged", fpa_answer(&a, FPA_ATTRACT, 1, 0, 1) == 1);
    }

    // ---- the whole story: attract, step, title stays, step starts
    {
        struct fpa_latch s = { 0, 0 };
        int joined = 0;
        fpa_latch_step(&s, joined, 0);
        check("story: attract runs (timeout answers 0)", fpa_answer(&s, FPA_ATTRACT, 1, joined, 99) == 0);
        fpa_latch_step(&s, joined, 1);
        check("story: step in attract: scene leaves for the title", fpa_answer(&s, FPA_ATTRACT, 1, joined, 99) == 99);
        check("story: same press does not start the game", fpa_answer(&s, FPA_START, 1, joined, 99) == 0);
        fpa_latch_step(&s, joined, 0);
        check("story: title stays (timeout answers 99)", fpa_answer(&s, FPA_ATTRACT, 1, joined, 99) == 99);
        check("story: the next step starts", fpa_answer(&s, FPA_START, 1, joined, 99) == 99);
        joined = 1;
        fpa_latch_step(&s, joined, 1);
        check("story: in play everything passes through", fpa_answer(&s, FPA_START, 1, joined, 99) == 99);
        joined = 0;
        fpa_latch_step(&s, joined, 0);
        check("story: after the game the attract is back", fpa_answer(&s, FPA_ATTRACT, 1, joined, 99) == 0);
    }

    // ---- the table against the executables
    for(i = 0; i < fpa_nbuilds; i++) {
        const struct fpa_build* b = &fpa_builds[i];
        const char* f = file_of(b->name);
        char path[512];
        struct img im;
        char what[160];
        if(!f) { check("every build has a file name in the test", 0); continue; }
        snprintf(path, sizeof(path), "%s/%s", dir, f);
        if(strcmp(f, "../pki_prime") == 0) snprintf(path, sizeof(path), "%s", prime);
        if(load(path, &im) != 0) { printf("skip: %s (%s not readable)\n", b->name, path); continue; }
        snprintf(what, sizeof(what), "%s: table matches %s", b->name, f);
        check(what, fpa_build_match(b, img_read, &im));
        for(j = 0; j < fpa_nbuilds; j++) {
            if(j == i) continue;
            if(fpa_build_match(&fpa_builds[j], img_read, &im)) {
                snprintf(what, sizeof(what), "%s: not matched by %s", f, fpa_builds[j].name);
                check(what, 0);
            }
        }
        // one wrong byte at the first site: nothing is patched
        {
            struct fpa_build bad = *b;
            struct fpa_site* sites = malloc(sizeof(struct fpa_site) * b->nsites);
            memcpy(sites, b->sites, sizeof(struct fpa_site) * b->nsites);
            sites[b->nsites - 1].at += 1;
            bad.sites = sites;
            snprintf(what, sizeof(what), "%s: a shifted site is refused", b->name);
            check(what, !fpa_build_match(&bad, img_read, &im));
            free(sites);
        }
        // every site is a distinct address, E8, and kinds are valid
        for(j = 0; j < b->nsites; j++) {
            int k;
            for(k = j + 1; k < b->nsites; k++)
                if(b->sites[j].at == b->sites[k].at) { snprintf(what, sizeof(what), "%s: duplicate site", b->name); check(what, 0); }
            if(b->sites[j].kind > 1) { snprintf(what, sizeof(what), "%s: bad kind", b->name); check(what, 0); }
        }
        free(im.d);
    }

    // ---- the inline family: the shadow bytes
    {
        unsigned char o[4];
        struct fpa_latch z = { 0, 0 }, a = { 1, 0 }, ar = { 1, 1 };
        fpa_shadow_calc(&z, 0, 5, 0, o);
        check("shadow: coin mode, nothing hidden", o[0] == 5 && o[1] == 0 && o[2] == 5 && o[3] == 0);
        fpa_shadow_calc(&z, 0, 0, 0, o);
        check("shadow: free play unarmed: attract and start see coin mode", o[0] != 0 && o[1] == 0 && o[2] != 0 && o[3] == 0);
        fpa_shadow_calc(&z, 0, 5, 1, o);
        check("shadow: event mode unarmed: event hidden, cpc real", o[0] == 5 && o[1] == 0 && o[3] == 0);
        fpa_shadow_calc(&a, 0, 0, 0, o);
        check("shadow: armed: attract real, start still hidden", o[0] == 0 && o[1] == 0 && o[2] != 0);
        fpa_shadow_calc(&a, 0, 5, 1, o);
        check("shadow: armed, event: attract sees the event", o[1] == 1 && o[3] == 0);
        fpa_shadow_calc(&ar, 0, 0, 0, o);
        check("shadow: armed and released: all real", o[0] == 0 && o[2] == 0);
        fpa_shadow_calc(&z, 1, 0, 0, o);
        check("shadow: a player joined: all real", o[0] == 0 && o[2] == 0);
    }
    for(i = 0; i < fpa_nibuilds; i++) {
        const struct fpa_ibuild* b = &fpa_ibuilds[i];
        const char* f = file_of(b->name);
        char path[512];
        struct img im;
        char what[160];
        int natt = 0, nst = 0;
        if(!f) { check("every inline build has a file name in the test", 0); continue; }
        snprintf(path, sizeof(path), "%s/%s", dir, f);
        if(strcmp(f, "../pki_prime") == 0) snprintf(path, sizeof(path), "%s", prime);
        if(load(path, &im) != 0) { printf("skip: %s (%s not readable)\n", b->name, path); continue; }
        snprintf(what, sizeof(what), "%s: inline table matches %s", b->name, f);
        check(what, fpa_ibuild_match(b, img_read, &im));
        for(j = 0; j < fpa_nibuilds; j++) {
            if(j == i) continue;
            if(fpa_ibuild_match(&fpa_ibuilds[j], img_read, &im)) { snprintf(what, sizeof(what), "%s: not matched by %s", f, fpa_ibuilds[j].name); check(what, 0); }
        }
        for(j = 0; j < fpa_nbuilds; j++)
            if(fpa_build_match(&fpa_builds[j], img_read, &im)) { snprintf(what, sizeof(what), "%s: not matched by getter build %s", f, fpa_builds[j].name); check(what, 0); }
        // every site: a distinct address, pairs (cpc + event) of one kind
        for(j = 0; j < b->nsites; j++) {
            int k;
            for(k = j + 1; k < b->nsites; k++)
                if(b->sites[j].at == b->sites[k].at) { snprintf(what, sizeof(what), "%s: duplicate inline site", b->name); check(what, 0); }
            if(b->sites[j].kind > 1 || b->sites[j].which > 1) { snprintf(what, sizeof(what), "%s: bad inline site", b->name); check(what, 0); }
            if(b->sites[j].kind == FPA_ATTRACT) natt++; else nst++;
        }
        snprintf(what, sizeof(what), "%s: %d attract and %d start operands", b->name, natt, nst);
        check(what, natt >= 12 && nst == 2);
        // a shifted site, a changed address and a changed anchor are refused
        {
            struct fpa_ibuild bad = *b;
            struct fpa_isite* sites = malloc(sizeof(struct fpa_isite) * b->nsites);
            memcpy(sites, b->sites, sizeof(struct fpa_isite) * b->nsites);
            sites[b->nsites - 1].at += 1;
            bad.sites = sites;
            snprintf(what, sizeof(what), "%s: a shifted inline site is refused", b->name);
            check(what, !fpa_ibuild_match(&bad, img_read, &im));
            sites[b->nsites - 1].at -= 1;
            bad.cpc += 1;
            snprintf(what, sizeof(what), "%s: another cpc address is refused", b->name);
            check(what, !fpa_ibuild_match(&bad, img_read, &im));
            bad.cpc -= 1;
            bad.sig[3] ^= 1;
            snprintf(what, sizeof(what), "%s: a changed anchor is refused", b->name);
            check(what, !fpa_ibuild_match(&bad, img_read, &im));
            free(sites);
        }
        free(im.d);
    }

    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails ? 1 : 0;
}
