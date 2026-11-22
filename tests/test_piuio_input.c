// Host test for the input side of the PIUIO shim: libusb/piuio.c (reading the
// real IOs), libusb/piuio_emu.c (what the emulated PIUIO / LXIO / button board
// answer the game) and libusb/piuio_input.h (the mapping between them).
//
// The real shim code is compiled in. What is faked is below it: the "true"
// libusb calls, which here talk to a synthetic PIUIO (0547:1002), button board
// (0d2f:1010) and LXIO (0d2f:1020), and the keyboard / network pads, whose
// byte arrays the test writes directly. The game side goes through the same
// exported libusb calls a game (or libusb-compat, or hidapi) uses.
//
//   gcc -std=gnu17 -Wall -DPIUIO_EMU_NO_POLL_THREAD -I . -I libusb -o /tmp/test_piuio_input
//       tests/test_piuio_input.c libusb/piuio.c libusb/piuio_emu.c -lpthread     (one line)
//   /tmp/test_piuio_input
//
// Covers IMPER-113 (LXIO bypass, UL / UR apart), IMPER-114 (Prime JE button
// board: real board, real LXIO, keyboard only), IMPER-115 (TEST <-> SERVICE
// for Infinity) and the failed-read part of IMPER-116.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "libusbi.h"
#include "piuio_emu.h"
#include "piuio.h"
#include "keyboards.h"
#include "twitch.h"
#include "autoplay.h"
#include "piuio_testhold.h"
#include "piuio_input.h"

enum piuio_testhold_result piuioemu_poll_step(time_t now);
extern unsigned char bytes_p[4], bytes_pb[2], bytes_pf[2];
extern unsigned char bytes_t[4], bytes_tb[2], bytes_tf[2], bytes_ts[8];

static int fails = 0;
static void check(const char *what, int ok) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) fails++;
}

// ---------------------------------------------------------------- fake world

// What is plugged in
static int have_piuio, have_board, have_lxio;
// What the synthetic devices say (active low)
static uint8_t sim_pad[2][4] = {{0xFF,0xFF,0xFF,0xFF},{0xFF,0xFF,0xFF,0xFF}}; // [player][sensor]
static uint8_t sim_sys[2] = {0xFF, 0xFF};      // coins, TEST, SERVICE (P1 word, P2 word)
static uint8_t sim_board[2] = {0xFF, 0xFF};
static uint8_t sim_lx[16];
// What the game side made them receive
static uint8_t got_piuio_out[8], got_board_out[8], got_lx_out[16];
static int n_board_out, n_lx_out;
static int sim_mux[2];
// Failures: 0 = none, otherwise the libusb error every read returns
static int fail_piuio, fail_board, fail_lxio;
static int fail_piuio_mux = -1;                 // only this sensor group fails

static struct libusb_device *real_dev[3];
static int n_real;

static struct libusb_device *mkdev(uint16_t vid, uint16_t pid, uint8_t addr) {
    struct libusb_device *d = calloc(1, sizeof(*d));
    d->bus_number = 1;
    d->device_address = addr;
    d->refcnt = 1;
    d->device_descriptor.bLength = 18;
    d->device_descriptor.bDescriptorType = LIBUSB_DT_DEVICE;
    d->device_descriptor.idVendor = vid;
    d->device_descriptor.idProduct = pid;
    return d;
}

static void plug(int piuio, int board, int lxio) {
    have_piuio = piuio; have_board = board; have_lxio = lxio;
    n_real = 0;
    memset(sim_lx, 0xFF, sizeof(sim_lx));
    if(piuio) real_dev[n_real++] = mkdev(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID, 10);
    if(board) real_dev[n_real++] = mkdev(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID, 11);
    if(lxio)  real_dev[n_real++] = mkdev(PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID, 12);
}

static int is(struct libusb_device *d, uint16_t vid, uint16_t pid) {
    return d->device_descriptor.idVendor == vid && d->device_descriptor.idProduct == pid;
}

