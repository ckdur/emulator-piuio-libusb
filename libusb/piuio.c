#include "libusbi.h"
#include "piuio_emu.h"
#include "piuio.h"
#include "piuio_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

unsigned char bytes_piuio[16] = { // From PIUIO
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 1P
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 2P
    0xFF, 0xFF, // Coins, service
    0xFF, 0xFF, // Frontal buttons in LX mode
    0xFF, 0xFF, 0xFF, 0xFF}; // Probably unused
unsigned char bytes_piuiob[2] = {0xFF, 0xFF}; // From a real PIUIObutton only

unsigned int is_piuiob = 0;
unsigned int is_lxio = 0;
libusb_context *piuio_ctx = NULL;

// ---------------------------------------------------------------- the boards
//
// The set of real boards changes while a game runs: a board can be plugged in,
// pulled out (or a whole hub with several of them), or stop answering. Every
// board keeps its own state, and what the rest of the shim sees (bytes_piuio,
// bytes_piuiob, is_lxio, is_piuiob, get_status) is made from the boards that
// are open now. So a board that goes away lets go of its own presses only.
//
// Threads: only the shim's poll thread calls poll_piuio() (transfers, lamps,
// opening and closing). boards_mutex covers one whole round, so finish_piuio()
// (libusb_exit) can never close a handle that is in use. table_mutex is held
// only while an entry of the table changes, so libusb_piuio_get_status() (any
// thread) never waits for a transfer. Nothing here uses libc calls with static
// state (the poll thread runs inside the game's process: IMPER-133).

enum board_type { BOARD_PIUIO, BOARD_LXIO, BOARD_LXIO2, BOARD_BUTTON };

static const char *board_name(enum board_type t) {
    switch(t) {
        case BOARD_PIUIO:  return "PIUIO";
        case BOARD_LXIO:   return "LXIO (1020)";
        case BOARD_LXIO2:  return "LXIO (1040)";
        default:           return "PIUIO button board";
    }
}

struct board {
    libusb_device_handle *handle;   // NULL: free entry
    libusb_device *dev;             // identity in the device list (the handle holds a reference)
    enum board_type type;
    int bus, addr;
    uint8_t state[16];              // its own presses, LXIO report layout (button board: [0..1])
    uint8_t seen_up[2];             // coin / service lines seen released since it was opened
    int probation;                  // it failed before: one dead round lets go of it again
    int64_t opened_ms;
    // Rounds in which no read succeeded, in a row
    int dead_rounds;
    int64_t dead_since_ms;
    // The same for each PIUIO sensor group
    int group_fails[4];
    int64_t group_since_ms[4];
    // For the log
    unsigned errors;
    int64_t error_log_ms;
};

static struct board boards[PIUIO_MAX_BOARDS];

// A board that failed (stopped answering, or cannot be opened) is retried
// less and less often: each try can cost a transfer timeout, which the boards
// that work have to wait for
struct sick {
    int used;
    uint16_t vid, pid;
    int bus, addr;                  // a replugged board gets a new address: it starts clean
    int strikes;
    int64_t retry_ms;
    int64_t log_ms;
};
static struct sick sicks[PIUIO_MAX_BOARDS * 2];

static pthread_mutex_t boards_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t table_mutex = PTHREAD_MUTEX_INITIALIZER;
static int boards_running = 0;      // between init_piuio() and finish_piuio()
static int64_t next_search_ms = 0;

static int64_t monotonic_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int64_t (*piuio_clock_ms)(void) = monotonic_ms;    // the tests move the time by hand

