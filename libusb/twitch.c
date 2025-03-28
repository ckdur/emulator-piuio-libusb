#include "piuio_emu.h"
#include "twitch.h"
#include "autoplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <math.h>
#include <fcntl.h>

//char bytes_t[4];
//char bytes_tb[2];
char bytes_g[4];
char bytes_gb[2];
int HandleBuffer(int deploy_full);
void HandleRequest(int req);
unsigned long GetCurrentTime(void);

struct command_spec* comms = NULL;
int scomms = 0;
int cap_comms = 128;

int sockfd, newsockfd, portno;
socklen_t clilen;
char buffer[256];
struct sockaddr_in serv_addr, cli_addr;
int n;

/*static void check_comms_capacity(int size) {
  if(size < cap_comms) {
    cap_comms = (size/128 + 1) * 128;
    comms = (struct command_spec*)realloc(comms, sizeof(struct command_spec)*cap_comms);
  }
}*/

static void error(const char *msg)
{
  perror(msg);
  exit(1);
}

void KeyHandler_Twitch_Init(void) {
	bytes_g[0] = 0xFF;
	bytes_g[1] = 0xFF;
	bytes_g[2] = 0xFF;
	bytes_g[3] = 0xFF;
	bytes_gb[0] = 0xFF;
	bytes_gb[1] = 0xFF;
	bytes_t[0] = 0xFF;
	bytes_t[1] = 0xFF;
	bytes_t[2] = 0xFF;
	bytes_t[3] = 0xFF;
	bytes_tb[0] = 0xFF;
	bytes_tb[1] = 0xFF;
	tlastchange = GetCurrentTime();
	
  //pthread_t server;
  
  comms = (struct command_spec*)malloc(sizeof(struct command_spec)*cap_comms);
  
  //if (pthread_mutex_init(&mutex, NULL) != 0) { 
  //  PRINTF("\n mutex init has failed for twitch server\n"); 
  //  return; 
  //} 

	//if(pthread_create(&server, NULL, runserver, NULL) != 0){
	//	puts("Could not create server twitch thread");
	//}
	
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");

  int opt = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  
  const char *val;
  int net_port = 1025;

  if ((val = getenv("_PIUIOEMU_TWITCH_PORT"))) {
    net_port = atoi(val);
    if(net_port <= 0 || net_port >= 65536) net_port = 1025; 
  }

  // Put it non/blocking
  int flags = fcntl(sockfd, F_GETFL);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
  
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(net_port);
  if (bind(sockfd, (struct sockaddr *) &serv_addr,
          sizeof(serv_addr)) < 0) 
          error("ERROR on binding");
  listen(sockfd,5);
  PRINTF("Twitch server listening on port %d\n", net_port);
}

#define MAXBUF (1*1024*1024)
char buf[MAXBUF];
int siz = 0;

char isListen = 0;

void KeyHandler_Twitch_Exit(void) {
  if(isListen || newsockfd >= 0) { 
    send(newsockfd, "Q\n", strlen("Q\n"), 0); // Notify of exit
    shutdown(newsockfd, SHUT_RDWR);
    close(newsockfd); 
    newsockfd = -1; 
    isListen = 0;
  }
  close(sockfd);
}

