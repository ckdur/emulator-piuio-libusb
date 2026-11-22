// Free play with the attract sequence for the older games (IMPER-119).
// See freeplay_attract.h for what it does and the format of the table.
//
// Group B (further down): Fiesta, Fiesta EX (getter family, same table as A)
// and Fiesta 2, Prime, Prime JE (the inline family: shadow bytes, see the
// header and the tables' comments).
//
// Group A: Zero, NX, NX2, NX2 CN, NXA, NXA CN, the games whose
// credit manager has getter functions. Their sites were found by reading the
// executables (the ones in exes_1_1, byte for byte the same as pki/<game>/piu):
//   FPA_ATTRACT  the title's ~52 s timeout (cmp $0xc4d), the scenes of the
//                attract (logo, movies, demo play) that leave for the title
//                when coins are present, the random pick of the intro
//   FPA_START    where a centre press with a credit joins a player
// Not hooked, on purpose: the sites that use the number itself (coin
// display, price), the coin-inserted jingle handlers, song select.
#define _GNU_SOURCE
#include "freeplay_attract.h"

#include <string.h>

#ifndef FPA_LOGIC_ONLY
#include "piuio_emu.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>

extern unsigned char bytes_fb[2];  // piuio_emu.c: button board, as the game last read it
#endif


// Zero
static const struct fpa_site fpa_zero_sites[] = {
    { 0x804df0a, 0, FPA_ATTRACT },
    { 0x806725d, 1, FPA_ATTRACT }, { 0x8067345, 1, FPA_ATTRACT }, { 0x80674d9, 1, FPA_ATTRACT }, { 0x8067c83, 1, FPA_ATTRACT }, { 0x807178b, 1, FPA_ATTRACT }, { 0x8071c4f, 1, FPA_ATTRACT }, { 0x8077faf, 1, FPA_ATTRACT }, { 0x807b7a8, 1, FPA_ATTRACT }, { 0x807c6fb, 1, FPA_ATTRACT },
    { 0x806739c, 0, FPA_START }, { 0x8067f65, 0, FPA_START }, { 0x8076f93, 0, FPA_START },
};
static const struct fpa_getter fpa_zero_getters[3] = {
    { 0x8058fc0, { 0x53, 0x8b, 0x4c, 0x24, 0x08, 0x8b, 0x51, 0x34, 0x0f, 0xb6, 0x82, 0xde, 0x0e, 0x00, 0x00, 0x84, 0xc0, 0x74, 0x1d, 0x80, 0xba, 0xd6, 0x0e, 0x00, 0x00, 0x01, 0x74, 0x14, 0x8b, 0x51, 0x38, 0x0f } },
    { 0x8059000, { 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x50, 0x34, 0x80, 0xba, 0xde, 0x0e, 0x00, 0x00, 0x00, 0x74, 0x10, 0x80, 0xba, 0xd6, 0x0e, 0x00, 0x00, 0x01, 0x74, 0x07, 0x8b, 0x40, 0x38, 0xc3, 0x8d, 0x76, 0x00 } },
    { 0, { 0 } },
};

// NX1
static const struct fpa_site fpa_nx1_sites[] = {
    { 0x8054737, 0, FPA_ATTRACT }, { 0x80600c0, 0, FPA_ATTRACT },
    { 0x806ce7e, 1, FPA_ATTRACT }, { 0x808c75b, 1, FPA_ATTRACT }, { 0x808c7c0, 1, FPA_ATTRACT }, { 0x808c897, 1, FPA_ATTRACT }, { 0x808cc1c, 1, FPA_ATTRACT },
    { 0x8072c49, 0, FPA_START }, { 0x807ae7e, 0, FPA_START }, { 0x807ff37, 0, FPA_START }, { 0x80827c7, 0, FPA_START }, { 0x808b2de, 0, FPA_START }, { 0x808ba42, 0, FPA_START }, { 0x808c7f6, 0, FPA_START }, { 0x808cdaa, 0, FPA_START },
};
static const struct fpa_getter fpa_nx1_getters[3] = {
    { 0x804e5c0, { 0x0f, 0xb6, 0x05, 0xd6, 0xdb, 0xe3, 0x09, 0x84, 0xc0, 0x74, 0x19, 0x80, 0x3d, 0xce, 0xdb, 0xe3, 0x09, 0x01, 0x74, 0x10, 0x8b, 0x4c, 0x24, 0x04, 0x0f, 0xb6, 0xd0, 0x8b, 0x41, 0x34, 0x89, 0xd1 } },
    { 0x804e5f0, { 0x80, 0x3d, 0xd6, 0xdb, 0xe3, 0x09, 0x00, 0x74, 0x17, 0x80, 0x3d, 0xce, 0xdb, 0xe3, 0x09, 0x01, 0x74, 0x0e, 0x8b, 0x54, 0x24, 0x04, 0x8b, 0x42, 0x34, 0xc3, 0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00 } },
    { 0, { 0 } },
};

