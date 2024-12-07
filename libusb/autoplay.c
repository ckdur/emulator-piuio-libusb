#include "autoplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>

// NOTE: For the name, search for "ANDAMIRO CO"

// For the demo vars, use the following:
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

// PIU PRIME v21 Autoplay variables
char* prime1_v21_name = (void*)0x81b4355; // "PUMP IT UP: PRIME"
char*prime1_v21_ver = (void*)0x81b6b84; // "V1.21.0"
unsigned char* prime1_v21_demo_var = (void*)0xE35CDE0;
unsigned int* prime1_v21_player1_auto = (void*)0xAB7E678;
unsigned int* prime1_v21_player2_auto = (void*)0xAB9F8A8;

// PIU PRIME v22 Autoplay variables
char* prime1_v22_name = (void*)0x81b42d5; // "PUMP IT UP: PRIME"
char*prime1_v22_ver = (void*)0x8189618; // "V1.22.0"
unsigned char* prime1_v22_demo_var = (void*)0xe35ce00;
unsigned int* prime1_v22_player1_auto = (void*)0xab7e698;
unsigned int* prime1_v22_player2_auto = (void*)0xab9f8c8;

// PIU PRIME2 v05_1
/*   
  if ((((&DAT_0ac1f5c4)[uVar12 * 0x849f] != -1) ||
      (*(int *)(&DAT_0ac1f5c0 + uVar12 * 0x2127c) != -1)) &&
     ((DAT_0aaf0b88 >> (uVar12 & 0x1f) & 1) != 0)) {
    FUN_080f2540();
  }
 */
char* prime2_v05_1_name = (void*)0x8189583; // "PUMP IT UP: PRIME2"
char* prime2_v05_1_ver = (void*)0x8189618; // "v2.05.1"
unsigned char* prime2_v05_1_demo_var = (void*)0xaaf0b84;
unsigned int* prime2_v05_1_player1_auto = (void*)0x0ac1f5c4;
unsigned int* prime2_v05_1_player2_auto = (void*)0x0ac40840;

// PIU XX v05_1
/*
  if ((((&DAT_08c2676c)[uVar10 * 0x8479] != -1) ||
      (*(int *)(&DAT_08c26768 + uVar10 * 0x211e4) != -1)) &&
     ((DAT_0a0634e8 >> (uVar10 & 0x1f) & 1) != 0)) {
    FUN_080a1de0();
  }
 */

char* xx_v08_3_name = (void*)0x81b81a2; // "PUMP IT UP: XX"
char* xx_v08_3_ver = (void*)0x81b8404; // "v2.08.3"
unsigned char* xx_v08_3_demo_var = (void*)0xa0634e4;
unsigned int* xx_v08_3_player1_auto = (void*)0x08c2676c;
unsigned int* xx_v08_3_player2_auto = (void*)0x08c47950;


// Extracted from https://stackoverflow.com/questions/1576300/checking-if-a-pointer-is-allocated-memory-or-not

jmp_buf jump;

void segv (int sig);
void segv (int sig)
{
    longjmp (jump, 1); 
}

static int memcheck (void *x) 
{
    volatile char c;
    int illegal = 0;
    struct sigaction old_action, new_action;

    // Set up the new signal handler
    new_action.sa_handler = segv;
    sigemptyset(&new_action.sa_mask); // No additional signals blocked during handler execution
    new_action.sa_flags = 0;         // No special flags

    // Save the current handler and set the new one
    if (sigaction(SIGSEGV, &new_action, &old_action) == -1) {
        return -1;
    }

    if (!setjmp (jump))
        c = *(char *) (x);
    else
        illegal = 1;

    if (sigaction(SIGSEGV, &old_action, NULL) == -1) {
        return -1;
    }

    return (illegal);
}

unsigned int* player1_auto = NULL;
unsigned int* player2_auto = NULL;

int auto_1 = -1;
int auto_2 = -1;

int auto_available = 0;
void check_autoplay(void) {

    if(memcheck(prime1_v21_name) == 0 && memcheck(prime1_v21_ver) == 0 && 
        memcheck(prime1_v21_player1_auto) == 0 && memcheck(prime1_v21_player2_auto) == 0 &&
        strcmp(prime1_v21_name, "PUMP IT UP: PRIME") == 0 && strcmp(prime1_v21_ver, "V1.21.0") == 0) {
        
        printf("Detected for PRIME v21\n");
        player1_auto = prime1_v21_player1_auto;
        player2_auto = prime1_v21_player2_auto;
        auto_available = 1;
    }

    else if(memcheck(prime1_v22_name) == 0 && memcheck(prime1_v22_ver) == 0 && 
        memcheck(prime1_v22_player1_auto) == 0 && memcheck(prime1_v22_player2_auto) == 0 &&
        strcmp(prime1_v22_name, "PUMP IT UP: PRIME") == 0 && strcmp(prime1_v22_ver, "V1.22.0") == 0) {
        
        printf("Detected for PRIME v22\n");
        player1_auto = prime1_v22_player1_auto;
        player2_auto = prime1_v22_player2_auto;
        auto_available = 1;
    }

    else if(memcheck(prime2_v05_1_name) == 0 && memcheck(prime2_v05_1_ver) == 0 && 
        memcheck(prime2_v05_1_player1_auto) == 0 && memcheck(prime2_v05_1_player2_auto) == 0 &&
        strcmp(prime2_v05_1_name, "PUMP IT UP: PRIME2") == 0 && strcmp(prime2_v05_1_ver, "v2.05.1") == 0) {
        
        printf("Detected for PRIME2 v05_1\n");
        player1_auto = prime2_v05_1_player1_auto;
        player2_auto = prime2_v05_1_player2_auto;
        auto_available = 1;
    }

    else if(memcheck(xx_v08_3_name) == 0 && memcheck(xx_v08_3_ver) == 0 && 
        memcheck(xx_v08_3_player1_auto) == 0 && memcheck(xx_v08_3_player2_auto) == 0 &&
        strcmp(xx_v08_3_name, "PUMP IT UP: XX") == 0 && strcmp(xx_v08_3_ver, "v2.08.3") == 0) {
        
        printf("Detected for XX v05_1\n");
        player1_auto = xx_v08_3_player1_auto;
        player2_auto = xx_v08_3_player2_auto;
        auto_available = 1;
    }
    else {
        printf("No autpplay support\n");
    }
}
void update_autoplay (void) {
  
    if(!auto_available) return;

    if(auto_1 != -1)
        (*player1_auto) = auto_1;
    else
        (*player1_auto) = 0xFFFFFFFF;

    if(auto_2 != -1)
        (*player2_auto) = auto_2;
    else
        (*player2_auto) = 0xFFFFFFFF;
}