unsigned long tlastaccept = 0;
static void handle_socket(void){
  if(!isListen) {
    // Try to accept the connection
    clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd, 
               (struct sockaddr *) &cli_addr, 
               &clilen);
    if (newsockfd < 0) {
      if(!(errno == EAGAIN || errno == EWOULDBLOCK)) {
        error("ERROR on accept");
      }
      //continue;
    }
    else {
      isListen = 1;
      tlastaccept = GetCurrentTime();
      return; // Let another iteration handle the socket
    }
  }
  
  else { // Is listening
    bzero(buffer,256);
    n = recv(newsockfd,buffer,255,MSG_DONTWAIT);
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("ERROR reading from socket");
        return;
    }
    else {
#ifdef DIE_AFTER_X_SECONDS
      unsigned long t2 = GetCurrentTime();
      if((t2 - tlastaccept) > (DIE_AFTER_X_SECONDS*1000000))
      {
        PRINTF("Timeout\n");
        isListen = 0;
        close(newsockfd);
        return; // Try in the next iteration to do the accept
      }
#endif
    }
    
    if(buffer[0] == 'Q') {
      PRINTF("Quiting the client\n");
      send(newsockfd, "Q\n", strlen("Q\n"), 0); // Notify of exit
      isListen = 0;
      close(newsockfd);
      return; // Try in the next iteration to do the accept
    }
    
    int req;
    if(n > 0) {
      // Renew the time of last connection
      tlastaccept = GetCurrentTime();
      while((siz+n) >= MAXBUF) {
        req = HandleBuffer(1);
        HandleRequest(req);
      }
      memcpy(buf + siz, buffer, n);
      siz += n;
      //PRINTF("Here is the message (%d): %s\n", n, buffer);
      //n = write(newsockfd, "I got your message", 18);
      //if (n < 0) {
      //  error("ERROR writing to socket");
      //  break;
      //}
    }
    req = HandleBuffer(0);
    HandleRequest(req);
  }
}

static void RebaseBuffer(int index) {
  // It is possible that ask us to rebase beyond the size. In this case
  // Make it empty
  if(index >= siz) {
    siz = 0;
    buf[siz] = 0;
    return;
  }
  if(index <= 0) return; // Do nothing here
  for(int k = 0, l = index; l < siz; k++, l++) {
    buf[k] = buf[l];
  }
  siz -= index;
  buf[siz] = 0;
}

unsigned long GetCurrentTime(void)
{
  struct timespec tv;
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return 1000000 * tv.tv_sec + tv.tv_nsec/1000;
}

double GetBeat(unsigned long t)
{
  return (double)(t-tlastchange) / 60000000.0 * fBPM ;
}

static double GetBeat2(unsigned long t, double BPM)
{
  return (double)(t) / 60000000.0 * BPM ;
}

/*static unsigned long GetTime(double b)
{
  if(b < 0) return 0;
  return (unsigned long) ((b) * 60000000.0 / fBPM );
}

static unsigned long GetTime2(double b, double BPM)
{
  if(b < 0) return 0;
  return (unsigned long) ((b) * 60000000.0 / BPM );
}*/

double GetCurrentBeat(void)
{
  unsigned long t = GetCurrentTime();
  return GetBeat(t);
}

unsigned long delay = 0;
int poll_change_lights = 0;
int poll_change_steps = 0;