// Messages of this file are always printed (no DEBUG needed): they are all a
// log has to tell about a board that drops out. Limited, so that a bad cable
// cannot flood it
static int64_t log_window_ms = 0;
static int log_lines = 0;
static unsigned log_dropped = 0;
static void board_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void board_log(const char *fmt, ...) {
    int64_t now = piuio_clock_ms();
    if(now - log_window_ms >= PIUIO_LOG_WINDOW_MS) {
        if(log_dropped) fprintf(stderr, "piuio: %u more messages were not printed\n", log_dropped);
        log_window_ms = now;
        log_lines = 0;
        log_dropped = 0;
    }
    if(log_lines >= PIUIO_LOG_LINES) { log_dropped++; return; }
    log_lines++;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fprintf(stderr, "piuio: %s\n", line);
}

static int board_count(void) {
    int n = 0;
    for(int k = 0; k < PIUIO_MAX_BOARDS; k++) if(boards[k].handle) n++;
    return n;
}

// is_piuiob: a real PIUIO button board is open
// is_lxio: a real LXIO is open (its front buttons replace the button board)
static void update_board_flags(void) {
    unsigned int b = 0, l = 0;
    for(int k = 0; k < PIUIO_MAX_BOARDS; k++) {
        if(!boards[k].handle) continue;
        if(boards[k].type == BOARD_BUTTON) b = 1;
        if(boards[k].type == BOARD_LXIO || boards[k].type == BOARD_LXIO2) l = 1;
    }
    is_piuiob = b;
    is_lxio = l;
}

// What the open boards say together (active low: a press on any of them is a
// press). Made aside and copied in one go: a game thread reads these arrays,
// and must never see a panel of a working board let go in between
static void publish_boards(void) {
    uint8_t io[16], button[2];
    memset(io, 0xFF, sizeof(io));
    memset(button, 0xFF, sizeof(button));
    for(int k = 0; k < PIUIO_MAX_BOARDS; k++) {
        const struct board *b = &boards[k];
        if(!b->handle) continue;
        if(b->type == BOARD_BUTTON) {
            for(int i = 0; i < 2; i++) button[i] &= b->state[i];
        }
        else {
            for(int i = 0; i < 16; i++) io[i] &= b->state[i];
        }
    }
    if(piuioemu_mode & EMU_PROPAGATE) {
        // Propagate the sensor state to all sensors
        uint8_t prop1 = io[0] & io[1] & io[2] & io[3];
        uint8_t prop2 = io[4] & io[5] & io[6] & io[7];
        for (int i = 0; i < 4; i++) {
            io[i+0] = prop1;
            io[i+4] = prop2;
        }
    }
    memcpy(bytes_piuio, io, sizeof(io));
    memcpy(bytes_piuiob, button, sizeof(button));
}

static struct sick *find_sick(uint16_t vid, uint16_t pid, int bus, int addr) {
    for(unsigned i = 0; i < sizeof(sicks) / sizeof(sicks[0]); i++)
        if(sicks[i].used && sicks[i].vid == vid && sicks[i].pid == pid &&
           sicks[i].bus == bus && sicks[i].addr == addr) return &sicks[i];
    return NULL;
}

// One more failure of this device: when to try it again
static struct sick *add_strike(uint16_t vid, uint16_t pid, int bus, int addr, int64_t now) {
    struct sick *s = find_sick(vid, pid, bus, addr);
    if(!s) {
        for(unsigned i = 0; i < sizeof(sicks) / sizeof(sicks[0]) && !s; i++)
            if(!sicks[i].used) s = &sicks[i];
        if(!s) s = &sicks[0];
        memset(s, 0, sizeof(*s));
        s->used = 1;
        s->vid = vid; s->pid = pid; s->bus = bus; s->addr = addr;
        s->log_ms = -PIUIO_REPEAT_LOG_MS;
    }
    if(s->strikes < 30) s->strikes++;
    int64_t wait = PIUIO_SEARCH_MS;
    for(int i = 1; i < s->strikes && wait < PIUIO_RETRY_MAX_MS; i++) wait *= 2;
    if(wait > PIUIO_RETRY_MAX_MS) wait = PIUIO_RETRY_MAX_MS;
    s->retry_ms = now + wait;
    return s;
}