int true_libusb_init_context(libusb_context **ctx, const struct libusb_init_option options[], int num_options) {
    (void)options; (void)num_options;
    if(ctx) *ctx = calloc(1, sizeof(**ctx));
    return 0;
}
void true_libusb_exit(libusb_context *ctx) { (void)ctx; }
ssize_t true_libusb_get_device_list(libusb_context *ctx, libusb_device ***list) {
    (void)ctx;
    libusb_device **l = calloc(n_real + 1, sizeof(*l));
    for(int i = 0; i < n_real; i++) l[i] = real_dev[i];
    *list = l;
    return n_real;
}
void true_libusb_free_device_list(libusb_device **list, int unref_devices) { (void)unref_devices; free(list); }
int true_libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc) {
    memcpy(desc, &dev->device_descriptor, sizeof(*desc));
    return 0;
}
int true_libusb_get_config_descriptor(libusb_device *dev, uint8_t i, struct libusb_config_descriptor **c) { (void)dev; (void)i; (void)c; return LIBUSB_ERROR_NOT_FOUND; }
int true_libusb_get_active_config_descriptor(libusb_device *dev, struct libusb_config_descriptor **c) { (void)dev; (void)c; return LIBUSB_ERROR_NOT_FOUND; }
void true_libusb_free_config_descriptor(struct libusb_config_descriptor *c) { (void)c; }
int true_libusb_open(libusb_device *dev, libusb_device_handle **h) {
    *h = calloc(1, sizeof(**h) + 64);
    (*h)->dev = dev;
    return 0;
}
void true_libusb_close(libusb_device_handle *h) { free(h); }
int true_libusb_kernel_driver_active(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int true_libusb_detach_kernel_driver(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int true_libusb_claim_interface(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int true_libusb_release_interface(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int true_libusb_set_interface_alt_setting(libusb_device_handle *h, int i, int a) { (void)h; (void)i; (void)a; return 0; }
int true_libusb_get_configuration(libusb_device_handle *h, int *c) { (void)h; *c = 1; return 0; }
int true_libusb_set_configuration(libusb_device_handle *h, int c) { (void)h; (void)c; return 0; }
int true_libusb_submit_transfer(struct libusb_transfer *t) { (void)t; return LIBUSB_ERROR_NO_DEVICE; }
int true_libusb_cancel_transfer(struct libusb_transfer *t) { (void)t; return LIBUSB_ERROR_NOT_FOUND; }
int true_libusb_handle_events(libusb_context *ctx) { (void)ctx; return 0; }
int libusb_handle_events_timeout_completed(libusb_context *ctx, struct timeval *tv, int *completed) { (void)ctx; (void)tv; (void)completed; return 0; }
libusb_device *libusb_ref_device(libusb_device *dev) { dev->refcnt++; return dev; }
const char *libusb_error_name(int code) { (void)code; return "error"; }

int true_libusb_control_transfer(libusb_device_handle *h, uint8_t type, uint8_t req,
    uint16_t value, uint16_t index, unsigned char *data, uint16_t len, unsigned int timeout) {
    (void)value; (void)index; (void)timeout;
    struct libusb_device *d = h->dev;
    if(req != PIUIO_CTL_REQ || len != 8) return LIBUSB_ERROR_PIPE;
    if(is(d, PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID)) {
        if(type == 0x40) {
            memcpy(got_piuio_out, data, 8);
            sim_mux[0] = data[0] & 3;
            sim_mux[1] = data[2] & 3;
            return fail_piuio ? fail_piuio : 8;
        }
        if(type == 0xC0) {
            if(fail_piuio) return fail_piuio;
            if(fail_piuio_mux == sim_mux[0]) return LIBUSB_ERROR_TIMEOUT;
            memset(data, 0xFF, 8);
            data[0] = sim_pad[0][sim_mux[0]];
            data[1] = sim_sys[0];
            data[2] = sim_pad[1][sim_mux[1]];
            data[3] = sim_sys[1];
            return 8;
        }
    }
    if(is(d, PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID)) {
        if(type == 0x40) { memcpy(got_board_out, data, 8); n_board_out++; return 8; }
        if(type == 0xC0) {
            if(fail_board) return fail_board;
            memset(data, 0xFF, 8);
            data[0] = sim_board[0];
            data[1] = sim_board[1];
            return 8;
        }
    }
    return LIBUSB_ERROR_PIPE;
}

int true_libusb_interrupt_transfer(libusb_device_handle *h, unsigned char ep,
    unsigned char *data, int len, int *transferred, unsigned int timeout) {
    (void)timeout;
    struct libusb_device *d = h->dev;
    *transferred = 0;
    if(!is(d, PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID) || len != 16) return LIBUSB_ERROR_PIPE;
    if(ep == PIULXIO_ENDPOINT_OUT) { memcpy(got_lx_out, data, 16); n_lx_out++; *transferred = 16; return 0; }
    if(ep == (0x80 | PIULXIO_ENDPOINT_IN)) {
        if(fail_lxio) return fail_lxio;
        memcpy(data, sim_lx, 16);
        *transferred = 16;
        return 0;
    }
    return LIBUSB_ERROR_PIPE;
}

// The other parts of the shim, not under test here
void init_keyboards(void) {}
void poll_keyboards(void) {}
int XNextEventEMU(void *d, void *e) { (void)d; (void)e; return 0; }
void KeyHandler_Twitch_Init(void) {}
void KeyHandler_Twitch_Poll(void) {}
void KeyHandler_Twitch_Exit(void) {}
void check_autoplay(void) {}
void update_autoplay(void) {}
void credits_poll(int is_menu) { (void)is_menu; }
void freeplay_attract_poll(int is_menu) { (void)is_menu; }
void credits_flush(void) {}

// ----------------------------------------------------------------- game side

static libusb_context *ctx;

static void start(void) {
    libusb_init_context(&ctx, NULL, 0);
    piuioemu_poll_step(1000);
}

// The first device with this id, opened: what a game does
static libusb_device_handle *game_open(uint16_t vid, uint16_t pid) {
    libusb_device **list;
    libusb_device_handle *h = NULL;
    ssize_t n = libusb_get_device_list(ctx, &list);
    for(ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor desc;
        if(libusb_get_device_descriptor(list[i], &desc) != 0 || desc.idVendor != vid) continue;
        // The emulated LXIO answers as 1020 in both LXIO modes (the games try both ids)
        int lx = vid == PIULXIO_VENDOR_ID && (pid == PIULXIO_PRODUCT_ID || pid == PIULXIO_PRODUCT_ID_2);
        if(desc.idProduct == pid || (lx && (desc.idProduct == PIULXIO_PRODUCT_ID || desc.idProduct == PIULXIO_PRODUCT_ID_2))) {
            if(libusb_open(list[i], &h) != 0) h = NULL;
            break;
        }
    }
    libusb_free_device_list(list, 0);
    return h;
}

// One PIUIO round as every legacy game does it: lamps + sensor group out, state in
static uint8_t g_lamp[4];
static void game_piuio(libusb_device_handle *h, int mux, uint8_t in[8]) {
    uint8_t out[8] = {0};
    out[0] = (g_lamp[0] & 0xFC) | mux; out[1] = g_lamp[1];
    out[2] = (g_lamp[2] & 0xFC) | mux; out[3] = g_lamp[3];
    libusb_control_transfer(h, 0x40, PIUIO_CTL_REQ, 0, 0, out, 8, 10000);
    memset(in, 0, 8);
    libusb_control_transfer(h, 0xC0, PIUIO_CTL_REQ, 0, 0, in, 8, 10000);
}
// The four groups: pads[0..3] P1, [4..7] P2; returns byte 1 (coins...) of the last
static uint8_t game_piuio_all(libusb_device_handle *h, uint8_t pads[8], uint8_t *sys2) {
    uint8_t in[8] = {0};
    for(int m = 0; m < 4; m++) {
        game_piuio(h, m, in);
        pads[m] = in[0];
        pads[4+m] = in[2];
    }
    if(sys2) *sys2 = in[3];
    return in[1];
}
// Prime JE (and prime): usb_control_msg(0xC0, 0xAE, 0, 0, buf, 8)
static uint8_t game_board(libusb_device_handle *h) {
    uint8_t in[8] = {0};
    libusb_control_transfer(h, 0xC0, PIUIO_CTL_REQ, 0, 0, in, 8, 10000);
    return in[0];
}
// The menu (menugl/usbio.c): an interrupt read on endpoint 2
static uint8_t menu_board(libusb_device_handle *h) {
    uint8_t in[16] = {0};
    int n = 0;
    libusb_interrupt_transfer(h, 0x02, in, 16, &n, 1000);
    return in[0];
}
static void game_lx(libusb_device_handle *h, uint8_t in[16]) {
    int n = 0;
    memset(in, 0, 16);
    libusb_interrupt_transfer(h, 0x80 | PIULXIO_ENDPOINT_IN, in, 16, &n, 1000);
}
static void game_lx_out(libusb_device_handle *h, const uint8_t out[16]) {
    uint8_t o[16];
    int n = 0;
    memcpy(o, out, 16);
    libusb_interrupt_transfer(h, PIULXIO_ENDPOINT_OUT, o, 16, &n, 1000);
}

static void step(void) { piuioemu_poll_step(1000); }

// The way the LXIO games really read (hidapi): an interrupt IN transfer is
// always pending, and completes when the game writes its output report
static int async_done;
static void LIBUSB_CALL async_cb(struct libusb_transfer *t) { (void)t; async_done++; }
static int game_lx_async(libusb_device_handle *h, uint8_t in[16]) {
    struct libusb_transfer *t = calloc(1, sizeof(*t));
    uint8_t out[16] = {0};
    memset(in, 0, 16);
    t->dev_handle = h;
    t->type = LIBUSB_TRANSFER_TYPE_INTERRUPT;
    t->endpoint = 0x80 | PIULXIO_ENDPOINT_IN;
    t->buffer = in;
    t->length = 16;
    t->callback = async_cb;
    async_done = 0;
    if(libusb_submit_transfer(t) != 0) return 0;
    game_lx_out(h, out);
    for(int i = 0; i < 50 && !async_done; i++) libusb_handle_events(ctx);
    int ok = async_done == 1 && t->status == LIBUSB_TRANSFER_COMPLETED && t->actual_length == 16;
    if(ok) free(t);
    return ok;
}

static void release_all(void) {
    memset(sim_pad, 0xFF, sizeof(sim_pad));
    sim_sys[0] = sim_sys[1] = 0xFF;
    sim_board[0] = sim_board[1] = 0xFF;
    memset(sim_lx, 0xFF, sizeof(sim_lx));
    memset(bytes_p, 0xFF, 4); memset(bytes_pb, 0xFF, 2); memset(bytes_pf, 0xFF, 2);
    memset(bytes_t, 0xFF, 4); memset(bytes_tb, 0xFF, 2); memset(bytes_tf, 0xFF, 2);
    memset(bytes_ts, 0xFF, 8);
    step();
}

// ------------------------------------------------------------- pure mapping

static int t_pure(void) {
    int ok = 1;
    for(int b = 0; b < 256; b++)
        if(piuio_lx_to_board(piuio_board_to_lx((uint8_t)b, 0), piuio_board_to_lx((uint8_t)b, 1)) != b) ok = 0;
    check("board -> LX -> board gives the board back, all 256 states", ok);

    check("board red P1 = UL+UR, nothing else", piuio_board_to_lx(0xFE, 0) == 0xFC && piuio_board_to_lx(0xFE, 1) == 0xFF);
    check("board left/right/green P1 = DL/DR/centre",
        piuio_board_to_lx(0xFD, 0) == 0xF7 && piuio_board_to_lx(0xFB, 0) == 0xEF && piuio_board_to_lx(0xF7, 0) == 0xFB);
    check("board red P2 = UL+UR of P2 only", piuio_board_to_lx(0xEF, 1) == 0xFC && piuio_board_to_lx(0xEF, 0) == 0xFF);
    check("LX UL alone = red, UR alone = red", piuio_lx_to_board(0xFE, 0xFF) == 0xFE && piuio_lx_to_board(0xFD, 0xFF) == 0xFE);
    check("LX P2 centre = green P2", piuio_lx_to_board(0xFF, 0xFB) == 0x7F);

    ok = 1;
    for(int x = 0; x < 256; x++) {
        uint8_t s = piuio_swap_test_service((uint8_t)x);
        if(piuio_swap_test_service(s) != x) ok = 0;
        if((s & ~(PIUIO_SYS_TEST | PIUIO_SYS_SERVICE)) != (x & ~(PIUIO_SYS_TEST | PIUIO_SYS_SERVICE))) ok = 0;
        if(!(s & PIUIO_SYS_TEST) != !(x & PIUIO_SYS_SERVICE)) ok = 0;
        if(!(s & PIUIO_SYS_SERVICE) != !(x & PIUIO_SYS_TEST)) ok = 0;
    }
    check("TEST <-> SERVICE swaps exactly those two bits, all 256 states", ok);

    check("failed read: timeout keeps, no device releases, short keeps, full is ok",
        piuio_read_result(-7, -7, 4) == PIUIO_READ_KEEP && piuio_read_result(-4, -4, 4) == PIUIO_READ_RELEASE &&
        piuio_read_result(2, 2, 4) == PIUIO_READ_KEEP && piuio_read_result(8, 8, 4) == PIUIO_READ_OK &&
        piuio_read_result(0, 0, 1) == PIUIO_READ_KEEP && piuio_read_result(0, 16, 1) == PIUIO_READ_OK);
    return fails;
}

// ----------------------------------------------------------------- IMPER-113

// A game that speaks LXIO, a real LXIO: pure bypass
static int t_lx_bypass(void) {
    setenv("_PIUIOEMU_WITH_PIULXIO", "2", 1);
    plug(0, 0, 1);
    start();
    libusb_device_handle *lx = game_open(PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID_2);
    check("LXIO game finds its (emulated) LXIO", lx != NULL);
    if(!lx) return fails;
    uint8_t in[16];

    int same = 1;
    srand(113);
    for(int n = 0; n < 5000; n++) {
        for(int i = 0; i < 16; i++) sim_lx[i] = (uint8_t)rand();
        sim_lx[8] |= PIUIO_SYS_TEST; // not the 5 s TEST hold
        step();
        game_lx(lx, in);
        if(memcmp(in, sim_lx, 16) != 0) same = 0;
    }
    check("real LXIO -> LXIO game: 5000 random reports arrive byte for byte", same);

    memset(sim_lx, 0xFF, 16); sim_lx[10] = 0xFE; step(); game_lx(lx, in);
    check("real LXIO UL alone stays UL alone (was UL+UR)", in[10] == 0xFE && in[11] == 0xFF);
    sim_lx[10] = 0xFD; sim_lx[11] = 0xFE; step(); game_lx(lx, in);
    check("real LXIO P1 UR alone, P2 UL alone", in[10] == 0xFD && in[11] == 0xFE);

    // UL then UR one after the other: the Basic / Full mode style of code
    int seq_ok = 1;
    const uint8_t seq[] = {0xFE, 0xFF, 0xFD, 0xFF, 0xFE, 0xFF, 0xFD, 0xFF};
    for(unsigned i = 0; i < sizeof(seq); i++) {
        sim_lx[10] = seq[i]; sim_lx[11] = 0xFF; step(); game_lx(lx, in);
        if(in[10] != seq[i]) seq_ok = 0;
    }
    check("UL, UR, UL, UR typed one at a time is read as typed", seq_ok);

    sim_lx[10] = 0xFD; sim_lx[11] = 0xFE; sim_lx[2] = 0xEF; step();
    check("the same through a pending IN transfer (hidapi's way)",
        game_lx_async(lx, in) && memcmp(in, sim_lx, 16) == 0);
    memset(sim_lx, 0xFF, 16); step();

    // Output: what the game writes is what the LXIO gets
    uint8_t out[16];
    for(int i = 0; i < 16; i++) out[i] = (uint8_t)(0xA3 + 17 * i);
    out[0] |= 0x03; out[2] |= 0x03;
    game_lx_out(lx, out);
    step();
    check("LXIO game's output report reaches the real LXIO untouched", memcmp(got_lx_out, out, 16) == 0);

    // Another source on top of a real LXIO only adds its own press
    memset(sim_lx, 0xFF, 16); sim_lx[10] = 0xFE; bytes_pf[0] = 0xFD; step(); game_lx(lx, in);
    check("keyboard UR on top of the LXIO's UL: both, nothing else", in[10] == 0xFC && in[11] == 0xFF && in[0] == 0xFF && in[8] == 0xFF);
    release_all();
    return fails;
}

// A game that speaks LXIO, a PIUIO with a button board: 4 buttons only
static int t_lx_from_board(void) {
    setenv("_PIUIOEMU_WITH_PIULXIO", "2", 1);
    plug(1, 1, 0);
    start();
    libusb_device_handle *lx = game_open(PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID_2);
    if(!lx) { check("LXIO game finds its LXIO", 0); return fails; }
    uint8_t in[16];

    sim_board[0] = 0xFE; step(); game_lx(lx, in);
    check("real board red P1 -> UL+UR (the board has one red)", in[10] == 0xFC && in[11] == 0xFF);
    sim_board[0] = 0xFD; step(); game_lx(lx, in);
    check("real board left P1 -> DL", in[10] == 0xF7);
    sim_board[0] = 0xFB; step(); game_lx(lx, in);
    check("real board right P1 -> DR", in[10] == 0xEF);
    sim_board[0] = 0xF7; step(); game_lx(lx, in);
    check("real board green P1 -> centre", in[10] == 0xFB);
    sim_board[0] = 0xEF; step(); game_lx(lx, in);
    check("real board red P2 -> UL+UR of P2", in[10] == 0xFF && in[11] == 0xFC);
    sim_board[0] = 0x7F; step(); game_lx(lx, in);
    check("real board green P2 -> centre of P2", in[11] == 0xFB);
    sim_board[0] = 0xFF;

    sim_pad[0][2] = 0xFE; sim_pad[1][0] = 0xEF; sim_sys[0] = 0xFB; step(); game_lx(lx, in);
    check("PIUIO sensors and coin reach the LXIO game, each sensor in its byte",
        in[0] == 0xFF && in[1] == 0xFF && in[2] == 0xFE && in[3] == 0xFF && in[4] == 0xEF && in[5] == 0xFF && in[8] == 0xFB &&
        in[10] == 0xFF && in[11] == 0xFF);
    release_all();
    return fails;
}

// A game that speaks LXIO, no IO at all: keyboard and network front buttons
static int t_lx_keyboard(void) {
    setenv("_PIUIOEMU_WITH_PIULXIO", "1", 1);
    plug(0, 0, 0);
    start();
    libusb_device_handle *lx = game_open(PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID);
    if(!lx) { check("LXIO game finds its LXIO", 0); return fails; }
    uint8_t in[16];

    bytes_pf[0] = 0xFE; step(); game_lx(lx, in);
    check("keyboard UL key (Delete) -> UL alone", in[10] == 0xFE && in[11] == 0xFF);
    bytes_pf[0] = 0xFD; step(); game_lx(lx, in);
    check("keyboard UR key (Page Down) -> UR alone", in[10] == 0xFD);
    bytes_pf[0] = 0xFF; bytes_pf[1] = 0xFE; step(); game_lx(lx, in);
    check("keyboard P2 UL key (keypad /) -> UL of P2 alone", in[10] == 0xFF && in[11] == 0xFE);
    bytes_pf[1] = 0xFF; bytes_pb[0] = 0xFE; step(); game_lx(lx, in);
    check("keyboard red key (Backspace) -> UL+UR, like the board", in[10] == 0xFC);
    bytes_pb[0] = 0xF5; step(); game_lx(lx, in);
    check("keyboard left + green -> DL + centre", in[10] == 0xF3);
    bytes_pb[0] = 0xFF;

    bytes_tf[0] = 0xFE; step(); game_lx(lx, in);
    check("network `f` UL -> UL alone", in[10] == 0xFE);
    bytes_tf[0] = 0xFF; bytes_tf[1] = 0xFD; step(); game_lx(lx, in);
    check("network `f` P2 UR -> UR of P2 alone", in[10] == 0xFF && in[11] == 0xFD);
    bytes_tf[1] = 0xFF; bytes_tb[0] = 0xEF; step(); game_lx(lx, in);
    check("network `b` red P2 -> UL+UR of P2", in[11] == 0xFC);
    bytes_tb[0] = 0xFF;

    bytes_p[0] = 0xFB; bytes_ts[5] = 0xFE; step(); game_lx(lx, in);
    check("keyboard pad = all four sensors, network single sensor = one",
        in[0] == 0xFB && in[1] == 0xFB && in[2] == 0xFB && in[3] == 0xFB && in[4] == 0xFF && in[5] == 0xFE && in[6] == 0xFF);
    release_all();
    return fails;
}

// A button-board game (default mode), a real LXIO: its front buttons are the board
static int t_board_from_lx(void) {
    plug(0, 0, 1);
    start();
    libusb_device_handle *bb = game_open(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID);
    if(!bb) { check("game finds the button board", 0); return fails; }
    sim_lx[10] = 0xFE; step();
    check("LXIO UL -> red", game_board(bb) == 0xFE);
    sim_lx[10] = 0xFD; step();
    check("LXIO UR -> red", game_board(bb) == 0xFE);
    sim_lx[10] = 0xFB; step();
    check("LXIO centre -> green", game_board(bb) == 0xF7);
    sim_lx[10] = 0xF7; step();
    check("LXIO DL -> left", game_board(bb) == 0xFD);
    sim_lx[10] = 0xEF; step();
    check("LXIO DR -> right", game_board(bb) == 0xFB);
    sim_lx[10] = 0xFF; sim_lx[11] = 0xFD; step();
    check("LXIO P2 UR -> red P2", game_board(bb) == 0xEF);
    sim_lx[11] = 0xFF; step();
    check("released: nothing sticks", game_board(bb) == 0xFF);
    release_all();
    return fails;
}

// ----------------------------------------------------------------- IMPER-114

// Prime JE (_PIUIOEMU_EMU_PIUIO_BUTTON=1), real PIUIO + real button board
static int t_je_real_board(void) {
    setenv("_PIUIOEMU_EMU_PIUIO_BUTTON", "1", 1);
    plug(1, 1, 0);
    start();
    libusb_device_handle *io = game_open(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID);
    libusb_device_handle *bb = game_open(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID);
    check("JE finds PIUIO 0547:1002 and button board 0d2f:1010", io != NULL && bb != NULL);
    if(!io || !bb) return fails;
    int cfg = 0;
    check("JE's set_configuration / claim_interface on the board succeed",
        libusb_set_configuration(bb, 1) == 0 && libusb_claim_interface(bb, 0) == 0 &&
        libusb_get_configuration(bb, &cfg) == 0 && cfg == 1);

    check("nothing pressed: board says FF", game_board(bb) == 0xFF);
    int ok = 1;
    for(int bit = 0; bit < 8; bit++) {
        sim_board[0] = (uint8_t)~(1 << bit); step();
        if(game_board(bb) != sim_board[0]) ok = 0;
        if(menu_board(bb) != sim_board[0]) ok = 0;
    }
    check("each of the 8 real buttons arrives alone (control read as JE does, and the menu's read)", ok);
    sim_board[0] = 0x00; step();
    check("all 8 real buttons together", game_board(bb) == 0x00);
    sim_board[0] = 0xFF; step();
    check("released", game_board(bb) == 0xFF);

    // With a real board the pads stay pads
    uint8_t pads[8];
    sim_pad[0][1] = 0xFB; sim_pad[1][3] = 0xFE; step();
    game_piuio_all(io, pads, NULL);
    check("pads are read as pads", pads[1] == 0xFB && pads[7] == 0xFE && pads[0] == 0xFF);
    check("pads do not press the buttons when a real board is there", game_board(bb) == 0xFF);
    release_all();

    // Lamps: what JE writes to the board reaches the real one
    uint8_t lamps[8] = {0x90, 0x00, 0, 0, 0, 0, 0, 0};
    n_board_out = 0;
    step();
    check("no lamp write before the game writes one", n_board_out == 0);
    libusb_control_transfer(bb, 0x40, PIUIO_CTL_REQ, 0, 0, lamps, 8, 10000);
    step();
    check("the game's lamp write reaches the real board", n_board_out == 1 && memcmp(got_board_out, lamps, 8) == 0);
    return fails;
}

// Prime JE, real LXIO: its front buttons are the board, pads stay pads
static int t_je_real_lxio(void) {
    setenv("_PIUIOEMU_EMU_PIUIO_BUTTON", "1", 1);
    plug(0, 0, 1);
    start();
    libusb_device_handle *io = game_open(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID);
    libusb_device_handle *bb = game_open(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID);
    check("JE finds PIUIO and button board on an LXIO machine", io != NULL && bb != NULL);
    if(!io || !bb) return fails;

    const struct { uint8_t lx; uint8_t board; const char *what; } m[] = {
        {0xFE, 0xFE, "UL -> red"}, {0xFD, 0xFE, "UR -> red"}, {0xFC, 0xFE, "UL+UR -> red"},
        {0xFB, 0xF7, "centre -> green"}, {0xF7, 0xFD, "DL -> left"}, {0xEF, 0xFB, "DR -> right"},
    };
    int ok1 = 1, ok2 = 1;
    for(unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        sim_lx[10] = m[i].lx; sim_lx[11] = 0xFF; step();
        if(game_board(bb) != m[i].board) { ok1 = 0; printf("     P1 %s: got %02X\n", m[i].what, game_board(bb)); }
        sim_lx[10] = 0xFF; sim_lx[11] = m[i].lx; step();
        uint8_t want = (uint8_t)((m[i].board << 4) | 0x0F);
        if(game_board(bb) != want) { ok2 = 0; printf("     P2 %s: got %02X\n", m[i].what, game_board(bb)); }
    }
    check("LXIO front buttons P1 press JE's board buttons", ok1);
    check("LXIO front buttons P2 press JE's board buttons", ok2);
    sim_lx[10] = sim_lx[11] = 0xFF; step();
    check("released", game_board(bb) == 0xFF);

    uint8_t pads[8];
    sim_lx[0] = 0xFE; sim_lx[3] = 0xFD; sim_lx[6] = 0xFB; sim_lx[8] = 0xFB; step();
    uint8_t sys = game_piuio_all(io, pads, NULL);
    check("LXIO sensors reach JE as PIUIO sensor groups, coin too",
        pads[0] == 0xFE && pads[1] == 0xFF && pads[3] == 0xFD && pads[6] == 0xFB && sys == 0xFB);
    check("pads do not press the buttons when a real LXIO is there", game_board(bb) == 0xFF);
    release_all();
    return fails;
}

// Prime JE, no IO: keyboard / network pads press the buttons
static int t_je_keyboard(void) {
    setenv("_PIUIOEMU_EMU_PIUIO_BUTTON", "1", 1);
    plug(0, 0, 0);
    start();
    libusb_device_handle *bb = game_open(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID);
    if(!bb) { check("JE finds the button board", 0); return fails; }

    check("nothing pressed", game_board(bb) == 0xFF);
    const struct { uint8_t pad; uint8_t board; const char *what; } m[] = {
        {0xFE, 0xFE, "UL -> red"}, {0xFD, 0xFE, "UR -> red"}, {0xFB, 0xF7, "centre -> green"},
        {0xF7, 0xFD, "DL -> left"}, {0xEF, 0xFB, "DR -> right"},
    };
    int ok1 = 1, ok2 = 1, ok3 = 1;
    for(unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        bytes_p[0] = m[i].pad; step();
        if(game_board(bb) != m[i].board) { ok1 = 0; printf("     keyboard P1 %s: got %02X\n", m[i].what, game_board(bb)); }
        bytes_p[0] = 0xFF; bytes_p[2] = m[i].pad; step();
        if(game_board(bb) != (uint8_t)((m[i].board << 4) | 0x0F)) ok2 = 0;
        bytes_p[2] = 0xFF; bytes_t[0] = m[i].pad; step();
        if(game_board(bb) != m[i].board) ok3 = 0;
        bytes_t[0] = 0xFF;
    }
    check("keyboard pad keys P1 press the board buttons (did nothing before)", ok1);
    check("keyboard pad keys P2 press the board buttons", ok2);
    check("network pads press the board buttons", ok3);
    bytes_ts[6] = 0xFB; step();
    check("a single network sensor (P2 centre) presses green P2", game_board(bb) == 0x7F);
    bytes_ts[6] = 0xFF; step();
    check("released", game_board(bb) == 0xFF);

    bytes_pb[0] = 0xF7; step();
    check("keyboard front key (Enter = green) still works", game_board(bb) == 0xF7);
    bytes_pb[0] = 0xFF; bytes_pf[0] = 0xFD; step();
    check("keyboard UR front key is the red of a board game", game_board(bb) == 0xFE);
    bytes_pf[0] = 0xFF; bytes_tf[1] = 0xFE; step();
    check("network `f` UL P2 is the red of P2", game_board(bb) == 0xEF);
    release_all();
    return fails;
}

// Prime JE, real PIUIO without a board: the pads press the buttons
static int t_je_pads_only(void) {
    setenv("_PIUIOEMU_EMU_PIUIO_BUTTON", "1", 1);
    plug(1, 0, 0);
    start();
    libusb_device_handle *bb = game_open(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID);
    if(!bb) { check("JE finds the button board", 0); return fails; }
    int ok = 1;
    for(int sensor = 0; sensor < 4; sensor++) {
        memset(sim_pad, 0xFF, sizeof(sim_pad));
        sim_pad[0][sensor] = 0xFB; step();
        if(game_board(bb) != 0xF7) ok = 0;
        memset(sim_pad, 0xFF, sizeof(sim_pad));
        sim_pad[1][sensor] = 0xF7; step();
        if(game_board(bb) != 0xDF) ok = 0;
    }
    check("real pads press the buttons from any of the four sensors", ok);
    release_all();
    check("released", game_board(bb) == 0xFF);
    return fails;
}

// Not JE (no _PIUIOEMU_EMU_PIUIO_BUTTON): pads never press buttons
static int t_prime_no_emu(void) {
    plug(1, 1, 0);
    start();
    libusb_device_handle *bb = game_open(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID);
    if(!bb) { check("game finds the button board", 0); return fails; }
    sim_pad[0][0] = 0xE0; bytes_p[2] = 0xE0; step();
    check("without the option the pads do not press the buttons", game_board(bb) == 0xFF);
    sim_board[0] = 0xBF; step();
    check("the real board still does", game_board(bb) == 0xBF);
    release_all();
    return fails;
}

// ----------------------------------------------------------------- IMPER-115

static int hold_until_exit(int seconds) {
    for(int q = 0; q <= seconds * 4; q++)
        if(piuioemu_poll_step(2000 + q / 4) == PIUIO_TESTHOLD_EXIT) return q / 4;
    return -1;
}

static int t_swap(void) {
    setenv("_PIUIOEMU_SWAP_TEST_SERVICE", "1", 1);
    plug(1, 0, 0);
    start();
    libusb_device_handle *io = game_open(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID);
    if(!io) { check("Infinity finds the PIUIO", 0); return fails; }
    uint8_t pads[8];

    sim_sys[0] = (uint8_t)~PIUIO_SYS_TEST; step();
    uint8_t sys = game_piuio_all(io, pads, NULL);
    check("TEST button: the game reads SERVICE (0x40), not TEST", sys == (uint8_t)~PIUIO_SYS_SERVICE);
    check("status feed still says TEST (the button itself)", bytes_f[8] == (uint8_t)~PIUIO_SYS_TEST);
    sim_sys[0] = (uint8_t)~PIUIO_SYS_SERVICE; step();
    sys = game_piuio_all(io, pads, NULL);
    check("SERVICE button: the game reads TEST (0x02)", sys == (uint8_t)~PIUIO_SYS_TEST);
    sim_sys[0] = (uint8_t)~(PIUIO_SYS_SERVICE | PIUIO_SYS_TEST | PIUIO_SYS_COIN); step();
    sys = game_piuio_all(io, pads, NULL);
    check("both + coin: both + coin", sys == (uint8_t)~(PIUIO_SYS_SERVICE | PIUIO_SYS_TEST | PIUIO_SYS_COIN));
    sim_sys[0] = (uint8_t)~PIUIO_SYS_COIN; sim_sys[1] = (uint8_t)~PIUIO_SYS_COIN; step();
    uint8_t sys2 = 0;
    sys = game_piuio_all(io, pads, &sys2);
    check("coins are not touched", sys == (uint8_t)~PIUIO_SYS_COIN && sys2 == (uint8_t)~PIUIO_SYS_COIN);

    sim_sys[0] = 0xFF; sim_sys[1] = 0xFF; bytes_p[1] = (uint8_t)~PIUIO_SYS_TEST; step();
    sys = game_piuio_all(io, pads, NULL);
    check("keyboard TEST (F6) is swapped the same way", sys == (uint8_t)~PIUIO_SYS_SERVICE);
    bytes_p[1] = 0xFF; step();

    // The 5 s hold that ends a game: the TEST button, not what the game calls TEST
    sim_sys[0] = (uint8_t)~PIUIO_SYS_SERVICE; step();
    check("SERVICE button held 60 s (the game's TEST) does not end the game", hold_until_exit(60) == -1);
    sim_sys[0] = 0xFF; piuioemu_poll_step(3000);
    sim_sys[0] = (uint8_t)~PIUIO_SYS_TEST;
    check("TEST button held 5 s ends the game", hold_until_exit(10) == 5);
    return fails;
}

static int t_noswap(void) {
    plug(1, 0, 0);
    start();
    libusb_device_handle *io = game_open(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID);
    if(!io) { check("game finds the PIUIO", 0); return fails; }
    uint8_t pads[8];
    sim_sys[0] = (uint8_t)~PIUIO_SYS_TEST; step();
    check("every other game: TEST is TEST", game_piuio_all(io, pads, NULL) == (uint8_t)~PIUIO_SYS_TEST);
    sim_sys[0] = (uint8_t)~PIUIO_SYS_SERVICE; step();
    check("every other game: SERVICE is SERVICE", game_piuio_all(io, pads, NULL) == (uint8_t)~PIUIO_SYS_SERVICE);
    sim_sys[0] = 0xFF; step();
    return fails;
}

// ------------------------------------------------- IMPER-116 (failed reads)

static int t_failed_reads(void) {
    plug(1, 1, 0);
    fail_piuio = LIBUSB_ERROR_TIMEOUT;      // dead from the first read on
    fail_board = LIBUSB_ERROR_TIMEOUT;
    start();
    libusb_device_handle *io = game_open(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID);
    libusb_device_handle *bb = game_open(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID);
    if(!io || !bb) { check("game finds PIUIO and board", 0); return fails; }
    uint8_t pads[8], want[8], sys2 = 0;
    memset(want, 0xFF, 8);

    for(int i = 0; i < 20; i++) step();
    uint8_t sys = game_piuio_all(io, pads, &sys2);
    check("PIUIO that never answers: nothing pressed (was: every pad pressed)", memcmp(pads, want, 8) == 0);
    check("PIUIO that never answers: no coin, no TEST, no SERVICE (was: all of them)", sys == 0xFF && sys2 == 0xFF);
    check("board that never answers: nothing pressed", game_board(bb) == 0xFF);

    // Works, something is held, then reads start failing: the state stays
    fail_piuio = 0; fail_board = 0;
    sim_pad[0][2] = 0xFE; sim_pad[1][1] = 0xEF; sim_sys[0] = 0xFF; sim_board[0] = 0xF7; step();
    sys = game_piuio_all(io, pads, &sys2);
    check("working again: the held pads are read", pads[2] == 0xFE && pads[5] == 0xEF && sys == 0xFF && game_board(bb) == 0xF7);
    fail_piuio = LIBUSB_ERROR_TIMEOUT; fail_board = LIBUSB_ERROR_IO;
    for(int i = 0; i < 20; i++) step();
    sys = game_piuio_all(io, pads, &sys2);
    want[2] = 0xFE; want[5] = 0xEF;
    check("reads fail: the last state is kept, no press appears", memcmp(pads, want, 8) == 0 && sys == 0xFF && sys2 == 0xFF);
    check("board reads fail: the last state is kept", game_board(bb) == 0xF7);

    // One sensor group fails, the others go on
    fail_piuio = 0; fail_board = 0; fail_piuio_mux = 2;
    sim_pad[0][2] = 0xFF; sim_pad[0][0] = 0xFD; sim_sys[0] = (uint8_t)~PIUIO_SYS_COIN; step();
    sys = game_piuio_all(io, pads, &sys2);
    check("one failing sensor group keeps its state, the others and the coin update",
        pads[2] == 0xFE && pads[0] == 0xFD && sys == (uint8_t)~PIUIO_SYS_COIN);
    fail_piuio_mux = -1; step();
    game_piuio_all(io, pads, NULL);
    check("the group reads again", pads[2] == 0xFF);

    // Unplugged: nobody is pressing it
    fail_piuio = LIBUSB_ERROR_NO_DEVICE; fail_board = LIBUSB_ERROR_NO_DEVICE; step();
    sys = game_piuio_all(io, pads, &sys2);
    memset(want, 0xFF, 8);
    check("PIUIO unplugged: everything is let go", memcmp(pads, want, 8) == 0 && sys == 0xFF && sys2 == 0xFF);
    check("board unplugged: everything is let go", game_board(bb) == 0xFF);
    return fails;
}

static int t_failed_lxio(void) {
    setenv("_PIUIOEMU_WITH_PIULXIO", "2", 1);
    plug(0, 0, 1);
    fail_lxio = LIBUSB_ERROR_TIMEOUT;
    start();
    libusb_device_handle *lx = game_open(PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID_2);
    if(!lx) { check("game finds the LXIO", 0); return fails; }
    uint8_t in[16], ff[16];
    memset(ff, 0xFF, 16);
    step(); game_lx(lx, in);
    check("LXIO that never answers: nothing pressed", memcmp(in, ff, 16) == 0);
    fail_lxio = 0; sim_lx[1] = 0xFB; sim_lx[10] = 0xFE; step(); game_lx(lx, in);
    fail_lxio = LIBUSB_ERROR_TIMEOUT; sim_lx[1] = 0xFF; step(); step(); game_lx(lx, in);
    check("LXIO reads fail: the last state is kept", in[1] == 0xFB && in[10] == 0xFE);
    fail_lxio = LIBUSB_ERROR_NO_DEVICE; step(); game_lx(lx, in);
    check("LXIO unplugged: everything is let go", memcmp(in, ff, 16) == 0);
    return fails;
}

// A PIUIO game on a real LXIO: the mux bits never reach the LXIO's lamps
static int t_piuio_game_on_lxio(void) {
    plug(0, 0, 1);
    start();
    libusb_device_handle *io = game_open(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID);
    if(!io) { check("game finds the PIUIO", 0); return fails; }
    uint8_t in[8];
    g_lamp[0] = 0x7C; g_lamp[1] = 0x04; g_lamp[2] = 0xFC; g_lamp[3] = 0x07;
    game_piuio(io, 3, in);
    step();
    check("PIUIO game on a real LXIO: lamps go out without the sensor-select bits",
        got_lx_out[0] == 0x7C && got_lx_out[1] == 0x04 && got_lx_out[2] == 0xFC && got_lx_out[3] == 0x07 && got_lx_out[4] == 0);
    return fails;
}

// ---------------------------------------------------------------------- main

static int run(const char *name, int (*fn)(void)) {
    printf("-- %s\n", name);
    fflush(stdout);
    pid_t pid = fork();
    if(pid == 0) {
        // Every scenario is its own process: the shim initializes once
        unsetenv("_PIUIOEMU_WITH_PIULXIO");
        unsetenv("_PIUIOEMU_WITH_PIUIO");
        unsetenv("_PIUIOEMU_WITH_PIUIOBUTTON");
        unsetenv("_PIUIOEMU_EMU_PIUIO_BUTTON");
        unsetenv("_PIUIOEMU_PROPAGATE");
        unsetenv("_PIUIOEMU_SWAP_TEST_SERVICE");
        int f = fn();
        fflush(stdout);
        _exit(f > 250 ? 250 : f);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if(!WIFEXITED(st)) { printf("FAIL: %s crashed\n", name); return 1; }
    return WEXITSTATUS(st);
}

int main(void) {
    int f = 0;
    f += run("mapping (piuio_input.h)", t_pure);
    f += run("IMPER-113: LXIO game + real LXIO = bypass", t_lx_bypass);
    f += run("IMPER-113: LXIO game + real PIUIO and button board", t_lx_from_board);
    f += run("IMPER-113: LXIO game, keyboard / network front buttons", t_lx_keyboard);
    f += run("button-board game + real LXIO", t_board_from_lx);
    f += run("IMPER-114: Prime JE + real PIUIO and button board", t_je_real_board);
    f += run("IMPER-114: Prime JE + real LXIO", t_je_real_lxio);
    f += run("IMPER-114: Prime JE, keyboard only", t_je_keyboard);
    f += run("IMPER-114: Prime JE + real PIUIO, no board", t_je_pads_only);
    f += run("button-board game without the pad option", t_prime_no_emu);
    f += run("IMPER-115: Infinity, TEST <-> SERVICE", t_swap);
    f += run("IMPER-115: every other game", t_noswap);
    f += run("IMPER-116: failed PIUIO / board reads", t_failed_reads);
    f += run("IMPER-116: failed LXIO reads", t_failed_lxio);
    f += run("PIUIO game on a real LXIO: lamps", t_piuio_game_on_lxio);
    printf(f ? "\n%d check(s) FAILED\n" : "\nall passed\n", f);
    return f ? 1 : 0;
}