// NX2
static const struct fpa_site fpa_nx2_sites[] = {
    { 0x805451f, 0, FPA_ATTRACT }, { 0x80620ec, 0, FPA_ATTRACT }, { 0x80910c6, 0, FPA_ATTRACT },
    { 0x8078b2b, 1, FPA_ATTRACT }, { 0x80a9c18, 1, FPA_ATTRACT }, { 0x80a9ec7, 1, FPA_ATTRACT }, { 0x80aa13e, 1, FPA_ATTRACT }, { 0x80aa297, 1, FPA_ATTRACT },
    { 0x80a8478, 0, FPA_START }, { 0x80a9320, 0, FPA_START }, { 0x80aa178, 0, FPA_START }, { 0x80ac170, 0, FPA_START },
};
static const struct fpa_getter fpa_nx2_getters[3] = {
    { 0x804dee0, { 0x0f, 0xb6, 0x05, 0xf5, 0x01, 0xe3, 0x09, 0x84, 0xc0, 0x74, 0x19, 0x80, 0x3d, 0xed, 0x01, 0xe3, 0x09, 0x01, 0x74, 0x10, 0x0f, 0xb6, 0xd0, 0x8b, 0x44, 0x24, 0x04, 0x89, 0xd1, 0x8b, 0x40, 0x34 } },
    { 0x804df10, { 0x80, 0x3d, 0xf5, 0x01, 0xe3, 0x09, 0x00, 0x74, 0x17, 0x80, 0x3d, 0xed, 0x01, 0xe3, 0x09, 0x01, 0x74, 0x0e, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x40, 0x34, 0xc3, 0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00 } },
    { 0, { 0 } },
};

// NX2 CN
static const struct fpa_site fpa_nx2cn_sites[] = {
    { 0x80544ef, 0, FPA_ATTRACT }, { 0x80620fc, 0, FPA_ATTRACT }, { 0x8091056, 0, FPA_ATTRACT },
    { 0x8078adb, 1, FPA_ATTRACT }, { 0x80a9638, 1, FPA_ATTRACT }, { 0x80a98e7, 1, FPA_ATTRACT }, { 0x80a9b5e, 1, FPA_ATTRACT }, { 0x80a9cb7, 1, FPA_ATTRACT },
    { 0x80a7e98, 0, FPA_START }, { 0x80a8d40, 0, FPA_START }, { 0x80a9b98, 0, FPA_START }, { 0x80abb90, 0, FPA_START },
};
static const struct fpa_getter fpa_nx2cn_getters[3] = {
    { 0x804deb0, { 0x0f, 0xb6, 0x05, 0x95, 0xad, 0xe2, 0x09, 0x84, 0xc0, 0x74, 0x19, 0x80, 0x3d, 0x8d, 0xad, 0xe2, 0x09, 0x01, 0x74, 0x10, 0x0f, 0xb6, 0xd0, 0x8b, 0x44, 0x24, 0x04, 0x89, 0xd1, 0x8b, 0x40, 0x34 } },
    { 0x804dee0, { 0x80, 0x3d, 0x95, 0xad, 0xe2, 0x09, 0x00, 0x74, 0x17, 0x80, 0x3d, 0x8d, 0xad, 0xe2, 0x09, 0x01, 0x74, 0x0e, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x40, 0x34, 0xc3, 0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00 } },
    { 0, { 0 } },
};