static void board_ids(enum board_type t, uint16_t *vid, uint16_t *pid) {
    switch(t) {
        case BOARD_PIUIO:  *vid = PIUIO_VENDOR_ID;       *pid = PIUIO_PRODUCT_ID; break;
        case BOARD_LXIO:   *vid = PIULXIO_VENDOR_ID;     *pid = PIULXIO_PRODUCT_ID; break;
        case BOARD_LXIO2:  *vid = PIULXIO_VENDOR_ID;     *pid = PIULXIO_PRODUCT_ID_2; break;
        default:           *vid = PIUIOBUTTON_VENDOR_ID; *pid = PIUIOBUTTON_PRODUCT_ID; break;
    }
}

static int board_type_of(uint16_t vid, uint16_t pid) {
    if(vid == PIUIO_VENDOR_ID && pid == PIUIO_PRODUCT_ID) return BOARD_PIUIO;
    if(vid == PIULXIO_VENDOR_ID && pid == PIULXIO_PRODUCT_ID) return BOARD_LXIO;
    if(vid == PIULXIO_VENDOR_ID && pid == PIULXIO_PRODUCT_ID_2) return BOARD_LXIO2;
    if(vid == PIUIOBUTTON_VENDOR_ID && pid == PIUIOBUTTON_PRODUCT_ID) return BOARD_BUTTON;
    return -1;
}

// Closes a board. Its presses are gone with it; the others keep theirs
static void drop_board(struct board *b) {
    libusb_device_handle *h = b->handle;
    pthread_mutex_lock(&table_mutex);
    b->handle = NULL;
    b->dev = NULL;
    update_board_flags();
    pthread_mutex_unlock(&table_mutex);
    publish_boards();
    // Released by hand: a child the game forked still has the descriptor open,
    // and would keep the claim (the board could not be claimed again)
    true_libusb_release_interface(h, 0);
    true_libusb_close(h);
}

static void lose_board(struct board *b, const char *why) {
    board_log("%s (bus %d address %d) %s: its buttons are let go, %d board(s) left",
        board_name(b->type), b->bus, b->addr, why, board_count() - 1);
    drop_board(b);
    // Look again in a second, not at once
    next_search_ms = piuio_clock_ms() + PIUIO_SEARCH_MS;
}

static void open_board(libusb_device *device, const struct libusb_device_descriptor *desc,
                       enum board_type type, int64_t now) {
    struct board *b = NULL;
    for(int k = 0; k < PIUIO_MAX_BOARDS && !b; k++)
        if(!boards[k].handle) b = &boards[k];
    if(!b) return;

    int bus = device->bus_number, addr = device->device_address;
    struct sick *s = find_sick(desc->idVendor, desc->idProduct, bus, addr);
    if(s && now < s->retry_ms) return;

    libusb_device_handle *dev_handle = NULL;
    const char *step = "open";
    int r = true_libusb_open(device, &dev_handle);
    if(r >= 0) {
        if (true_libusb_kernel_driver_active(dev_handle, 0) == 1) {
            true_libusb_detach_kernel_driver(dev_handle, 0);
        }
        step = "claim";
        r = true_libusb_claim_interface(dev_handle, 0);
        if(r < 0) true_libusb_close(dev_handle);
    }
    if(r < 0) {
        s = add_strike(desc->idVendor, desc->idProduct, bus, addr, now);
        if(now - s->log_ms >= PIUIO_REPEAT_LOG_MS) {
            s->log_ms = now;
            board_log("%s (bus %d address %d): %s failed: %s; trying again later",
                board_name(type), bus, addr, step, libusb_error_name(r));
        }
        return;
    }

    pthread_mutex_lock(&table_mutex);
    memset(b, 0, sizeof(*b));
    b->dev = device;
    b->type = type;
    b->bus = bus;
    b->addr = addr;
    memset(b->state, 0xFF, sizeof(b->state));
    b->probation = s != NULL;
    b->opened_ms = now;
    b->error_log_ms = now - PIUIO_ERROR_LOG_MS;
    b->handle = dev_handle;
    update_board_flags();
    pthread_mutex_unlock(&table_mutex);
    // A board on probation is tried again and again: only say it is back once
    // it answers (board_answered)
    if(!b->probation)
        board_log("%s connected (bus %d address %d), %d board(s) now",
            board_name(type), bus, addr, board_count());
}

