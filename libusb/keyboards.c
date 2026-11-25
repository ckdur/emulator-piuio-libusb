#define _GNU_SOURCE
#include "keyboards.h"
#include "profile_keys.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glob.h>
#include <linux/input.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/ioctl.h>

// X11 includes
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

/* Vector of keyboards to open*/
FILE **kbds;
char key_map[KEY_MAX/8+1];
char key_map2[KEY_MAX/8+1];
int nKeyboards = 0;

#define MAX_LINE_LENGTH 512
// tests/test_keyboards_profile.c points these into a folder of its own
#ifndef KEYBOARDS_PROC_DEVICES
#define KEYBOARDS_PROC_DEVICES "/proc/bus/input/devices"
#endif
#ifndef KEYBOARDS_DEV_INPUT
#define KEYBOARDS_DEV_INPUT "/dev/input"
#endif
static bool is_keyboard_device(char *ev_line) {
  // Check if EV line contains keyboard capability (e.g., EV=120013)
  return (strstr(ev_line, "EV=120013") != NULL) || (strstr(ev_line, "EV=1") != NULL);
}

static void extract_event_handler(char *handlers_line, char *event_handler) {
  char *token = strtok(handlers_line, " ");
  while (token != NULL) {
    if (strncmp(token, "event", 5) == 0) {
      strncpy(event_handler, token, MAX_LINE_LENGTH - 1);
      break;
    }
    token = strtok(NULL, " ");
  }
}

/* Opens every keyboard of /proc/bus/input/devices (else /dev/input/by-path/
 * *event-kbd) and adds it to kbds. -> how many were found. Quiet unless
 * `verbose`. */
static int scan_keyboards(int verbose) {
  int found = 0;
  FILE *file = fopen(KEYBOARDS_PROC_DEVICES, "r");
  if (!file) {
    if(verbose) perror("Error opening /proc/bus/input/devices");
    goto other_init_keyboards;
  }

  char line[MAX_LINE_LENGTH];
  bool is_keyboard = false;
  char name[MAX_LINE_LENGTH] = {0};
  char handlers[MAX_LINE_LENGTH] = {0};
  char event_handler[MAX_LINE_LENGTH] = {0};

  while (fgets(line, sizeof(line), file)) {
    // Check if we reached the end of the current device block
    if (strcmp(line, "\n") == 0) {
      // If we identified it as a keyboard, print the details
      if (is_keyboard && event_handler[0]) {
        if(verbose) {
          printf("Keyboard found:\n");
          printf("  Name: %s", name);
          printf("  Event Handler: %s", event_handler);
          printf("\n");
        }

        char event_handler_file[MAX_LINE_LENGTH+32];
        // the handler ends in "\n" when it is the last one of the line
        event_handler[strcspn(event_handler, "\r\n")] = 0;
        snprintf(event_handler_file, MAX_LINE_LENGTH+32, KEYBOARDS_DEV_INPUT "/%s", event_handler);
        if(verbose) printf("  Opening file: %s\n", event_handler_file);
        FILE* keybHandle = fopen(event_handler_file, "r");
        if(keybHandle == NULL) {
          if(verbose) printf("  Not opened\n");
        }
        else {
          FILE **other_kbds = kbds;
          kbds = (FILE**)malloc((nKeyboards+1)*sizeof(FILE*));
          if(nKeyboards) memcpy(kbds, other_kbds, (nKeyboards)*sizeof(FILE*));
          free(other_kbds);
          kbds[nKeyboards++] = keybHandle;
          found++;
        }
      }
      // Reset variables for the next device
      is_keyboard = false;
      name[0] = '\0';
      handlers[0] = '\0';
      event_handler[0] = '\0';
      continue;
    }

    // Check for device name
    if (strncmp(line, "N: Name=", 8) == 0) {
      strncpy(name, line + 8, sizeof(name) - 1);
    }
    // Check for handlers
    else if (strncmp(line, "H: Handlers=", 12) == 0) {
      strncpy(handlers, line + 12, sizeof(handlers) - 1);
      // Extract only the eventX handler
      extract_event_handler(handlers, event_handler);
    }
    // Check for event capabilities
    else if (strncmp(line, "B: EV=", 6) == 0) {
      if (is_keyboard_device(line)) {
        is_keyboard = true;
      }
    }
  }

  fclose(file);
other_init_keyboards:
  if(found == 0) {
    glob_t keyboards;
    if(glob(KEYBOARDS_DEV_INPUT "/by-path/*event-kbd", 0, 0, &keyboards) == 0 && keyboards.gl_pathc > 0) {
        if(verbose) printf("Found %d keyboards\n", (int)keyboards.gl_pathc);
        for(size_t n = 0; n < keyboards.gl_pathc; n++) {
            FILE *k = fopen(keyboards.gl_pathv[n], "r");
            if(verbose) printf("Opening file: %s\n", keyboards.gl_pathv[n]);
            if(k == NULL) continue;
            FILE **other_kbds = kbds;
            kbds = (FILE**)malloc((nKeyboards+1)*sizeof(FILE*));
            if(nKeyboards) memcpy(kbds, other_kbds, (nKeyboards)*sizeof(FILE*));
            free(other_kbds);
            kbds[nKeyboards++] = k;
            found++;
        }
    }
    globfree(&keyboards);
  }
  return found;
}

