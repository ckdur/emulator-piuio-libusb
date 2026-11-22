#ifndef _CREDITS_H
#define _CREDITS_H

// Global credits and free play with attract: where a game keeps its credits.
// See credits_logic.h for what is done with them and credits.c for the table
// of the games that are not our own sources.

#include <stdint.h>

#ifndef API_EXPORTED
#define API_EXPORTED __attribute__((visibility("default")))
#endif

#define CREDITS_CONSTRUCT_VERSION 1

struct credit_construct {
    int version;             // CREDITS_CONSTRUCT_VERSION
    const char* name;        // for the log only
    // Coin counter, a 32 bit int: at coins_off, or at *coins_base + coins_off
    // (an object the game allocates).
    void** coins_base;
    uintptr_t coins_off;
    // Settings: at the addresses below, or at *set_base + offset.
    void** set_base;
    uintptr_t cpc_off;       // coins per credit; 0 in the game = free play
    int cpc_size;            // 1 or 4 bytes
    uintptr_t free_off;      // byte, 1 = free play / event mode. 0: the game has none
    uintptr_t limit_off;     // byte, the operator's credit limit (0 = no limit). 0: none
    int max_credits;         // cap in the game's coin routine, whole credits. 0: none known
    // Table entries (credits.c): used only when guard_len bytes at guard are
    // guard_bytes, so a build that merely looks alike is never written to.
    const unsigned char* guard;
    const unsigned char* guard_bytes;
    int guard_len;
};

// A game built from our own sources (mk3_linux_ports) tells where its
// credits are, the way report_autoplay does for autoplay.
void API_EXPORTED report_credits(const struct credit_construct* con);

// What the menu shows: whole credits in the pool and coins waiting in it.
// Returns 0 when global credits are switched off.
int API_EXPORTED libusb_piuio_credits(int* credits, int* coins);

// Every read of a game: the two system bytes (coin 1 in sys1, coin 2 in
// sys2, active low) before the game gets them. A coin line that is down when
// the game starts reading is held up until it was seen up (IMPER-116).
// (In the header: piuio_emu.c is its only user, and a test that links
// piuio_emu.c without credits.c needs no stand-in for it.)
#include "credits_logic.h"
static inline void credits_coin_guard(unsigned char* sys1, unsigned char* sys2)
{
#ifdef PIUIO_EMU_NO_POLL_THREAD
    // tests/test_piuio_input.c compares what a game reads byte for byte from
    // its first read on; the rule itself is checked in tests/test_credits.c
    (void)sys1; (void)sys2;
#else
    static struct credits_guard guard[2];
    if(!credits_guard_step(&guard[0], !(*sys1 & 0x04))) *sys1 |= 0x04;
    if(!credits_guard_step(&guard[1], !(*sys2 & 0x04))) *sys2 |= 0x04;
#endif
}

// The shim's poll thread calls this on every round. is_menu: the process
// reported itself with libusb_piuio_report_menu().
void credits_poll(int is_menu);
// Before the process ends (TEST hold, libusb_exit): the pool file is current.
void credits_flush(void);

#endif
