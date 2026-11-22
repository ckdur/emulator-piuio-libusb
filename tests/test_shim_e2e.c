// The built PIUIO shim (libusb-1.0.so itself, with its poll thread and its
// network pad port), driven like a game drives it, with no IO board at all:
// the presses come in through the pad port (libusb/twitch.c), the way the
// profile web presses. tests/test_piuio_input.c covers the real boards with
// synthetic ones; this one checks that the library as shipped does the same.
//
// 32-bit container (ckdur/linux32-ckdur), after `make` in this folder:
//
//   gcc -std=gnu99 -Wall -I libusb -o /tmp/test_shim_e2e tests/test_shim_e2e.c
//       -L libusb/.libs -lusb-1.0 -lpthread -ldl -lrt -lm                 (one line)
//   LD_LIBRARY_PATH=libusb/.libs /tmp/test_shim_e2e
//
//  - Prime JE's option (_PIUIOEMU_EMU_PIUIO_BUTTON): pads press the emulated
//    button board, read with JE's own request (IMPER-114)
//  - an LXIO game: `f` presses UL alone, `b` (a board's red) UL+UR (IMPER-113)
//  - Infinity's option (_PIUIOEMU_SWAP_TEST_SERVICE): the game reads TEST as
//    SERVICE; the 5 s hold that ends the game is still the TEST button (IMPER-115)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "libusb.h"

// keyboards.c refers to it; a game brings libX11
unsigned long XLookupKeysym(void *ev, int i);
unsigned long XLookupKeysym(void *ev, int i) { (void)ev; (void)i; return 0; }

static int fails = 0;
static void check(const char *what, int ok) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
    if(!ok) fails++;
}

static void ms(int n) { struct timespec ts = {n / 1000, (n % 1000) * 1000000L}; nanosleep(&ts, NULL); }

static int port;
static int pad_sock = -1;
static void pads(const char *line) {
    if(pad_sock < 0) {
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        pad_sock = socket(AF_INET, SOCK_STREAM, 0);
        if(connect(pad_sock, (struct sockaddr *)&a, sizeof(a)) != 0) { perror("pad port"); exit(3); }
        ms(50);
    }
    send(pad_sock, line, strlen(line), 0);
    ms(60);     // the poll thread takes it
}

static libusb_context *ctx;
static libusb_device_handle *open_dev(uint16_t vid, uint16_t pid, uint16_t pid2) {
    libusb_device **list;
    libusb_device_handle *h = NULL;
    ssize_t n = libusb_get_device_list(ctx, &list);
    for(ssize_t i = 0; i < n && !h; i++) {
        struct libusb_device_descriptor d;
        if(libusb_get_device_descriptor(list[i], &d) != 0) continue;
        if(d.idVendor == vid && (d.idProduct == pid || d.idProduct == pid2))
            if(libusb_open(list[i], &h) != 0) h = NULL;
    }
    libusb_free_device_list(list, 1);
    return h;
}

static void start(void) {
    char p[16];
    snprintf(p, sizeof(p), "%d", port);
    setenv("_PIUIOEMU_TWITCH_PORT", p, 1);
    libusb_init(&ctx);
    ms(100);
}

static uint8_t board(libusb_device_handle *bb) {
    uint8_t in[8];
    memset(in, 0, 8);
    libusb_control_transfer(bb, 0xC0, 0xAE, 0, 0, in, 8, 10000);    // JE: usb_control_msg(0xC0, 0xAE, ...)
    return in[0];
}

static uint8_t piuio_sys(libusb_device_handle *io) {
    uint8_t out[8] = {0}, in[8];
    memset(in, 0, 8);
    libusb_control_transfer(io, 0x40, 0xAE, 0, 0, out, 8, 10000);
    libusb_control_transfer(io, 0xC0, 0xAE, 0, 0, in, 8, 10000);
    return in[1];
}

static int t_je(void) {
    setenv("_PIUIOEMU_EMU_PIUIO_BUTTON", "1", 1);
    start();
    libusb_device_handle *bb = open_dev(0x0D2F, 0x1010, 0x1010);
    check("JE: the button board 0d2f:1010 is there and opens", bb != NULL);
    if(!bb) return fails;
    check("JE: set_configuration and claim_interface succeed",
        libusb_set_configuration(bb, 1) == 0 && libusb_claim_interface(bb, 0) == 0);
    check("JE: nothing pressed", board(bb) == 0xFF);
    pads("d FFFFFFFB\n");
    check("JE: P1 centre pad presses green", board(bb) == 0xF7);
    pads("d FFFEFFFF\n");
    check("JE: P2 up-left pad presses red P2", board(bb) == 0xEF);
    pads("d FFFFFFFF\ne FFFFEFFF FFFFFFFF\n");
    check("JE: one sensor of P1 down-right presses right", board(bb) == 0xFB);
    pads("e FFFFFFFF FFFFFFFF\nb FFFD\n");
    check("JE: a front button (left) through `b`", board(bb) == 0xFD);
    pads("b FFFF\nf FFFD\n");
    check("JE: front UR through `f` is red", board(bb) == 0xFE);
    pads("f FFFF\n");
    check("JE: released", board(bb) == 0xFF);
    pads("Q\n");
    return fails;
}

