#define _GNU_SOURCE
#define _USE_GNU
#include "autoplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <inttypes.h>

#include <sys/mman.h>
#define UNPROTECT(addr,len) (mprotect((void*)(addr-(addr%len)),len,PROT_READ|PROT_WRITE|PROT_EXEC))

// NOTE: For the name, search for "ANDAMIRO CO"

// For the demo vars, use the following: (POST-NXA)
/* For the demo vars, use the following:
    - Search for the "DEMO" parsing. There shall be a strcasecmp with it, and sets certain global in 1
    - The global is the "m_bDemo" variable, which is global and shows the "DEMO MODE"
    - Also sets two variables in 0. Those ones are the judgament that will be chosen
    - Now, as for the actual auto-play, the program sets the judgament into the autoplay at some point
      and puts that variable
    - Then just use the other one, which is usually at +4
 */

/* Code for the demo looks like this "decompliled, in ghidra"
  glPopMatrix();
  glMatrixMode(0x1701);
  glLoadIdentity();
  glOrtho(0,(double)(DAT_08b9b4a8 + -0x80000000) + 2147483648.0,0,
          (double)(DAT_08b9b4ac + -0x80000000) + 2147483648.0,0xc07f400000000000,0x407f400000000000)
  ;
  glMatrixMode(0x1700);
  glLoadIdentity();
  if ((DAT_0e35ce04 & 1) != 0) {
    glPushMatrix();
    glColor4f(0x3f800000,0x3f800000,0x3f800000,0x3f800000);
    FUN_08071730();
    glPopMatrix();
  }
  if ((DAT_0e35ce04 & 2) != 0) {
    glPushMatrix();
    glColor4f(0x3f800000,0x3f800000,0x3f800000,0x3f800000);
    FUN_08071730();
    glPopMatrix();
  }
  uVar9 = 0;
  uVar19 = 0;
  if (DAT_0ab15779 != '\x01') {
    uVar9 = 99;
    if (DAT_0e35ce08 + 1U < 100) {
      uVar9 = DAT_0e35ce08 + 1U;
    }
    uVar19 = uVar9 % 10;
    uVar9 = (uVar9 / 10) % 10;
  }
  uVar8 = (uint)DAT_0e35ce14;
  iVar17 = *(int *)(param_1 + 8 + (uVar19 + 0x2c + uVar8 * 10) * 4);
  iVar22 = *(int *)(param_1 + 0x1dc);
  if (iVar17 != 0) {
    **(int **)(iVar22 + 0x20) = iVar17;
  }
  iVar17 = *(int *)(param_1 + 8 + (uVar9 + 0x2c + uVar8 * 10) * 4);
  if (iVar17 != 0) {
    **(int **)(iVar22 + 0x1c) = iVar17;
  }
  iVar17 = *(int *)(param_1 + 0xb0 + uVar8 * 4);
  if (iVar17 != 0) {
    **(int **)(iVar22 + 0x18) = iVar17;
  }
  cVar6 = DAT_08b99980;
  iVar17 = FUN_0806a570();
  if (-1 < iVar17) {
    FUN_0806b8e0();
    cVar6 = DAT_08b99980;
  }
  uVar9 = 0;
  if (cVar6 != '\0') {
    uVar9 = DAT_0e35ce04 - 1;
  }
  if ((&DAT_0ab7e698)[uVar9 * 0x848c] == -1) {
    if (((&DAT_0ab7e694)[uVar9 * 0x848c] != -1) && ((DAT_0e35ce04 >> (uVar9 & 0x1f) & 1) != 0)) { 
      FUN_08063f40();
    }
  }   
  else if ((DAT_0e35ce04 >> (uVar9 & 0x1f) & 1) != 0) {
    FUN_08063f40();
  }
  uVar9 = 1;
  if (DAT_08b99980 != '\0') {
    uVar9 = DAT_0e35ce04 - 1;
  }
  if ((((&DAT_0ab7e698)[uVar9 * 0x848c] != -1) || ((&DAT_0ab7e694)[uVar9 * 0x848c] != -1)) &&
     ((DAT_0e35ce04 >> (uVar9 & 0x1f) & 1) != 0)) {
    FUN_08063f40();
  }

  See line 80. DAT_0ab7e694 is the judgament, but DAT_0ab7e698 is the real one

 */
// Anything pre-NXA is not needed. Those are just version 1 and the address just works
// You need to put 1 