// Looks at the device list: boards that left are closed, new ones are opened.
// With libusb's Linux hotplug backend this costs no bus traffic and no
// enumeration: it reads the waiting hotplug messages and copies a cached list
static void search_boards(int64_t now) {
    libusb_device **device_list = NULL;
    ssize_t num_devices = true_libusb_get_device_list(piuio_ctx, &device_list);
    if (num_devices < 0) {
        board_log("cannot get the USB device list: %s", libusb_error_name((int)num_devices));
        return;
    }

    for(int k = 0; k < PIUIO_MAX_BOARDS; k++) {
        if(!boards[k].handle) continue;
        ssize_t i = 0;
        while(i < num_devices && device_list[i] != boards[k].dev) i++;
        if(i == num_devices) lose_board(&boards[k], "was unplugged");
    }

    int listed[sizeof(sicks) / sizeof(sicks[0])] = {0};
    for (ssize_t i = 0; i < num_devices; ++i) {
        libusb_device *device = device_list[i];
        struct libusb_device_descriptor desc;

        if (true_libusb_get_device_descriptor(device, &desc) < 0) continue;
        int type = board_type_of(desc.idVendor, desc.idProduct);
        if(type < 0) continue;

        struct sick *s = find_sick(desc.idVendor, desc.idProduct, device->bus_number, device->device_address);
        if(s) listed[s - sicks] = 1;

        int k = 0;
        while(k < PIUIO_MAX_BOARDS && !(boards[k].handle && boards[k].dev == device)) k++;
        if(k < PIUIO_MAX_BOARDS) continue;     // open already

        open_board(device, &desc, (enum board_type)type, now);
        s = find_sick(desc.idVendor, desc.idProduct, device->bus_number, device->device_address);
        if(s) listed[s - sicks] = 1;
    }
    // A device that left starts clean when something shows up there again
    for(unsigned i = 0; i < sizeof(sicks) / sizeof(sicks[0]); i++)
        if(sicks[i].used && !listed[i]) sicks[i].used = 0;

    // Free the device list
    true_libusb_free_device_list(device_list, 1);
}

void finish_piuio(void){
    pthread_mutex_lock(&boards_mutex);
    boards_running = 0;
    for(int k = 0; k < PIUIO_MAX_BOARDS; k++)
        if(boards[k].handle) drop_board(&boards[k]);
    pthread_mutex_unlock(&boards_mutex);
}

void init_piuio(void){
    pthread_mutex_lock(&boards_mutex);
    if(!boards_running) {
        boards_running = 1;
        memset(sicks, 0, sizeof(sicks));
        int64_t now = piuio_clock_ms();
        search_boards(now);
        next_search_ms = now + PIUIO_SEARCH_MS;
        if(!board_count())
            board_log("no IO board found; looking again every %d ms", PIUIO_SEARCH_MS);
        publish_boards();
    }
    pthread_mutex_unlock(&boards_mutex);
}

// Coins and service make credits when they go down. A line that is down on a
// board that was just opened (stuck, or held since before it dropped out) is
// held up until it was seen up (IMPER-116)
#define BOARD_CREDIT_LINES (PIUIO_SYS_COIN | PIUIO_SYS_SERVICE | PIUIO_SYS_CLEAR)
static uint8_t guard_lines(struct board *b, int player, uint8_t sys) {
    b->seen_up[player] |= sys & BOARD_CREDIT_LINES;
    return sys | (uint8_t)(BOARD_CREDIT_LINES & ~b->seen_up[player]);
}