int HandleBuffer(int deploy_full) {
  if(siz <= 0) return 0;
  //PRINTF("Entering: HandleBuffer\n");
  buf[siz] = 0; // To treat is as string
  char* bufaux = buf;
  char* bufend = buf + siz;
  int req = STATE_REQUEST_NONE;
  while(siz != 0) {
    char* f = strchr(bufaux, '\n');
    if(f == NULL) {
      // It ended?
      int remsiz = strlen(bufaux);
      if((bufaux+remsiz) >= bufend) {
        // Rebase at the last buffer, maybe transmission is not complete yet
        RebaseBuffer((int)(bufaux - buf));
        break;
      }
      else if(deploy_full) {
        // Drop the buffer
        siz = 0;
        buf[siz] = 0;
        PRINTF("WARN: Buffer dropped\n");
        break;
      }
    }
    else {
      // It found the \n, convert it to \0 and do the parsing
      (*f) = 0;
      //PRINTF("Command: %s\n", bufaux);
      char* f2 = bufaux;
      char* fs = bufaux;
      // Now, analyze the command
      
      while(1) {
        // Try to find the first space
        f2 = strchr(f2, ' ');
        if(f2 == NULL) f2 = f; // If there is no space, just take the command whole
        
        // Nullify the space, so we can treat it like a string
        (*f2) = 0;
        
        // Now, we have here an argument try to see what it is
        // Assume that they are already filtered
        //PRINTF("  Argument: %s\n", fs);
        
        // Common commands
        if(strcmp(fs, "delay")  == 0) {
          char* conv = f2+1;
          long k;
          int arg = sscanf(conv, "%ld", &k);
          if(arg == 1) {
            if(((long)delay + k*1000) <= 0) delay = 0;
            delay = (unsigned long)((long)delay + k*1000);
            if(delay > 30000000) delay = 30000000;
          }
          break;
        }

        if(strcmp(fs, "req")  == 0) {
          char* conv = f2+1;
          int k;
          int arg = sscanf(conv, "%d", &k);
          if(arg == 1 && k < STATE_REQUEST_END) {
            req = k;
          }
          break;
        }

        if(strcmp(fs, "d") == 0) {
          uint32_t st;
          char* conv = f2+1;
          int arg = sscanf(conv, "%x", &st);
          if(arg == 1) {
            uint32_t* pt_g = (uint32_t*)bytes_g;
            (*pt_g) = st;
          }
          break;
        }

        if(strcmp(fs, "b") == 0) {
          uint32_t st;
          char* conv = f2+1;
          int arg = sscanf(conv, "%x", &st);
          if(arg == 1) {
            uint16_t* pt_gb = (uint16_t*)bytes_gb;
            (*pt_gb) = (uint16_t)st;
          }
          break;
        }
        
        // Try to put the search pointer afterwards;
        f2++;
        fs = f2;
        if(f2 >= f) break; // Nothing more to see here
      }

      bufaux = f + 1;
      if(bufaux >= bufend) {
        // Just drop it, nothing else to see here
        siz = 0;
        buf[siz] = 0;
        break;
      }
    }
  }
  //PRINTF("Current state of the buf: %s\n", buf);
  return req;
}

static void OnUpdateBPM(unsigned long bef, double bBPM) {
  // In this function, we want to update the beat of all the stuff, as the reference updated
  
  double beats = GetBeat2(tlastchange - bef, bBPM);
  PRINTF("The number of beats to update: %g\n", beats);

  // NOTE: Here was a procedure to change all beats. It does not exist anymore
}

void KeyHandler_Twitch_Poll(void) {
  KeyHandler_Twitch_UpdateLights(bytes_l);
  
  bytes_t[0] = bytes_g[0];
  bytes_t[1] = bytes_g[1];
  bytes_t[2] = bytes_g[2];
  bytes_t[3] = bytes_g[3];
  bytes_tb[0] = bytes_gb[0];
  bytes_tb[1] = bytes_gb[1];
  unsigned long time = GetCurrentTime();
  
  handle_socket();

#define MAX_PROCS 128
  int expired[MAX_PROCS];
  int count = 0;
  
  // Explore all the commands for now
  for(int i = 0; i < scomms; i++) {
    if(time >= comms[i].time) {
      bytes_t[0] &= comms[i].p1;
      bytes_t[2] &= comms[i].p2;
      bytes_t[1] &= comms[i].s1;
      bytes_t[3] &= comms[i].s2;
      // Mark as expired only if the hold is not vigent
      if(comms[i].isHold) {
        if(time <= comms[i].timeEnd) {
          continue;
        }
      }
      expired[count] = i;
      count++;
      if(count >= MAX_PROCS) break; // Process no more
    }
  }
  // Erase all the commands expired
  if(count > 0) {
    int ie = 0;
    int dest = 0;
    //PRINTF("Count = %d\n", count);
    for(int i = 0; i < scomms; i++) {
      if(expired[ie] != i)
      {
        if(dest != i) { // A little performance
          memcpy(&comms[dest], &comms[i], sizeof(struct command_spec));
          //PRINTF("Source: %d, Destination: %d\n", i, dest);
        }
        dest++;
      }
      else { 
        ie++;
      }
    }
    scomms -= count;
  }
}