// NXA
static const struct fpa_site fpa_nxa_sites[] = {
    { 0x805a0ef, 0, FPA_ATTRACT }, { 0x806801c, 0, FPA_ATTRACT }, { 0x80b76b6, 0, FPA_ATTRACT },
    { 0x808ed34, 2, FPA_ATTRACT }, { 0x80db538, 2, FPA_ATTRACT }, { 0x80db7e7, 2, FPA_ATTRACT }, { 0x80dba5e, 2, FPA_ATTRACT }, { 0x80dbbb7, 2, FPA_ATTRACT },
    { 0x80c0afd, 0, FPA_START }, { 0x80d9898, 0, FPA_START }, { 0x80dac20, 0, FPA_START }, { 0x80dba98, 0, FPA_START }, { 0x80df2d0, 0, FPA_START },
};
static const struct fpa_getter fpa_nxa_getters[3] = {
    { 0x80550b0, { 0x80, 0x3d, 0xbf, 0xd1, 0xe7, 0x09, 0x00, 0x74, 0x17, 0x80, 0x3d, 0xb7, 0xd1, 0xe7, 0x09, 0x01, 0x74, 0x0e, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x40, 0x38, 0xc3, 0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00 } },
    { 0, { 0 } },
    { 0x80556c0, { 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x50, 0x34, 0x85, 0xd2, 0x75, 0x15, 0x80, 0x3d, 0xbf, 0xd1, 0xe7, 0x09, 0x00, 0x75, 0x12, 0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00, 0x8d, 0xbf, 0x00, 0x00, 0x00, 0x00 } },
};

// NXA CN
static const struct fpa_site fpa_nxacn_sites[] = {
    { 0x805a2df, 0, FPA_ATTRACT }, { 0x806836c, 0, FPA_ATTRACT }, { 0x80b8646, 0, FPA_ATTRACT },
    { 0x808f67e, 2, FPA_ATTRACT }, { 0x80df01c, 2, FPA_ATTRACT }, { 0x80df337, 2, FPA_ATTRACT }, { 0x80df59e, 2, FPA_ATTRACT }, { 0x80df7da, 2, FPA_ATTRACT },
    { 0x80c1f5d, 0, FPA_START }, { 0x80dc7ff, 0, FPA_START }, { 0x80de6a0, 0, FPA_START }, { 0x80df5d8, 0, FPA_START }, { 0x80e2f28, 0, FPA_START },
};
static const struct fpa_getter fpa_nxacn_getters[3] = {
    { 0x8055130, { 0x80, 0x3d, 0x3f, 0x13, 0xe8, 0x09, 0x00, 0x74, 0x17, 0x80, 0x3d, 0x37, 0x13, 0xe8, 0x09, 0x01, 0x74, 0x0e, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x40, 0x38, 0xc3, 0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00 } },
    { 0, { 0 } },
    { 0x8055820, { 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x50, 0x34, 0x85, 0xd2, 0x75, 0x15, 0x80, 0x3d, 0x3f, 0x13, 0xe8, 0x09, 0x00, 0x75, 0x12, 0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00, 0x8d, 0xbf, 0x00, 0x00, 0x00, 0x00 } },
};

// Fiesta 1.20 (getters: 0 = credits, returns 99 in free play; 1 = any coins, bool)
// ATTRACT: the title timeout (d40e6) and its credit test that resets the timer
// (d3f98), the fade of the title (d2c29), the title voice at its start
// (d27e4), the logo / movie / high score scenes (808569d, 80aaf83, 80abf85),
// the random intro pick (805b439) and the scene sequencer (8068a3c).
// START: the centre press that joins (d29ef, d2a0c, d2084, d21dd) and the
// song select joins (acf10 = UseCredit path, c53b0).
// Left alone: d3e2d and d29a2 (they play the coin jingle), d3e5b / d412c /
// d433e / d4372 (what the title draws, which lamps blink, the voice).
static const struct fpa_site fpa_fiesta_sites[] = {
    { 0x80d40e6, 1, FPA_ATTRACT }, { 0x80d2c29, 1, FPA_ATTRACT }, { 0x80d27e4, 1, FPA_ATTRACT },
    { 0x808569d, 1, FPA_ATTRACT }, { 0x80aaf83, 1, FPA_ATTRACT }, { 0x80abf85, 1, FPA_ATTRACT },
    { 0x80d3f98, 0, FPA_ATTRACT }, { 0x805b439, 0, FPA_ATTRACT }, { 0x8068a3c, 0, FPA_ATTRACT },
    { 0x80d29ef, 0, FPA_START }, { 0x80d2a0c, 0, FPA_START }, { 0x80d2084, 0, FPA_START }, { 0x80d21dd, 0, FPA_START },
    { 0x80acf10, 0, FPA_START }, { 0x80c53b0, 0, FPA_START },
};
static const struct fpa_getter fpa_fiesta_getters[3] = {
    { 0x8055424, { 0x80, 0x3d, 0x3f, 0x4d, 0xe5, 0x09, 0x00, 0x74, 0x11, 0x80, 0x3d, 0x36, 0x4d, 0xe5, 0x09, 0x01, 0x74, 0x08, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x40, 0x34, 0xc3, 0xb8, 0x63, 0x00, 0x00, 0x00, 0xc3 } },
    { 0x8055444, { 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x50, 0x38, 0x85, 0xd2, 0x75, 0x1b, 0x80, 0x3d, 0x3f, 0x4d, 0xe5, 0x09, 0x00, 0x74, 0x12, 0x80, 0x3d, 0x36, 0x4d, 0xe5, 0x09, 0x01, 0x74, 0x09, 0x8b, 0x40, 0x34 } },
    { 0, { 0 } },
};

