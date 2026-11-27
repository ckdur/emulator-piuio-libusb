// Host test for the hot-plug of the real IO boards (libusb/piuio.c): boards
// that are plugged in or pulled out while a game runs, boards that stop
// answering, several at once, and what the game reads meanwhile.
//
// The real piuio.c and piuio_emu.c are compiled in. The "true" libusb below
// them is a fake USB bus: devices come and go, each one can be told to fail,
// and a transfer that times out costs its timeout on a clock the test owns
// (piuio_clock_ms), so a minute of retries runs at once. Every handle is
// tracked: a transfer on a closed handle, or a second close, fails the test.
//
//   gcc -std=gnu17 -Wall -g -fsanitize=address,undefined -DPIUIO_EMU_NO_POLL_THREAD -I . -I libusb
//       -o /tmp/test_piuio_hotplug tests/test_piuio_hotplug.c libusb/piuio.c libusb/piuio_emu.c -lpthread   (one line)
//   ASAN_OPTIONS=detect_leaks=0 /tmp/test_piuio_hotplug            (-v: show the shim's messages)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
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
void libusb_piuio_get_status(int *status);

static int fails = 0;
static int verbose = 0;
static void check(const char *what, int ok) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) fails++;
}

// ------------------------------------------------------------------ fake bus

enum kind { K_PIUIO, K_BUTTON, K_LXIO, K_LXIO2, K_OTHER };
static const uint16_t kind_ids[][2] = {
    {PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID}, {PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID},
    {PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID}, {PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID_2},
    {0x1d6b, 0x0002},
};

struct fake {
    struct libusb_device dev;       // first: libusb_device* == struct fake*
    enum kind kind;
    int plugged;                    // 0: every transfer says NO_DEVICE
    int listed;                     // in the device list
    int fail;                       // every transfer returns this error
    int fail_reads;                 // only this many more reads time out
    int fail_mux;                   // this sensor group times out (-1 none)
    int fail_open, fail_claim;
    // what it says
    uint8_t pad[2][4], sys[2], button[2], lx[16];
    // what it got
    uint8_t out[16];
    int n_out, n_reads, n_opens, mux[2];
};

#define MAX_FAKE 512
static struct fake *bus[MAX_FAKE];
static int n_bus;
static int next_addr = 2;
static int list_error;

static struct fake *plug(enum kind k) {
    struct fake *f = calloc(1, sizeof(*f));
    f->kind = k;
    f->plugged = f->listed = 1;
    f->fail_mux = -1;
    memset(f->pad, 0xFF, sizeof(f->pad));
    memset(f->sys, 0xFF, sizeof(f->sys));
    memset(f->button, 0xFF, sizeof(f->button));
    memset(f->lx, 0xFF, sizeof(f->lx));
    f->dev.bus_number = 1;
    f->dev.device_address = (uint8_t)next_addr++;
    f->dev.refcnt = 1;
    f->dev.device_descriptor.bLength = 18;
    f->dev.device_descriptor.bDescriptorType = LIBUSB_DT_DEVICE;
    f->dev.device_descriptor.idVendor = kind_ids[k][0];
    f->dev.device_descriptor.idProduct = kind_ids[k][1];
    bus[n_bus++] = f;
    return f;
}
// Pulled out, and libusb knows
static void unplug(struct fake *f) { f->plugged = 0; f->listed = 0; }

#define MAX_HANDLES 4096
static struct handle { libusb_device_handle h; struct fake *f; int open, claimed; unsigned char pad[64]; } *handles[MAX_HANDLES];
static int n_handles, misuse, max_timeout;

static int open_handles(const struct fake *f) {
    int n = 0;
    for(int i = 0; i < n_handles; i++) if(handles[i]->open && (!f || handles[i]->f == f)) n++;
    return n;
}

static int64_t fake_now = 100000;
static int64_t stalled_ms;                      // time spent in timeouts
static int64_t fake_clock(void) { return fake_now; }

static int transfer_error(struct fake *f, int is_read, unsigned int timeout) {
    if((int)timeout > max_timeout) max_timeout = timeout;
    if(timeout == 0) misuse++;
    int r = 0;
    if(!f->plugged) r = LIBUSB_ERROR_NO_DEVICE;
    else if(f->fail) r = f->fail;
    else if(is_read && f->fail_reads > 0) { f->fail_reads--; r = LIBUSB_ERROR_TIMEOUT; }
    if(r == LIBUSB_ERROR_TIMEOUT) { fake_now += timeout; stalled_ms += timeout; }
    return r;
}

static struct handle *check_handle(libusb_device_handle *h) {
    struct handle *x = (struct handle *)h;
    if(!x->open) misuse++;
    return x;
}

