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
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

struct credits_pool {
    int credits;
    int coins;
    int enabled;
    unsigned gen;   // generation of the file it was read from (see the two copies below)
};

#define CREDITS_POOL_MAX 9999

static inline int credits_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline void credits_pool_defaults(struct credits_pool* p)
{
    p->credits = 0;
    p->coins = 0;
    p->enabled = 1;
    p->gen = 0;
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
                else if(strcmp(line, "gen") == 0) p->gen = (unsigned)strtoul(eq + 1, NULL, 10);
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

// ---- Two copies, a generation and a check (a power cut at any moment) ----------
// The machine can lose power at any time (the cabinet's switch, the supply). The
// pool is therefore kept in two files, /SETTINGS/credits.ini and the previous
// generation, credits.ini.prev, each with a generation counter and the CRC-32 of
// the lines above its "check=" line. A save writes a temporary file and syncs it,
// moves the current file to .prev, renames the temporary one in and syncs the
// folder: at every step at least one complete, valid copy exists. A load takes
// the valid copy with the higher generation. A file without a "check=" line
// (written by an older shim, or edited by hand) is accepted as it is; a file
// whose check does not match (a damaged disk) is ignored in favour of the other
// copy. Every new file starts with the marker line below, and a file that has the
// marker must have a matching check: a half-written one is therefore damaged, it
// cannot pass for an older file. To edit the file by hand, delete the marker line
// and the check= line (and keep the credits= line: a file with none is damaged).
#define CREDITS_MARK "#ntdec-credits-v2\n"
static inline unsigned credits_crc32(const char* s, size_t n)
{
    unsigned crc = 0xFFFFFFFFu;
    size_t i;
    int j;
    for(i = 0; i < n; i++) {
        crc ^= (unsigned char)s[i];
        for(j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// The pool as text with its generation and check line.
static inline int credits_pool_format_ck(const struct credits_pool* p, char* out, int n)
{
    int k = snprintf(out, n, CREDITS_MARK "[credits]\ncredits=%d\ncoins=%d\n[options]\nenabled=%d\ngen=%u\n",
        p->credits, p->coins, p->enabled, p->gen);
    int m;
    if(k < 0 || k >= n) return -1;
    m = snprintf(out + k, n - k, "check=%08x\n", credits_crc32(out, (size_t)k));
    if(m < 0 || k + m >= n) return -1;
    return k + m;
}

// 0: usable, the check matches; 3: usable, there is no check line (an older
// shim or a hand edit); 1: damaged (the check does not match); 2: nothing
// there. 'p' is the defaults unless the result is 0 or 3.
static inline int credits_pool_parse_ck(const char* txt, struct credits_pool* p)
{
    const char* line;
    const char* ck = NULL;
    credits_pool_defaults(p);
    if(txt == NULL || *txt == 0) return 2;
    for(line = txt; line != NULL && *line; ) {
        if(strncmp(line, "check=", 6) == 0) ck = line;
        line = strchr(line, '\n');
        if(line) line++;
    }
    if(strncmp(txt, CREDITS_MARK, strlen(CREDITS_MARK)) == 0) {      // a file of this version: the check is mandatory
        char* endp = NULL;
        unsigned want;
        if(ck == NULL) return 1;
        want = (unsigned)strtoul(ck + 6, &endp, 16);
        if(endp == ck + 6 || credits_crc32(txt, (size_t)(ck - txt)) != want) return 1;
        credits_pool_parse(txt, p);
        return 0;
    }
    if(strstr(txt, "credits=") == NULL) return 1;                       // not a pool file at all
    credits_pool_parse(txt, p);
    return 3;
}

// Which copy to use: the usable one with the higher generation (the main file on
// a tie); a file with no check line counts as the newest (it is what an
// operator just edited, or what an older shim wrote and there is no other);
// none usable: the defaults. Returns 0 (a), 1 (b) or -1 (neither).
static inline int credits_pool_choose(const char* a, const char* b, struct credits_pool* out)
{
    struct credits_pool pa, pb;
    int ra = credits_pool_parse_ck(a, &pa);
    int rb = credits_pool_parse_ck(b, &pb);
    int va = ra == 0 || ra == 3;
    int vb = rb == 0 || rb == 3;
    unsigned ga = ra == 3 ? ~0u : pa.gen;
    unsigned gb = rb == 3 ? ~0u : pb.gen;
    if(va && (!vb || ga >= gb)) { *out = pa; return 0; }
    if(vb) { *out = pb; return 1; }
    credits_pool_defaults(out);
    return -1;
}

// Raw system calls: inside a game, open() and fopen() are other preloads' (titledelay.so
// redirects "/SETTINGS/..." into the game's save folder).
static inline long credits_sys_read_file(const char* path, char* buf, size_t n)
{
    long got = -1;
    int fd = (int)syscall(SYS_open, path, O_RDONLY, 0);
    if(fd >= 0) {
        got = syscall(SYS_read, fd, buf, n - 1);
        syscall(SYS_close, fd);
    }
    buf[got > 0 ? got : 0] = 0;
    return got;
}

static inline void credits_pool_load_files(const char* path, struct credits_pool* p)
{
    char a[512], b[512], prev[320];
    snprintf(prev, sizeof(prev), "%s.prev", path);
    credits_sys_read_file(path, a, sizeof(a));
    credits_sys_read_file(prev, b, sizeof(b));
    credits_pool_choose(a, b, p);
}

static inline void credits_pool_sync_dir(const char* path)
{
    char dir[320];
    char* slash;
    int fd;
    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    if(slash == NULL) return;
    if(slash == dir) slash[1] = 0; else *slash = 0;
    fd = (int)syscall(SYS_open, dir, O_RDONLY | O_DIRECTORY, 0);
    if(fd < 0) return;
    syscall(SYS_fsync, fd);
    syscall(SYS_close, fd);
}

// Saves 'p' as generation p->gen + 1. A step that fails leaves the files as they were.
static inline void credits_pool_save_files(const char* path, const struct credits_pool* p)
{
    char buf[256], tmp[320], prev[320];
    struct credits_pool q = *p;
    int n, fd;
    q.gen = p->gen + 1;
    n = credits_pool_format_ck(&q, buf, sizeof(buf));
    if(n <= 0) return;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    snprintf(prev, sizeof(prev), "%s.prev", path);
    fd = (int)syscall(SYS_open, tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) return;
    if(syscall(SYS_write, fd, buf, n) != n) {
        syscall(SYS_close, fd);
        return;
    }
    syscall(SYS_fsync, fd);
    syscall(SYS_close, fd);
    syscall(SYS_rename, path, prev);     // the current file becomes the previous generation
    if(syscall(SYS_rename, tmp, path) != 0) syscall(SYS_rename, prev, path);   // could not: put it back
    credits_pool_sync_dir(path);
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