// As for NX, it needs a VERSION2
// The demo variable is encapsulated in the PLAY object treated as a 
/*
(gdb) print *((char**)(*(int*)($esp+4) + 0x14018 + 16*0x18))
$46 = 0x80fa849 "PLAY"

 So, once you find the Begin() AKA function that host the -demo parsing, you will notice also the vtable


                             PTR_FUN_0810f728                                XREF[5]:     FUN_080686c0:080686d3(*), 
                                                                                          FUN_08068bb0:08068bc3(*), 
                                                                                          FUN_080690a0:080690b0(*), 
                                                                                          FUN_08069410:08069420(*), 
                                                                                          FUN_08069780:08069790(*)  
        0810f728 10 94 06 08     addr       FUN_08069410
        0810f72c 80 97 06 08     addr       FUN_08069780
        0810f730 00 aa 06 08     addr       CPlayEngine::Begin()
        0810f734 e0 be 06 08     addr       FUN_0806bee0
        0810f738 80 c6 06 08     addr       FUN_0806c680
        0810f73c b0 df 06 08     addr       FUN_0806dfb0
        0810f740 80 e3 06 08     addr       FUN_0806e380
        0810f744 f8              ??         F8h
        0810f745 fe              ??         FEh
        0810f746 ff              ??         FFh
        0810f747 ff              ??         FFh
        0810f748 00              ??         00h                                              ?  ->  0810f800
        0810f749 f8              ??         F8h
        0810f74a 10              ??         10h
        0810f74b 08              ??         08h
                             PTR_LAB_0810f74c                                XREF[10]:    FUN_080686c0:080686d9(*), 
                                                                                          FUN_080686c0:080686e4(*), 
                                                                                          FUN_08068bb0:08068bc9(*), 
                                                                                          FUN_08068bb0:08068bd4(*), 
                                                                                          FUN_080690a0:080690a1(*), 
                                                                                          FUN_080690a0:080690bc(*), 
                                                                                          FUN_08069410:08069411(*), 
                                                                                          FUN_08069410:0806942c(*), 
                                                                                          FUN_08069780:08069781(*), 
                                                                                          FUN_08069780:0806979c(*)  
        0810f74c 00 0b 07 08     addr       LAB_08070b00
        0810f750 10 0b 07 08     addr       LAB_08070b10
        0810f754 20 0b 07 08     addr       LAB_08070b20
        0810f758 30 0b 07 08     addr       LAB_08070b30
        0810f75c 40 0b 07 08     addr       LAB_08070b40
        0810f760 50 0b 07 08     addr       LAB_08070b50
        0810f764 60 0b 07 08     addr       LAB_08070b60

Any of those will work to get the "proc_to_manager_offset"

As for the proc_manager and GetProc, just search for "PLAY" and you will notice the function with the object call
As is an C++ functuion, the first argument is the proc_manager, the second is the "PLAY", and the function is actually the GetPRoc

 */

// For anything before PREX1, needs version 5
// First, locate the demo state var, which should be the same as the
// game state var. Locate anything that &0x10 is different than zero
// and the "else" condition should call two functions
// The first one is the random judgament

typedef void* (*procmanager_GetProc)(void* obj, const char* name);

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

