#ifndef CREDITS_LOGIC_H
#define CREDITS_LOGIC_H

// Global credits (IMPER-117) and free play with attract (IMPER-119): the decisions,
// with no dependency on the shim, so that tests/test_credits.c can check them
// on the host (like piuio_testhold.h). credits.c does the memory and file
// access around them.
//
// The pool, /SETTINGS/credits.ini:
//
//   [credits]
//   credits=3      whole credits, shared by the menu and every game
//   coins=1        coins dropped in the menu, not yet turned into credits:
//                  the next game that starts takes them at its own price
//   [options]
//   enabled=1      0: every game keeps its own credits, nothing is saved
//
// The games' own settings are never changed: a game still decides how many
// coins make a credit. Whole credits are kept; a part of a credit (2 coins of
// 3) stays in the game that got it and is lost when that game ends.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct credits_pool {
    int credits;
    int coins;
    int enabled;
};

#define CREDITS_POOL_MAX 9999

static inline int credits_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline void credits_pool_defaults(struct credits_pool* p)
{
    p->credits = 0;
    p->coins = 0;
    p->enabled = 1;
}

// Reads "key=value" lines; sections are only for the reader's eyes. Unknown
// lines are ignored, missing keys keep their defaults, values are clamped.
static inline void credits_pool_parse(const char* txt, struct credits_pool* p)
{
    credits_pool_defaults(p);
    while(txt != NULL && *txt) {
        const char* end = strchr(txt, '\n');
        size_t len = end ? (size_t)(end - txt) : strlen(txt);
        char line[64];
        if(len < sizeof(line)) {
            char* eq;
            memcpy(line, txt, len);
            line[len] = 0;
            eq = strchr(line, '=');
            if(eq != NULL && line[0] != '#' && line[0] != ';' && line[0] != '[') {
                int v = atoi(eq + 1);
                *eq = 0;
                if(strcmp(line, "credits") == 0) p->credits = credits_clamp(v, 0, CREDITS_POOL_MAX);
                else if(strcmp(line, "coins") == 0) p->coins = credits_clamp(v, 0, CREDITS_POOL_MAX);
                else if(strcmp(line, "enabled") == 0) p->enabled = v != 0;
            }
        }
        if(end == NULL) break;
        txt = end + 1;
    }
}

static inline int credits_pool_format(const struct credits_pool* p, char* out, int n)
{
    return snprintf(out, n, "[credits]\ncredits=%d\ncoins=%d\n[options]\nenabled=%d\n",
        p->credits, p->coins, p->enabled);
}

// ---- Coin switch --------------------------------------------------------
// A coin counts when the line goes down after it was seen up for
// CREDITS_EDGE_STABLE polls in a row. A line that is down from the first
// poll (a board that answers zeros while it starts, a stuck switch) is never
// a coin: that is the "ghost credit" of IMPER-116.
#define CREDITS_EDGE_STABLE 3

struct credits_edge {
    int up_polls;
    int down;
};

static inline int credits_edge_step(struct credits_edge* e, int pressed)
{
    if(!pressed) {
        if(e->up_polls < CREDITS_EDGE_STABLE) e->up_polls++;
        e->down = 0;
        return 0;
    }
    if(e->down) return 0;
    e->down = 1;
    if(e->up_polls < CREDITS_EDGE_STABLE) {
        // never seen up long enough: not a coin, and it has to be released
        // and stay up again before it can be one
        e->up_polls = 0;
        return 0;
    }
    e->up_polls = 0;
    return 1;
}

// What a game is shown of a coin line (IMPER-116). Until the line was seen
// up for CREDITS_EDGE_STABLE reads in a row it is shown as up: Exceed 2
// takes a line that is down on its very first read as a coin (its detector
// starts armed), which made one read of zeros at start-up a credit.
struct credits_guard {
    int up_reads;
    int armed;
};

// pressed: the line as it is. Returns the line as the game is to see it.
static inline int credits_guard_step(struct credits_guard* g, int pressed)
{
    if(g->armed) return pressed;
    if(pressed) {
        g->up_reads = 0;
        return 0;
    }
    if(++g->up_reads >= CREDITS_EDGE_STABLE) g->armed = 1;
    return 0;
}

// ---- The game's numbers ----------------------------------------------------
struct credits_game {
    int coins;        // the game's coin counter
    int cpc;          // coins per credit; 0 = the game is in free play
    int freeflag;     // the game's free play / event flag
    int limit;        // operator's credit limit, 0 = none
    int max_credits;  // cap of the game's own coin routine, 0 = unknown
};

static inline int credits_game_freeplay(const struct credits_game* g)
{
    return g->cpc <= 0 || g->freeflag == 1;
}

static inline int credits_cap(const struct credits_game* g)
{
    int cap = g->max_credits > 0 ? g->max_credits : 99;
    if(g->limit > 0 && g->limit < cap) cap = g->limit;
    return cap;
}

// The coin count a game starts with. real_coins: coins the shim itself saw
// dropped since the game started (credits_edge_step). When it saw none,
// whatever the game counted on its own while it started is a ghost and is
// dropped; otherwise the game's count is kept on top of the pool.
// *excess gets the whole credits that do not fit under the game's cap: they
// stay in the pool and are handed over as credits are spent.
static inline int credits_restore(const struct credits_game* g, const struct credits_pool* pool,
    int real_coins, int* excess)
{
    int cap = credits_cap(g);
    int coins = pool->coins + (real_coins > 0 && g->coins > 0 ? g->coins : 0);
    int credits = pool->credits + coins / g->cpc;
    int rest = coins % g->cpc;
    *excess = 0;
    if(credits >= cap) {
        *excess = credits - cap;
        credits = cap;
        rest = 0; // the game's own routine drops the rest at the cap too
    }
    return credits * g->cpc + rest;
}

// What goes to the pool file while a game runs: its whole credits and the
// ones waiting over its cap.
static inline int credits_pool_now(const struct credits_game* g, int excess)
{
    int c = g->coins / g->cpc + excess;
    return credits_clamp(c, 0, CREDITS_POOL_MAX);
}

// ---- Free play with the attract sequence (IMPER-119) -----------------------
// A game in its own free play answered "99 coins" to its title and attract
// sequence, so the title stayed up for ever. The games now get 0 there (the
// MK3 ports in their source, Exceed / Exceed 2 by a patch of that one answer
// in memory, credits.c) until a centre step is pressed: then 99, so the title
// comes up and stays until a player starts, as XX and Phoenix do. The game
// is still in its own free play (it draws FREE PLAY, it starts with no
// credit): only the question the attract sequence asks changed.
//
// woken: the answer is 99. joined: a player has joined (a game is starting
// or running). demo: that "game" is the demo play of the attract sequence.
// centre_down: a centre step is pressed. Returns the new woken.
static inline int credits_fp_step(int woken, int joined, int demo, int centre_down)
{
    if(joined && !demo) return 0;   // the next title starts with the attract again
    if(centre_down) return 1;
    return woken;
}

#endif
