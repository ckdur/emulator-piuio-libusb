// F9 / F10 in keyboard mode (/SETTINGS/.keyboard): put the machine's default
// profile of player 1 / player 2 in, for the old USB-profile games (NX2, NXA,
// Fiesta .. Fiesta 2). The shim only reports the key press: the line "1 @" /
// "2 @" goes to savegen.so's card fifo, which knows whether that side is in
// already (second press = out). Where savegen is not loaded nobody reads the fifo
// and the write fails with ENXIO: nothing happens. The newer games (ampass.so:
// Prime 2, XX, Phoenix) have the same keys through X11, but the game does not
// see the PS/2 keys there (no focus / grab): the shim sends the tap to
// ampass.so's fifo too, which works whatever the focus is.
//
// Host test: ../tests/test_profile_keys.c
#ifndef PROFILE_KEYS_H
#define PROFILE_KEYS_H

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

struct profile_keys { int down[2]; };

// One scan: f9 / f10 = the key is held now. Writes the fifo line of each
// fresh press ("1 @", "2 @") into lines[] and returns how many. Holding a key
// is one press.
static inline int profile_keys_step(struct profile_keys *st, int f9, int f10, const char *lines[2])
{
  int n = 0, now[2] = { f9 != 0, f10 != 0 };
  for(int i = 0; i < 2; i++) {
    if(now[i] && !st->down[i]) lines[n++] = i ? "2 @" : "1 @";
    st->down[i] = now[i];
  }
  return n;
}

static inline int profile_keys_write(const char *fp, const char *line)
{
  // Straight to the kernel: the games' own shims hook open() (titledelay,
  // savegen, the dongle emulator) and one of them hung the poll thread here.
  int fd = (int)syscall(SYS_open, fp, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
  if(fd < 0) return -1;
  char buf[16];
  size_t n = strlen(line);
  memcpy(buf, line, n);
  buf[n++] = '\n';
  int r = syscall(SYS_write, fd, buf, n) == (long)n ? 0 : -1;
  syscall(SYS_close, fd);
  return r;
}

// Tell whoever listens that side `line[0]` ("1 @" / "2 @") was asked for:
// savegen.so's card fifo ("1 @" = default profile in / out) and ampass.so's
// fifo ("1" = tap reader 1 with the [P1] card of ampass.ini, i.e. the
// emulated card whose profile is /SETTINGS/profiles/P1). A game has one of
// them; the other is not there, and its write fails at once, never blocking
// and never creating the fifo. 0 if one took it.
static inline int profile_keys_send(const char *line)
{
  const char *fp = getenv("_SAVEGEN_FIFO");
  if(fp == NULL || *fp == 0) fp = "/tmp/savegen/cards.fifo";
  const char *ap = getenv("_AMPASS_FIFO");
  if(ap == NULL || *ap == 0) ap = "/tmp/ampass.fifo";
  char side[2] = { line[0], 0 };
  int a = profile_keys_write(fp, line);
  int b = profile_keys_write(ap, side);
  return (a == 0 || b == 0) ? 0 : -1;
}

#endif