double fBPM = 120.0; // Assumed from the beginning
unsigned long tlastchange = 0;
char L1P = 0 ;
char L2P = 0 ;
char L3P = 0 ;
char L4P = 0 ;
char L5P = 0 ;
#define SAMPLES 5
char sensed = 0;
unsigned long tlast[SAMPLES] = {0, 0, 0, 0, 0};
#define TOLERANCE 0.05 // From 0 to 1 (0 to 100%)
void KeyHandler_Twitch_UpdateLights(unsigned char* bytes) {
  
  // Subprogram to detect the BPM and the delay of a song
  
  // Extract the light status
  char L1 = (bytes[2] & 0x80) ? 1:0 ;
  char L2 = (bytes[3] & 0x01) ? 1:0 ;
  char L3 = (bytes[3] & 0x04) ? 1:0 ;
  char L4 = (bytes[3] & 0x02) ? 1:0 ;
  char L5 = (bytes[1] & 0x04) ? 1:0 ;
  
  // Lights rise?
  char L1R = (!L1P && L1) ;
  char L2R = (!L2P && L2) ;
  char L3R = (!L3P && L3) ;
  char L4R = (!L4P && L4) ;
  char L5R = (!L5P && L5) ;
  
  if(L1R || L2R || L3R || L4R || L5R) {
    // Any light just rised. 
    // This is going to be a beat marker
    unsigned long t = GetCurrentTime();
    
    // This is the first time sensed?
    for(int i = SAMPLES-2; i >= 0; i--)
      tlast[i+1] = tlast[i];
    tlast[0] = t;
    if(sensed < SAMPLES) {
      sensed++;
      L1P = L1 ;  L2P = L2 ;  L3P = L3 ;  L4P = L4 ;  L5P = L5 ;
      return;
    }
    
    // Sense the BPM here and see if matches (or a power-of-two equivalent)
    double decBPM = 0.0;
    int count = 0;
    for(int i = 0; i < (SAMPLES-1); i++) {
      // 1/us (1000000 us / 1 s) (60 s / 1 m) = 1/m (or BPM) 
      double dBPM = 60000000.0/(double)(tlast[i] - tlast[i+1]);
      if(dBPM > 0.0) {
        double exp = round(log(fBPM/dBPM)/log(2.0));
        double cBPM = dBPM*pow(2.0, exp); // Co-related BPM
        decBPM += cBPM;
        count++;
      }
    }
    decBPM /= (double)count; 
    
    // We need to avoid that BPM is negative or zero
    if(decBPM > 0.0) {
      // The objective is to get a correlation between the current BPM
      
      // Get the log2 of the detected BPM divided by the current BPM, also round it
      double exp = round(log(fBPM/decBPM)/log(2.0));
      
      // So, lets see if there is a correlation
      double cBPM = decBPM*pow(2.0, exp); // Co-related BPM
      double diff = abs(cBPM - fBPM) / fBPM;
      if(diff > TOLERANCE) {
        // The tolerance test didnt pass. 
        // That means that there is a change of BPM.
        // Careful, this does not detect slow parts obviusly
        while(cBPM > 400.0) // I mean, there is no such song at 400 BPM or more
          cBPM /= 2.0;
        while(cBPM < 80.0) // The slowest BPM in PIU is 80
          cBPM *= 2.0;
        PRINTF("BPM change: %g -> %g (%g), exp=%g, diff=%g\n", fBPM, cBPM, decBPM, exp, diff);
        unsigned long bef = tlastchange;
        double bBPM = fBPM;
        fBPM = cBPM;
        tlastchange = t;
        OnUpdateBPM(bef, bBPM);
      }
    }
  }
  
  
  L1P = L1 ;  L2P = L2 ;  L3P = L3 ;  L4P = L4 ;  L5P = L5 ;
}

