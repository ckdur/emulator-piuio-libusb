// Global credits (IMPER-117) and free play with attract (IMPER-119).
//
// One pool of credits for the menu and every game, kept in
// /SETTINGS/credits.ini (credits_logic.h has the format and the rules):
//   - in the menu, a coin goes to the pool as a coin ("coins=");
//   - when a game starts, its coin counter is set to the pool's credits at
//     that game's own price (plus the coins from the menu), and from then on
//     the pool follows the game's whole credits;
// Nothing here changes a game's settings or its executable on disk: the coin
// counter in memory is the only thing written for the credits.
//
// Free play with attract: a game in its own free play kept its title up for
// ever (credits_logic.h, credits_fp_step). The MK3 ports are changed in their
// source; for the games of credit_table the one instruction that answers "99
// coins" to the title and the attract sequence is replaced in memory, after
// its bytes were checked, by a read of fp_value below.
//
// Which games: the ones that tell us where their credits are
// (report_credits: mk3_linux_ports, our own sources) and the ones in
// credit_table below. Every other game runs exactly as before.
#define _GNU_SOURCE
#include "credits.h"
#include "credits_logic.h"
#include "piuio_emu.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#ifndef SYS_process_vm_readv
#define SYS_process_vm_readv 347   // i386
#define SYS_process_vm_writev 348
#endif

#define CREDITS_FILE "/SETTINGS/credits.ini"
#define CREDITS_TICK_MS 50        // memory and file work, at most this often
#define CREDITS_SETTLE_MS 3000    // a game's numbers must hold this long before the restore
#define CREDITS_REPRICE_MS 15000  // after the restore: the game may still load its price

// ---- Games that are not our sources ------------------------------------------
// Exceed, Exceed 2: class CCreditMgr { CSetupMgr* setup; int coins; }, with the
// operator's settings in the setup object (0x7f2 coins per credit, 0x7ec
// event mode, 0x7eb credit limit). The guard is the game's own main loop
// reading the coin switch:
//   mov setup,%esi; cmpb $0,0x7f2(%esi); je +0xf; push $0; push creditmgr
#define EX_GUARD(setup, mgr) (const unsigned char[]){ \
    0x8b, 0x35, (setup) & 0xff, ((setup) >> 8) & 0xff, ((setup) >> 16) & 0xff, ((setup) >> 24) & 0xff, \
    0x80, 0xbe, 0xf2, 0x07, 0x00, 0x00, 0x00, 0x74, 0x0f, 0x6a, 0x00, \
    0xff, 0x35, (mgr) & 0xff, ((mgr) >> 8) & 0xff, ((mgr) >> 16) & 0xff, ((mgr) >> 24) & 0xff }
//
// Free play with attract: CCreditMgr::GetCoin() ends with
//   mov $0x63,%eax; ret        (free play)
//   mov 0x4(%eax),%eax; ret    (the coin counter)
// and the first of the two is what the title and the attract screens get
// (the start check is GetCredit()). getcoin99 is that instruction. flags is
// the game's player flags: bits 0 and 1, a player has joined; the demo play
// of the attract sequence is flags[demo_off] & demo_mask.
struct credit_entry {
    struct credit_construct con;
    uintptr_t getcoin99;
    uintptr_t flags;
    int demo_off;
    unsigned char demo_mask;
};

#define EX_ENTRY(title, at, setup, mgr, getcoin, flagsat, demooff, demomask) { { \
    .version = CREDITS_CONSTRUCT_VERSION, .name = title, \
    .coins_base = (void**)(mgr), .coins_off = 4, \
    .set_base = (void**)(setup), .cpc_off = 0x7f2, .cpc_size = 1, \
    .free_off = 0x7ec, .limit_off = 0x7eb, .max_credits = 0, \
    .guard = (const unsigned char*)(at), .guard_bytes = EX_GUARD(setup, mgr), .guard_len = 23 }, \
    getcoin, flagsat, demooff, demomask }

static const struct credit_entry credit_table[] = {
    EX_ENTRY("Exceed (mini)",           0x804d8cd, 0x80a8dbc, 0x80a8dc4, 0x804e91c, 0x80a8d74, 2, 0x10),
    // Exceed 2: no demo play was seen to join players in 7 minutes of its
    // attract sequence, and which bit would mark one is not known: none.
    EX_ENTRY("Exceed 2 (Dec  7 2004)",  0x804dd25, 0x80bed3c, 0x80bed48, 0x804ee8c, 0x80becd4, 0, 0),
    EX_ENTRY("Exceed 2 (Aug 18 2005)",  0x804dce9, 0x80bae7c, 0x80bae84, 0x804ee50, 0x80bae14, 0, 0),
};

