// Host test of the two-copy credits file (credits_logic.h, "Two copies..."): a power cut at
// any step of a save leaves a usable file.
//   gcc -std=gnu17 -Wall -Wextra -I libusb -o /tmp/test_credits_ab tests/test_credits_ab.c && /tmp/test_credits_ab
#include <stdio.h>
#include <sys/stat.h>
#include "credits_logic.h"

static int fails = 0;
static void check(const char *what, int ok) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if(!ok) fails++; }

static void put(const char *path, const char *txt) { FILE *f = fopen(path, "wb"); if(f) { fputs(txt, f); fclose(f); } }
static void rm(const char *path) { unlink(path); }
static void cp(const char *from, const char *to) {
    char b[600]; FILE *f = fopen(from, "rb"); size_t n; if(!f) return;
    n = fread(b, 1, sizeof(b) - 1, f); fclose(f); b[n] = 0; put(to, b);
}
static int load_credits(const char *path) { struct credits_pool p; credits_pool_load_files(path, &p); return p.credits; }
static unsigned load_gen(const char *path) { struct credits_pool p; credits_pool_load_files(path, &p); return p.gen; }
static void save(const char *path, int credits, int coins) {
    struct credits_pool p; credits_pool_load_files(path, &p);
    p.credits = credits; p.coins = coins; credits_pool_save_files(path, &p);
}

int main(void) {
    char dir[] = "/tmp/credits_ab_XXXXXX", f[300], pv[300], tm[300], buf[256];
    struct credits_pool p, q;

    check("tmp dir", mkdtemp(dir) != NULL);
    snprintf(f, sizeof(f), "%s/credits.ini", dir);
    snprintf(pv, sizeof(pv), "%s/credits.ini.prev", dir);
    snprintf(tm, sizeof(tm), "%s/credits.ini.tmp", dir);

    // ---- format and parse
    credits_pool_defaults(&p); p.credits = 12; p.coins = 2; p.gen = 7;
    check("format fits", credits_pool_format_ck(&p, buf, sizeof(buf)) > 0);
    check("checked text parses (0)", credits_pool_parse_ck(buf, &q) == 0 && q.credits == 12 && q.coins == 2 && q.gen == 7);
    { char bad[256]; snprintf(bad, sizeof(bad), "%s", buf); bad[strstr(bad, "credits=12") - bad + 8] = '9';
      check("one changed digit is damaged (1)", credits_pool_parse_ck(bad, &q) == 1); }
    { char cut[256]; snprintf(cut, sizeof(cut), "%.30s", buf);
      check("a cut text is damaged (1)", credits_pool_parse_ck(cut, &q) == 1); }
    { char cut2[256]; snprintf(cut2, sizeof(cut2), "%.*s", (int)(strstr(buf, "check=") - buf), buf);
      check("a text cut right before check= is damaged (1)", credits_pool_parse_ck(cut2, &q) == 1); }
    check("garbage is damaged (1)", credits_pool_parse_ck("\x01\x02garbage\n", &q) == 1);
    check("nothing is 2", credits_pool_parse_ck("", &q) == 2 && credits_pool_parse_ck(NULL, &q) == 2);
    check("an older file (no check) is usable (3)", credits_pool_parse_ck("[credits]\ncredits=5\ncoins=1\n[options]\nenabled=0\n", &q) == 3 && q.credits == 5 && q.enabled == 0);

    // ---- choose
    { char a[256], b[256]; struct credits_pool pa, pb;
      credits_pool_defaults(&pa); pa.credits = 3; pa.gen = 5; credits_pool_format_ck(&pa, a, sizeof(a));
      credits_pool_defaults(&pb); pb.credits = 8; pb.gen = 4; credits_pool_format_ck(&pb, b, sizeof(b));
      check("higher generation wins", credits_pool_choose(a, b, &q) == 0 && q.credits == 3);
      check("order does not matter", credits_pool_choose(b, a, &q) == 1 && q.credits == 3);
      check("a damaged copy loses to the valid one", credits_pool_choose(CREDITS_MARK "credits=99\ncheck=00000000\n", b, &q) == 1 && q.credits == 8);
      check("both damaged: defaults", credits_pool_choose(CREDITS_MARK "credits=99\ncheck=00000000\n", CREDITS_MARK "coins=9\ncheck=00000001\n", &q) == -1 && q.credits == 0 && q.enabled == 1);
      check("a hand-edited file (no check) beats the checked copy", credits_pool_choose("credits=1\nenabled=0\n", a, &q) == 0 && q.enabled == 0 && q.credits == 1);
    }

    // ---- files: saves and power cuts
    check("no file: defaults", load_credits(f) == 0);
    save(f, 3, 0); check("first save", load_credits(f) == 3 && load_gen(f) == 1);
    save(f, 4, 1); save(f, 5, 0);
    check("third save is generation 3", load_credits(f) == 5 && load_gen(f) == 3);
    { struct stat st; check("the previous generation is kept", stat(pv, &st) == 0); }

    // cut after the new copy was written but before the rename: a leftover .tmp is ignored
    put(tm, "[credits]\ncredits=77\n"); check("a leftover .tmp is ignored", load_credits(f) == 5); rm(tm);
    // cut between the two renames: the main file is missing, .prev is the one before it
    cp(f, "/tmp/credits_ab_keep"); rm(f);
    check("main missing: the previous generation (4)", load_credits(f) == 4);
    // cut while the disk wrote the main file: half of it
    cp("/tmp/credits_ab_keep", f);
    { char half[300]; FILE *h = fopen(f, "rb"); size_t n = h ? fread(half, 1, sizeof(half) - 1, h) : 0; if(h) fclose(h); half[n / 2] = 0; put(f, half); }
    check("a half-written main: the previous generation", load_credits(f) == 4);
    // garbage in the main file
    put(f, "\x01\x02garbage\n"); check("garbage main: the previous generation", load_credits(f) == 4);
    // both gone
    rm(f); rm(pv); check("both gone: defaults", load_credits(f) == 0);
    // saving again after a loss starts a valid pair
    save(f, 2, 0); save(f, 1, 0); check("a new pair after a loss", load_credits(f) == 1 && load_gen(f) == 2);
    rm(f); rm(pv); rm(tm); rm("/tmp/credits_ab_keep"); rmdir(dir);

    if(fails) { printf("%d FAILED\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
