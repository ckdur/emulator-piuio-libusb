#ifndef _KEYBOARDS_H
#define _KEYBOARDS_H

extern unsigned char bytes_p[4];
extern unsigned char bytes_pb[2];   // front buttons, button board format (one red)
extern unsigned char bytes_pf[2];   // front buttons, LX format: UL and UR apart ([0] P1, [1] P2)
void init_keyboards(void);
void init_profile_keys(void);   // no keyboard mode: F9 / F10 only (profile_keys.h)
void poll_keyboards(void);

int XNextEventEMU(void *disp, void *event_ret);

#endif
