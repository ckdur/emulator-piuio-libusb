// 32-bit test (run in the build container) of the shim's credits code
// (libusb/credits.c, IMPER-117): every entry of the game table against the real
// executables, the two counter models (RAW, DIRECT), pointer indirection, the
// guard rule, the coins-per-credit limit of the newer games and the restore.
// credits.c is included, so its statics are the ones checked.
//
//   gcc -m32 -std=gnu99 -Wall -I . -I libusb -fPIE -pie -o /tmp/test_credits_table tests/test_credits_table.c -lpthread -lrt -ldl
//   /tmp/test_credits_table [pki dir with <game>/piu]
// (default dir: build.multiloader.release/pki next to the repository root; the table checks
// against executables are skipped for a game whose file is absent)
#define _GNU_SOURCE
#include <stdio.h>
#include "credits.c"

unsigned char bytes_f[16], bytes_fb[2], bytes_l[16], bytes_lb[8];   // what piuio_emu.c owns
int bytes_lb_set, piuioemu_mode;

static int fails = 0;
static void check(const char* what, int ok) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if(!ok) fails++; }

// ---- A game's file as its memory (32-bit ELF, PT_LOAD segments) -----------------
static uint32_t le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned char* slurp(const char* path, size_t* n)
{
    FILE* f = fopen(path, "rb");
    unsigned char* d;
    if(f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = malloc(*n);
    if(fread(d, 1, *n, f) != *n) { free(d); d = NULL; }
    fclose(f);
    return d;
}
static int img_has(const unsigned char* d, uintptr_t addr, const unsigned char* want, int n)
{
    uint32_t phoff = le32(d + 28);
    unsigned phnum = d[44] | d[45] << 8, i;
    for(i = 0; i < phnum; i++) {
        const unsigned char* ph = d + phoff + 32 * i;
        uint32_t off = le32(ph + 4), va = le32(ph + 8), fsz = le32(ph + 16);
        if(le32(ph) != 1 || addr < va || addr + n > (uintptr_t)va + fsz) continue;
        return memcmp(d + off + (addr - va), want, n) == 0;
    }
    return 0;
}

#define NTABLE ((int)(sizeof(credit_table) / sizeof(credit_table[0])))

// ---- Fake game memory ----------------------------------------------------------------
static void setcon(const struct credit_construct* c)
{
    con = *c;
    con_set = 1;
    restored = 0;
    excess = 0;
    pool_written = -1;
    options_read = 0;
    valid_since = 0;
    real_coins = 0;
}
static int32_t rd32(uintptr_t a) { int32_t v; memcpy(&v, (void*)a, 4); return v; }
static void wr32(uintptr_t a, int32_t v) { memcpy((void*)a, &v, 4); }
// Puts a pool in the file as an edit of the current one: pool_save keeps the two
// copies by generation, so a pool built from nothing (gen 0) would lose to .prev.
static void seed_pool(int credits, int coins)
{
    struct credits_pool p;
    pool_load(&p);
    p.credits = credits;
    p.coins = coins;
    p.enabled = 1;
    pool_save(&p);
}

int main(int argc, char** argv)
{
    char path[512];
    const char* dir = argc > 1 ? argv[1] : "build.multiloader.release/pki";
    static const struct { const char* dir; const char* prefix; } games[] = {
        { "zero", "Zero" }, { "nx1", "NX1" }, { "nx2", "NX2" }, { "nx2_cn", "NX2 CN" },
        { "nxa", "NXA" }, { "nxa_cn", "NXA CN" }, { "fiesta", "Fiesta 1.20" }, { "fex", "Fiesta EX" },
        { "f2", "Fiesta 2" }, { "prime", "Prime (" }, { "prime_je", "Prime JE" }, { "prime2", "Prime 2" },
        { "xx", "XX" }, { "phoenix", "Phoenix" }, { "infinity", "Infinity" }, { "pjump", "Pump Jump" },
        { "exceed_mini", "Exceed (mini)" }, { "exceed_2", "Exceed 2" },
    };
    int i, j, checked = 0;
    char tmp[64];
    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(tmp, sizeof(tmp), "/tmp/test_credits_pool.%d", (int)getpid());
    setenv("_PIU_CREDITS_FILE", tmp, 1);

    // ---- every entry against the real executables: each one matches its own game only
    for(i = 0; i < (int)(sizeof(games) / sizeof(games[0])); i++) {
        size_t n;
        unsigned char* d;
        int own = 0, other = 0;
        snprintf(path, sizeof(path), "%s/%s/piu", dir, games[i].dir);
        d = slurp(path, &n);
        if(d == NULL) { printf("skip: %s (no %s)\n", games[i].prefix, path); continue; }
        for(j = 0; j < NTABLE; j++) {
            const struct credit_construct* c = &credit_table[j].con;
            if(!img_has(d, (uintptr_t)c->guard, c->guard_bytes, c->guard_len)) continue;
            if(strncmp(c->name, games[i].prefix, strlen(games[i].prefix)) == 0) own++;
            else other++;
        }
        snprintf(path, sizeof(path), "%s's executable matches its entry, and no other", games[i].prefix);
        check(path, own == 1 && other == 0);
        checked++;
        free(d);
    }
    printf("(%d executables found)\n", checked);
    {
        int names = 0, ok = 1;
        for(i = 0; i < NTABLE; i++) {
            const struct credit_construct* c = &credit_table[i].con;
            names += c->guard_len == 32 || c->guard_len == 23;
            ok &= c->guard_len <= 32 && c->version == CREDITS_CONSTRUCT_VERSION;
        }
        check("table: 3 Exceed + 16 games, guards fit the 32 bytes read", NTABLE == 3 + 16 && names == NTABLE && ok);
    }

    // ---- table_lookup: the guard rule, on memory made like the game's
    for(i = 0; i < NTABLE; i++) {
        const struct credit_construct* c = &credit_table[i].con;
        uintptr_t page = (uintptr_t)c->guard & ~(uintptr_t)0xfff;
        unsigned char* m = mmap((void*)page, 0x2000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(m != (void*)page) { snprintf(path, sizeof(path), "%s: no memory at %p", c->name, (void*)page); check(path, 0); continue; }
        con_set = 0; table_tried = 0;
        memcpy((void*)c->guard, c->guard_bytes, c->guard_len);
        table_lookup();
        snprintf(path, sizeof(path), "%s: found by its guard", c->name);
        check(path, con_set && strcmp(con.name, c->name) == 0);
        ((unsigned char*)c->guard)[c->guard_len - 1] ^= 1;
        con_set = 0; table_tried = 0;
        table_lookup();
        snprintf(path, sizeof(path), "%s: one byte different: refused", c->name);
        check(path, !con_set);
        munmap(m, 0x2000);
    }
    con_set = 0;

    // ---- the models on fake memory (anywhere: the entries' own addresses are above)
    {
        unsigned char* m = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        uintptr_t b = (uintptr_t)m;
        struct credits_game g;
        struct credits_pool p;
        struct credit_construct raw = { .version = CREDITS_CONSTRUCT_VERSION, .name = "raw",
            .coins_off = b + 0x10, .cpc_off = b + 0x20, .cpc_size = 1, .free_off = b + 0x21, .limit_off = b + 0x22 };
        struct credit_construct dir = { .version = CREDITS_CONSTRUCT_VERSION, .name = "direct",
            .coins_off = b + 0x30, .part_off = b + 0x34, .cpc_off = b + 0x20, .cpc_size = 1, .free_off = b + 0x21, .limit_off = b + 0x22 };
        long t = 100000;
        struct credit_construct ptr;
        void** pc = (void**)(b + 0x100);   // pointer to the object with the counters
        void** ps = (void**)(b + 0x104);   // pointer to the setup object

        m[0x20] = 3; m[0x21] = 0; m[0x22] = 0;
        // RAW
        setcon(&raw);
        wr32(b + 0x10, 7);
        check("RAW: coins as they are", game_read(&g) == 0 && g.coins == 7 && g.cpc == 3);
        check("RAW: set 11 coins", game_set_coins(11, 3) == 0 && rd32(b + 0x10) == 11);
        // DIRECT
        setcon(&dir);
        wr32(b + 0x30, 4); wr32(b + 0x34, 2);
        check("DIRECT: 4 credits + 2 coins at 3 per credit = 14 coins", game_read(&g) == 0 && g.coins == 14 && g.cpc == 3);
        check("DIRECT: set 17 coins: 5 credits, part 2", game_set_coins(17, 3) == 0 && rd32(b + 0x30) == 5 && rd32(b + 0x34) == 2);
        check("DIRECT: set 12 coins: 4 credits, part 0", game_set_coins(12, 3) == 0 && rd32(b + 0x30) == 4 && rd32(b + 0x34) == 0);
        wr32(b + 0x34, 9);
        check("DIRECT: a part above the price counts as at most price - 1", game_read(&g) == 0 && g.coins == 4 * 3 + 2);
        wr32(b + 0x34, -1);
        check("DIRECT: a negative part is not the table's game", game_read(&g) != 0);
        wr32(b + 0x34, 0); wr32(b + 0x30, 5000000);
        check("DIRECT: absurd credits are refused", game_read(&g) != 0);
        wr32(b + 0x30, 4);
        m[0x20] = 0;
        check("DIRECT: price 0 = free play, credits only", game_read(&g) == 0 && g.coins == 4 && credits_game_freeplay(&g));
        m[0x20] = 3;
        // coins per credit above max_cpc: skipped
        dir.max_cpc = 9;
        setcon(&dir);
        m[0x20] = 9;
        check("max_cpc: 9 coins per credit is read", game_read(&g) == 0 && g.cpc == 9);
        m[0x20] = 10;
        check("max_cpc: 10 coins per credit is not (another coin mode)", game_read(&g) != 0);
        // the whole poll: nothing written for cpc 10
        wr32(b + 0x30, 0); wr32(b + 0x34, 0);
        seed_pool(6, 0);
        t += 10000; valid_since = t - 5000;
        game_poll(t);
        check("max_cpc: the restore leaves the game alone", rd32(b + 0x30) == 0 && !restored);
        m[0x20] = 3;
        // restore in the poll, DIRECT, and the hit-me protection
        seed_pool(6, 2);
        wr32(b + 0x30, 1); wr32(b + 0x34, 1);   // what the game counted while it started: a ghost
        t += 10000; valid_since = t - 5000;
        game_poll(t);
        check("restore: 6 credits and the menu's 2 coins (part 2), the game's own count dropped",
            restored && rd32(b + 0x30) == 6 && rd32(b + 0x34) == 2);
        pool_load(&p);
        check("restore: the pool follows the game's whole credits", p.credits == 6);
        wr32(b + 0x30, 4); wr32(b + 0x34, 1);   // the game used 2 credits, got a coin
        t += 100; game_poll(t);
        pool_load(&p);
        check("after the game spent: pool 4 credits, the part credit is not kept", p.credits == 4);
        credits_flush();   // owner_pid is 0 here: nothing happens, no crash
        // a real coin seen on the switch keeps the game's own count
        setcon(&dir);
        seed_pool(2, 0);
        wr32(b + 0x30, 1); wr32(b + 0x34, 0);
        real_coins = 1;
        t += 10000; valid_since = t - 5000;
        game_poll(t);
        check("a coin seen on the switch: the game's credit is kept on top", rd32(b + 0x30) == 3);
        // over the cap: Pump Jump style cap 20
        dir.max_credits = 5;
        setcon(&dir);
        seed_pool(9, 0);
        wr32(b + 0x30, 0); wr32(b + 0x34, 0);
        t += 10000; valid_since = t - 5000;
        game_poll(t);
        check("cap 5: 5 credits go in, 4 wait in the pool", rd32(b + 0x30) == 5 && excess == 4);
        wr32(b + 0x30, 3);
        t += 100; game_poll(t);
        check("the game spent 2: 2 of the waiting ones are handed over", rd32(b + 0x30) == 5 && excess == 2);
        pool_load(&p);
        check("and the pool keeps 5 + 2", p.credits == 7);
        // pointer indirection (Zero, Infinity, Pump Jump): objects made later
        ptr = (struct credit_construct){ .version = CREDITS_CONSTRUCT_VERSION, .name = "ptr",
            .coins_base = pc, .coins_off = 0x38, .set_base = ps, .cpc_off = 0x384, .cpc_size = 4,
            .free_off = 0, .limit_off = 0xdd, .max_credits = 99 };
        setcon(&ptr);
        *pc = NULL; *ps = NULL;
        check("pointers: objects not made yet: cannot be read", game_read(&g) != 0);
        *pc = m + 0x1000; *ps = m + 0x2000;
        wr32(b + 0x1000 + 0x38, 9);
        wr32(b + 0x2000 + 0x384, 3);
        m[0x2000 + 0xdd] = 0;
        check("pointers: coins and a 4 byte price behind them", game_read(&g) == 0 && g.coins == 9 && g.cpc == 3);
        check("pointers: set writes behind the pointer", game_set_coins(15, 3) == 0 && rd32(b + 0x1038) == 15);
        wr32(b + 0x2000 + 0x384, 0x7fffffff);
        check("pointers: a price no game has: refused", game_read(&g) != 0);
        wr32(b + 0x2000 + 0x384, 5);
        m[0x2000 + 0xdd] = 7;
        check("pointers: the limit byte behind the settings pointer", game_read(&g) == 0 && g.limit == 7 && credits_cap(&g) == 7);
        // version 1 constructs (a game built before)
        {
            struct credit_construct v1;
            memset(&v1, 0xaa, sizeof(v1));   // garbage after the old fields
            v1 = (struct credit_construct){ .version = 1, .name = "old", .coins_off = b + 0x10, .cpc_off = b + 0x20, .cpc_size = 1 };
            memset((char*)&v1 + offsetof(struct credit_construct, part_base), 0xaa, sizeof(v1) - offsetof(struct credit_construct, part_base));
            con_set = 0;
            report_credits(&v1);
            check("a version 1 construct is RAW, no max_cpc", con_set && con.part_off == 0 && con.max_cpc == 0 && con.coins_off == b + 0x10);
        }
        munmap(m, 0x4000);
    }
    unlink(tmp);
    snprintf(path, sizeof(path), "%s.prev", tmp);
    unlink(path);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