/* Initializes the keyboards: keyboard mode (/SETTINGS/.keyboard), the
 * keyboard is the PIUIO (pads, buttons; F9 / F10 included) */
char en_load_XNextEvent = 0;
void init_keyboards(void) {
  en_load_XNextEvent = 1;
  scan_keyboards(1);
}

/* Without keyboard mode: a keyboard is only read for F9 / F10 (the machine's
 * default profile of P1 / P2, profile_keys.h). The pads and buttons keep
 * coming from the real board alone: nothing else of the keyboard is looked
 * at, and the X11 key map stays off. No keyboard: nothing happens, and the
 * devices are looked for again every PROFILE_KEYS_RESCAN_MS (one plugged in
 * later). */
static int profile_keys_only = 0;
static struct timespec profile_keys_next_scan, profile_keys_next_poll;
#define PROFILE_KEYS_POLL_MS 25
#define PROFILE_KEYS_RESCAN_MS 5000
void init_profile_keys(void) {
  profile_keys_only = 1;
  scan_keyboards(0);
  clock_gettime(CLOCK_MONOTONIC, &profile_keys_next_scan);
  profile_keys_next_poll = profile_keys_next_scan;
}

static int profile_keys_due(struct timespec *next, long ms) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  if(now.tv_sec < next->tv_sec || (now.tv_sec == next->tv_sec && now.tv_nsec < next->tv_nsec))
    return 0;
  next->tv_sec = now.tv_sec + ms / 1000;
  next->tv_nsec = now.tv_nsec + (ms % 1000) * 1000000L;
  if(next->tv_nsec >= 1000000000L) { next->tv_sec++; next->tv_nsec -= 1000000000L; }
  return 1;
}

char bytes_b[4] = {0xFF,0xFF,0xFF,0xFF};
char bytes_w[2] = {0xFF,0xFF};
char bytes_wf[2] = {0xFF,0xFF};

char load_XNextEvent = 0;
int (*XNextEvent_Original)(Display *display, XEvent *event_return);