static void board_error(struct board *b, int r) {
    b->errors++;
    int64_t now = piuio_clock_ms();
    // A single timeout now and then is not worth a line of its own: it is
    // counted, and the count is printed at most every PIUIO_ERROR_LOG_MS
    if(b->errors >= PIUIO_DEAD_ROUNDS && now - b->error_log_ms >= PIUIO_ERROR_LOG_MS) {
        board_log("%s (bus %d address %d): %u failed transfers, last: %s",
            board_name(b->type), b->bus, b->addr, b->errors,
            r < 0 ? libusb_error_name(r) : "short answer");
        b->errors = 0;
        b->error_log_ms = now;
    }
}

static void board_answered(struct board *b) {
    if(b->probation) {
        b->probation = 0;
        board_log("%s works after earlier failures (bus %d address %d), %d board(s) now",
            board_name(b->type), b->bus, b->addr, board_count());
    }
    b->dead_rounds = 0;
    // Forgiven once it has worked for a while, so that a board that answers
    // once and dies again stays on the slow retry
    if(piuio_clock_ms() - b->opened_ms >= PIUIO_HEALTHY_MS) {
        uint16_t vid, pid;
        board_ids(b->type, &vid, &pid);
        struct sick *s = find_sick(vid, pid, b->bus, b->addr);
        if(s) s->used = 0;
    }
}

// A round without one good read. One keeps the state; several in a row over
// some time let go of the board and open it again
static void board_dead_round(struct board *b, int window_ms) {
    int64_t now = piuio_clock_ms();
    if(!b->dead_rounds) b->dead_since_ms = now;
    if(b->dead_rounds < 1000000) b->dead_rounds++;
    if(!b->probation &&
       (b->dead_rounds < PIUIO_DEAD_ROUNDS || now - b->dead_since_ms < window_ms))
        return;

    uint16_t vid, pid;
    board_ids(b->type, &vid, &pid);
    struct sick *s = add_strike(vid, pid, b->bus, b->addr, now);
    if(!b->probation)
        board_log("%s (bus %d address %d) does not answer (%d rounds, %d ms): "
            "its buttons are let go, %d board(s) left; opening it again later",
            board_name(b->type), b->bus, b->addr, b->dead_rounds,
            (int)(now - b->dead_since_ms), board_count() - 1);
    else if(now - s->log_ms >= PIUIO_REPEAT_LOG_MS) {
        s->log_ms = now;
        board_log("%s (bus %d address %d) still does not answer; trying again from time to time",
            board_name(b->type), b->bus, b->addr);
    }
    drop_board(b);
}

// A board that is suspect gets one chance per round, so that it costs the
// others one timeout and not two
static int fail_limit(const struct board *b) {
    return (b->dead_rounds || b->probation) ? 1 : 2;
}

