// Host test of libusb/profile_keys.h: F9 / F10 -> fifo line "1 @" / "2 @",
// one line per press, none while held, and no write without a reader.
//
//   gcc -std=gnu17 -Wall -Wextra -I libusb -o /tmp/test_profile_keys tests/test_profile_keys.c && /tmp/test_profile_keys

#include <stdio.h>
#include <sys/stat.h>
#include "profile_keys.h"

static int fails = 0;
static void check(const char *what, int ok) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) fails++;
}

int main(void) {
    struct profile_keys st = {{0, 0}};
    const char *l[2];
    check("idle: nothing", profile_keys_step(&st, 0, 0, l) == 0);
    check("F9 press: one line", profile_keys_step(&st, 1, 0, l) == 1 && !strcmp(l[0], "1 @"));
    check("F9 held: nothing", profile_keys_step(&st, 1, 0, l) == 0);
    check("F9 released: nothing", profile_keys_step(&st, 0, 0, l) == 0);
    check("F9 again: a new line (the toggle is savegen's)", profile_keys_step(&st, 1, 0, l) == 1);
    check("F10 while F9 held: only P2", profile_keys_step(&st, 1, 1, l) == 1 && !strcmp(l[0], "2 @"));
    profile_keys_step(&st, 0, 0, l);
    check("both at once: two lines, P1 first", profile_keys_step(&st, 1, 1, l) == 2 && !strcmp(l[0], "1 @") && !strcmp(l[1], "2 @"));

    char path[] = "/tmp/pk_test_fifo";
    unlink(path);
    setenv("_SAVEGEN_FIFO", path, 1);
    check("no fifo: send fails, creates nothing", profile_keys_send("1 @") != 0 && access(path, F_OK) != 0);
    mkfifo(path, 0600);
    check("fifo without reader: send fails (ENXIO), no block", profile_keys_send("1 @") != 0);
    int rd = open(path, O_RDWR | O_NONBLOCK);
    check("fifo with reader: send ok", profile_keys_send("2 @") == 0);
    char b[16] = {0};
    check("reader gets the line", read(rd, b, sizeof(b) - 1) == 4 && !strcmp(b, "2 @\n"));
    close(rd);
    unlink(path);
    // ampass.so's fifo gets the same line ("@": its key toggle, ampass.c)
    char ap[] = "/tmp/pk_test_ampass";
    unlink(ap);
    setenv("_AMPASS_FIFO", ap, 1);
    mkfifo(ap, 0600);
    rd = open(ap, O_RDWR | O_NONBLOCK);
    check("only ampass listens: send ok", profile_keys_send("2 @") == 0);
    memset(b, 0, sizeof(b));
    check("ampass fifo gets the same line", read(rd, b, sizeof(b) - 1) == 4 && !strcmp(b, "2 @\n"));
    check("savegen fifo is not created by the send", access(path, F_OK) != 0);
    close(rd);
    unlink(ap);
    check("nobody listens: send fails", profile_keys_send("1 @") != 0);
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
