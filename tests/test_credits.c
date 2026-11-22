// Host test for the rules of global credits and free play with attract in the
// PIUIO shim (libusb/credits_logic.h; IMPER-116, IMPER-117, IMPER-119).
//
//   gcc -std=gnu17 -Wall -Wextra -I libusb -o /tmp/test_credits tests/test_credits.c && /tmp/test_credits

#include <stdio.h>
#include "credits_logic.h"

static int fails = 0;
static void check(const char *what, int ok) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) fails++;
}

// Coins counted for a line that reads 'levels' (1 = pressed), one per poll.
static int coins_of(const char *levels) {
    struct credits_edge e = {0};
    int n = 0;
    for(; *levels; levels++) n += credits_edge_step(&e, *levels == '1');
    return n;
}

int main(void) {
    struct credits_pool p;
    struct credits_game g;
    char buf[256];
    int excess;

    // ---- pool file
    credits_pool_parse("", &p);
    check("no file: no credits, enabled", p.credits == 0 && p.coins == 0 && p.enabled == 1);
    credits_pool_parse("[credits]\ncredits=7\ncoins=2\n[options]\nenabled=0\n", &p);
    check("file read", p.credits == 7 && p.coins == 2 && p.enabled == 0);
    credits_pool_format(&p, buf, sizeof(buf));
    credits_pool_parse(buf, &p);
    check("written file reads back the same", p.credits == 7 && p.coins == 2 && p.enabled == 0);
    credits_pool_parse("credits=-5\r\ncoins=99999999\njunk\ncredits\n=3\n", &p);
    check("bad values are clamped, junk ignored", p.credits == 0 && p.coins == CREDITS_POOL_MAX && p.enabled == 1);

    // ---- coin switch (IMPER-116: no coin from a line that starts pressed)
    check("a pulse after the line was up is one coin", coins_of("0000011000") == 1);
    check("two pulses are two coins", coins_of("000011100001110") == 2);
    check("a line that is down from the first poll is no coin", coins_of("1111111111") == 0);
    check("down at the start, released, then a real pulse: one coin", coins_of("1111000011000") == 1);
    check("a glitch right after the start is no coin", coins_of("0100000") == 0);
    check("a held line counts once", coins_of("00001111111111111") == 1);

    // ---- what a game is shown of a coin line
    {
        struct credits_guard gd = {0};
        const char *line = "1111100000110011", *want = "0000000000110011";
        int ok = 1, i;
        for(i = 0; line[i]; i++) ok &= credits_guard_step(&gd, line[i] == '1') == (want[i] == '1');
        check("a line down at the first reads is shown up until it was up; later coins pass", ok);
        gd = (struct credits_guard){0};
        for(ok = 1, i = 0; i < 1000; i++) ok &= credits_guard_step(&gd, 1) == 0;
        check("a line that never comes up is never a coin", ok);
        gd = (struct credits_guard){0};
        line = "0000100"; want = "0000100";
        for(ok = 1, i = 0; line[i]; i++) ok &= credits_guard_step(&gd, line[i] == '1') == (want[i] == '1');
        check("a line that starts up is shown as it is", ok);
    }

    // ---- restore
    g = (struct credits_game){ .coins = 0, .cpc = 3, .max_credits = 99 };
    p = (struct credits_pool){ .credits = 4, .coins = 0, .enabled = 1 };
    check("4 credits at 3 coins per credit: 12 coins", credits_restore(&g, &p, 0, &excess) == 12 && excess == 0);
    p.coins = 5;
    check("5 coins from the menu: one more credit and 2 coins", credits_restore(&g, &p, 0, &excess) == 17);
    g.coins = 3; p.coins = 0;
    check("a credit the game counted with no coin seen is a ghost: dropped", credits_restore(&g, &p, 0, &excess) == 12);
    check("the same with a coin seen on the switch: kept", credits_restore(&g, &p, 1, &excess) == 15);
    g = (struct credits_game){ .coins = 0, .cpc = 2, .max_credits = 9 };
    p = (struct credits_pool){ .credits = 20, .coins = 1, .enabled = 1 };
    check("over the game's cap: the cap, the rest waits", credits_restore(&g, &p, 0, &excess) == 18 && excess == 11);
    g.limit = 5;
    check("the operator's limit is the cap when lower", credits_restore(&g, &p, 0, &excess) == 10 && excess == 15);
    g = (struct credits_game){ .coins = 7, .cpc = 3 };
    check("pool follows whole credits, part credits are not kept", credits_pool_now(&g, 0) == 2);
    check("credits waiting over the cap stay in the pool", credits_pool_now(&g, 11) == 13);
    g = (struct credits_game){ .cpc = 0 };
    check("coins per credit 0 is the game's own free play", credits_game_freeplay(&g));
    g = (struct credits_game){ .cpc = 1, .freeflag = 1 };
    check("event flag is the game's own free play", credits_game_freeplay(&g));

    // ---- free play with attract: what the title and the attract get as "coins"
    {
        int w = 0;
        w = credits_fp_step(w, 0, 0, 0);
        check("nobody there: 0 coins, the attract runs", w == 0);
        w = credits_fp_step(w, 3, 1, 0);
        check("the demo play joins both players: still 0", w == 0);
        w = credits_fp_step(w, 3, 1, 1);
        check("a centre step during the demo play: 99, the title comes up", w == 1);
        w = credits_fp_step(w, 0, 0, 0);
        w = credits_fp_step(w, 0, 0, 0);
        check("and stays up", w == 1);
        w = credits_fp_step(w, 1, 0, 1);
        check("a player joins (the start): back to 0 for the next title", w == 0);
        w = credits_fp_step(w, 1, 0, 1);
        check("centre steps while a game runs change nothing", w == 0);
        w = credits_fp_step(w, 0, 0, 0);
        check("after the game: 0, the attract runs again", w == 0);
        w = credits_fp_step(w, 0, 0, 1);
        check("a centre step on the title or the logo: 99", w == 1);
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