static void poll_lxio(struct board *b) {
    // A game that speaks LXIO itself: its report goes out as it is
    // (pure bypass, IMPER-113). A game that speaks PIUIO: LX output =
    // PIUIO lamp bytes 0..3 without mux bits, the rest is 0.
    unsigned char datout[16];
    int transferred = 0;
    memcpy(datout, bytes_l, 16);
    if(!(piuioemu_mode & (WITH_PIULXIO | WITH_PIULXIO_2))) {
        datout[0] &= 0xFC;
        datout[2] &= 0xFC;
    }
    PRINTF("lxio: Out endp\n");
    int w = true_libusb_interrupt_transfer(b->handle,
        PIULXIO_ENDPOINT_OUT, datout, 16,
        &transferred, PIUIO_TIMEOUT_MS);
    if(w == PIUIO_ERR_NO_DEVICE) { lose_board(b, "is gone"); return; }
    if(w < 0) {
        board_error(b, w);
        if(w == LIBUSB_ERROR_TIMEOUT && fail_limit(b) == 1) { board_dead_round(b, PIUIO_DEAD_MS); return; }
    }
    PRINTF("lxio: In endp\n");
    unsigned char datin[16];
    memset(datin, 0xFF, sizeof(datin));
    transferred = 0;
    int r = true_libusb_interrupt_transfer(b->handle,
        0x80 | PIULXIO_ENDPOINT_IN, datin, 16,
        &transferred, PIUIO_TIMEOUT_MS);
    switch(piuio_read_result(r, transferred, 1)) {
        case PIUIO_READ_OK: {
            int n = transferred > 16 ? 16 : transferred;
            if(n > 8) datin[8] = guard_lines(b, 0, datin[8]);
            if(n > 9) datin[9] = guard_lines(b, 1, datin[9]);
            memcpy(b->state, datin, n);
            board_answered(b);
            break;
        }
        case PIUIO_READ_RELEASE:
            lose_board(b, "is gone");
            return;
        default: // Keep the last known state on timeouts
            board_error(b, r);
            board_dead_round(b, (w >= 0 && r == LIBUSB_ERROR_TIMEOUT) ? PIUIO_LXIO_QUIET_MS : PIUIO_DEAD_MS);
            break;
    }

#ifdef DEBUG
    PRINTF("LXIO poll: ");
    for(int i = 0; i < 16; i++) {
        PRINTF("%2x, ", (uint32_t)b->state[i]);
    }
    PRINTF("\n");
#endif
}

static void poll_legacy(struct board *b) {
    int ok = 0, failed = 0;
    unsigned char coin1 = 0xFF, coin2 = 0xFF;

    for (int i = 0; i < 4 && failed < fail_limit(b); i++)
    {
        unsigned char datin[8];
        unsigned char datout[8];
        memset(datin, 0xFF, 8);
        memset(datout, 0, 8);
        datout[0] = (bytes_l[0] & 0xFC) | (i & 0x3);
        datout[1] = bytes_l[1];
        datout[2] = (bytes_l[2] & 0xFC) | (i & 0x3);
        datout[3] = bytes_l[3];
        int r = true_libusb_control_transfer(
            b->handle, USB_DIR_OUT | USB_TYPE_VENDOR, PIUIO_CTL_REQ,
            0, 0, datout, 8, PIUIO_TIMEOUT_MS);
        // The sensor group was not selected: reading would give another one
        if(r != LIBUSB_ERROR_TIMEOUT && r != PIUIO_ERR_NO_DEVICE)
            r = true_libusb_control_transfer(
                b->handle, USB_DIR_IN | USB_TYPE_VENDOR, PIUIO_CTL_REQ,
                0, 0, datin, 8, PIUIO_TIMEOUT_MS);
        switch(piuio_read_result(r, r, 4)) {
            case PIUIO_READ_OK:
                b->state[i+0] = datin[0];
                b->state[i+4] = datin[2];
                coin1 &= datin[1];
                coin2 &= datin[3];
                b->group_fails[i] = 0;
                ok++;
                break;
            case PIUIO_READ_RELEASE:
                lose_board(b, "is gone");
                return;
            default: { // This sensor group keeps its last state
                int64_t now = piuio_clock_ms();
                failed++;
                board_error(b, r);
                if(!b->group_fails[i]) b->group_since_ms[i] = now;
                if(b->group_fails[i] < 1000000) b->group_fails[i]++;
                // ... but not for ever: a panel must not stick down while
                // the other groups go on answering
                if(b->group_fails[i] >= PIUIO_DEAD_ROUNDS && now - b->group_since_ms[i] >= PIUIO_DEAD_MS)
                    b->state[i+0] = b->state[i+4] = 0xFF;
                break;
            }
        }
    }
    if(ok) {
        // Coins, TEST and SERVICE come with every answer
        b->state[8] = guard_lines(b, 0, coin1);
        b->state[9] = guard_lines(b, 1, coin2);
        board_answered(b);
    }
    else board_dead_round(b, PIUIO_DEAD_MS);
}