// Fiesta EX 1.21 (same code shape as Fiesta; found by matching the call
// sites' neighbourhoods and reading the ones that differ)
static const struct fpa_site fpa_fex_sites[] = {
    { 0x80df5d1, 1, FPA_ATTRACT }, { 0x80ddd99, 1, FPA_ATTRACT }, { 0x80dd8d4, 1, FPA_ATTRACT },
    { 0x8087b62, 1, FPA_ATTRACT }, { 0x80ad995, 1, FPA_ATTRACT }, { 0x80aee95, 1, FPA_ATTRACT },
    { 0x80df30d, 0, FPA_ATTRACT }, { 0x805bbd9, 0, FPA_ATTRACT }, { 0x806a1e0, 0, FPA_ATTRACT }, { 0x806a202, 0, FPA_ATTRACT },
    { 0x80ddb5f, 0, FPA_START }, { 0x80ddb7c, 0, FPA_START }, { 0x80dd174, 0, FPA_START }, { 0x80dd2cd, 0, FPA_START },
    { 0x80b3046, 0, FPA_START }, { 0x80bdac6, 0, FPA_START },
};
static const struct fpa_getter fpa_fex_getters[3] = {
    { 0x8055984, { 0x80, 0x3d, 0x7f, 0xe8, 0xe6, 0x09, 0x00, 0x74, 0x14, 0x80, 0x3d, 0x76, 0xe8, 0xe6, 0x09, 0x01, 0x74, 0x0b, 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x80, 0xe4, 0x01, 0x00, 0x00, 0xc3, 0xb8, 0x63, 0x00 } },
    { 0x80559a8, { 0x8b, 0x44, 0x24, 0x04, 0x8b, 0x90, 0xe8, 0x01, 0x00, 0x00, 0x85, 0xd2, 0x75, 0x1e, 0x80, 0x3d, 0x7f, 0xe8, 0xe6, 0x09, 0x00, 0x74, 0x15, 0x80, 0x3d, 0x76, 0xe8, 0xe6, 0x09, 0x01, 0x74, 0x0c } },
    { 0, { 0 } },
};

const struct fpa_build fpa_builds[] = {
    { "Zero", fpa_zero_getters, fpa_zero_sites, (int)(sizeof(fpa_zero_sites) / sizeof(fpa_zero_sites[0])),
      0x34, 0xede, 0xed6, 0x8117028, 0x4 },
    { "NX (NX1)", fpa_nx1_getters, fpa_nx1_sites, (int)(sizeof(fpa_nx1_sites) / sizeof(fpa_nx1_sites[0])),
      0x0, 0x9e3dbd6, 0x9e3dbce, 0x81f8904, 0x0 },
    { "NX2", fpa_nx2_getters, fpa_nx2_sites, (int)(sizeof(fpa_nx2_sites) / sizeof(fpa_nx2_sites[0])),
      0x0, 0x9e301f5, 0x9e301ed, 0x81c60c4, 0x0 },
    { "NX2 CN", fpa_nx2cn_getters, fpa_nx2cn_sites, (int)(sizeof(fpa_nx2cn_sites) / sizeof(fpa_nx2cn_sites[0])),
      0x0, 0x9e2ad95, 0x9e2ad8d, 0x81c5f44, 0x0 },
    { "NXA", fpa_nxa_getters, fpa_nxa_sites, (int)(sizeof(fpa_nxa_sites) / sizeof(fpa_nxa_sites[0])),
      0x0, 0x9e7d1bf, 0x9e7d1b7, 0x81edd04, 0x0 },
    { "NXA CN", fpa_nxacn_getters, fpa_nxacn_sites, (int)(sizeof(fpa_nxacn_sites) / sizeof(fpa_nxacn_sites[0])),
      0x0, 0x9e8133f, 0x9e81337, 0x81f1e84, 0x0 },
    { "Fiesta", fpa_fiesta_getters, fpa_fiesta_sites, (int)(sizeof(fpa_fiesta_sites) / sizeof(fpa_fiesta_sites[0])),
      0x0, 0x9e54d3f, 0x9e54d36, 0x81e9284, 0x0 },
    { "Fiesta EX", fpa_fex_getters, fpa_fex_sites, (int)(sizeof(fpa_fex_sites) / sizeof(fpa_fex_sites[0])),
      0x0, 0x9e6e87f, 0x9e6e876, 0x81f4924, 0x0 },
};
const int fpa_nbuilds = (int)(sizeof(fpa_builds) / sizeof(fpa_builds[0]));