int true_libusb_init_context(libusb_context **ctx, const struct libusb_init_option options[], int num_options) {
    (void)options; (void)num_options;
    if(ctx) *ctx = calloc(1, sizeof(**ctx));
    return 0;
}
void true_libusb_exit(libusb_context *ctx) { (void)ctx; }
ssize_t true_libusb_get_device_list(libusb_context *ctx, libusb_device ***list) {
    (void)ctx;
    if(list_error) return list_error;
    libusb_device **l = calloc(n_bus + 1, sizeof(*l));
    int n = 0;
    for(int i = 0; i < n_bus; i++) if(bus[i]->listed) l[n++] = &bus[i]->dev;
    *list = l;
    return n;
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
    struct fake *f = (struct fake *)dev;
    f->n_opens++;
    if(f->fail_open) return f->fail_open;
    if(!f->plugged) return LIBUSB_ERROR_NO_DEVICE;
    struct handle *x = calloc(1, sizeof(*x));
    x->h.dev = dev;
    x->f = f;
    x->open = 1;
    handles[n_handles++] = x;       // never freed: a use after close is seen
    *h = &x->h;
    return 0;
}
void true_libusb_close(libusb_device_handle *h) { check_handle(h)->open = 0; }
int true_libusb_kernel_driver_active(libusb_device_handle *h, int i) { (void)i; check_handle(h); return 0; }
int true_libusb_detach_kernel_driver(libusb_device_handle *h, int i) { (void)i; check_handle(h); return 0; }
int true_libusb_claim_interface(libusb_device_handle *h, int i) {
    (void)i;
    struct handle *x = check_handle(h);
    if(x->f->fail_claim) return x->f->fail_claim;
    x->claimed = 1;
    return 0;
}
int true_libusb_release_interface(libusb_device_handle *h, int i) { (void)i; check_handle(h)->claimed = 0; return 0; }
int true_libusb_set_interface_alt_setting(libusb_device_handle *h, int i, int a) { (void)h; (void)i; (void)a; return 0; }
int true_libusb_get_configuration(libusb_device_handle *h, int *c) { (void)h; *c = 1; return 0; }
int true_libusb_set_configuration(libusb_device_handle *h, int c) { (void)h; (void)c; return 0; }
int true_libusb_submit_transfer(struct libusb_transfer *t) { (void)t; return LIBUSB_ERROR_NO_DEVICE; }
int true_libusb_cancel_transfer(struct libusb_transfer *t) { (void)t; return LIBUSB_ERROR_NOT_FOUND; }
int true_libusb_handle_events(libusb_context *ctx) { (void)ctx; return 0; }
int libusb_handle_events_timeout_completed(libusb_context *ctx, struct timeval *tv, int *completed) { (void)ctx; (void)tv; (void)completed; return 0; }
libusb_device *libusb_ref_device(libusb_device *dev) { dev->refcnt++; return dev; }
const char *libusb_error_name(int code) {
    switch(code) {
        case LIBUSB_ERROR_TIMEOUT: return "LIBUSB_ERROR_TIMEOUT";
        case LIBUSB_ERROR_PIPE: return "LIBUSB_ERROR_PIPE";
        case LIBUSB_ERROR_BUSY: return "LIBUSB_ERROR_BUSY";
        default: return "LIBUSB_ERROR";
    }
}

int true_libusb_control_transfer(libusb_device_handle *h, uint8_t type, uint8_t req,
    uint16_t value, uint16_t index, unsigned char *data, uint16_t len, unsigned int timeout) {
    (void)value; (void)index;
    struct fake *f = check_handle(h)->f;
    if(req != PIUIO_CTL_REQ || len != 8) return LIBUSB_ERROR_PIPE;
    int is_read = type == 0xC0;
    // A dead sensor group: the read after it was selected times out
    if(is_read && f->kind == K_PIUIO && f->plugged && f->fail_mux == f->mux[0]) {
        fake_now += timeout; stalled_ms += timeout;
        return LIBUSB_ERROR_TIMEOUT;
    }
    int r = transfer_error(f, is_read, timeout);
    if(r) return r;
    if(!is_read) {
        memcpy(f->out, data, 8);
        f->n_out++;
        f->mux[0] = data[0] & 3;
        f->mux[1] = data[2] & 3;
        return 8;
    }
    f->n_reads++;
    memset(data, 0xFF, 8);
    if(f->kind == K_PIUIO) {
        data[0] = f->pad[0][f->mux[0]];
        data[1] = f->sys[0];
        data[2] = f->pad[1][f->mux[1]];
        data[3] = f->sys[1];
    }
    else if(f->kind == K_BUTTON) {
        data[0] = f->button[0];
        data[1] = f->button[1];
    }
    else return LIBUSB_ERROR_PIPE;
    return 8;
}

int true_libusb_interrupt_transfer(libusb_device_handle *h, unsigned char ep,
    unsigned char *data, int len, int *transferred, unsigned int timeout) {
    struct fake *f = check_handle(h)->f;
    *transferred = 0;
    if((f->kind != K_LXIO && f->kind != K_LXIO2) || len != 16) return LIBUSB_ERROR_PIPE;
    int is_read = ep == (0x80 | PIULXIO_ENDPOINT_IN);
    int r = transfer_error(f, is_read, timeout);
    if(r) return r;
    if(is_read) { memcpy(data, f->lx, 16); f->n_reads++; }
    else { memcpy(f->out, data, 16); f->n_out++; }
    *transferred = 16;
    return 0;
}

