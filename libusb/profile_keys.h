// F9 / F10: the machine's default profile of player 1 / player 2, in or out.
// Read from any keyboard, with or without keyboard mode (/SETTINGS/.keyboard):
// without it keyboards.c looks at these two keys only, so a keyboard on a
// cabinet never acts as pads or buttons (init_profile_keys). The shim only
// reports the key press: the line "1 @" / "2 @" goes to
//  - savegen.so's card fifo (old USB-profile games NX2, NXA, Fiesta .. Fiesta 2),
//    which knows whether that side is in already (second press = out);
//  - ampass.so's fifo (Prime 2, XX, Phoenix; the same keys through X11 do not
//    reach the game: no focus / grab). There "@" taps the [PN] card when the
//    automatic taps are off (keyboard mode), and takes that side's card off
//    the reader / puts it back when they are on (a real cabinet: the default
//    profile logs in by itself, F9 lets that side play without it).
// Where neither is loaded nobody reads the fifos and the write fails with
// ENXIO: nothing happens.
//
// Host tests: ../tests/test_profile_keys.c, ../tests/test_keyboards_profile.c
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
// savegen.so's card fifo and ampass.so's fifo get the same line. A game has
// one of them; the other is not there, and its write fails at once, never
// blocking and never creating the fifo. 0 if one took it.
static inline int profile_keys_send(const char *line)
{
  const char *fp = getenv("_SAVEGEN_FIFO");
  if(fp == NULL || *fp == 0) fp = "/tmp/savegen/cards.fifo";
  const char *ap = getenv("_AMPASS_FIFO");
  if(ap == NULL || *ap == 0) ap = "/tmp/ampass.fifo";
  int a = profile_keys_write(fp, line);
  int b = profile_keys_write(ap, line);
  return (a == 0 || b == 0) ? 0 : -1;
}

#endif