int XNextEventEMU(void *disp, void *event_ret)
{
    Display *display = (Display *)disp;
    XEvent *event_return = (XEvent *)event_ret;
    // Load the hook if there isn't it yet
    if(!load_XNextEvent)
    {
        XNextEvent_Original = dlsym(RTLD_NEXT, "XNextEvent");
        load_XNextEvent = 1;
    }

    // Load the original function
    int nRet = XNextEvent_Original(display, event_return);
    if(!en_load_XNextEvent) return nRet;
    XEvent ev; int ks; char press;
    memcpy(&ev, event_return, sizeof(XEvent));

    //printf("CKDUR: XNextEvent with Event = %d\n", ev.type);

    // Do event acording to us
    switch  (ev.type) {
    case KeyPress:
    case KeyRelease:
        press = ev.type==KeyPress?1:0;
        ks = XLookupKeysym(&ev.xkey, 0);

#define SETMAPKEY(byte, place, value) {if(!press) byte[place] |= value; else byte[place] &= ~value;}
        if((ks == XK_KP_Home) || (ks == XK_r))
            SETMAPKEY(bytes_b, 2, 0x1);
        if((ks == XK_KP_Page_Up) || (ks == XK_y))
            SETMAPKEY(bytes_b, 2, 0x2);
        if((ks == XK_KP_Begin) || (ks == XK_g))
            SETMAPKEY(bytes_b, 2, 0x4);
        if((ks == XK_KP_End) || (ks == XK_v))
            SETMAPKEY(bytes_b, 2, 0x8);
        if((ks == XK_KP_Page_Down) || (ks == XK_n))
            SETMAPKEY(bytes_b, 2, 0x10);
        if((ks == XK_u))
            SETMAPKEY(bytes_b, 2, 0x20);
        if((ks == XK_i))
            SETMAPKEY(bytes_b, 2, 0x40);
        if((ks == XK_o))
            SETMAPKEY(bytes_b, 2, 0x80);

        if((ks == XK_q))
            SETMAPKEY(bytes_b, 0, 0x1);
        if((ks == XK_e))
            SETMAPKEY(bytes_b, 0, 0x2);
        if((ks == XK_s))
            SETMAPKEY(bytes_b, 0, 0x4);
        if((ks == XK_z))
            SETMAPKEY(bytes_b, 0, 0x8);
        if((ks == XK_c))
            SETMAPKEY(bytes_b, 0, 0x10);
        if((ks == XK_p))
            SETMAPKEY(bytes_b, 0, 0x20);
        if((ks == XK_j))
            SETMAPKEY(bytes_b, 0, 0x40);
        if((ks == XK_k))
            SETMAPKEY(bytes_b, 0, 0x80);

        if((ks == XK_l))
            SETMAPKEY(bytes_b, 3, 0x1);
        if((ks == XK_m))
            SETMAPKEY(bytes_b, 3, 0x2);
        if((ks == XK_6))
            SETMAPKEY(bytes_b, 3, 0x4);
        if((ks == XK_7))
            SETMAPKEY(bytes_b, 3, 0x8);
        if((ks == XK_8))
            SETMAPKEY(bytes_b, 3, 0x10);
        if((ks == XK_9))
            SETMAPKEY(bytes_b, 3, 0x20);
        if((ks == XK_0))
            SETMAPKEY(bytes_b, 3, 0x40);
        if((ks == XK_comma))
            SETMAPKEY(bytes_b, 3, 0x80);
            
        if((ks == XK_F5))
            SETMAPKEY(bytes_b, 1, 0x1);
        if((ks == XK_F6))
            SETMAPKEY(bytes_b, 1, 0x2);
        if((ks == XK_F7))
            SETMAPKEY(bytes_b, 1, 0x4);
        if((ks == XK_F8))
            SETMAPKEY(bytes_b, 1, 0x8);
        if((ks == XK_F9))
            SETMAPKEY(bytes_b, 1, 0x10);
        if((ks == XK_F10))
            SETMAPKEY(bytes_b, 1, 0x20);
        if((ks == XK_F11))
            SETMAPKEY(bytes_b, 1, 0x40);
        if((ks == XK_F12))
            SETMAPKEY(bytes_b, 1, 0x80);

        if((ks == XK_space))
        {
            SETMAPKEY(bytes_b, 2, 0x1F);
            SETMAPKEY(bytes_b, 0, 0x1F);
        }

        if((ks == XK_BackSpace))
            SETMAPKEY(bytes_w, 0, 0x1);
        if((ks == XK_Left))
            SETMAPKEY(bytes_w, 0, 0x2);
        if((ks == XK_Right))
            SETMAPKEY(bytes_w, 0, 0x4);
        if((ks == XK_Return))
            SETMAPKEY(bytes_w, 0, 0x8);
        if((ks == XK_KP_Subtract))
            SETMAPKEY(bytes_w, 0, 0x10);
        if((ks == XK_KP_Left))
            SETMAPKEY(bytes_w, 0, 0x20);
        if((ks == XK_KP_Right))
            SETMAPKEY(bytes_w, 0, 0x40);
        if((ks == XK_KP_Enter))
            SETMAPKEY(bytes_w, 0, 0x80);

        // The two reds of an LXIO front panel, one at a time (see poll_keyboards)
        if((ks == XK_Delete))
            SETMAPKEY(bytes_wf, 0, 0x1);
        if((ks == XK_Next))
            SETMAPKEY(bytes_wf, 0, 0x2);
        if((ks == XK_KP_Divide))
            SETMAPKEY(bytes_wf, 1, 0x1);
        if((ks == XK_KP_Multiply))
            SETMAPKEY(bytes_wf, 1, 0x2);

        //printf("Final state: %x\n", *(int*)bytes_p);

        /*if(bFastFire)
        {
            //bStateFastFire++;
            //if(bStateFastFire >= bMaxStateFastFire) bStateFastFire = 0;
            tick = get_time_ms();
            if((tick-last_tick) >= ((double) bMaxStateFastFire)) last_tick = tick;
            bytes_p[2] &= (tick-last_tick) < ((double) bMaxStateFastFire/2)?0xE0:0xFF;
            bytes_p[0] &= (tick-last_tick) < ((double) bMaxStateFastFire/2)?0xE0:0xFF;
        }*/
        break;
    }
    
    return nRet;
}