// The other parts of the shim, not under test here
void init_keyboards(void) {}
void init_profile_keys(void) {}
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

// ------------------------------------------------------------------- helpers

// Coin edges and "everything pressed" are watched on every single round
static int coin_edges[2], coin_prev[2], saw_all_pressed;

static void step(void) {
    piuioemu_poll_step(1000);
    for(int p = 0; p < 2; p++) {
        int down = !(bytes_piuio[8 + p] & PIUIO_SYS_COIN);
        if(down && !coin_prev[p]) coin_edges[p]++;
        coin_prev[p] = down;
    }
    if(bytes_piuio[8] == 0 || bytes_piuio[0] == 0) saw_all_pressed = 1;
}
// Rounds for this long: a round takes 2 ms, plus what its timeouts take
static void run_ms(int ms) {
    int64_t until = fake_now + ms;
    while(fake_now < until) { fake_now += 2; step(); }
}
static int status(void) { int s = -1; libusb_piuio_get_status(&s); return s; }

static libusb_context *ctx;
static void start(void) {
    piuio_clock_ms = fake_clock;
    libusb_init_context(&ctx, NULL, 0);
    step();
}

// The log: stderr of the scenario goes to a file
static char log_path[64];
static char *log_text(void) {
    static char buf[1 << 20];
    fflush(stderr);
    FILE *f = fopen(log_path, "r");
    size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
    if(f) fclose(f);
    buf[n] = 0;
    return buf;
}
static int log_count(const char *what) {
    int n = 0;
    for(const char *p = log_text(); (p = strstr(p, what)) != NULL; p += strlen(what)) n++;
    return n;
}

static int all_ff(const uint8_t *p, int n) {
    for(int i = 0; i < n; i++) if(p[i] != 0xFF) return 0;
    return 1;
}

static void final_checks(void) {
    check("no transfer or close on a closed handle, no unlimited timeout", misuse == 0);
    check("every transfer to a board waits PIUIO_TIMEOUT_MS at the most", max_timeout <= PIUIO_TIMEOUT_MS);
    check("never \"everything pressed\"", !saw_all_pressed);
}