unsigned char bytes_f_prev[16] = { // As LX
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 1P
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 2P
    0xFF, 0xFF, // Coins, service
    0xFF, 0xFF, // Frontal buttons in LX mode
    0xFF, 0xFF, 0xFF, 0xFF}; // Probably unused
unsigned char bytes_l_prev[4] = {0x00, 0x00, 0x00, 0x00};

void HandleRequest(int req) {
  char msg[256] = "";
  if(req == STATE_REQUEST_LIGHTS) {
    int p1[5] = {
      (bytes_l[0] & 0x4) ? 1:0, 
      (bytes_l[0] & 0x8) ? 1:0, 
      (bytes_l[0] & 0x10) ? 1:0, 
      (bytes_l[0] & 0x20) ? 1:0, 
      (bytes_l[0] & 0x40) ? 1:0};
    int p2[5] = {
      (bytes_l[2] & 0x4) ? 1:0, 
      (bytes_l[2] & 0x8) ? 1:0, 
      (bytes_l[2] & 0x10) ? 1:0, 
      (bytes_l[2] & 0x20) ? 1:0, 
      (bytes_l[2] & 0x40) ? 1:0};

    // L2(clone): bytes_l[3] & 0x04
    // L1:        bytes_l[2] & 0x80
    // Neon:      bytes_l[1] & 0x04
    // L1(clone): bytes_l[3] & 0x01
    // L2:        bytes_l[3] & 0x02

    int l2c  = (bytes_l[3] & 0x04) ? 1:0 ;
    int l1   = (bytes_l[2] & 0x80) ? 1:0 ;
    int neon = (bytes_l[1] & 0x04) ? 1:0 ;
    int l1c  = (bytes_l[3] & 0x01) ? 1:0 ;
    int l2   = (bytes_l[3] & 0x02) ? 1:0 ;

    snprintf(msg, sizeof(msg), "%d%d%d%d%d %d%d%d%d%d %d%d%d%d%d\n", 
      p1[0], p1[1], p1[2], p1[3], p1[4], 
      p2[0], p2[1], p2[2], p2[3], p2[4], 
      l2c, l1, neon, l1c, l2);

    send(newsockfd, msg, strlen(msg), 0);
    //PRINTF("Sent the light status\n");
  }
  if(req == STATE_REQUEST_LIGHTS_RAW) {
    snprintf(msg, sizeof(msg), "%.2x%.2x%.2x%.2x\n", 
      bytes_l[3], bytes_l[2], bytes_l[1], bytes_l[0]);

    send(newsockfd, msg, strlen(msg), 0);
    //PRINTF("Sent the light raw status\n");
  }
  if(req == STATE_REQUEST_STEP) {
    unsigned char p1r = bytes_f[0] & bytes_f[1] & bytes_f[2] & bytes_f[3];
    unsigned char p2r = bytes_f[4] & bytes_f[5] & bytes_f[6] & bytes_f[7];
    int p1[5] = {
      (p1r & 0x1) ? 1:0, 
      (p1r & 0x2) ? 1:0, 
      (p1r & 0x4) ? 1:0, 
      (p1r & 0x8) ? 1:0, 
      (p1r & 0x10) ? 1:0};
    int p2[5] = {
      (p2r & 0x1) ? 1:0, 
      (p2r & 0x2) ? 1:0, 
      (p2r & 0x4) ? 1:0, 
      (p2r & 0x8) ? 1:0, 
      (p2r & 0x10) ? 1:0};

    int service = (bytes_f[8] & 0x02) ? 1:0;
    int coin1 = (bytes_f[8] & 0x04) ? 1:0;
    int test = (bytes_f[8] & 0x40) ? 1:0;
    int clear = (bytes_f[8] & 0x80) ? 1:0;
    int coin2 = (bytes_f[9] & 0x04) ? 1:0;

    snprintf(msg, sizeof(msg), "%d%d%d%d%d %d%d%d%d%d %d%d%d%d%d\n", 
      p1[0], p1[1], p1[2], p1[3], p1[4], 
      p2[0], p2[1], p2[2], p2[3], p2[4], 
      service, coin1, test, clear, coin2);

    send(newsockfd, msg, strlen(msg), 0);
    //PRINTF("Sent the step status\n");
  }
  if(req == STATE_REQUEST_STEP_RAW) {
    snprintf(msg, sizeof(msg), "%.2x%.2x%.2x%.2x %.2x%.2x%.2x%.2x %.2x%.2x%.2x%.2x\n", 
      bytes_f[3], bytes_f[2], bytes_f[1], bytes_f[0],
      bytes_f[7], bytes_f[6], bytes_f[5], bytes_f[4],
      bytes_f[11], bytes_f[10], bytes_f[9], bytes_f[8]);

    send(newsockfd, msg, strlen(msg), 0);
    //PRINTF("Sent the step raw status\n");
  }
  if(req == STATE_REQUEST_LIGHTS_RECURRENT) {
    poll_change_lights = 1;
    send(newsockfd, "ok\n", strlen("ok\n"), 0);
  }
  if(req == STATE_REQUEST_LIGHTS_RECURRENT_OFF) {
    poll_change_lights = 0;
    send(newsockfd, "off\n", strlen("off\n"), 0);
  }
  if(req == STATE_REQUEST_STEP_RECURRENT) {
    poll_change_steps = 1;
    send(newsockfd, "ok\n", strlen("ok\n"), 0);
  }
  if(req == STATE_REQUEST_STEP_RECURRENT_OFF) {
    poll_change_steps = 0;
    send(newsockfd, "off\n", strlen("off\n"), 0);
  }
  if(req == STATE_AUTOPLAY_ON) {
    auto_1 = 0;
    auto_2 = 0;
    send(newsockfd, "ok\n", strlen("ok\n"), 0);
  }
  if(req == STATE_AUTOPLAY_OFF) {
    auto_1 = -1;
    auto_2 = -1;
    send(newsockfd, "off\n", strlen("off\n"), 0);
  }
  if(req == STATE_GET_GAME_NAME) {
    snprintf(msg, sizeof(msg), "%s,%s\n", 
      game_name, game_ver);

    send(newsockfd, msg, strlen(msg), 0);
    //PRINTF("Sent the step raw status\n");
  }

  unsigned char bytes_l_next[4];  
  memcpy(bytes_l_next, bytes_l, sizeof(bytes_l));
  bytes_l_next[0] &= 0xFC;
  bytes_l_next[2] &= 0xFC;
  if(poll_change_lights && memcmp(bytes_l_prev, bytes_l_next, sizeof(bytes_l)) != 0) {
    snprintf(msg, sizeof(msg), "l %.2x%.2x%.2x%.2x\n", 
      bytes_l_next[3], bytes_l_next[2], bytes_l_next[1], bytes_l_next[0]);

    send(newsockfd, msg, strlen(msg), 0);
  }

  if(poll_change_steps && memcmp(bytes_f_prev, bytes_f, sizeof(bytes_f)) != 0) {
    snprintf(msg, sizeof(msg), "s %.2x%.2x%.2x%.2x %.2x%.2x%.2x%.2x %.2x%.2x%.2x%.2x\n", 
      bytes_f[3], bytes_f[2], bytes_f[1], bytes_f[0],
      bytes_f[7], bytes_f[6], bytes_f[5], bytes_f[4],
      bytes_f[11], bytes_f[10], bytes_f[9], bytes_f[8]);

    send(newsockfd, msg, strlen(msg), 0);
  }
  memcpy(bytes_f_prev, bytes_f, sizeof(bytes_f));
  memcpy(bytes_l_prev, bytes_l_next, sizeof(bytes_l));
}