struct autoplay_construct all_contstructs[] = {
    // PIU 1st, Schaff Compilation new version
    {
        .version = 0,
        .cmp_name = "IT UP",
        .cmp_ver = "VER 0.53.1999.9.31",
        .name = (void*)(0x8134ade + 5),
        .ver = (void*)(0x8134e43),
        .demo_var = (void*)0, // TODO
        .p1_a = (void*)0, // TODO
        .p2_a = (void*)0,
        .const_val = 0x1,
    },
    // PIU 2nd, Schaff Compilation new version
    {
        .version = 0,
        .cmp_name = "IT UP",
        .cmp_ver = "Dec 27 1999",
        .name = (void*)(0x812e6da + 5),
        .ver = (void*)(0x812e7e8),
        .demo_var = (void*)0, // TODO
        .p1_a = (void*)0, // TODO
        .p2_a = (void*)0,
        .const_val = 0x1,
    },
    // PIU OBG, Schaff Compilation new version
    {
        .version = 0, //5,
        .cmp_name = "it up 3rd (v%d.%02d)",
        .cmp_ver = "May 07 2000",
        .name = (void*)(0x80ffc06 + 5),
        .ver = (void*)(0x80ffc22),
        .demo_var = (void*) 0, //0x81081ca, // orig: data_108bac // TODO: Does not work
        .p1_a = (void*)0, //0x844e22a, // orig: data_44ec0c
        .p2_a = (void*)0, //0x844e22a,
        .const_val = 0x1,
        .rjumps = (const char*[]){
            "\x90\x90", 
            "\x90\x90\x90\x90\x90\x90"}, // NOPs only
        .jumps = (void*[]){(void*)0x8051678, (void*)0x80518cc},
        .sjumps = (int[]){2, 6},
        .njumps = 2,
        .rand_judgament = (void*)0, // Just a regular rand
    },
    // PIU OBG SE, Schaff Compilation new version
    {
        .version = 0,//5,
        .cmp_name = "IT UP (R4/v%d.%02d)",
        .cmp_ver = "Aug 27 2000",
        .name = (void*)(0x8118e0f + 5),
        .ver = (void*)(0x8118e2b),
        .demo_var = (void*)0, //0x81234df, // orig: data_12ad28 // TODO: Does not work
        .p1_a = (void*)0, //0x8459ab3, // orig: data_4612fc
        .p2_a = (void*)0, //0x8459ab3,
        .const_val = 0x1,
        .rjumps = (const char*[]){
            "\x90\x90", 
            "\x90\x90"}, // NOPs only
        .jumps = (void*[]){(void*)0x80519c1, (void*)0x8051c24},
        .sjumps = (int[]){2, 2},
        .njumps = 2,
        .rand_judgament = (void*)0, // Just a regular rand
    },
    // PIU The collection, Schaff Compilation new version
    {
        .version = 0, //5,
        .cmp_name = "IT UP (R%d/v%d.%02d)",
        .cmp_ver = "Nov 14 2000",
        .name = (void*)(0x8119f16 + 5),
        .ver = (void*)(0x8119f32),
        .demo_var = (void*)0, //0x8124d4e, // orig: data_12b6a4 // TODO: Does not work
        .p1_a = (void*)0, //0x86df096, // orig: data_6e59ec
        .p2_a = (void*)0,
        .const_val = 0x1,
        .rjumps = (const char*[]){
            "\x90\x90", 
            "\x90\x90"}, // NOPs only
        .jumps = (void*[]){(void*)0x80519c0, (void*)0x8051c1c},
        .sjumps = (int[]){2, 2},
        .njumps = 2,
        .rand_judgament = (void*)0, // Just a regular rand
    },
    // PIU Perfect collection, Schaff Compilation new version
    {
        .version = 0, //5,
        .cmp_name = "IT UP (R%d/v%d.%02d)",
        .cmp_ver = "Dec 18 2000",
        .name = (void*)(0x8115ec1 + 5),
        .ver = (void*)(0x8115edb),
        .demo_var = (void*)0, //0x81206d2, // orig: data_12b028 // TODO: Does not work
        .p1_a = (void*)0, //0x86d6fc2, // orig: data_6e1918
        .p2_a = (void*)0, //0x86d6fc2,
        .const_val = 0x1,
        .rjumps = (const char*[]){
            "\x90\x90", 
            "\x90\x90"}, // NOPs only
        .jumps = (void*[]){(void*)0x8051469, (void*)0x8051692},
        .sjumps = (int[]){2, 2},
        .njumps = 2,
        .rand_judgament = (void*)0x8050e9d, // func_23e2a
    },
    // PIU Extra, Schaff Compilation new version
    {
        .version = 0, //5,
        .cmp_name = "IT UP (R4/v%d.%02d)",
        .cmp_ver = "Mar 08 2001",
        .name = (void*)(0x8124cae + 5),
        .ver = (void*)(0x8124cc7),
        .demo_var = (void*)0, //0x81c4096, // orig: data_1cfb08 // TODO: Does not work
        .p1_a = (void*)0, //0x82fbe96, // orig: data_307908
        .p2_a = (void*)0, //0x82fbe96,
        .const_val = 0x1,
        .rjumps = (const char*[]){
            "\x90\x90", 
            "\x90\x90\x90\x90\x90\x90"}, // NOPs only
        .jumps = (void*[]){(void*)0x805565a, (void*)0x8055bf1},
        .sjumps = (int[]){2, 6},
        .njumps = 2,
        .rand_judgament = (void*)0, // Does not exist.
    },
    // PIU Premiere 1, Schaff Compilation new version
    {
        .version =0, // 5,
        .cmp_name = "IT UP (R%d/v%d.%02d)",
        .cmp_ver = "Feb 22 2001",
        .name = (void*)(0x8116eae + 5),
        .ver = (void*)(0x8116ec8),
        .demo_var = (void*)0, //0x814e65a, // orig: data_157fb0 // TODO: Does not work
        .p1_a = (void*)0, //0x887d60a, // orig: data_886f60
        .p2_a = (void*)0, //0x887d60a,
        .const_val = 0x1,
        .rjumps = (const char*[]){
            "\x90\x90", 
            "\x90\x90"}, // NOPs only
        .jumps = (void*[]){(void*)0x805170c, (void*)0x805192e},
        .sjumps = (int[]){2, 2},
        .njumps = 2,
        .rand_judgament = (void*)0x8051139, // func_240e0
    },
    // PIU Prex 1, Schaff Compilation new version
    {
        .version = 0, //5,
        .cmp_name = "IT UP (REV2 / %d)",
        .cmp_ver = "",
        .name = (void*)(0x811e4f7 + 5),
        .ver = (void*)(0x811e4f7 + 5), // TODO: The version is still not patched
        .demo_var = (void*)0, //0x81293ce, // orig: data_12bd20
        .p1_a = (void*)0, //0x89c6cd2, // orig: data_9c9624 // NOTE: Does not work
        .p2_a = (void*)0, //0x89c6cd2, // orig: data_9c9624
        .const_val = 0x1,
        .rjumps = (const char*[]){
            "\x90\x90", 
            "\x90\x90"}, // NOPs only
        .jumps = (void*[]){(void*)0x805568d, (void*)0x8055b16},
        .sjumps = (int[]){2, 2},
        .njumps = 2,
        .rand_judgament = (void*)0x80543cf, // func_27368
    },
    // PIU Rebirth, Schaff Compilation new version
    {
        .version = 4,
        .cmp_name = "IT UP (REBIRTH / %d)",
        .cmp_ver = "",
        .name = (void*)(0x8122437 + 5),
        .ver = (void*)(0x8122451),
        .demo_var = (void*)0x813257a, // orig: data_140ecc
        .p1_a = (void*)0x8a59366, // orig: data_a67cb8
        .p2_a = (void*)0x8a59366,
        .const_val = 0x1,
    },
    // PIU Premiere 2, Schaff Compilation new version
    {
        .version = 4,
        .cmp_name = "IT UP (PREMIERE 2/%d)",
        .cmp_ver = "CO.,LTD.",
        .name = (void*)(0x8122527 + 5),
        .ver = (void*)(0x8122542 + 20),
        .demo_var = (void*)0x813267a, // orig: data_140fcc
        .p1_a = (void*)0x8a596e6, // orig: data_a68038
        .p2_a = (void*)0x8a596e6,
        .const_val = 0x1,
    },
    // PIU Prex 2, Schaff Compilation new version
    {
        .version = 4,
        .cmp_name = "IT UP (PREX 2/%d)",
        .cmp_ver = "CO.,LTD.",
        .name = (void*)(0x81271a7 + 5),
        .ver = (void*)(0x81271be + 20),
        .demo_var = (void*)0x8137876, // orig: data_1411c4
        .p1_a = (void*)0x8a4e9e6, // orig: data_a58334
        .p2_a = (void*)0x8a4e9e6,
        .const_val = 0x1,
    },
    // PIU Premiere 3, Schaff Compilation new version
    {
        .version = 4,
        .cmp_name = "IT UP (PREMIERE 3/%d)",
        .cmp_ver = "CO.,LTD.",
        .name = (void*)(0x8128c63 + 5),
        .ver = (void*)(0x8128c7e + 20),
        .demo_var = (void*)0x813895d, // Actually not used, originally  data_14152c
        .p1_a = (void*)0x8a54246, // 1 and 2 are the same, originally data_a5c3f4
        .p2_a = (void*)0x8a54246,
        .const_val = 0x1,
    },
    // PIU Prex 3, Schaff Compilation new version
    {
        .version = 4,
        .cmp_name = "IT UP (PREX 3 / %d)",
        .cmp_ver = "CO.,LTD.",
        .name = (void*)(0x812825e + 5),
        .ver = (void*)(0x8128277 + 20),
        .demo_var = (void*)0x813895d, // Actually not used, originally  data_1416bc
        .p1_a = (void*)0x8a53829, // 1 and 2 are the same, originally data_a5c588
        .p2_a = (void*)0x8a53829,
        .const_val = 0x1,
    },
    // PIU Exceed 1, Mini
    {
        .version = 3,
        .cmp_name = "IT UP - THE EXCEED",
        .cmp_ver = "(BUILD:%d)",
        .name = (void*)(0x806fd9e + 5),
        .ver = (void*)0x8077aa8,
        .demo_var = (void*)0x80a8d74, // Actually, not used
        .p1_a = (void*)0x3c91d, // 1 and 2 are the same
        .p2_a = (void*)0x3c91d,
        .proc_manager = (void*)0x80a8dd0,
        .procmanager_GetProc = (void*)0x805e7c4,
        .proc_to_manager_offset = (void*)0xfffcf008, // Got it from the virtual table
        .const_val = 1,
    },
    // PIU Exceed 2, Dec  7 2004
    /*   
  if ((*(char *)(param_1 + 0x4abf1) != '\0') || ((m_bDemo & 0x1000000) != 0)) {
    FUN_0805fbfe(param_1,0);
    FUN_0805fbfe();
  }
    */
    {
        .version = 3,
        .cmp_name = "IT UP - THE EXCEED 2",
        .cmp_ver = "Dec  7 2004",
        .name = (void*)(0x8079ada + 5),
        .ver = (void*)0x807d258,
        .demo_var = (void*)0x80becd4, // Actually, not used
        .p1_a = (void*)0x4abf1, // 1 and 2 are the same
        .p2_a = (void*)0x4abf1,
        .proc_manager = (void*)0x80bed54,
        .procmanager_GetProc = (void*)0x8061c62,
        .proc_to_manager_offset = (void*)0xfffcf008, // Got it from the virtual table
        .const_val = 1,
    },
    // PIU Exceed 2, Aug 18 2005
    /*      
  if ((*(char *)(param_1 + 0x4abf1) != '\0') || ((m_bDemo & 0x1000000) != 0)) {
    FUN_0805ebee(param_1,0);
    FUN_0805ebee();
  }
    */
    {
        .version = 3,
        .cmp_name = "IT UP - THE EXCEED 2",
        .cmp_ver = "Aug 18 2005",
        .name = (void*)(0x80781ba + 5),
        .ver = (void*)0x807b721,
        .demo_var = (void*)0x80bae14, // Actually, not used
        .p1_a = (void*)0x4abf1, // 1 and 2 are the same
        .p2_a = (void*)0x4abf1,
        .proc_manager = (void*)0x80bae90,
        .procmanager_GetProc = (void*)0x8060bde,
        .proc_to_manager_offset = (void*)0xfffcf008, // Got it from the virtual table
        .const_val = 1,
    },
    // PIU ZERO v03
    /*
         
  iVar5 = FUN_08059000(DAT_0811702c);
    if ((iVar5 != 0) || (34999 < *(uint *)(param_1 + 0xdce0))) {
      bVar11 = true;
    }
    iVar5 = *(int *)(param_1 + 0xdcd0);
  }
  if ((*(char *)(param_1 + 0xdd7d) != '\0') || ((*(byte *)(iVar5 + 7) & 1) != 0)) {
    if ((*(uint *)(iVar5 + 4) & 0x540) == 0) {
      FUN_0808d4e0(param_1,0,0);
      uVar3 = 0;
      bVar12 = true;
    }
     */
    {
        .version = 3,
        .cmp_name = "IT UP: ZERO",
        .cmp_ver = "(BUILD:1.03)",
        .name = (void*)(0x80ff766 + 5),
        .ver = (void*)0x80ff777,
        .demo_var = (void*)0xdcd0, // Actually, not used
        // 0x9dc8a60 +
        .p1_a = (void*)0xdd7d, // 1 and 2 are the same
        .p2_a = (void*)0xdd7d,
        .proc_manager = (void*)0x8117018,
        .procmanager_GetProc = (void*)0x8057880,
        .proc_to_manager_offset = (void*)0xfffffeec, // Got it from the virtual table
        .const_val = 1,
    },
    // PIU NX v08
    {
        .version = 2,
        .cmp_name = "IT UP: NX",
        .cmp_ver = "(BUILD:1.08)",
        .name = (void*)(0x80fa83a + 5),
        .ver = (void*)0x81140e6,
        .demo_var = (void*)0x81f8901,
        // 0xabd54e0 +
        .p1_a = (void*)0x10480, // 1 and 2 are the same
        .p2_a = (void*)0x10480,
        .proc_manager = (void*)0x814bca0,
        .procmanager_GetProc = (void*)0x805ed50,
        .proc_to_manager_offset = (void*)0xfffffef8, // Got it from the virtual table
        .const_val = 1,
    },
    // PIU NX2 v54
    {
        .version = 1,
        .cmp_name = "IT UP: NX2",
        .cmp_ver = "(BUILD:1.54%s)",
        .name = (void*)(0x80f1a62 + 5),
        .ver = (void*)0x80fa05c,
        .demo_var = (void*)0x81c5d81,
        .p1_a = (void*)0x9e34b6c,
        .p2_a = (void*)0x9e372a0,
        .const_val = 1,
    },
    // PIU NX2 v60
    {
        .version = 1,
        .cmp_name = "IT UP: NX2",
        .cmp_ver = "(BUILD:1.60%s)",
        .name = (void*)(0x80f19c2 + 5),
        .ver = (void*)0x80f9ffc,
        .demo_var = (void*)0x81c60c1,
        .p1_a = (void*)0x9e3a18c,
        .p2_a = (void*)0x9e3c8c0,
        .const_val = 1,
    },
    // PIU NX2 CN
    {
        .version = 1,
        .cmp_name = "IT UP: NXC",
        .cmp_ver = "(BUILD:1.56%s)",
        .name = (void*)(0x80f9bdc + 5),
        .ver = (void*)0x80f9bec,
        .demo_var = (void*)0x81c5f41,
        .p1_a = (void*)0x9e34d2c,
        .p2_a = (void*)0x9e37460,
    },
    // PIU NXA v10
    /*
    iVar19 = (&DAT_09e96580)[iVar18 * 0xd44];
    if (iVar19 == -1) {
      if (*(int *)(&DAT_09e9657c + iVar20) != -1) {
        iVar19 = 0;
        if (DAT_0a84f494 != 0) {
          iVar19 = local_24;
        }
    */
    {
        .version = 0,
        .cmp_name = "IT UP: NXA",
        .cmp_ver = "(BUILD:1.10%s)",
        .name = (void*)(0x8113aca + 5),
        .ver = (void*)0x811caca,
        .demo_var = (void*)0x81edd01,
        .p1_a = (void*)0x9e96580,
        .p2_a = (void*)0x9e99a90,
    },
    // PIU NXA CN
    {
        .version = 0,
        .cmp_name = "IT UP: NXAC",
        .cmp_ver = "(BUILD:1.13%s)",
        .name = (void*)(0x8120b8a + 5),
        .ver = (void*)0x8120b9b,
        .demo_var = (void*)0x81f1e81,
        .p1_a = (void*)0x9e9a6fc,
        .p2_a = (void*)0x9e9dc0c,
    },
    // PIU Fiesta v20
    /*
        iVar9 = iVar19 * 0x211f4;
        iVar22 = (&DAT_0a91de5c)[iVar19 * 0x847d];
        if (iVar22 == -1) {
        if ((&DAT_0a91de58)[iVar19 * 0x847d] != -1) {
            iVar22 = 0;
    */
    {
        .version = 0,
        .cmp_name = "IT UP: FIESTA",
        .cmp_ver = "1.20",
        .name = (void*)(0x812c501 + 5),
        .ver = (void*)0x812ca00,
        .demo_var = (void*)0x81e9280,
        .p1_a = (void*)0xa91de5c,
        .p2_a = (void*)0xa93f050,
    },
    // PIU Fiesta EX v51
    /*
        iVar9 = iVar19 * 0x211f8;
        iVar22 = (&DAT_0a92a1e0)[iVar19 * 0x847e];
        if (iVar22 == -1) {
        if ((&DAT_0a92a1dc)[iVar19 * 0x847e] != -1) {
            iVar22 = 0;
    */
    {
        .version = 0,
        .cmp_name = "IT UP: FIESTA EX",
        .cmp_ver = "1.51",
        .name = (void*)(0x81378a4 + 5),
        .ver = (void*)0x8137e60,
        .demo_var = (void*)0x81f4920,
        .p1_a = (void*)0xa92a1e0,
        .p2_a = (void*)0xa94b3d8,
    },
    // PIU Fiesta 2 v60
    /*
        if ((((&DAT_09440050)[uVar17 * 0x847a] != -1) ||
            (*(int *)(&DAT_0944004c + uVar17 * 0x211e8) != -1)) &&
            ((DAT_09d83764 >> (uVar17 & 0x1f) & 1) != 0)) {
            FUN_080a6de0();
        }
    */
    {
        .version = 0,
        .cmp_name = "IT UP: FIESTA 2(%s)",
        .cmp_ver = "V1.60",
        .name = (void*)(0x81a6bb0 + 5),
        .ver = (void*)0x81a6ce4,
        .demo_var = (void*)0x9d83760,
        .p1_a = (void*)0x9440050,
        .p2_a = (void*)0x9461238,
    },
    {
        .version = 0,
        .cmp_name = "IT UP: FIESTA 2(%s)",
        .cmp_ver = "V1.61",
        .name = (void*)(0x81a7a94 + 5),
        .ver = (void*)0x81a7bdc,
        .demo_var = (void*)0x9d846e0,
        .p1_a = (void*)0x9440fd0,
        .p2_a = (void*)0x94621b8,
    },
    // PIU PRIME JE v17
    // Now, this one was fucked up by snax, we use a slightly different string
    {
        .version = 0,
        .cmp_name = "IT UP: PRIME JP",
        .cmp_ver = "1.17.0",
        .name = (void*)(0x81b39ea + 5), // 81b39ea
        .ver = (void*)0x81b3c04,
        .demo_var = (void*)0xe35e4c0,
        .p1_a = (void*)0xab7fb58,
        .p2_a = (void*)0xaba0d88,
    },
    // PIU PRIME v21 Autoplay variables
    {
        .version = 0,
        .cmp_name = "IT UP: PRIME",
        .cmp_ver = "V1.21.0",
        .name = (void*)(0x81b4355 + 5),
        .ver = (void*)0x81b6b84,
        .demo_var = (void*)0xE35CDE0,
        .p1_a = (void*)0xAB7E678,
        .p2_a = (void*)0xAB9F8A8,
    },
    // PIU PRIME v22 Autoplay variables
    {
        .version = 0,
        .cmp_name = "IT UP: PRIME",
        .cmp_ver = "V1.22.0",
        .name = (void*)(0x81b42d5 + 5),
        .ver = (void*)0x81b6b04,
        .demo_var = (void*)0xe35ce00,
        .p1_a = (void*)0xab7e698,
        .p2_a = (void*)0xab9f8c8,
    },
    // PIU PRIME2 v05_1
    /*   
        if ((((&DAT_0ac1f5c4)[uVar12 * 0x849f] != -1) ||
            (*(int *)(&DAT_0ac1f5c0 + uVar12 * 0x2127c) != -1)) &&
        ((DAT_0aaf0b88 >> (uVar12 & 0x1f) & 1) != 0)) {
        FUN_080f2540();
        }
    */
    {
        .version = 0,
        .cmp_name = "IT UP: PRIME2",
        .cmp_ver = "v2.05.1",
        .name = (void*)(0x8189583 + 5),
        .ver = (void*)0x8189618,
        .demo_var = (void*)0xaaf0b84,
        .p1_a = (void*)0x0ac1f5c4,
        .p2_a = (void*)0x0ac40840,
    },
    // PIU XX v05_1
    /*
        if ((((&DAT_08c2676c)[uVar10 * 0x8479] != -1) ||
            (*(int *)(&DAT_08c26768 + uVar10 * 0x211e4) != -1)) &&
        ((DAT_0a0634e8 >> (uVar10 & 0x1f) & 1) != 0)) {
        FUN_080a1de0();
        }
    */
    {
        .version = 0,
        .cmp_name = "IT UP: XX",
        .cmp_ver = "v2.08.3",
        .name = (void*)(0x81b81a2 + 5),
        .ver = (void*)0x81b8404,
        .demo_var = (void*)0xa0634e4,
        .p1_a = (void*)0x08c2676c,
        .p2_a = (void*)0x08c47950,
    },
    {
        .version = 0,
        .cmp_name = "IT UP: XX",
        .cmp_ver = "v2.08.3",
        .name = (void*)(0x81b81a2 + 5),
        .ver = (void*)0x81b8404,
        .demo_var = (void*)0xa0634e4,
        .p1_a = (void*)0x08c2676c,
        .p2_a = (void*)0x08c47950,
    },
    // PIU Infinity (only for identification)
    {
        .version = 0,
        .cmp_name = "It Up Infinity 1.10.0",
        .cmp_ver = "1.10.0",
        .name = (void*)(0x859abff + 5),
        .ver = (void*)0x859ac13,
        .demo_var = (void*)0,
        .p1_a = (void*)0,
        .p2_a = (void*)0,
    },
};