// What a PIUIO game reads, through the emulated device
static libusb_device_handle *game_open(uint16_t vid, uint16_t pid) {
    libusb_device **list;
    libusb_device_handle *h = NULL;
    ssize_t n = libusb_get_device_list(ctx, &list);
    for(ssize_t i = 0; i < n && !h; i++) {
        struct libusb_device_descriptor desc;
        if(libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if(desc.idVendor == vid && desc.idProduct == pid && list[i]->bus_number == 0xFF)
            if(libusb_open(list[i], &h) != 0) h = NULL;
    }
    libusb_free_device_list(list, 0);
    return h;
}
static void game_piuio(libusb_device_handle *h, int mux, const uint8_t lamps[4], uint8_t in[8]) {
    uint8_t out[8] = {0};
    out[0] = (lamps[0] & 0xFC) | mux; out[1] = lamps[1];
    out[2] = (lamps[2] & 0xFC) | mux; out[3] = lamps[3];
    libusb_control_transfer(h, 0x40, PIUIO_CTL_REQ, 0, 0, out, 8, 10000);
    memset(in, 0, 8);
    libusb_control_transfer(h, 0xC0, PIUIO_CTL_REQ, 0, 0, in, 8, 10000);
}

// ----------------------------------------------------------------- scenarios

// No board at all when the game starts; one is plugged in later
static void t_late_board(void) {
    start();
    libusb_device_handle *io = game_open(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID);
    check("no board at start: the game has its (emulated) PIUIO, status 0", io != NULL && status() == 0);
    run_ms(5000);
    check("nothing pressed while there is no board", all_ff(bytes_piuio, 16) && all_ff(bytes_piuiob, 2));
    struct fake *p = plug(K_PIUIO);
    p->pad[0][1] = 0xFB;
    int64_t t = fake_now;
    while(!(status() & WITH_PIUIO) && fake_now < t + 5000) run_ms(2);
    check("plugged during the game: found within about a second", status() == WITH_PIUIO && fake_now - t <= PIUIO_SEARCH_MS + 10);
    run_ms(10);
    const uint8_t lamps[4] = {0x80, 0x04, 0x40, 0x01};
    uint8_t in[8];
    game_piuio(io, 1, lamps, in);
    check("the game reads the late board's panel", in[0] == 0xFB);
    run_ms(10);
    check("and the board gets the game's lamps", (p->out[0] & 0xFC) == 0x80 && p->out[1] == 0x04 && (p->out[2] & 0xFC) == 0x40 && p->out[3] == 0x01);
    check("one handle, claimed once, opened once", open_handles(p) == 1 && p->n_opens == 1);
    check("the connection is in the log", log_count("no IO board found") == 1 && log_count("PIUIO connected") == 1);
    final_checks();
}

// Pulled out and plugged again, on the same or another port
static void t_replug(void) {
    struct fake *p = plug(K_PIUIO);
    start();
    libusb_device_handle *io = game_open(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID);
    const uint8_t lamps[4] = {0x84, 0x01, 0x00, 0x02};
    uint8_t in[8];
    game_piuio(io, 0, lamps, in);
    for(int n = 0; n < 5; n++) {
        p->pad[1][2] = 0xEF; run_ms(100);
        if(bytes_piuio[6] != 0xEF) check("held panel read", 0);
        unplug(p);
        step();
        if(!(all_ff(bytes_piuio, 16) && status() == 0 && open_handles(p) == 0)) check("unplugged: released and closed at once", 0);
        run_ms(2500);
        p = plug(K_PIUIO);              // a new address, as after any replug
        run_ms(PIUIO_SEARCH_MS + 20);
        if(!(status() == WITH_PIUIO && open_handles(p) == 1)) check("replugged: opened again", 0);
        run_ms(10);
        if(!((p->out[0] & 0xFC) == 0x84 && p->out[1] == 0x01 && p->out[3] == 0x02)) check("replugged: the lamps are sent to it again", 0);
        if(!all_ff(bytes_piuio, 16)) check("replugged: nothing carried over", 0);
    }
    check("five unplug / replug rounds: released, closed, found again, lamps again", fails == 0);
    check("exactly one handle left open", open_handles(NULL) == 1);
    check("disconnects are in the log", log_count("is gone") + log_count("was unplugged") >= 3);

    // Not listed any more, although the transfers still seem to work
    p->pad[0][0] = 0xFE; run_ms(20);
    p->listed = 0;
    run_ms(PIUIO_SEARCH_MS + 20);
    check("a board that left the device list is released and closed", all_ff(bytes_piuio, 16) && status() == 0 && open_handles(NULL) == 0);

    // The first seconds of a pull: I/O errors, before the hub reports it
    p = plug(K_PIUIO);
    p->pad[0][0] = 0xFE; run_ms(PIUIO_SEARCH_MS + 20);
    p->fail = LIBUSB_ERROR_IO; run_ms(20);
    check("a few ms of I/O errors keep the state", bytes_piuio[0] == 0xFE && status() == WITH_PIUIO);
    unplug(p); step();
    check("then it is gone: released", all_ff(bytes_piuio, 16) && status() == 0);

    // The list itself fails: nothing changes
    p = plug(K_PIUIO);
    p->pad[0][3] = 0xFD; run_ms(PIUIO_SEARCH_MS + 20);
    list_error = LIBUSB_ERROR_NO_MEM; run_ms(3000);
    check("a failing device list drops no board", bytes_piuio[3] == 0xFD && status() == WITH_PIUIO);
    list_error = 0;
    final_checks();
}

// Several boards, one of them drops: the others keep what they hold
static void t_one_of_many(void) {
    struct fake *p = plug(K_PIUIO), *b = plug(K_BUTTON), *l = plug(K_LXIO2);
    plug(K_OTHER);
    start();
    check("PIUIO, button board and LXIO all open", status() == (WITH_PIUIO | WITH_PIUIOBUTTON | WITH_PIULXIO_2));
    check("both flags set", is_lxio == 1 && is_piuiob == 1);
    p->pad[0][0] = 0xFE; p->sys[0] = (uint8_t)~PIUIO_SYS_TEST;
    l->lx[5] = 0xFB; l->lx[10] = 0xFE;
    b->button[0] = 0xF7;
    run_ms(20);
    check("presses of all three at once", bytes_piuio[0] == 0xFE && bytes_piuio[5] == 0xFB && bytes_piuio[10] == 0xFE && bytes_piuiob[0] == 0xF7);

    int never_lost = 1;
    unplug(l);
    for(int i = 0; i < 1500; i++) {
        run_ms(2);
        if(bytes_piuio[0] != 0xFE || bytes_piuiob[0] != 0xF7) never_lost = 0;
    }
    check("LXIO pulled: only its panel and front button are let go", bytes_piuio[5] == 0xFF && bytes_piuio[10] == 0xFF);
    check("the PIUIO's and the board's presses never flickered", never_lost);
    check("is_lxio follows, is_piuiob stays", is_lxio == 0 && is_piuiob == 1 && status() == (WITH_PIUIO | WITH_PIUIOBUTTON));

    unplug(b); run_ms(20);
    check("button board pulled: is_piuiob follows", is_piuiob == 0 && bytes_piuiob[0] == 0xFF && bytes_piuio[0] == 0xFE);

    // Another LXIO on top of the PIUIO: both count
    l = plug(K_LXIO); l->lx[4] = 0xF7; run_ms(PIUIO_SEARCH_MS + 20);
    check("an LXIO plugged next to the PIUIO adds its presses", bytes_piuio[0] == 0xFE && bytes_piuio[4] == 0xF7 && status() == (WITH_PIUIO | WITH_PIULXIO));

    // Two of the same kind
    struct fake *p2 = plug(K_PIUIO); p2->pad[1][3] = 0xEF; run_ms(PIUIO_SEARCH_MS + 20);
    check("two PIUIOs: both read", bytes_piuio[0] == 0xFE && bytes_piuio[7] == 0xEF);
    unplug(p); run_ms(20);
    check("one of the two pulled: the other stays", bytes_piuio[0] == 0xFF && bytes_piuio[7] == 0xEF);
    final_checks();
}

// A hub with every board on it goes away, and comes back
static void t_hub(void) {
    struct fake *p = plug(K_PIUIO), *b = plug(K_BUTTON), *l = plug(K_LXIO);
    struct fake *h = plug(K_OTHER);
    start();
    p->pad[0][0] = 0xFE; l->lx[1] = 0xFE; b->button[0] = 0xFE;
    uint8_t lamps_b[8] = {0x90, 0x01};
    libusb_device_handle *bb = game_open(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID);
    libusb_control_transfer(bb, 0x40, PIUIO_CTL_REQ, 0, 0, lamps_b, 8, 10000);
    run_ms(20);
    unplug(h); unplug(p); unplug(b); unplug(l);
    step();
    check("hub pulled: everything let go, everything closed", all_ff(bytes_piuio, 16) && all_ff(bytes_piuiob, 2) && open_handles(NULL) == 0 && status() == 0);
    run_ms(4000);
    h = plug(K_OTHER); p = plug(K_PIUIO); b = plug(K_BUTTON); l = plug(K_LXIO);
    run_ms(PIUIO_SEARCH_MS + 20);
    check("hub back: all three open again", status() == (WITH_PIUIO | WITH_PIUIOBUTTON | WITH_PIULXIO) && open_handles(NULL) == 3);
    check("button board lamps sent again", b->n_out > 0 && b->out[0] == 0x90 && b->out[1] == 0x01);
    final_checks();
}

// A different kind of board takes the place of the first one
static void t_other_type(void) {
    setenv("_PIUIOEMU_EMU_PIUIO_BUTTON", "1", 1);   // Prime JE
    struct fake *p = plug(K_PIUIO);
    start();
    libusb_device_handle *bb = game_open(PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID);
    uint8_t in[8];
    p->pad[0][0] = 0xFB; run_ms(20);
    libusb_control_transfer(bb, 0xC0, PIUIO_CTL_REQ, 0, 0, in, 8, 10000);
    check("JE, PIUIO without buttons: the centre panel presses green", in[0] == 0xF7);
    unplug(p); run_ms(100);
    struct fake *l = plug(K_LXIO);
    l->lx[1] = 0xFB; run_ms(PIUIO_SEARCH_MS + 20);
    libusb_control_transfer(bb, 0xC0, PIUIO_CTL_REQ, 0, 0, in, 8, 10000);
    check("swapped for an LXIO: it has real buttons, the pads are pads again", is_lxio == 1 && in[0] == 0xFF && bytes_piuio[1] == 0xFB);
    l->lx[1] = 0xFF; l->lx[10] = 0xFB; run_ms(20);
    libusb_control_transfer(bb, 0xC0, PIUIO_CTL_REQ, 0, 0, in, 8, 10000);
    check("its centre button presses green", in[0] == 0xF7);
    // The lamps of a PIUIO game go to the LXIO without the mux bits
    libusb_device_handle *io = game_open(PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID);
    const uint8_t lamps[4] = {0x7C, 0x04, 0xFC, 0x07};
    uint8_t r[8];
    game_piuio(io, 3, lamps, r);
    run_ms(20);
    check("lamps to the swapped-in LXIO, no sensor-select bits", l->out[0] == 0x7C && l->out[2] == 0xFC && l->out[3] == 0x07);
    final_checks();
}

// A game that speaks LXIO: what it sees does not depend on what is plugged in
static void t_lx_game(void) {
    setenv("_PIUIOEMU_WITH_PIULXIO", "2", 1);
    start();
    // The emulated LXIO answers as 1020 in both LXIO modes (the games try both ids)
    libusb_device_handle *lx = game_open(PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID);
    check("LXIO game, no board: it has its LXIO", lx != NULL);
    if(!lx) return;
    uint8_t out[16], in[16];
    for(int i = 0; i < 16; i++) out[i] = (uint8_t)(0x13 + 7 * i);
    int n = 0;
    libusb_interrupt_transfer(lx, PIULXIO_ENDPOINT_OUT, out, 16, &n, 1000);
    struct fake *p = plug(K_PIUIO);
    run_ms(PIUIO_SEARCH_MS + 20);
    check("a PIUIO plugged later gets the LXIO game's lamps", p->n_out > 0 && p->out[1] == out[1] && p->out[3] == out[3]);
    unplug(p); run_ms(50);
    struct fake *l = plug(K_LXIO);
    l->lx[10] = 0xFE; run_ms(PIUIO_SEARCH_MS + 20);
    check("then a real LXIO: the game's report reaches it untouched", memcmp(l->out, out, 16) == 0);
    libusb_interrupt_transfer(lx, 0x80 | PIULXIO_ENDPOINT_IN, in, 16, &n, 1000);
    check("and its report reaches the game byte for byte", memcmp(in, l->lx, 16) == 0);
    final_checks();
}

// Single timeouts now and then: nothing happens
static void t_transient(void) {
    struct fake *p = plug(K_PIUIO), *b = plug(K_BUTTON), *l = plug(K_LXIO);
    start();
    p->pad[0][0] = 0xFE; l->lx[5] = 0xFB; b->button[0] = 0xF7; run_ms(50);
    int held = 1;
    for(int i = 0; i < 40; i++) {
        struct fake *x = i % 3 == 0 ? p : i % 3 == 1 ? b : l;
        x->fail_reads = 1 + i % 2;      // one or two reads in a row
        for(int k = 0; k < 20; k++) {
            run_ms(2);
            if(bytes_piuio[0] != 0xFE || bytes_piuio[5] != 0xFB || bytes_piuiob[0] != 0xF7 || bytes_piuio[8] != 0xFF) held = 0;
        }
    }
    check("40 single / double timeouts: the held state never changes", held);
    check("no board was closed or reopened", p->n_opens == 1 && b->n_opens == 1 && l->n_opens == 1 && open_handles(NULL) == 3);
    check("and nothing said it was let go", log_count("does not answer") == 0);

    // A burst of immediate errors, shorter than the time window
    p->fail = LIBUSB_ERROR_PIPE; b->fail = LIBUSB_ERROR_PIPE; l->fail = LIBUSB_ERROR_PIPE;
    run_ms(PIUIO_DEAD_MS - 20);
    check("fast errors for less than the time window: state kept", bytes_piuio[0] == 0xFE && bytes_piuio[5] == 0xFB && bytes_piuiob[0] == 0xF7 && open_handles(NULL) == 3);
    p->fail = b->fail = l->fail = 0;
    run_ms(20);
    p->fail = b->fail = l->fail = LIBUSB_ERROR_PIPE;
    run_ms(PIUIO_DEAD_MS - 20);
    check("a good round in between starts the count again", open_handles(NULL) == 3);
    run_ms(100);
    check("fast errors that last: all let go", all_ff(bytes_piuio, 16) && all_ff(bytes_piuiob, 2) && open_handles(NULL) == 0);
    final_checks();
}

// A board that stops answering (and stays listed), next to a good one
static void persistent(enum kind k) {
    struct fake *good = plug(k == K_PIUIO ? K_LXIO : K_PIUIO);
    struct fake *x = plug(k);
    start();
    if(good->kind == K_LXIO) good->lx[2] = 0xFE; else good->pad[0][2] = 0xFE;
    if(k == K_PIUIO) x->pad[1][0] = 0xEF; else if(k == K_BUTTON) x->button[0] = 0xBF; else x->lx[4] = 0xEF;
    run_ms(100);
    x->fail = LIBUSB_ERROR_TIMEOUT;
    int64_t t = fake_now, released = 0;
    int good_held = 1;
    while(fake_now < t + 5000 && !released) {
        run_ms(1);
        if(bytes_piuio[2] != 0xFE) good_held = 0;
        if(open_handles(x) == 0) released = fake_now - t;
    }
    printf("     dead board let go after %d ms\n", (int)released);
    check("a dead board is let go after 250 ms .. 1 s", released >= PIUIO_DEAD_MS && released <= 1000);
    check("its presses are gone, the good board's stay", bytes_piuio[4] == 0xFF && bytes_piuiob[0] == 0xFF && bytes_piuio[2] == 0xFE && good_held);

    // Two minutes dead: retried, slower and slower, and cheaply
    int opens = x->n_opens, reads = good->n_reads;
    stalled_ms = 0;
    int64_t from = fake_now;
    while(fake_now < from + 120000) {
        run_ms(2);
        if(bytes_piuio[2] != 0xFE) good_held = 0;
    }
    printf("     120 s dead: %d reopen tries, %d ms stalled, %d reads of the good board\n",
        x->n_opens - opens, (int)stalled_ms, good->n_reads - reads);
    check("reopen tries: some, but not one per second", x->n_opens - opens >= 10 && x->n_opens - opens <= 20);
    check("the dead board takes less than 2 % of the time", stalled_ms < 2400);
    check("the good board is read all along and never lets go", good_held && good->n_reads - reads > 30000);
    check("two minutes of this are a handful of log lines", log_count("piuio:") < 20);

    // It recovers
    x->fail = 0;
    run_ms(PIUIO_RETRY_MAX_MS + 100);
    check("it answers again: opened and read", open_handles(x) == 1 &&
        (k == K_PIUIO ? bytes_piuio[4] == 0xEF : k == K_BUTTON ? bytes_piuiob[0] == 0xBF : bytes_piuio[4] == 0xEF));
    check("the recovery is in the log", log_count("works after earlier failures") == 1);
    // After a while its record is clean: one timeout does not drop it
    run_ms(PIUIO_HEALTHY_MS + 100);
    x->fail_reads = 1;
    opens = x->n_opens;
    run_ms(200);
    check("healthy again: a single timeout is a single timeout", x->n_opens == opens && open_handles(x) == 1);
    // Replugged while it is on the slow retry: no waiting
    x->fail = LIBUSB_ERROR_TIMEOUT; run_ms(40000);
    unplug(x); run_ms(1100);
    x = plug(k);
    int64_t t2 = fake_now;
    while(open_handles(x) == 0 && fake_now < t2 + 15000) run_ms(2);
    check("replugging it clears the slow retry", fake_now - t2 <= PIUIO_SEARCH_MS + 20);
    final_checks();
}
static void t_dead_piuio(void) { persistent(K_PIUIO); }
static void t_dead_button(void) { persistent(K_BUTTON); }
static void t_dead_lxio(void) { persistent(K_LXIO2); }

// An LXIO that takes its output but has no input report for a while
static void t_lxio_quiet(void) {
    struct fake *l = plug(K_LXIO);
    start();
    l->lx[0] = 0xFE; run_ms(100);
    l->fail_reads = 1000000;
    run_ms(PIUIO_LXIO_QUIET_MS - 100);
    check("LXIO quiet (output still taken) for 900 ms: held state kept, not reopened", bytes_piuio[0] == 0xFE && l->n_opens == 1 && open_handles(l) == 1);
    l->fail_reads = 0; run_ms(50);
    l->fail_reads = 1000000; run_ms(PIUIO_LXIO_QUIET_MS - 100);
    check("one report in between starts the count again", bytes_piuio[0] == 0xFE && open_handles(l) == 1);
    run_ms(400);
    check("quiet for longer: let go", all_ff(bytes_piuio, 16) && open_handles(l) == 0);
    l->fail_reads = 0; run_ms(PIUIO_SEARCH_MS * 2 + 100);
    check("and opened again when it can be", bytes_piuio[0] == 0xFE && open_handles(l) == 1);
    final_checks();
}

// One PIUIO sensor group stops answering, the other three go on
static void t_one_group(void) {
    struct fake *p = plug(K_PIUIO);
    start();
    for(int m = 0; m < 4; m++) p->pad[0][m] = 0xFE;
    run_ms(50);
    p->fail_mux = 1;
    run_ms(100);
    check("a dead sensor group keeps its state at first", bytes_piuio[1] == 0xFE);
    run_ms(600);
    check("then only that group is let go: the panel cannot stick", bytes_piuio[1] == 0xFF && bytes_piuio[0] == 0xFE && bytes_piuio[2] == 0xFE && bytes_piuio[3] == 0xFE);
    p->pad[0][0] = p->pad[0][2] = p->pad[0][3] = 0xFD;
    run_ms(200);
    check("the three others are still read, also the ones after it", bytes_piuio[0] == 0xFD && bytes_piuio[2] == 0xFD && bytes_piuio[3] == 0xFD);
    check("the board stays open", p->n_opens == 1 && open_handles(p) == 1);
    p->fail_mux = -1; run_ms(50);
    check("the group comes back", bytes_piuio[1] == 0xFE);
    final_checks();
}

// A coin (or service) line that is down when a board comes back makes no credit
static void coins(enum kind k) {
    struct fake *x = plug(k);
    start();
    uint8_t *sys = k == K_PIUIO ? &x->sys[0] : &x->lx[8];
    run_ms(100);
    *sys = (uint8_t)~(PIUIO_SYS_COIN | PIUIO_SYS_SERVICE); run_ms(100);
    check("a coin while connected counts once", coin_edges[0] == 1);
    // It drops out with the line down, and comes back with it still down
    x->fail = LIBUSB_ERROR_TIMEOUT; run_ms(3000);
    check("let go: the coin line is up", bytes_piuio[8] == 0xFF);
    x->fail = 0; run_ms(15000);
    check("back with the line still down: no second coin, no service",
        open_handles(x) == 1 && coin_edges[0] == 1 && bytes_piuio[8] == 0xFF);
    *sys = 0xFF; run_ms(100);
    *sys = (uint8_t)~PIUIO_SYS_COIN; run_ms(100);
    check("seen up once, the next coin counts", coin_edges[0] == 2 && bytes_piuio[8] == (uint8_t)~PIUIO_SYS_COIN);
    // The same over an unplug
    uint8_t keep = *sys;
    unplug(x); run_ms(100);
    x = plug(k);
    if(k == K_PIUIO) x->sys[0] = keep; else x->lx[8] = keep;
    run_ms(PIUIO_SEARCH_MS + 100);
    check("replugged with the line down: no coin", coin_edges[0] == 2);
    // Transient errors with a coin held: still one edge
    if(k == K_PIUIO) x->sys[0] = 0xFF; else x->lx[8] = 0xFF;
    run_ms(100);
    if(k == K_PIUIO) x->sys[0] = (uint8_t)~PIUIO_SYS_COIN; else x->lx[8] = (uint8_t)~PIUIO_SYS_COIN;
    run_ms(100);
    for(int i = 0; i < 20; i++) { x->fail_reads = 2; run_ms(150); }
    check("timeouts while a coin is held: no extra edge", coin_edges[0] == 3 && coin_edges[1] == 0);
    // TEST is not a credit line: held, it is read at once
    unplug(x); run_ms(100);
    x = plug(k);
    if(k == K_PIUIO) x->sys[0] = (uint8_t)~PIUIO_SYS_TEST; else x->lx[8] = (uint8_t)~PIUIO_SYS_TEST;
    run_ms(PIUIO_SEARCH_MS + 100);
    check("TEST held on a new board is read", bytes_piuio[8] == (uint8_t)~PIUIO_SYS_TEST);
    final_checks();
}
static void t_coins_piuio(void) { coins(K_PIUIO); }
static void t_coins_lxio(void) { coins(K_LXIO); }

// A board that cannot be opened or claimed
static void t_open_fails(void) {
    struct fake *p = plug(K_PIUIO);
    p->fail_claim = LIBUSB_ERROR_BUSY;
    start();
    run_ms(120000);
    check("claim fails: no handle left open", open_handles(NULL) == 0 && status() == 0);
    printf("     %d tries in 120 s\n", p->n_opens);
    check("tried again, slower and slower", p->n_opens >= 10 && p->n_opens <= 20);
    check("said once a minute at the most", log_count("claim failed") >= 1 && log_count("claim failed") <= 3);
    p->fail_claim = 0;
    run_ms(PIUIO_RETRY_MAX_MS + 100);
    check("claim works: the board is in", open_handles(p) == 1 && status() == WITH_PIUIO);
    struct fake *l = plug(K_LXIO);
    l->fail_open = LIBUSB_ERROR_ACCESS;
    run_ms(3000);
    check("open fails on a second board: the first is not touched", open_handles(p) == 1 && open_handles(l) == 0 && p->n_opens < 25);
    l->fail_open = 0; run_ms(PIUIO_RETRY_MAX_MS + 100);
    check("and it joins later", open_handles(l) == 1);
    final_checks();
}

// A bad cable: in and out all the time, with a hub full of boards
static void t_log_limit(void) {
    start();
    for(int i = 0; i < 100; i++) {
        struct fake *p = plug(K_PIUIO), *b = plug(K_BUTTON), *l = plug(K_LXIO);
        run_ms(1010);
        unplug(p); unplug(b); unplug(l);
        run_ms(20);
    }
    int lines = log_count("piuio:");
    printf("     %d lines for 300 replugs in 103 s\n", lines);
    check("the log is limited", lines <= (PIUIO_LOG_LINES + 1) * 12);
    check("and says how much it left out", log_count("more messages were not printed") >= 1);
    run_ms(PIUIO_LOG_WINDOW_MS + 100);
    plug(K_PIUIO); run_ms(1100);
    check("no handle leak", open_handles(NULL) == 1);
    final_checks();
}

// libusb_exit: everything closed, nothing opened again
static void t_exit(void) {
    struct fake *p = plug(K_PIUIO);
    plug(K_LXIO);
    start();
    p->pad[0][0] = 0xFE; run_ms(20);
    libusb_exit(ctx);
    check("libusb_exit closes every board and lets go", open_handles(NULL) == 0 && all_ff(bytes_piuio, 16) && status() == 0);
    plug(K_BUTTON);
    run_ms(5000);
    check("nothing is opened after it", open_handles(NULL) == 0);
    libusb_exit(ctx);
    check("a second libusb_exit is harmless", misuse == 0);
    final_checks();
}

// ---------------------------------------------------------------------- main

static int run(const char *name, void (*fn)(void)) {
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
        snprintf(log_path, sizeof(log_path), "/tmp/piuio_hotplug_%d.log", (int)getpid());
        if(!freopen(log_path, "w", stderr)) _exit(1);
        fn();
        if(verbose) printf("%s", log_text());
        unlink(log_path);
        fflush(stdout);
        _exit(fails > 250 ? 250 : fails);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if(!WIFEXITED(st)) { printf("FAIL: %s crashed\n", name); return 1; }
    return WEXITSTATUS(st);
}

int main(int argc, char **argv) {
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    int f = 0;
    f += run("no board at start, one plugged later", t_late_board);
    f += run("unplug and replug", t_replug);
    f += run("several boards, one drops", t_one_of_many);
    f += run("a whole hub", t_hub);
    f += run("another kind of board takes over (Prime JE)", t_other_type);
    f += run("LXIO game while the real boards change", t_lx_game);
    f += run("transient timeouts and short error bursts", t_transient);
    f += run("persistent timeouts: PIUIO", t_dead_piuio);
    f += run("persistent timeouts: button board", t_dead_button);
    f += run("persistent timeouts: LXIO", t_dead_lxio);
    f += run("LXIO without input reports", t_lxio_quiet);
    f += run("one PIUIO sensor group dies", t_one_group);
    f += run("no phantom coins: PIUIO", t_coins_piuio);
    f += run("no phantom coins: LXIO", t_coins_lxio);
    f += run("open / claim failures", t_open_fails);
    f += run("log rate limit", t_log_limit);
    f += run("libusb_exit", t_exit);
    printf(f ? "\n%d check(s) FAILED\n" : "\nall passed\n", f);
    return f ? 1 : 0;
}
