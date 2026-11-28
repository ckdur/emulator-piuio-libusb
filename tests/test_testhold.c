// Host test for the TEST-hold rule of the PIUIO shim (libusb/piuio_testhold.h):
// a game ends after TEST is held 5 s, the menu never does (IMPER-34).
//
//   gcc -std=gnu17 -Wall -Wextra -I libusb -o /tmp/test_testhold tests/test_testhold.c && /tmp/test_testhold

#include <stdio.h>
#include "piuio_testhold.h"

static int fails = 0;
static void check(const char *what, int ok) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) fails++;
}

// Hold TEST from t0 for 'secs' seconds, one step every 1/4 s at whole-second
// time() granularity; return the first EXIT/MENU result seen and when.
static enum piuio_testhold_result hold(int is_menu, int secs, int *at, int *menus) {
    struct piuio_testhold h = {0};
    enum piuio_testhold_result first = PIUIO_TESTHOLD_NONE;
    *at = -1;
    *menus = 0;
    for(int q = 0; q <= secs * 4; q++) {
        time_t now = 1000 + q / 4;
        enum piuio_testhold_result r = piuio_testhold_step(&h, 1, now, is_menu);
        if(r == PIUIO_TESTHOLD_MENU) (*menus)++;
        if((r == PIUIO_TESTHOLD_EXIT || r == PIUIO_TESTHOLD_MENU) && first == PIUIO_TESTHOLD_NONE) {
            first = r;
            *at = q / 4;
        }
        if(r == PIUIO_TESTHOLD_EXIT) break;
    }
    return first;
}

int main(void) {
    int at, menus;

    check("game: 4 s hold does nothing", hold(0, 4, &at, &menus) == PIUIO_TESTHOLD_NONE);
    check("game: 5 s hold ends the game", hold(0, 5, &at, &menus) == PIUIO_TESTHOLD_EXIT && at == 5);

    check("menu: 5 s hold does not end the menu", hold(1, 5, &at, &menus) == PIUIO_TESTHOLD_MENU && at == 5);
    hold(1, 60, &at, &menus);
    {
        // A minute-long hold on the menu never yields EXIT.
        struct piuio_testhold h = {0};
        int exits = 0;
        for(int q = 0; q <= 240; q++)
            if(piuio_testhold_step(&h, 1, 1000 + q / 4, 1) == PIUIO_TESTHOLD_EXIT) exits++;
        check("menu: 60 s hold never ends the menu", exits == 0 && menus >= 10);
    }

    {
        // Releasing TEST restarts the count: 3 s + release + 3 s is no hold.
        struct piuio_testhold h = {0};
        int exits = 0;
        for(int t = 0; t <= 3; t++) exits += piuio_testhold_step(&h, 1, 2000 + t, 0) == PIUIO_TESTHOLD_EXIT;
        piuio_testhold_step(&h, 0, 2004, 0);
        for(int t = 5; t <= 8; t++) exits += piuio_testhold_step(&h, 1, 2000 + t, 0) == PIUIO_TESTHOLD_EXIT;
        check("game: a release in between restarts the count", exits == 0);
    }

    {
        // A short press (what opens Settings in the menu) never triggers anything.
        struct piuio_testhold h = {0};
        int r1 = piuio_testhold_step(&h, 1, 3000, 0);
        int r2 = piuio_testhold_step(&h, 0, 3000, 0);
        check("short press: starts and clears, nothing else", r1 == PIUIO_TESTHOLD_STARTED && r2 == PIUIO_TESTHOLD_NONE && !h.held);
    }

    // IMPER-162: only a run script that asks for it ends without exit()
    check("quick exit: off when unset, empty or 0",
          !piuio_testhold_quick(NULL) && !piuio_testhold_quick("") && !piuio_testhold_quick("0"));
    check("quick exit: on with 1", piuio_testhold_quick("1"));

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