static void poll_button_board(struct board *b) {
    // The game itself talks to the button board with the PIUIO vendor
    // request (0x40 / 0xC0, 0xAE, 8 bytes), not with interrupt
    // transfers: first the lamps, then the buttons. Do the same, with
    // the lamps the game last wrote to the emulated board.
    unsigned char dat[8];
    if(bytes_lb_set) {
        memcpy(dat, bytes_lb, 8);
        int w = true_libusb_control_transfer(b->handle,
            USB_DIR_OUT | USB_TYPE_VENDOR, PIUIO_CTL_REQ,
            0, 0, dat, 8, PIUIO_TIMEOUT_MS);
        if(w == PIUIO_ERR_NO_DEVICE) { lose_board(b, "is gone"); return; }
        if(w < 0) {
            board_error(b, w);
            if(w == LIBUSB_ERROR_TIMEOUT && fail_limit(b) == 1) { board_dead_round(b, PIUIO_DEAD_MS); return; }
        }
    }
    memset(dat, 0xFF, sizeof(dat));
    int r = true_libusb_control_transfer(b->handle,
        USB_DIR_IN | USB_TYPE_VENDOR, PIUIO_CTL_REQ,
        0, 0, dat, 8, PIUIO_TIMEOUT_MS);
    switch(piuio_read_result(r, r, 2)) {
        case PIUIO_READ_OK:
            memcpy(b->state, dat, 2);
            board_answered(b);
            break;
        case PIUIO_READ_RELEASE:
            lose_board(b, "is gone");
            break;
        default:
            board_error(b, r);
            board_dead_round(b, PIUIO_DEAD_MS);
            break;
    }
}

// Reads the real IOs, and only that: bytes_piuio is what the real PIUIO / LXIO
// boards say (LX layout), bytes_piuiob what a real button board says. Turning
// one into the other (LX front buttons <-> button board, pads as buttons) is
// done when an emulated device answers the game: piuio_input.h.
//
// A read that fails never changes the state (a zeroed buffer would be "every
// button pressed", coin and SERVICE included: IMPER-116); a board that is
// gone, or has not answered for a while, lets go of everything it held.
//
// The lamps need nothing special when a board comes (back): the current ones
// are written to every board on every round.
void poll_piuio(void){
    pthread_mutex_lock(&boards_mutex);
    if(boards_running) {
        int64_t now = piuio_clock_ms();
        if(now >= next_search_ms) {
            next_search_ms = now + PIUIO_SEARCH_MS;
            search_boards(now);
        }
        for(int k = 0; k < PIUIO_MAX_BOARDS; k++) {
            struct board *b = &boards[k];
            if(!b->handle) continue;
            switch(b->type) {
                case BOARD_PIUIO:  poll_legacy(b); break;
                case BOARD_BUTTON: poll_button_board(b); break;
                default:           poll_lxio(b); break;
            }
            // A slow board must not hold back what the ones before it said
            publish_boards();
        }
        publish_boards();
    }
    pthread_mutex_unlock(&boards_mutex);
}

void API_EXPORTED libusb_piuio_get_status(int* status);
void API_EXPORTED libusb_piuio_get_status(int* status) {
    PRINTF("piuio_emu: libusb_piuio_get_status %d\n", __LINE__);
    *status = 0;

    pthread_mutex_lock(&table_mutex);
    for(int k = 0; k < PIUIO_MAX_BOARDS; k++) {
        if(!boards[k].handle) continue;
        switch(boards[k].type) {
            case BOARD_PIUIO:  *status |= WITH_PIUIO; break;
            case BOARD_LXIO:   *status |= WITH_PIULXIO; break;
            case BOARD_LXIO2:  *status |= WITH_PIULXIO_2; break;
            case BOARD_BUTTON: *status |= WITH_PIUIOBUTTON; break;
        }
    }
    pthread_mutex_unlock(&table_mutex);
}