// ---- The inline family: Fiesta 2 1.61, Prime 1.22, Prime JE 1.17 ---------------
// These builds have no getter: every "are there coins?" test is inlined as
//     [cmpl $0,<credits>; jne yes]  cmpb $0,<cpc>; je yes;
//     cmpb $1,<event>; je yes;  <coins> != 0 -> yes
// (the `cmpb $0x0,<cpc>` is `80 3d <addr32> 00`, the event one `80 3d <addr32> 01`;
// the cpc one is sometimes laid out as jne to a block that holds the event one).
// The sites were found by reading the executables (exes_1_1/f2_161, the
// pki/prime/piu and exes_1_1/JE_1_17_0/piu-PRIMEJE117 images, plain ELF, no
// packer) and sorted by what the code does after the test. One unit = the
// cpc test plus the event test that follows it:
//   FPA_ATTRACT  the title's ~52 s timeout (cmp $0xc4d, which is skipped while
//                "coins" are present: also the reset of the title timer), the
//                three attract scenes that leave for the title (logo, movies,
//                high scores: a call that asks for the next scene with eax=1),
//                the next-scene sequencer (picks 1 or 4/5 by the same test),
//                the random pick of the intro (two rand() calls)
//   FPA_START    the title's join: a press with "coins" (free play) joins P1/P2
//                (cmp $0x3c on the title timer, then the joined flags are set)
// Not patched, on purpose: the coin-inserted handlers (they set the 300 tick
// timer and play the coin sound), the coin jingle of the title (a call with
// the 0x24 flag), the lamp blink and the voice of the title, the title's
// credit display, the single event tests (timers, blink speed), the
// song-select / in-game tests, the mode-switch in the Lua glue (strcasecmp).
#define FPA_I(cpc, ev, kind) { (cpc), 0, (kind) }, { (ev), 1, (kind) }

// Fiesta 2 1.61: B 8090d61/8091740 (title timeout), H 809b7dd/809b7f9
// (sequencer), I 809c203/809c3b8, J 809d0c3/809d0e0, M 80c58b6/80c67e9 (the three
// scenes), O 80d6c40/80d70ad (rand pick); C 80911e0/80914b7 (join)
static const struct fpa_isite fpa_f2_sites[] = {
    FPA_I(0x8090d61, 0x8091740, FPA_ATTRACT), FPA_I(0x809b7dd, 0x809b7f9, FPA_ATTRACT),
    FPA_I(0x809c203, 0x809c3b8, FPA_ATTRACT), FPA_I(0x809d0c3, 0x809d0e0, FPA_ATTRACT),
    FPA_I(0x80c58b6, 0x80c67e9, FPA_ATTRACT), FPA_I(0x80d6c40, 0x80d70ad, FPA_ATTRACT),
    FPA_I(0x80911e0, 0x80914b7, FPA_START),
};
// Prime 1.22: B 80b71a5/80b7fa4, H 80a97ed/80a9809, scenes 80abcb9/80af1dd,
// 80d6f8a/80d7128, 80d9742/80d9760, rand 80c7c4f/80c835b; join 80b77a8/80b7844
static const struct fpa_isite fpa_prime_sites[] = {
    FPA_I(0x80b71a5, 0x80b7fa4, FPA_ATTRACT), FPA_I(0x80a97ed, 0x80a9809, FPA_ATTRACT),
    FPA_I(0x80abcb9, 0x80af1dd, FPA_ATTRACT), FPA_I(0x80d6f8a, 0x80d7128, FPA_ATTRACT),
    FPA_I(0x80d9742, 0x80d9760, FPA_ATTRACT), FPA_I(0x80c7c4f, 0x80c835b, FPA_ATTRACT),
    FPA_I(0x80b77a8, 0x80b7844, FPA_START),
};
// Prime JE 1.17: B 809d361/809dd60, H 80d285d/80d2879, scenes 80b34f4/80b36a0,
// 80d662d/80d9b72, 80e5b72/80e5b90, rand 80af897/80b001a; join 809d550/809d6d8
static const struct fpa_isite fpa_je_sites[] = {
    FPA_I(0x809d361, 0x809dd60, FPA_ATTRACT), FPA_I(0x80d285d, 0x80d2879, FPA_ATTRACT),
    FPA_I(0x80b34f4, 0x80b36a0, FPA_ATTRACT), FPA_I(0x80d662d, 0x80d9b72, FPA_ATTRACT),
    FPA_I(0x80e5b72, 0x80e5b90, FPA_ATTRACT), FPA_I(0x80af897, 0x80b001a, FPA_ATTRACT),
    FPA_I(0x809d550, 0x809d6d8, FPA_START),
};

