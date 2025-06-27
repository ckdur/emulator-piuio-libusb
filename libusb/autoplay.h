#ifndef _AUTOPLAY_H
#define _AUTOPLAY_H

#ifndef API_EXPORTED
#define API_EXPORTED __attribute__((visibility("default")))
#endif

extern int auto_available;
extern int auto_1;
extern int auto_2;
extern const char* game_name;
extern const char* game_ver;
void check_autoplay(void);
void update_autoplay(void);

struct autoplay_construct {
  int version;
  // All version - matching
  const char* cmp_name;
  const char* cmp_ver;
  char* name;
  char* ver;
  // Versions 0-4,- demovar and judgament
  unsigned char* demo_var;
  unsigned int* p1_a;
  unsigned int* p2_a;
  // Version 2-3 - Proc manager stuff
  void* proc_manager;
  void* procmanager_GetProc;
  void* proc_to_manager_offset;
  unsigned int const_val;
  // Version 5 - Location of jumps and random judgament
  const char** rjumps; // Data to replace

  void** jumps; // Actual location addresses
  int* sjumps; // Size to patch
  int njumps; // Number of jumps
  void* rand_judgament; // Location of the random judgament function
};

void API_EXPORTED report_autoplay(struct autoplay_construct* con);

# endif // _AUTOPLAY_H
