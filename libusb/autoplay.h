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
  // Optional (check_autoplay only, never read by report_autoplay): address of
  // the game's own DEMO set-up, the three instructions
  //   movb $1,demo_var; movl $0,p1_a-4; movl $0,p2_a-4
  // (c6 05 <demo_var> 01 c7 05 <p1_a-4> 00000000 c7 05 <p2_a-4> 00000000).
  // When set, the construct is used only if these bytes are there exactly, so
  // a build whose strings match but whose layout differs is never poked.
  unsigned char* demo_set;
};

// The 27 bytes check_autoplay expects at con->demo_set (see above).
void autoplay_demo_set_bytes(const struct autoplay_construct* con, unsigned char out[27]);

void API_EXPORTED report_autoplay(struct autoplay_construct* con);

# endif // _AUTOPLAY_H
