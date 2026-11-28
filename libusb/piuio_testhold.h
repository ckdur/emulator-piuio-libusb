#ifndef PIUIO_TESTHOLD_H
#define PIUIO_TESTHOLD_H

// TEST held for PIUIO_TESTHOLD_SECONDS ends the process: that is how a
// player leaves a game and gets back to the menu (the menu waits for the game
// and is relaunched by glmenuos.sh).
//
// The menu itself is never ended this way (IMPER-34, owner's decision): it
// reports itself with libusb_piuio_report_menu() and a TEST hold then does
// nothing to it. Restarting or shutting down the machine is done only from
// the menu's Settings entries. Keyboard TEST goes through the same bits
// (keyboards.c), so it follows the same rule.
//
// How it ends is piuio_testhold_quick(): exit() from the shim's poll thread
// runs the game's exit handlers and C++ static destructors while its other
// threads are still running.  PMod crashed that way on every TEST exit
// (IMPER-162: its main thread called into destroyed objects, and so did its
// signal handler, until the signal stack overflowed).  Its run script sets
// _PIUIOEMU_TESTHOLD_QUICK=1: the shim saves its own state and ends the
// process with _exit(), which runs nothing of the game's.
//
// Kept apart from piuio_emu.c, with no other dependency, so that
// tests/test_testhold.c can check it on the host.

#include <stdlib.h>
#include <time.h>

#define PIUIO_TESTHOLD_SECONDS 5

enum piuio_testhold_result {
    PIUIO_TESTHOLD_NONE = 0,   // nothing to do
    PIUIO_TESTHOLD_STARTED,    // TEST just went down: timing starts
    PIUIO_TESTHOLD_MENU,       // held long enough, but this is the menu: ignored
    PIUIO_TESTHOLD_EXIT,       // held long enough: end the process
};

struct piuio_testhold {
    int held;        // TEST has been down since 'since'
    time_t since;
};

// value: _PIUIOEMU_TESTHOLD_QUICK, NULL when unset
static inline int piuio_testhold_quick(const char *value) {
    return value != NULL && atoi(value) != 0;
}

// One poll step. test_down: TEST is pressed now. is_menu: the process has
// called libusb_piuio_report_menu().
static inline enum piuio_testhold_result
piuio_testhold_step(struct piuio_testhold *h, int test_down, time_t now, int is_menu)
{
    if(!test_down) {
        h->held = 0;
        return PIUIO_TESTHOLD_NONE;
    }
    if(!h->held) {
        h->held = 1;
        h->since = now;
        return PIUIO_TESTHOLD_STARTED;
    }
    if(now - h->since < PIUIO_TESTHOLD_SECONDS)
        return PIUIO_TESTHOLD_NONE;
    if(is_menu) {
        // Start over: a hold that goes on keeps doing nothing.
        h->held = 0;
        return PIUIO_TESTHOLD_MENU;
    }
    return PIUIO_TESTHOLD_EXIT;
}

#endif