int size_all_constructs = sizeof(all_contstructs) / sizeof(struct autoplay_construct);

// Extracted from https://stackoverflow.com/questions/1576300/checking-if-a-pointer-is-allocated-memory-or-not

//sigjmp_buf jump;
char always_valid[1] = {0x0};
volatile int signald = 0;

#define UNUSED(x) (void)(x)
void segv (int sig, siginfo_t *si, void* ctx);
void segv (int sig, siginfo_t *si, void* ctx)
{
    ucontext_t* context = ctx;
    UNUSED(sig);
    UNUSED(si);
    //siglongjmp (jump, 1); 
    // Restore it to something valid so it can pass
    signald = 1;
#if defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
    context->uc_mcontext.gregs[REG_EAX] = (int)always_valid;
#else
    #error "Incompatible architecture for this implementation"
#endif
}

static int memcheck (void *x) 
{
    volatile int illegal = 0;
    struct sigaction old_action, new_action;

    // Set up the new signal handler
    new_action.sa_sigaction = segv;
    sigemptyset(&new_action.sa_mask); // No additional signals blocked during handler execution
    new_action.sa_flags = SA_SIGINFO;         // No special flags

    // Save the current handler and set the new one
    if (sigaction(SIGSEGV, &new_action, &old_action) == -1) {
        return -1;
    }

    signald = 0;
    //volatile char c;
    //c = *(char *) (to_check);
    asm volatile ("movl %0, %%eax; mov (%%eax), %%al;":
        :"r"(x)         /* input */
        :"%eax"         /* clobbered register */
    );

    if(signald)
        illegal = 1;

    if (sigaction(SIGSEGV, &old_action, NULL) == -1) {
        return -1;
    }

    return (illegal);
}