#define keyp(keymap, key) (keymap[key/8] & (1 << (key % 8)))
static struct profile_keys prof_keys;
// F9 / F10 alone (no keyboard mode): at most every PROFILE_KEYS_POLL_MS
static void poll_profile_keys_only(void) {
  if(nKeyboards == 0) {
    if(profile_keys_due(&profile_keys_next_scan, PROFILE_KEYS_RESCAN_MS)) scan_keyboards(0);
    if(nKeyboards == 0) return;
  }
  if(!profile_keys_due(&profile_keys_next_poll, PROFILE_KEYS_POLL_MS)) return;
  int f9_held = 0, f10_held = 0;
  for(int i = 0; i < nKeyboards; i++) if(kbds[i] != NULL) {
    memset(key_map, 0, sizeof(key_map));
    if(ioctl(fileno(kbds[i]), EVIOCGKEY(sizeof(key_map)), key_map) < 0) continue;   // unplugged
    if(keyp(key_map, KEY_F9)) f9_held = 1;
    if(keyp(key_map, KEY_F10)) f10_held = 1;
  }
  int l[2];
  int n = profile_keys_step(&prof_keys, f9_held, f10_held, l);
  for(int i = 0; i < n; i++) profile_keys_send(l[i]);
}

void poll_keyboards(void) {
  int f9_held = 0, f10_held = 0;
  unsigned char keyb_in_p[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  unsigned char keyb_in_pb[2] = {0xFF, 0xFF};
  unsigned char keyb_in_pf[2] = {0xFF, 0xFF};
  if(profile_keys_only) {
    // the keyboard is no PIUIO here: bytes_p / bytes_pb / bytes_pf stay
    // "nothing pressed" (bytes_b & co. too: XNextEventEMU is off)
    poll_profile_keys_only();
    memcpy(bytes_p, keyb_in_p, 4);
    memcpy(bytes_pb, keyb_in_pb, 2);
    memcpy(bytes_pf, keyb_in_pf, 2);
    return;
  }
  for(int i = 0; i < nKeyboards; i++) if(kbds[i] != NULL)
  {
    memset(key_map, 0, sizeof(key_map));    //  Initate the array to zero's
    ioctl(fileno(kbds[i]), EVIOCGKEY(sizeof(key_map)), key_map);    //  Fill the keymap with the current keyboard state

    char byte = 0;
    if(keyp(key_map, KEY_KP7) || keyp(key_map, KEY_R))
        byte |= (0x1);
    if(keyp(key_map, KEY_KP9) || keyp(key_map, KEY_Y))
        byte |= (0x2);
    if(keyp(key_map, KEY_KP5) || keyp(key_map, KEY_G))
        byte |= (0x4);
    if(keyp(key_map, KEY_KP1) || keyp(key_map, KEY_V))
        byte |= (0x8);
    if(keyp(key_map, KEY_KP3) || keyp(key_map, KEY_N))
        byte |= (0x10);
    if(keyp(key_map, KEY_U))
        byte |= (0x20);
    if(keyp(key_map, KEY_I))
        byte |= (0x40);
    if(keyp(key_map, KEY_O))
        byte |= (0x80);
    keyb_in_p[2] &= ~byte;

    byte = 0;
    if(keyp(key_map, KEY_Q))
        byte |= (0x1);
    if(keyp(key_map, KEY_E))
        byte |= (0x2);
    if(keyp(key_map, KEY_S))
        byte |= (0x4);
    if(keyp(key_map, KEY_Z))
        byte |= (0x8);
    if(keyp(key_map, KEY_C))
        byte |= (0x10);
    if(keyp(key_map, KEY_P))
        byte |= (0x20);
    if(keyp(key_map, KEY_J))
        byte |= (0x40);
    if(keyp(key_map, KEY_K))
        byte |= (0x80);
    keyb_in_p[0] &= ~byte;

    byte = 0;
    if(keyp(key_map, KEY_L))
        byte |= (0x1);
    if(keyp(key_map, KEY_M))
        byte |= (0x2);
    if(keyp(key_map, KEY_6))
        byte |= (0x4);
    if(keyp(key_map, KEY_7))
        byte |= (0x8);
    if(keyp(key_map, KEY_8))
        byte |= (0x10);
    if(keyp(key_map, KEY_9))
        byte |= (0x20);
    if(keyp(key_map, KEY_0))
        byte |= (0x40);
    if(keyp(key_map, KEY_COMMA))
        byte |= (0x80);
    keyb_in_p[3] &= ~byte;

      byte = 0;
    if(keyp(key_map, KEY_F5))
        byte |= (0x1);
    if(keyp(key_map, KEY_F6))
        byte |= (0x2);
    if(keyp(key_map, KEY_F7))
        byte |= (0x4);
    if(keyp(key_map, KEY_F8))
        byte |= (0x8);
    if(keyp(key_map, KEY_F9))
        byte |= (0x10);
    if(keyp(key_map, KEY_F10))
        byte |= (0x20);
    if(keyp(key_map, KEY_F11))
        byte |= (0x40);
    if(keyp(key_map, KEY_F12))
        byte |= (0x80);
    keyb_in_p[1] &= ~byte;
    if(keyp(key_map, KEY_F9)) f9_held = 1;
    if(keyp(key_map, KEY_F10)) f10_held = 1;

    if(keyp(key_map, KEY_SPACE))
    {
      keyb_in_p[2] &= 0xE0;
      keyb_in_p[0] &= 0xE0;
    }
    
    byte = 0;
    if(keyp(key_map, KEY_BACKSPACE))
        byte |= (0x1);
    if(keyp(key_map, KEY_LEFT))
        byte |= (0x2);
    if(keyp(key_map, KEY_RIGHT))
        byte |= (0x4);
    if(keyp(key_map, KEY_ENTER))
        byte |= (0x8);
    if(keyp(key_map, KEY_KPMINUS))
        byte |= (0x10);
    if(keyp(key_map, KEY_KP4))
        byte |= (0x20);
    if(keyp(key_map, KEY_KP6))
        byte |= (0x40);
    if(keyp(key_map, KEY_KPENTER))
        byte |= (0x80);
    keyb_in_pb[0] &= ~byte;
    keyb_in_pb[1] &= 0xFF;

    // Front buttons. Backspace / keypad minus are the red button of a
    // button board: an LXIO game gets UL+UR from them, like from the board
    // (it has one red per player). The front panel of an LXIO has two reds:
    // Delete = UL and Page Down = UR for P1 (the keys above the Left and
    // Right arrows, which are DL and DR), keypad / = UL and keypad * = UR
    // for P2. A button-board game takes either as its red (IMPER-113).
    byte = 0;
    if(keyp(key_map, KEY_DELETE))
        byte |= (0x1);
    if(keyp(key_map, KEY_PAGEDOWN))
        byte |= (0x2);
    keyb_in_pf[0] &= ~byte;
    byte = 0;
    if(keyp(key_map, KEY_KPSLASH))
        byte |= (0x1);
    if(keyp(key_map, KEY_KPASTERISK))
        byte |= (0x2);
    keyb_in_pf[1] &= ~byte;
  }
  {
    int l[2];
    int n = profile_keys_step(&prof_keys, f9_held, f10_held, l);
    for(int i = 0; i < n; i++) profile_keys_send(l[i]);
  }
  for(int i = 0; i < 4; i++) {
    keyb_in_p[i] &= bytes_b[i];
  }
  for(int i = 0; i < 2; i++) {
    keyb_in_pb[i] &= bytes_w[i];
  }
  for(int i = 0; i < 2; i++) {
    keyb_in_pf[i] &= bytes_wf[i];
  }
  memcpy(bytes_p, keyb_in_p, 4);
  memcpy(bytes_pb, keyb_in_pb, 2);
  memcpy(bytes_pf, keyb_in_pf, 2);
}
