#include "keyboards.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/input.h>
#include <stdbool.h>

/* Vector of keyboards to open*/
FILE **kbds;
int i = 0, k, l;
char key_map[KEY_MAX/8+1];
char key_map2[KEY_MAX/8+1];
int nKeyboards = 0;

#define MAX_LINE_LENGTH 512
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

/* Initializes the keyboards */
void init_keyboards(void) {
  FILE *file = fopen("/proc/bus/input/devices", "r");
  if (!file) {
    perror("Error opening /proc/bus/input/devices");
    return;
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
      if (is_keyboard) {
        printf("Keyboard found:\n");
        printf("  Name: %s", name);
        printf("  Event Handler: %s", event_handler);
        printf("\n");
        
        char event_handler_file[MAX_LINE_LENGTH+32];
        snprintf(event_handler_file, MAX_LINE_LENGTH+32, "/dev/input/%s", event_handler);
        printf("  Opening file: %s\n", event_handler_file);
        FILE* keybHandle = fopen(event_handler_file, "r");
        if(keybHandle == NULL) {
          printf("  Not opened\n");
          continue;
        }
        
        if(nKeyboards == 0) {
          nKeyboards++;
          kbds = (FILE**)malloc(nKeyboards*sizeof(FILE*));
        }
        else {
          FILE **other_kbds = kbds;
          kbds = (FILE**)malloc((nKeyboards+1)*sizeof(FILE*));
          memcpy(kbds, other_kbds, (nKeyboards)*sizeof(FILE*));
          free(other_kbds);
          nKeyboards++;
        }
        kbds[nKeyboards-1] = keybHandle;
      }
      // Reset variables for the next device
      is_keyboard = false;
      name[0] = '\0';
      handlers[0] = '\0';
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
}

#define keyp(keymap, key) (keymap[key/8] & (1 << (key % 8)))
void poll_keyboards(void) {
  bytes_p[0] = 0xFF;
  bytes_p[1] = 0xFF;
  bytes_p[2] = 0xFF;
  bytes_p[3] = 0xFF;
  for(i = 0; i < nKeyboards; i++) if(kbds[i] != NULL)
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
    bytes_p[2] &= ~byte;

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
    bytes_p[0] &= ~byte;

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
    bytes_p[3] &= ~byte;

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
    bytes_p[1] &= ~byte;

    if(keyp(key_map, KEY_SPACE))
    {
      bytes_p[2] &= 0xE0;
      bytes_p[0] &= 0xE0;
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
    bytes_pb[0] = ~byte;
    bytes_pb[1] = 0xFF;
  }
}