unsigned int* player1_auto = NULL;
unsigned int* player2_auto = NULL;
const char* game_name = "NONE";
const char* game_ver = "NONE";

int auto_1 = -1;
int auto_2 = -1;

int auto_available = 0;
int auto_version = 0;
int auto_chosen = 0;
int auto_val = 0;

int auto_patched = 0;
char** auto_orig_jumps;

static void try_get_version_3(int i) {

    procmanager_GetProc func = all_contstructs[i].procmanager_GetProc;
    void* proc_manager = (void*)(*(int*)all_contstructs[i].proc_manager);
    void* prept = (void*)func(proc_manager, "PLAY");
    if(memcheck(prept) != 0) return;
    void* pt = (void*)((int)prept + (int)all_contstructs[i].proc_to_manager_offset);

    if(memcheck(pt) != 0) return;
    /*void* pt2 = (void*)(*(int*)((int)pt + (int)all_contstructs[i].p1_a));
    if(memcheck(pt2) != 0) return;
    player1_auto = (void*)((int)pt2 + 4);
    player2_auto = (void*)((int)pt2 + 4);*/
    player1_auto = (void*)((char*)pt + (int)all_contstructs[i].p1_a);
    player2_auto = (void*)((char*)pt + (int)all_contstructs[i].p2_a);

    printf("Function returned (v3): %p\n", pt);
    printf("p1: %p, p2: %p\n", player1_auto, player2_auto);
    return;
}

