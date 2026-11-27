#ifndef PIUIO_H
#define PIUIO_H

#include <stdint.h>

extern unsigned char bytes_piuio[16];
extern unsigned char bytes_piuiob[2];
extern unsigned int is_piuiob; // A real PIUIO button board is connected now
extern unsigned int is_lxio;   // A real LXIO is connected now

// Hot-plug of the real boards (piuio.c). Times in milliseconds
#define PIUIO_MAX_BOARDS     8
#define PIUIO_SEARCH_MS      1000   // how often the device list is looked at
// One transfer. A healthy board answers in well under 1 ms (a full-speed
// board behind a hub adds a few ms of split transactions at the most; a device
// being resumed takes a few tens). A timeout that was not needed costs nothing
// (the state is kept), while every ms of it stalls the boards that work when
// one is dead: 50 ms is far above the first and still short for the second
#define PIUIO_TIMEOUT_MS     50
// A board is let go after this many rounds without one good read, in a row,
// that also lasted this long (fast errors need the time, timeouts the count).
// The same two limits let go of one PIUIO sensor group
#define PIUIO_DEAD_ROUNDS    3
#define PIUIO_DEAD_MS        250
// An LXIO that takes its output report and only has no input report to give:
// it gets longer. PMOD's LXIO driver has always taken a read that fails after
// 1000 ms for a lost board, and no sooner
#define PIUIO_LXIO_QUIET_MS  1000
#define PIUIO_RETRY_MAX_MS   10000  // slowest retry of a board that keeps failing
#define PIUIO_HEALTHY_MS     5000   // working this long forgets its failures
#define PIUIO_LOG_WINDOW_MS  10000
#define PIUIO_LOG_LINES      20     // at most this many messages per window
#define PIUIO_ERROR_LOG_MS   10000  // failed-transfer counts, per board
#define PIUIO_REPEAT_LOG_MS  60000  // "still failing", per board

extern int64_t (*piuio_clock_ms)(void); // monotonic; the tests replace it

void init_piuio(void);
void poll_piuio(void);
void finish_piuio(void);

extern libusb_context *piuio_ctx;

#endif // PIUIO_H