static int t_prime(void) {
    start();
    libusb_device_handle *bb = open_dev(0x0D2F, 0x1010, 0x1010);
    if(!bb) { check("prime: button board opens", 0); return fails; }
    pads("d FFFFFFFB\n");
    check("without the option a pad does not press a button", board(bb) == 0xFF);
    pads("d FFFFFFFF\nb FFF7\n");
    check("without the option a front button still does", board(bb) == 0xF7);
    pads("Q\n");
    return fails;
}

static int t_lx(void) {
    setenv("_PIUIOEMU_WITH_PIULXIO", "1", 1);
    start();
    libusb_device_handle *lx = open_dev(0x0D2F, 0x1020, 0x1040);
    check("LXIO game: the LXIO is there and opens", lx != NULL);
    if(!lx) return fails;
    uint8_t in[16];
    int n = 0;
    pads("f FFFE\n");
    libusb_interrupt_transfer(lx, 0x81, in, 16, &n, 1000);
    check("LXIO game: `f` UL is UL alone", n == 16 && in[10] == 0xFE && in[11] == 0xFF);
    pads("f FDFF\n");
    libusb_interrupt_transfer(lx, 0x81, in, 16, &n, 1000);
    check("LXIO game: `f` UR of P2 is UR of P2 alone", in[10] == 0xFF && in[11] == 0xFD);
    pads("f FFFF\nb FFFE\n");
    libusb_interrupt_transfer(lx, 0x81, in, 16, &n, 1000);
    check("LXIO game: a board's red (`b`) is UL+UR", in[10] == 0xFC && in[11] == 0xFF);
    pads("b FFFF\n");
    libusb_interrupt_transfer(lx, 0x81, in, 16, &n, 1000);
    uint8_t ff[16];
    memset(ff, 0xFF, 16);
    check("LXIO game: released, the whole report is FF", memcmp(in, ff, 16) == 0);
    pads("Q\n");
    return fails;
}

static int t_swap(void) {
    setenv("_PIUIOEMU_SWAP_TEST_SERVICE", "1", 1);
    start();
    libusb_device_handle *io = open_dev(0x0547, 0x1002, 0x1002);
    if(!io) { check("Infinity: PIUIO opens", 0); return fails; }
    pads("d FFFFFDFF\n");   // TEST = bit 9
    check("Infinity: TEST button is read as SERVICE", piuio_sys(io) == 0xBF);
    pads("d FFFFBFFF\n");   // SERVICE = bit 14
    check("Infinity: SERVICE button is read as TEST", piuio_sys(io) == 0xFD);
    // ... and held for 7 s it does not end the game, though the game reads TEST
    for(int i = 0; i < 70; i++) { ms(100); piuio_sys(io); }
    check("Infinity: SERVICE held 7 s does not end the game", 1);
    pads("d FFFFFFFF\n");
    return fails;       // still alive: the caller sees this exit code
}

static int t_swap_hold(void) {
    setenv("_PIUIOEMU_SWAP_TEST_SERVICE", "1", 1);
    start();
    pads("d FFFFFDFF\n");   // the TEST button, held: the shim ends the process with exit(0)
    ms(9000);
    return 99;              // not reached when the hold works
}

static int t_noswap(void) {
    start();
    libusb_device_handle *io = open_dev(0x0547, 0x1002, 0x1002);
    if(!io) { check("PIUIO opens", 0); return fails; }
    pads("d FFFFFDFF\n");
    check("every other game: TEST is TEST", piuio_sys(io) == 0xFD);
    pads("d FFFFFFFF\nQ\n");
    return fails;
}

static int run(const char *name, int (*fn)(void), int *code) {
    static int n = 0;
    printf("-- %s\n", name);
    fflush(stdout);
    port = 21000 + (getpid() % 20000) + n++;
    pid_t pid = fork();
    if(pid == 0) {
        unsetenv("_PIUIOEMU_WITH_PIULXIO");
        unsetenv("_PIUIOEMU_EMU_PIUIO_BUTTON");
        unsetenv("_PIUIOEMU_SWAP_TEST_SERVICE");
        int f = fn();
        fflush(stdout);
        _exit(f > 250 ? 250 : f);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if(!WIFEXITED(st)) { printf("FAIL: %s crashed (status %x)\n", name, st); return 1; }
    if(code) { *code = WEXITSTATUS(st); return 0; }
    return WEXITSTATUS(st);
}

int main(void) {
    int f = 0, code = -1;
    f += run("Prime JE: pads as button board", t_je, NULL);
    f += run("no option: pads are not buttons", t_prime, NULL);
    f += run("LXIO game: front buttons", t_lx, NULL);
    f += run("Infinity: TEST <-> SERVICE", t_swap, NULL);
    run("Infinity: the TEST button held ends the game", t_swap_hold, &code);
    printf("%s: Infinity: TEST button held 5 s ends the game (exit code %d)\n", code == 0 ? "ok  " : "FAIL", code);
    if(code != 0) f++;
    f += run("no option: TEST is TEST", t_noswap, NULL);
    printf(f ? "\n%d check(s) FAILED\n" : "\nall passed\n", f);
    return f ? 1 : 0;
}