void check_autoplay(void) {

    for(int i = 0; i < size_all_constructs; i++) {
        if(memcheck(all_contstructs[i].name) == 0 && memcheck(all_contstructs[i].ver) == 0 && 
            strcmp(all_contstructs[i].name, all_contstructs[i].cmp_name) == 0 && 
            memcmp(all_contstructs[i].ver, all_contstructs[i].cmp_ver, strlen(all_contstructs[i].cmp_ver)) == 0) {
        
            int skip_mem = 0;
            printf("Detected for %s (%s)\n", all_contstructs[i].cmp_name, all_contstructs[i].cmp_ver);
            if(all_contstructs[i].version == 2) {
                procmanager_GetProc func = all_contstructs[i].procmanager_GetProc;
                void* pt = (void*)((int)func(all_contstructs[i].proc_manager, "PLAY") + (int)all_contstructs[i].proc_to_manager_offset);
                printf("Function returned: %p\n", pt);
                player1_auto = (void*)((char*)pt + (int)all_contstructs[i].p1_a);
                player2_auto = (void*)((char*)pt + (int)all_contstructs[i].p2_a);
            }
            else if(all_contstructs[i].version == 3){
                try_get_version_3(i);
                skip_mem = 1;
            }
            else {
                player1_auto = all_contstructs[i].p1_a;
                player2_auto = all_contstructs[i].p2_a;
            }
            // Removing that ugly stuff about the NTDEC
            // Good watermark if people ever copy our stuff
            all_contstructs[i].name -= 5;
            UNPROTECT((int)all_contstructs[i].name, 4096);
            memcpy(all_contstructs[i].name, "PUMP", 4); // Replace HACK/FUCK for PUMP
            if(all_contstructs[i].version == 4) {
                if(strlen(all_contstructs[i].cmp_ver) != 0) {
                    all_contstructs[i].ver -= 20;
                    UNPROTECT((int)all_contstructs[i].ver, 4096);
                    memcpy(all_contstructs[i].ver+10, "ANDAMIRO CO. ", 8+5); // Replace NTDEC/FMG for ANDAMIRO
                }
            }
            if(all_contstructs[i].version == 0) {
                UNPROTECT((int)all_contstructs[i].ver, 4096);
                strcpy(all_contstructs[i].ver, all_contstructs[i].cmp_ver);
            }
            if(all_contstructs[i].version == 5) {
                // Alloc and backup the jumps
                int njumps = all_contstructs[i].njumps;
                auto_orig_jumps = malloc(njumps*sizeof(void*));
                for(int k = 0; k < njumps; k++) {
                    int sjump = all_contstructs[i].sjumps[k];
                    void* jump = all_contstructs[i].jumps[k];
                    auto_orig_jumps[k] = malloc(sjump);
                    memcpy(auto_orig_jumps[k], jump, sjump);
                    UNPROTECT((int)jump, 4096);
                }
            }

            printf("p1: %p, p2: %p\n", player1_auto, player2_auto);
            auto_version = all_contstructs[i].version;
            game_name = all_contstructs[i].name;
            game_ver = all_contstructs[i].ver;
            auto_val = all_contstructs[i].const_val;
            auto_chosen = i;
            if(!(memcheck(player1_auto) == 0 && memcheck(player2_auto) == 0) && !skip_mem)
                 continue;
            auto_available = 1;
            break;
        }
    }
    if(!auto_available) {
        printf("No autoplay support\n");
    }
}