const struct fpa_ibuild fpa_ibuilds[] = {
    { "Fiesta 2", fpa_f2_sites, (int)(sizeof(fpa_f2_sites) / sizeof(fpa_f2_sites[0])),
      0x93fc200, 0x93fc1f6, 0x9d846e4, 0x8090d61, { 0x80, 0x3d, 0x00, 0xc2, 0x3f, 0x09, 0x00, 0x0f, 0x85, 0xd2, 0x09, 0x00, 0x00, 0x8b, 0xac, 0x24, 0x90, 0x00, 0x00, 0x00, 0x8b, 0x55, 0x30, 0x8b, 0x4d, 0x34, 0x85, 0xd2, 0x0f, 0x84, 0x45, 0x06 } },
    { "Prime", fpa_prime_sites, (int)(sizeof(fpa_prime_sites) / sizeof(fpa_prime_sites[0])),
      0xab15782, 0xab15779, 0xe35ce04, 0x80b71a5, { 0x80, 0x3d, 0x82, 0x57, 0xb1, 0x0a, 0x00, 0x0f, 0x85, 0xf2, 0x0d, 0x00, 0x00, 0xc7, 0x04, 0x24, 0x00, 0x91, 0x1f, 0x08, 0xe8, 0x32, 0xa7, 0xf9, 0xff, 0x80, 0x0d, 0x24, 0x3f, 0x33, 0x0e, 0x40 } },
    { "Prime JE", fpa_je_sites, (int)(sizeof(fpa_je_sites) / sizeof(fpa_je_sites[0])),
      0xab16c42, 0xab16c39, 0xe35e4c4, 0x809d361, { 0x80, 0x3d, 0x42, 0x6c, 0xb1, 0x0a, 0x00, 0x0f, 0x85, 0xf2, 0x09, 0x00, 0x00, 0xc7, 0x04, 0x24, 0xc0, 0x9c, 0x1f, 0x08, 0xe8, 0x96, 0x45, 0xfb, 0xff, 0x80, 0x0d, 0xe4, 0x53, 0x33, 0x0e, 0x40 } },
};
const int fpa_nibuilds = (int)(sizeof(fpa_ibuilds) / sizeof(fpa_ibuilds[0]));

int fpa_ibuild_match(const struct fpa_ibuild* b, fpa_read_fn rd, void* ctx)
{
    unsigned char have[FPA_SIG_LEN];
    int i;
    if(rd(b->anchor, have, FPA_SIG_LEN, ctx) != 0 || memcmp(have, b->sig, FPA_SIG_LEN) != 0) return 0;
    for(i = 0; i < b->nsites; i++) {
        const struct fpa_isite* s = &b->sites[i];
        unsigned char c[7];
        uint32_t a;
        if(rd(s->at, c, 7, ctx) != 0 || c[0] != 0x80 || c[1] != 0x3d) return 0;
        memcpy(&a, c + 2, 4);
        if(s->which == 0) { if(a != (uint32_t)b->cpc || c[6] != 0x00) return 0; }
        else { if(a != (uint32_t)b->event || c[6] != 0x01) return 0; }
    }
    return 1;
}

int fpa_build_match(const struct fpa_build* b, fpa_read_fn rd, void* ctx)
{
    unsigned char have[FPA_SIG_LEN];
    int i;
    for(i = 0; i < 3; i++) {
        if(b->getters[i].at == 0) continue;
        if(rd(b->getters[i].at, have, FPA_SIG_LEN, ctx) != 0) return 0;
        if(memcmp(have, b->getters[i].sig, FPA_SIG_LEN) != 0) return 0;
    }
    for(i = 0; i < b->nsites; i++) {
        const struct fpa_site* s = &b->sites[i];
        unsigned char c[5];
        uint32_t rel;
        if(s->getter > 2 || b->getters[s->getter].at == 0) return 0;
        if(rd(s->at, c, 5, ctx) != 0 || c[0] != 0xe8) return 0;
        memcpy(&rel, c + 1, 4);
        if((uint32_t)(s->at + 5 + rel) != (uint32_t)b->getters[s->getter].at) return 0;
    }
    return 1;
}

