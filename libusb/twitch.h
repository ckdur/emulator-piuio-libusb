#ifndef _KEYHANDLER_TWITCH_H
#define _KEYHANDLER_TWITCH_H

extern unsigned char bytes_t[4];
extern unsigned char bytes_tb[2];
extern double fBPM;
extern unsigned long tlastchange;
extern unsigned long delay;
void KeyHandler_Twitch_Init(void);
void KeyHandler_Twitch_Poll(void);
void KeyHandler_Twitch_UpdateLights(unsigned char* bytes);
void KeyHandler_Twitch_Exit(void);
extern char bytes_g[4]; // Purely for graphic purposes

struct command_spec {
  char p1;
  char p2;
  char s1;
  char s2;
  unsigned long time;
  char isHold;
  unsigned long timeEnd;
};

enum STATE_REQUEST_ENUM {
  STATE_REQUEST_NONE,
  STATE_REQUEST_LIGHTS,
  STATE_REQUEST_STEP,
  STATE_REQUEST_LIGHTS_RAW,
  STATE_REQUEST_STEP_RAW,
  STATE_REQUEST_LIGHTS_RECURRENT,
  STATE_REQUEST_STEP_RECURRENT,
  STATE_REQUEST_LIGHTS_RECURRENT_OFF,
  STATE_REQUEST_STEP_RECURRENT_OFF,
  STATE_REQUEST_END
};

extern struct command_spec* comms;
extern int scomms;
extern int auto_2;

unsigned long GetCurrentTime(void);
double GetBeat(unsigned long t);
double GetCurrentBeat(void);

extern double limitAnarchy;
extern double currentAnarchy;
extern int directionAnarchy;

#endif