void update_autoplay (void) {
  
    if(!auto_available) return;

    int i = auto_chosen;

    if(auto_version == 3) {
        // The object may not be created when libusb was called
        // so we try to retrieve again
        // we cannot do the autoplay until the object is created
        if(player1_auto == NULL) {
            try_get_version_3(i);
            if(player1_auto == NULL) return; // Still not ready
        }

        // And just put the variable like normal
    }

    if(auto_version == 4) {
        // Version 4 only puts the variable as a char
        if(auto_1 != -1) (*(char*)player1_auto) = 0x1;
        else (*(char*)player1_auto) = 0x0;
        return;
    }

    if(auto_version == 5) {
        // Version 5 is for anything below prex1
        // here, the demo is actually tied to the demo variable (or state)
        // the only way is to patch the executable directly
        if(!auto_patched && auto_1 != -1) {
            // Patch the jumps
            int njumps = all_contstructs[i].njumps;
            for(int k = 0; k < njumps; k++) {
                int sjump = all_contstructs[i].sjumps[k];
                void* jump = all_contstructs[i].jumps[k];
                memcpy(jump, all_contstructs[i].rjumps, sjump);
            }
            auto_patched = 1;
        }
        else if(!auto_patched && auto_1 == -1) {
            // Reverse the jumps
            int njumps = all_contstructs[i].njumps;
            for(int k = 0; k < njumps; k++) {
                int sjump = all_contstructs[i].sjumps[k];
                void* jump = all_contstructs[i].jumps[k];
                memcpy(jump, auto_orig_jumps, sjump);
            }
            auto_patched = 0;
        }

        return;
    }

    // All other versions...
    if(auto_1 != -1)
        (*player1_auto) = auto_version == 0 ? auto_1: auto_val;
    else
        /*(*player1_auto) = auto_version == 0 ? 0xFFFFFFFF : 0*/;

    if(auto_2 != -1)
        (*player2_auto) = auto_version == 0 ? auto_2: auto_val;
    else
        /*(*player2_auto) = auto_version == 0 ? 0xFFFFFFFF : 0*/;
}
