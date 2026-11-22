#ifndef _FREEPLAY_ATTRACT_H
#define _FREEPLAY_ATTRACT_H

// Free play with the attract sequence for the older games (IMPER-119).
//
// The problem. In free play (coins per credit 0) or event mode the game's
// "any coins?" getters answer 99, so the title never times out into the
// attract sequence and every attract scene jumps back to the title at once.
// The target is what XX, Phoenix and Prime 2 do: the attract (logo, movies,
// demo play) runs until a centre step is pressed, then the title comes up and
// stays, and the next centre step starts the game. The game stays in its own
// free play (it draws FREE PLAY, it starts without a credit).
//
// The way. Nothing on disk changes. When the game is running the shim looks
// for the build in fpa_builds[] (every byte it relies on must match, else
// nothing is touched) and rewrites the rel32 of the listed `call getter`
// sites in memory, so they call a hook of ours with the getter's own
// signature (int f(void* mgr), cdecl). The hook gives the getter's answer
// back, except in free play / event mode with no player joined, where
//   FPA_ATTRACT site (title timeout, scenes, ...): 0 until the latch is armed
//   FPA_START site (a press of the centre panel joins): 0 until the latch is
//                armed AND the panel was released once since
// so the first centre step of the attract only wakes the title, and the
// next one starts. The latch (struct fpa_latch) is armed by a centre step
// seen by the poll thread, and cleared when a player has joined: the next
// title starts with the attract again. Nothing changes when the game is not
// in free play / event mode (the hooks return the game's own answer).
//
// ---- Table format (to add a build: one struct fpa_build in
// freeplay_attract.c, nothing else) -------------------------------------------
//   getters[3]  up to three getter functions of the build: address and the
//               first FPA_SIG_LEN bytes of the function (the build's
//               signature; absolute addresses inside it make it unique).
//               Getter index 0..2 is only a name: sites say which they call.
//               Their original code stays; a hook calls it, then decides.
//               An unused slot has address 0.
//   sites[]     { address of the E8 opcode of `call getter`, getter index,
//                 FPA_ATTRACT | FPA_START }. Each is checked to be
//               E8 + a rel32 that reaches getters[index].at, before anything
//               is written; one mismatch and the build is not patched.
//   free play   the game is in free play when the coins-per-credit byte is 0
//               or the event byte is 1. Either both are absolute addresses
//               (setup_off 0), or offsets in the setup object the manager
//               points to: *(mgr + setup_off) + offset.
//   flags       address of the player-joined dword (bits 0 and 1, one per
//               player); flags_deref != 0: that address holds a pointer to
//               an object and the dword is at +flags_deref.
// The "inline" family (Fiesta 2, Prime, Prime JE: `cmpb $0,<cpc>` inside the
// game's own code, no getter to call) has its own table, struct fpa_ibuild
// (the shadow byte variant, below).
//
// Tested without a game: tests/test_freeplay_attract.c (the latch, the answer,
// and that every table entry matches the real executable).

#include <stddef.h>
#include <stdint.h>

#define FPA_SIG_LEN 32
#define FPA_ATTRACT 0
#define FPA_START 1

struct fpa_site {
    uintptr_t at;           // the E8 opcode of the call
    unsigned char getter;   // index in fpa_build.getters
    unsigned char kind;     // FPA_ATTRACT | FPA_START
};

struct fpa_getter {
    uintptr_t at;
    unsigned char sig[FPA_SIG_LEN];
};

struct fpa_build {
    const char* name;
    const struct fpa_getter* getters;   // [3]
    const struct fpa_site* sites;
    int nsites;
    uintptr_t setup_off;     // 0: cpc and event are addresses
    uintptr_t cpc;           // coins per credit (byte): 0 = free play
    uintptr_t event;         // event mode (byte): 1 = free
    uintptr_t flags;
    uintptr_t flags_deref;
};

// The inline family. The game tests free play with
//   cmpb $0x0,<cpc>   (80 3d <addr32> 00)   and   cmpb $0x1,<event>   (80 3d <addr32> 01)
// at ~20 places. The shim rewrites the <addr32> of the sites it classified
// to the address of one of four shadow bytes it owns (fpa_shadow[4] in freeplay_attract.c):
//   [0] cpc and [1] event for the FPA_ATTRACT sites, [2] and [3] for the FPA_START ones.
// A shadow holds the real byte, except in free play (cpc 0 or event 1) with
// no player joined: attract: cpc nonzero and event 0 ("coins absent") until
// the latch is armed; start: the same until armed and released. Nothing
// else of the game is touched, so the game stays in its own free play (the
// title draws FREE PLAY) and the sites that were not classified read the
// real bytes.
struct fpa_isite {
    uintptr_t at;           // the 0x80 opcode of the cmpb
    unsigned char which;    // 0: the cpc test, 1: the event test
    unsigned char kind;     // FPA_ATTRACT | FPA_START
};

struct fpa_ibuild {
    const char* name;
    const struct fpa_isite* sites;
    int nsites;
    uintptr_t cpc, event;   // the game's bytes
    uintptr_t flags;        // player-joined dword (bits 0, 1)
    uintptr_t anchor;       // FPA_SIG_LEN bytes of code here, as the build's signature
    unsigned char sig[FPA_SIG_LEN];
};

// ---- The rules (no memory access) -------------------------------------------
struct fpa_latch {
    int armed;      // a centre step woke the title
    int released;   // and the panel was up once since
};

// One poll. joined: a player has joined. centre: a centre step is pressed.
static inline void fpa_latch_step(struct fpa_latch* l, int joined, int centre)
{
    if(joined) {            // the game started: the next title starts with the attract
        l->armed = 0;
        l->released = 0;
    } else if(centre) {
        if(!l->armed) {
            l->armed = 1;
            l->released = 0;
        }
    } else if(l->armed) {
        l->released = 1;
    }
}

// What a hooked call answers: orig is the getter's own answer.
static inline int fpa_answer(const struct fpa_latch* l, int kind, int free_play, int joined, int orig)
{
    if(!free_play || joined) return orig;
    if(kind == FPA_START) return (l->armed && l->released) ? orig : 0;
    return l->armed ? orig : 0;
}

// The four shadow bytes for the inline family (index = kind * 2 + which).
static inline void fpa_shadow_calc(const struct fpa_latch* l, int joined, unsigned char cpc, unsigned char ev,
                                   unsigned char out[4])
{
    int free_play = cpc == 0 || ev == 1;
    int hide_a = free_play && !joined && !l->armed;
    int hide_s = free_play && !joined && !(l->armed && l->released);
    out[0] = hide_a ? (cpc ? cpc : 1) : cpc;
    out[1] = hide_a ? 0 : ev;
    out[2] = hide_s ? (cpc ? cpc : 1) : cpc;
    out[3] = hide_s ? 0 : ev;
}

// ---- The builds -------------------------------------------------------------
typedef int (*fpa_read_fn)(uintptr_t addr, void* buf, size_t n, void* ctx); // 0 = ok
extern const struct fpa_build fpa_builds[];
extern const int fpa_nbuilds;
// 1 when every getter signature and every site of b reads back as in the table.
int fpa_build_match(const struct fpa_build* b, fpa_read_fn rd, void* ctx);

extern const struct fpa_ibuild fpa_ibuilds[];
extern const int fpa_nibuilds;
// 1 when the anchor and every site of b read back as in the table.
int fpa_ibuild_match(const struct fpa_ibuild* b, fpa_read_fn rd, void* ctx);

// The shim's poll thread, every round (piuio_emu.c). is_menu: the menu process.
void freeplay_attract_poll(int is_menu);

#endif