#ifndef FPA_LOGIC_ONLY
// ---- Memory of the game, without faulting ------------------------------------
// The same idea as credits.c (the kernel answers EFAULT instead of a signal:
// a packed game is not unpacked yet when the shim starts), kept local so that
// this file does not depend on that one.
static int fpa_mem_read(uintptr_t addr, void* buf, size_t n, void* ctx)
{
    static int use_proc = 0;
    struct iovec l = { buf, n }, r = { (void*)addr, n };
    (void)ctx;
    if(!use_proc) {
        long k = syscall(SYS_process_vm_readv, getpid(), &l, 1, &r, 1, 0);
        if(k == (long)n) return 0;
        if(k >= 0 || errno != ENOSYS) return -1;
        use_proc = 1;   // a kernel built without it: /proc/self/mem
    }
    {
        int fd = open("/proc/self/mem", O_RDONLY);
        ssize_t k;
        if(fd < 0) return -1;
        k = pread64(fd, buf, n, (off64_t)(uint32_t)addr);
        close(fd);
        return k == (ssize_t)n ? 0 : -1;
    }
}

// ---- State ----------------------------------------------------------------------
static const struct fpa_build* fpa_b = NULL;        // set once the game is patched
static volatile struct fpa_latch fpa_l;
static int (*fpa_orig[3])(void*);
static long fpa_start_ms = 0, fpa_tried_ms = 0;
static int fpa_gave_up = 0;

static long fpa_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

// The game's own memory, read by the hook and the poll: only after the
// build matched, so these addresses are the ones the game reads itself.
static int fpa_free_play(const struct fpa_build* b, void* mgr)
{
    const volatile unsigned char *cpc, *ev;
    if(b->setup_off != 0) {
        const unsigned char* setup = *(const unsigned char* const*)((const char*)mgr + b->setup_off);
        if(setup == NULL) return 0;
        cpc = setup + b->cpc;
        ev = setup + b->event;
    } else {
        cpc = (const volatile unsigned char*)b->cpc;
        ev = (const volatile unsigned char*)b->event;
    }
    return *cpc == 0 || *ev == 1;
}

static int fpa_joined_at(uintptr_t flags, uintptr_t deref)
{
    const unsigned char* p = (const unsigned char*)flags;
    if(deref != 0) {
        p = *(const unsigned char* const*)flags;
        if(p == NULL) return 0;
        p += deref;
    }
    return (*(const volatile uint32_t*)p & 3) != 0;
}

static int fpa_joined(const struct fpa_build* b)
{
    return fpa_joined_at(b->flags, b->flags_deref);
}

// ---- The inline family: the shadow bytes ----------------------------------------
static const struct fpa_ibuild* fpa_ib = NULL;       // set once the game is patched
static volatile unsigned char fpa_shadow[4];

static void fpa_shadow_update(const struct fpa_ibuild* b)
{
    struct fpa_latch l;
    unsigned char out[4];
    int i;
    l.armed = fpa_l.armed;
    l.released = fpa_l.released;
    fpa_shadow_calc(&l, fpa_joined_at(b->flags, 0), *(const volatile unsigned char*)b->cpc,
                    *(const volatile unsigned char*)b->event, out);
    for(i = 0; i < 4; i++) fpa_shadow[i] = out[i];
}

static int fpa_hook(int g, int kind, void* mgr)
{
    int r = fpa_orig[g](mgr);
    const struct fpa_build* b = fpa_b;
    struct fpa_latch l;
    if(b == NULL) return r;
    l.armed = fpa_l.armed;
    l.released = fpa_l.released;
    return fpa_answer(&l, kind, fpa_free_play(b, mgr), fpa_joined(b), r);
}

// The game calls with the stack as its own compiler left it: realign.
#define FPA_HOOKS(g) \
    __attribute__((force_align_arg_pointer)) static int fpa_hook_a##g(void* mgr) { return fpa_hook(g, FPA_ATTRACT, mgr); } \
    __attribute__((force_align_arg_pointer)) static int fpa_hook_s##g(void* mgr) { return fpa_hook(g, FPA_START, mgr); }
FPA_HOOKS(0)
FPA_HOOKS(1)
FPA_HOOKS(2)

static int (*const fpa_hooks[2][3])(void*) = {
    { fpa_hook_a0, fpa_hook_a1, fpa_hook_a2 },
    { fpa_hook_s0, fpa_hook_s1, fpa_hook_s2 },
};