// ---- Memory of our own process, without faulting -----------------------------
// The addresses come from a table (or from an object the game may not have
// made yet), so every access goes through the kernel, which answers EFAULT
// instead of sending SIGSEGV to a game that has its own handler.
// (process_vm_readv; /proc/self/mem on a kernel built without it.)
static int mem_fd = -1;
static int mem_use_proc = 0;

static int mem_proc(uintptr_t addr, void* buf, size_t n, int wr)
{
    long r;
    if(mem_fd < 0) mem_fd = syscall(SYS_open, "/proc/self/mem", O_RDWR, 0);
    if(mem_fd < 0) return -1;
    r = wr ? syscall(SYS_pwrite64, mem_fd, buf, n, (uint32_t)addr, 0)
           : syscall(SYS_pread64, mem_fd, buf, n, (uint32_t)addr, 0);
    return r == (long)n ? 0 : -1;
}

static int mem_rw(uintptr_t addr, void* buf, size_t n, int wr)
{
    struct iovec l = { buf, n }, r = { (void*)addr, n };
    long got;
    if(addr < 0x1000) return -1;
    if(mem_use_proc) return mem_proc(addr, buf, n, wr);
    got = syscall(wr ? SYS_process_vm_writev : SYS_process_vm_readv, getpid(), &l, 1, &r, 1, 0);
    if(got < 0 && (errno == ENOSYS || errno == EPERM)) {
        mem_use_proc = 1;
        return mem_proc(addr, buf, n, wr);
    }
    return got == (long)n ? 0 : -1;
}

static int mem_read(uintptr_t addr, void* out, size_t n) { return mem_rw(addr, out, n, 0); }
static int mem_write(uintptr_t addr, const void* in, size_t n) { return mem_rw(addr, (void*)in, n, 1); }

// ---- The pool file -------------------------------------------------------------
// Raw system calls: inside a game, open() and fopen() are other preloads'
// (titledelay.so sends "/SETTINGS/..." to the game's save folder).
static const char* pool_path(void)
{
    const char* p = getenv("_PIU_CREDITS_FILE"); // tests
    return p != NULL && p[0] ? p : CREDITS_FILE;
}

static void pool_load(struct credits_pool* p)
{
    char buf[512];
    long n = -1;
    int fd = syscall(SYS_open, pool_path(), O_RDONLY, 0);
    if(fd >= 0) {
        n = syscall(SYS_read, fd, buf, sizeof(buf) - 1);
        syscall(SYS_close, fd);
    }
    buf[n > 0 ? n : 0] = 0;
    credits_pool_parse(buf, p);
}

static void pool_save(const struct credits_pool* p)
{
    char buf[256], tmp[300];
    int n = credits_pool_format(p, buf, sizeof(buf));
    int fd;
    snprintf(tmp, sizeof(tmp), "%s.tmp", pool_path());
    fd = syscall(SYS_open, tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) return;
    if(syscall(SYS_write, fd, buf, n) != n) {
        syscall(SYS_close, fd);
        return;
    }
    syscall(SYS_fsync, fd);
    syscall(SYS_close, fd);
    syscall(SYS_rename, tmp, pool_path());
}

// Only the two numbers this process owns are replaced: the options belong to
// whoever edits the file (the menu's setting, the operator).
static void pool_store(int credits, int coins)
{
    struct credits_pool p;
    pool_load(&p);
    if(p.credits == credits && p.coins == coins) return;
    p.credits = credits;
    p.coins = coins;
    pool_save(&p);
}

// ---- State -----------------------------------------------------------------------
static struct credit_construct con;   // the running game's, when con_set
static int con_set = 0;
static int table_tried = 0;

static struct credits_edge edge_coin1, edge_coin2;
static int real_coins = 0;            // coins seen on the switches since the start

static long tick_last = 0;
static long valid_since = 0;          // the game's numbers have been readable since
static int restored = 0;
static long restored_at = 0;
static int restored_cpc = 0;
static int restored_coins = 0;
static int restored_credits = 0;
static int excess = 0;
static int pool_written = -1;         // credits last written by this process
static int options_read = 0;
static struct credits_pool options;   // enabled, as read when the game started
static int atexit_set = 0;
static pid_t owner_pid = 0;        // the process the poll thread runs in

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void API_EXPORTED report_credits(const struct credit_construct* c)
{
    if(c == NULL || c->version != CREDITS_CONSTRUCT_VERSION) return;
    con = *c;
    con_set = 1;
    PRINTF("credits: %s reported its credits\n", c->name ? c->name : "game");
}

