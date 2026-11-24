// Host test of libusb/keyboards.c without keyboard mode (init_profile_keys):
// a keyboard is read for F9 / F10 only, every other key (pads, buttons) is
// left alone, no keyboard breaks nothing, and keyboard mode is unchanged.
// The evdev state comes from an ioctl() of this file (it overrides libc's in
// this program), the devices list from a folder of the test's own.
//
//   gcc -std=gnu17 -Wall -I libusb -DKEYBOARDS_PROC_DEVICES='"/tmp/tkp/devices"'
//       -DKEYBOARDS_DEV_INPUT='"/tmp/tkp"' -o /tmp/test_keyboards_profile
//       tests/test_keyboards_profile.c libusb/keyboards.c -lX11               (one line)
//   /tmp/test_keyboards_profile
#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/input.h>
#include "keyboards.h"

unsigned char bytes_p[4] = {0xFF, 0xFF, 0xFF, 0xFF};
unsigned char bytes_pb[2] = {0xFF, 0xFF};
unsigned char bytes_pf[2] = {0xFF, 0xFF};

#define DIR_ "/tmp/tkp"
static unsigned char held[KEY_MAX / 8 + 1];
static int ioctls;

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    (void)fd;
    if(_IOC_TYPE(req) == 'E' && _IOC_NR(req) == 0x18) {     // EVIOCGKEY(len)
        ioctls++;
        size_t n = _IOC_SIZE(req);
        memcpy(arg, held, n < sizeof(held) ? n : sizeof(held));
        return (int)n;
    }
    errno = ENOTTY;
    return -1;
}

static void press(int key, int on)
{
    if(on) held[key / 8] |= 1 << (key % 8); else held[key / 8] &= ~(1 << (key % 8));
}

static int fails;
static void check(const char *what, int ok)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) fails++;
}

static void write_devices(int with_keyboard)
{
    mkdir(DIR_, 0755);
    FILE *f = fopen(DIR_ "/devices", "w");
    if(with_keyboard)
        fputs("I: Bus=0011 Vendor=0001 Product=0001 Version=ab41\n"
              "N: Name=\"AT Translated Set 2 keyboard\"\n"
              "H: Handlers=sysrq kbd event7\n"
              "B: EV=120013\n\n", f);
    fclose(f);
    fclose(fopen(DIR_ "/event7", "w"));
}

static int all_ff(void)
{
    for(int i = 0; i < 4; i++) if(bytes_p[i] != 0xFF) return 0;
    return bytes_pb[0] == 0xFF && bytes_pb[1] == 0xFF && bytes_pf[0] == 0xFF && bytes_pf[1] == 0xFF;
}

static int fifo_line(int fd, char *buf, size_t n)
{
    memset(buf, 0, n);
    ssize_t r = read(fd, buf, n - 1);
    return r < 0 ? 0 : (int)r;
}

static void step(void) { usleep(30000); poll_keyboards(); }   // past the 25 ms poll interval

int main(void)
{
    char b[32];
    setenv("_SAVEGEN_FIFO", DIR_ "/sg.fifo", 1);
    setenv("_AMPASS_FIFO", DIR_ "/am.fifo", 1);

    // 1. no keyboard mode, a keyboard is there
    pid_t pid = fork();
    if(pid == 0) {
        write_devices(1);
        unlink(DIR_ "/sg.fifo"); unlink(DIR_ "/am.fifo");
        mkfifo(DIR_ "/sg.fifo", 0600); mkfifo(DIR_ "/am.fifo", 0600);
        int sg = open(DIR_ "/sg.fifo", O_RDWR | O_NONBLOCK), am = open(DIR_ "/am.fifo", O_RDWR | O_NONBLOCK);
        init_profile_keys();
        press(KEY_Q, 1); press(KEY_ENTER, 1); press(KEY_DELETE, 1); press(KEY_F5, 1);
        step();
        check("pads / buttons on the keyboard: nothing pressed", all_ff());
        check("  and no fifo line", fifo_line(sg, b, sizeof(b)) == 0);
        press(KEY_F9, 1);
        step();
        check("F9: \"1 @\" to savegen", fifo_line(sg, b, sizeof(b)) == 4 && !strcmp(b, "1 @\n"));
        check("F9: \"1 @\" to ampass", fifo_line(am, b, sizeof(b)) == 4 && !strcmp(b, "1 @\n"));
        check("  pads still untouched (F9 is no pad bit here)", all_ff());
        step();
        check("F9 held: one press only", fifo_line(sg, b, sizeof(b)) == 0);
        press(KEY_F9, 0); press(KEY_F10, 1);
        step();
        check("F10: \"2 @\"", fifo_line(sg, b, sizeof(b)) == 4 && !strcmp(b, "2 @\n"));
        int before = ioctls;
        for(int i = 0; i < 100; i++) poll_keyboards();      // the poll thread runs every ~0.5 ms
        check("rate limited: at most one read in 100 back-to-back polls", ioctls - before <= 1);
        fflush(stdout);
        _exit(fails);
    }
    int stv;
    waitpid(pid, &stv, 0);
    fails += WEXITSTATUS(stv);

    // 2. no keyboard mode, no keyboard at all; one plugged in later
    pid = fork();
    if(pid == 0) {
        write_devices(0);
        unlink(DIR_ "/sg.fifo");
        mkfifo(DIR_ "/sg.fifo", 0600);
        int sg = open(DIR_ "/sg.fifo", O_RDWR | O_NONBLOCK);
        init_profile_keys();
        press(KEY_F9, 1);
        step();
        check("no keyboard: nothing, no crash", all_ff() && fifo_line(sg, b, sizeof(b)) == 0);
        int before = ioctls;
        for(int i = 0; i < 50; i++) poll_keyboards();
        check("no keyboard: no device read at all", ioctls == before);
        fflush(stdout);
        _exit(fails);
    }
    waitpid(pid, &stv, 0);
    fails += WEXITSTATUS(stv);

    // 3. keyboard mode as before: the keyboard is the PIUIO, F9 too
    pid = fork();
    if(pid == 0) {
        write_devices(1);
        unlink(DIR_ "/sg.fifo");
        mkfifo(DIR_ "/sg.fifo", 0600);
        int sg = open(DIR_ "/sg.fifo", O_RDWR | O_NONBLOCK);
        init_keyboards();
        press(KEY_Q, 1); press(KEY_F9, 1);
        poll_keyboards();
        check("keyboard mode: Q is P1 UL", (bytes_p[0] & 0x01) == 0);
        check("keyboard mode: F9 still sends \"1 @\"", fifo_line(sg, b, sizeof(b)) == 4 && !strcmp(b, "1 @\n"));
        fflush(stdout);
        _exit(fails);
    }
    waitpid(pid, &stv, 0);
    fails += WEXITSTATUS(stv);

    unlink(DIR_ "/sg.fifo"); unlink(DIR_ "/am.fifo"); unlink(DIR_ "/devices"); unlink(DIR_ "/event7");
    rmdir(DIR_);
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