static int fpa_patch(const struct fpa_build* b)
{
    int i;
    for(i = 0; i < 3; i++) fpa_orig[i] = (int (*)(void*))b->getters[i].at;
    for(i = 0; i < b->nsites; i++) {
        const struct fpa_site* s = &b->sites[i];
        uintptr_t page = s->at & ~(uintptr_t)0xfff;
        size_t len = ((s->at + 5 + 0xfff) & ~(uintptr_t)0xfff) - page;   // the page(s) of these 5 bytes only
        uint32_t rel = (uint32_t)((uintptr_t)fpa_hooks[s->kind][s->getter] - (s->at + 5));
        if(mprotect((void*)page, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return -1;
        memcpy((void*)(s->at + 1), &rel, 4);
        mprotect((void*)page, len, PROT_READ | PROT_EXEC);   // the game's text
    }
    fpa_b = b;
    PRINTF("freeplay_attract: %s: %d sites\n", b->name, b->nsites);
    return 0;
}

// The page(s) of n bytes at addr: writable for one memcpy, back to the game's text.
static int fpa_poke(uintptr_t addr, const void* src, size_t n)
{
    uintptr_t page = addr & ~(uintptr_t)0xfff;
    size_t len = ((addr + n + 0xfff) & ~(uintptr_t)0xfff) - page;
    if(mprotect((void*)page, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return -1;
    memcpy((void*)addr, src, n);
    mprotect((void*)page, len, PROT_READ | PROT_EXEC);
    return 0;
}

static int fpa_ipatch(const struct fpa_ibuild* b)
{
    int i;
    fpa_shadow_update(b);   // right before the game can read them
    for(i = 0; i < b->nsites; i++) {
        const struct fpa_isite* s = &b->sites[i];
        uint32_t a = (uint32_t)(uintptr_t)&fpa_shadow[s->kind * 2 + s->which];
        if(fpa_poke(s->at + 2, &a, 4) != 0) return -1;
    }
    fpa_ib = b;
    PRINTF("freeplay_attract: %s: %d inline sites\n", b->name, b->nsites);
    return 0;
}

static void fpa_find(void)
{
    int i;
    for(i = 0; i < fpa_nibuilds; i++) {
        if(!fpa_ibuild_match(&fpa_ibuilds[i], fpa_mem_read, NULL)) continue;
        if(fpa_ipatch(&fpa_ibuilds[i]) != 0) {
            PRINTF("freeplay_attract: %s: mprotect failed\n", fpa_ibuilds[i].name);
        }
        return;
    }
    for(i = 0; i < fpa_nbuilds; i++) {
        if(!fpa_build_match(&fpa_builds[i], fpa_mem_read, NULL)) continue;
        if(fpa_patch(&fpa_builds[i]) != 0) {
            PRINTF("freeplay_attract: %s: mprotect failed\n", fpa_builds[i].name);
        }
        return;
    }
}

void freeplay_attract_poll(int is_menu)
{
    long now;
    if(is_menu) return;
    if(fpa_b == NULL && fpa_ib == NULL) {
        if(fpa_gave_up) return;
        // The game may not be in memory yet (packed, still loading): look
        // twice a second for the first three minutes, then leave it.
        now = fpa_now_ms();
        if(fpa_start_ms == 0) {
            fpa_start_ms = now;
            if(getenv("_PIU_NO_FP_ATTRACT") != NULL) { fpa_gave_up = 1; return; }
        }
        if(now - fpa_start_ms > 180000) { fpa_gave_up = 1; return; }
        if(now - fpa_tried_ms < 500) return;
        fpa_tried_ms = now;
        fpa_find();
        if(fpa_b == NULL && fpa_ib == NULL) return;
    }
    {
        // a centre step on any pad, or a green front button (as credits.c)
        int centre = (~(bytes_f[0] & bytes_f[1] & bytes_f[2] & bytes_f[3] &
                        bytes_f[4] & bytes_f[5] & bytes_f[6] & bytes_f[7]) & 0x04) |
                     (~bytes_fb[0] & 0x88);
        struct fpa_latch l;
        l.armed = fpa_l.armed;
        l.released = fpa_l.released;
        fpa_latch_step(&l, fpa_b ? fpa_joined(fpa_b) : fpa_joined_at(fpa_ib->flags, 0), centre != 0);
        fpa_l.armed = l.armed;
        fpa_l.released = l.released;
        if(fpa_ib) fpa_shadow_update(fpa_ib);
    }
}
#endif