// ---- Free play with attract, for the games of the table ------------------------
static int fp_value = 0;                 // what "any coins?" gets in free play: 0 or 99
static int fp_woken = 0;
static const struct credit_entry* fp_entry = NULL;  // set once the game is patched

static void fp_patch(const struct credit_entry* e)
{
    static const unsigned char want[10] = { 0xb8, 0x63, 0x00, 0x00, 0x00, 0xc3, 0x8b, 0x40, 0x04, 0xc3 };
    unsigned char have[10], code[6], flags[4];
    uintptr_t value = (uintptr_t)&fp_value;
    if(e->getcoin99 == 0 || getenv("_PIU_NO_FP_ATTRACT") != NULL) return;
    if(mem_read(e->getcoin99, have, sizeof(have)) != 0 || memcmp(have, want, sizeof(want)) != 0) return;
    if(mem_read(e->flags, flags, sizeof(flags)) != 0) return;
    // mov fp_value,%eax; ret: the same six bytes long
    code[0] = 0xa1;
    memcpy(code + 1, &value, 4);
    code[5] = 0xc3;
    if(mprotect((void*)(e->getcoin99 & ~(uintptr_t)0xfff), 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return;
    memcpy((void*)e->getcoin99, code, sizeof(code));
    fp_entry = e;
    PRINTF("credits: %s: free play with attract\n", e->con.name);
}

static void fp_poll(void)
{
    const volatile unsigned char* flags;
    int centre;
    if(fp_entry == NULL) return;
    flags = (const volatile unsigned char*)fp_entry->flags;
    centre = ~(bytes_f[0] & bytes_f[1] & bytes_f[2] & bytes_f[3] &
               bytes_f[4] & bytes_f[5] & bytes_f[6] & bytes_f[7]) & 0x04;
    fp_woken = credits_fp_step(fp_woken, flags[0] & 3,
        flags[fp_entry->demo_off] & fp_entry->demo_mask, centre != 0);
    fp_value = fp_woken ? 99 : 0;
}

static void table_lookup(void)
{
    unsigned int i;
    table_tried = 1;
    for(i = 0; i < sizeof(credit_table) / sizeof(credit_table[0]); i++) {
        const struct credit_construct* c = &credit_table[i].con;
        unsigned char have[32];
        if(c->guard_len > (int)sizeof(have)) continue;
        if(mem_read((uintptr_t)c->guard, have, c->guard_len) != 0) continue;
        if(memcmp(have, c->guard_bytes, c->guard_len) != 0) continue;
        con = *c;
        con_set = 1;
        PRINTF("credits: %s\n", c->name);
        fp_patch(&credit_table[i]);
        return;
    }
}

// The game's numbers; -1 while they cannot be read (objects not made yet).
static int game_addr(void** base, uintptr_t off, uintptr_t* out)
{
    uintptr_t b = 0;
    if(base != NULL) {
        if(mem_read((uintptr_t)base, &b, sizeof(b)) != 0 || b == 0) return -1;
    }
    *out = b + off;
    return 0;
}

static int game_byte(uintptr_t off, int* out)
{
    uintptr_t a;
    unsigned char v;
    if(game_addr(con.set_base, off, &a) != 0 || mem_read(a, &v, 1) != 0) return -1;
    *out = v;
    return 0;
}

static int game_read(struct credits_game* g)
{
    uintptr_t a;
    int32_t coins;
    memset(g, 0, sizeof(*g));
    if(game_addr(con.coins_base, con.coins_off, &a) != 0 || mem_read(a, &coins, 4) != 0) return -1;
    g->coins = coins;
    if(con.cpc_size == 4) {
        int32_t cpc;
        if(game_addr(con.set_base, con.cpc_off, &a) != 0 || mem_read(a, &cpc, 4) != 0) return -1;
        g->cpc = cpc;
    }
    else if(game_byte(con.cpc_off, &g->cpc) != 0) return -1;
    if(con.free_off != 0 && game_byte(con.free_off, &g->freeflag) != 0) return -1;
    if(con.limit_off != 0 && game_byte(con.limit_off, &g->limit) != 0) return -1;
    g->max_credits = con.max_credits;
    // a counter no game would hold: not what the table says it is
    if(g->coins < 0 || g->coins > 1000000 || g->cpc < 0 || g->cpc > 255) return -1;
    return 0;
}

static int game_set_coins(int coins)
{
    uintptr_t a;
    int32_t v = coins;
    if(game_addr(con.coins_base, con.coins_off, &a) != 0) return -1;
    return mem_write(a, &v, 4);
}

static void coin_switches(void)
{
    // active low: byte 8 bit 2 is coin 1, byte 9 bit 2 is coin 2
    real_coins += credits_edge_step(&edge_coin1, !(bytes_f[8] & 0x04));
    real_coins += credits_edge_step(&edge_coin2, !(bytes_f[9] & 0x04));
}

static void menu_poll(void)
{
    struct credits_pool p;
    int n = real_coins;
    if(n == 0) return;
    real_coins = 0;
    pool_load(&p);
    if(!p.enabled) return;
    p.coins = credits_clamp(p.coins + n, 0, CREDITS_POOL_MAX);
    pool_save(&p);
}

static void game_poll(long now)
{
    struct credits_game g;

    if(!con_set) {
        // On the first round: the games of the table are not packed, and
        // the patch for free play goes in before their title runs.
        if(table_tried) return;
        table_lookup();
        valid_since = now;
        if(!con_set) return;
    }
    if(game_read(&g) != 0) {
        valid_since = now;
        return;
    }
    if(!options_read) {
        pool_load(&options);
        options_read = 1;
    }
    if(credits_game_freeplay(&g)) {
        // The operator's own free play: no credits to keep.
        valid_since = now;
        return;
    }

    if(!options.enabled) return;

    if(!restored) {
        struct credits_pool p;
        int coins;
        if(now - valid_since < CREDITS_SETTLE_MS) return;
        pool_load(&p);
        coins = credits_restore(&g, &p, real_coins, &excess);
        if(game_set_coins(coins) != 0) return;
        restored = 1;
        restored_at = now;
        restored_cpc = g.cpc;
        restored_coins = coins;
        restored_credits = coins / g.cpc;
        g.coins = coins;
        pool_written = credits_pool_now(&g, excess);
        pool_store(pool_written, 0);
        PRINTF("credits: restored %d credit(s), %d over the cap\n", restored_credits, excess);
        return;
    }

    // The game read its price from its save after the restore: the credits
    // handed over are the same number at the new price.
    if(now - restored_at < CREDITS_REPRICE_MS && g.cpc != restored_cpc && g.coins == restored_coins) {
        restored_coins = restored_credits * g.cpc;
        restored_cpc = g.cpc;
        game_set_coins(restored_coins);
        g.coins = restored_coins;
    }

    // Credits over the game's cap go in as the game spends its own.
    if(excess > 0) {
        int room = credits_cap(&g) - g.coins / g.cpc;
        if(room > 0) {
            int n = room < excess ? room : excess;
            if(game_set_coins(g.coins + n * g.cpc) == 0) {
                excess -= n;
                g.coins += n * g.cpc;
            }
        }
    }

    {
        int c = credits_pool_now(&g, excess);
        if(c != pool_written) {
            pool_written = c;
            pool_store(c, 0);
        }
    }
}

void credits_poll(int is_menu)
{
    long now;
    coin_switches();
    if(!is_menu) fp_poll();
    now = now_ms();
    if(tick_last != 0 && now - tick_last < CREDITS_TICK_MS) return;
    if(tick_last == 0) valid_since = now;
    tick_last = now;
    if(!atexit_set) {
        atexit_set = 1;
        owner_pid = getpid();
        atexit(credits_flush);
    }
    if(is_menu) menu_poll();
    else game_poll(now);
}

void credits_flush(void)
{
    struct credits_game g;
    // a forked child ends with the game's exit handlers too, on a copy of
    // the game's memory as it was at the fork
    if(owner_pid == 0 || getpid() != owner_pid) return;
    if(!con_set || !restored || !options.enabled) return;
    if(game_read(&g) != 0 || credits_game_freeplay(&g)) return;
    pool_written = credits_pool_now(&g, excess);
    pool_store(pool_written, 0);
}

int API_EXPORTED libusb_piuio_credits(int* credits, int* coins)
{
    struct credits_pool p;
    pool_load(&p);
    if(credits != NULL) *credits = p.credits;
    if(coins != NULL) *coins = p.coins;
    return p.enabled;
}
