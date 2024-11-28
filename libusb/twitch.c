#include "piuio_emu.h"
#include "twitch.h"
//#include "autoplay.h"

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
void HandleBuffer(int deploy_full);
unsigned long GetCurrentTime(void);

struct command_spec* comms = NULL;
int scomms = 0;
int cap_comms = 128;

int sockfd, newsockfd, portno;
socklen_t clilen;
char buffer[256];
struct sockaddr_in serv_addr, cli_addr;
int n;

static void check_comms_capacity(int size) {
  if(size < cap_comms) {
    cap_comms = (size/128 + 1) * 128;
    comms = (struct command_spec*)realloc(comms, sizeof(struct command_spec)*cap_comms);
  }
}

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
  //  printf("\n mutex init has failed for twitch server\n"); 
  //  return; 
  //} 

	//if(pthread_create(&server, NULL, runserver, NULL) != 0){
	//	puts("Could not create server twitch thread");
	//}
	
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");
  
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
  printf("Twitch server listening on port %d\n", net_port);
}

#define MAXBUF (1*1024*1024)
char buf[MAXBUF];
int siz = 0;

char isListen = 0;
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
      unsigned long t2 = GetCurrentTime();
      if((t2 - tlastaccept) > 5000000) // 5 seconds
      {
        printf("Timeout\n");
        isListen = 0;
        close(newsockfd);
        return; // Try in the next iteration to do the accept
      }
    }
    
    if(buffer[0] == 'Q') {
      printf("Quiting the client\n");
      isListen = 0;
      close(newsockfd);
      return; // Try in the next iteration to do the accept
    }
    
    if(n > 0) {
      // Renew the time of last connection
      tlastaccept = GetCurrentTime();
      while((siz+n) >= MAXBUF) {
        HandleBuffer(1);
      }
      memcpy(buf + siz, buffer, n);
      siz += n;
      //printf("Here is the message (%d): %s\n", n, buffer);
      //n = write(newsockfd, "I got your message", 18);
      //if (n < 0) {
      //  error("ERROR writing to socket");
      //  break;
      //}
    }
    HandleBuffer(0);
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

double GetCurrentBeat(void)
{
  unsigned long t = GetCurrentTime();
  return GetBeat(t);
}

double limitAnarchy = 0.8;
double currentAnarchy = 0.5;
int directionAnarchy = 0;
int numberUsers = 100;
double constVote = 1.0; // Each person has to vote 1 times
unsigned long tlastVoted = 0;

int ul = 0, ur = 0, ce = 0, dl = 0, dr = 0;
unsigned long delay = 250000; // 5 secs
double maxBeats = 8.0;
void HandleBuffer(int deploy_full) {
  if(siz <= 0) return;
  //printf("Entering: HandleBuffer\n");
  buf[siz] = 0; // To treat is as string
  char* bufaux = buf;
  char* bufend = buf + siz;
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
        printf("WARN: Buffer dropped\n");
        break;
      }
    }
    else {
      // It found the \n, convert it to \0 and do whatever
      (*f) = 0;
      //printf("Command: %s\n", bufaux);
      char* f2 = bufaux;
      char* fs = bufaux;
      char first = 1;
      char p1s[12];
      char p2s[12];
      int pscount = 0;
      memset(p1s, 0xFFFFFFFF, 12);
      memset(p2s, 0xFFFFFFFF, 12);
      struct command_spec spec;
      spec.p1 = 0xFF;
      spec.p2 = 0xFF;
      spec.isHold = 0;
      // To determinate the beat, we need to get the current time
      // add the delay time
      // and also assume for now that we are going to aligh
      // at 1/4 of the beat
      // So, the beat has to be a multiple of 1/4
      unsigned long t = GetCurrentTime() + delay;
      double addBeat = 0.25;
      double addHold = 0.0;
      double beatAlign = 0.25;
      char notadd = 0;
      
      // Now, analyze the command
      
      while(1) {
        // Try to find the first space
        f2 = strchr(f2, ' ');
        if(f2 == NULL) f2 = f; // If there is no space, just take the command whole
        
        // Nullify the space, so we can treat it like a string
        (*f2) = 0;
        
        // Now, we have here an argument try to see what it is
        // Assume that they are already filtered
        //printf("  Argument: %s\n", fs);
        
        // If this is the first one, then try
        if(first) {
          // Common commands
          
          if(strcmp(fs, "delay")  == 0) {
            notadd = 1;
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
          
          if(strcmp(fs, "setlimit") == 0) {
            char* conv = f2+1;
            double k;
            int arg = sscanf(conv, "%lg", &k);
            if(arg == 1) {
              limitAnarchy = k;
              if(limitAnarchy < 0.0) limitAnarchy = 0.0;
              if(limitAnarchy > 1.0) limitAnarchy = 1.0;
            }
            break;
          }
          
          if(strcmp(fs, "constvote") == 0) {
            char* conv = f2+1;
            double k;
            int arg = sscanf(conv, "%lg", &k);
            if(arg == 1) {
              constVote = k;
            }
            break;
          }
          
          if(strcmp(fs, "users") == 0) {
            char* conv = f2+1;
            int k;
            int arg = sscanf(conv, "%d", &k);
            if(arg == 1) {
              numberUsers = k;
            }
            break;
          }
          
          if(memcmp(fs, "freeplay", 8) == 0 || memcmp(fs, "anarchy", 7) == 0) {
            tlastVoted = GetCurrentTime();
            // Modulate the vote
            currentAnarchy -= 1.0/(double)numberUsers/constVote;
            if(currentAnarchy < 0.0) currentAnarchy = 0.0;
            directionAnarchy--;
            break;
          }
          
          if(memcmp(fs, "autoplay", 8) == 0 || memcmp(fs, "democracy", 9) == 0) {
            tlastVoted = GetCurrentTime();
            // Modulate the vote
            currentAnarchy += 1.0/(double)numberUsers/constVote;
            if(currentAnarchy > 1.0) currentAnarchy = 1.0;
            directionAnarchy++;
            break;
          }
        
          if(strcmp(fs, "upleft")  == 0) {
            spec.p2 &= ~0x1;
          }
          if(strcmp(fs, "upright")  == 0) {
            spec.p2 &= ~0x2;
          }
          if(strcmp(fs, "center")  == 0) {
            spec.p2 &= ~0x4;
          }
          if(strcmp(fs, "downleft")  == 0) {
            spec.p2 &= ~0x8;
          }
          if(strcmp(fs, "downright")  == 0) {
            spec.p2 &= ~0x10;
          }
          int len = strlen(fs);
          for(int i = 0; i < len; i++) {
            if(fs[i] == 'q' || fs[i] == '7') {
              p2s[pscount] &= ~0x1;
            }
            if(fs[i] == 'e' || fs[i] == '9') {
              p2s[pscount] &= ~0x2;
            }
            if(fs[i] == 's' || fs[i] == '5') {
              p2s[pscount] &= ~0x4;
            }
            if(fs[i] == 'z' || fs[i] == '1') {
              p2s[pscount] &= ~0x8;
            }
            if(fs[i] == 'c' || fs[i] == '3') {
              p2s[pscount] &= ~0x10;
            }
            if(fs[i] == '-') {
              if(pscount >= 11) {
                break;
              }
              pscount++;
            }
          }
        }
        
        else { 
          // Search for the rest
          
          // x/x
          if('0' <= fs[0] && fs[0] <= '9' &&
             '0' <= fs[2] && fs[2] <= '9' &&
             fs[1] == '/') {
             // Get how much do we need to add
             addBeat += (double)(fs[0] - '0')/(double)(fs[2] - '0');
             // Also get the align
             // The 2 is just for forcing 1/4
             if(fs[2] == '2') beatAlign = 0.25;
             else beatAlign = 1.0/(double)(fs[2] - '0');
          }
          
          // hxxx.xxx
          else if(fs[0] == 'h') {
            char* conv = fs+1;
            double beat = 0.0;
            int arg = sscanf(conv, "%lg", &beat);
            if(arg == 1) {
              if(beat < 8.0) {
                addHold += beat;
                spec.isHold = 1;
              }
            }
          }
          
          // xxx.xxx
          else {
            double beat = 0.0;
            int arg = sscanf(fs, "%lg", &beat);
            if(arg == 1) {
              addBeat += beat;
            }
          }
        }
        
        // Flag as this is not the first one
        first = 0;
        
        // Try to put the search pointer afterwards;
        f2++;
        fs = f2;
        if(f2 >= f) break; // Nothing more to see here
      }
      
      // Calculate the beats
      // We add 1 because adding the if(fmod(beat, beatAlign) == 0.0) is almost always true
      spec.beat = (floor(GetBeat(t) / beatAlign) + 1.0) * beatAlign + addBeat; 
      spec.beatEnd = spec.beat + addHold;
      
      // if this is true, we add the command
      if(addBeat <= maxBeats && strcmp(bufaux, "nothing") != 0 && !notadd && pscount >= 0) {
        for(int k = 0; k <= pscount; k++) {
          spec.p1 = p1s[k];
          spec.p2 = p2s[k];
          
          check_comms_capacity(scomms + 1);
          memcpy(&comms[scomms], &spec, sizeof(struct command_spec));
          scomms++;
          
          printf("Added command (%d:%d): %.2x %.2x %g %g (%d)\n", scomms, cap_comms, spec.p1, spec.p2, spec.beat, spec.beatEnd, spec.isHold);
          
          spec.beat += addBeat;
          spec.beatEnd += addBeat;
        }
        
        
        // TODO: Ok, now we added the command, but now we need to check the holds a little
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
  //printf("Current state of the buf: %s\n", buf);
}

static void OnUpdateBPM(unsigned long bef, double bBPM) {
  // In this function, we want to update the beat of all the stuff, as the reference updated
  
  double beats = GetBeat2(tlastchange - bef, bBPM);
  printf("The number of beats to update: %g\n", beats);
  for(int i = 0; i < scomms; i++) {
    comms[i].beat -= beats;
    comms[i].beatEnd -= beats;
    printf("Command %d changed: %g\n", i, comms[i].beat);
  }
}

int auto_2=0;
unsigned long lastAutoplayChange = 0;
void KeyHandler_Twitch_Poll(void) {
  
  bytes_t[0] = 0xFF;
  bytes_t[1] = 0xFF;
  bytes_t[2] = 0xFF;
  bytes_t[3] = 0xFF;
  bytes_tb[0] = 0xFF;
  bytes_tb[1] = 0xFF;
  unsigned long time = GetCurrentTime();
  double beat = GetBeat(time);
  
  handle_socket();
  
  // Autoplay stuff
  if(currentAnarchy >= limitAnarchy) {
    if((time - lastAutoplayChange) > 1000000) {
      if(currentAnarchy >= 0.99) auto_2 = 0;
      else auto_2 = rand() % 4;
      lastAutoplayChange = time;
    }
  }
  else {
    auto_2 = -1;
  }

#define MAX_PROCS 128
  int expired[MAX_PROCS];
  int count = 0;
  // Erase the last voted at certain number of beats
  if(directionAnarchy != 0) {
    double beatLastVoted = GetBeat(tlastVoted);
    if((beat - beatLastVoted) > 4.0)
      directionAnarchy = 0;
  }
  
  // Explore all the commands for now
  for(int i = 0; i < scomms; i++) {
    if(beat >= comms[i].beat) {
      bytes_t[0] &= comms[i].p1;
      bytes_t[2] &= comms[i].p2;
      bytes_g[0] &= comms[i].p1;
      bytes_g[2] &= comms[i].p2;
      // Mark as expired only if the hold is not vigent
      if(comms[i].isHold) {
        if(beat <= comms[i].beatEnd) {
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
    //printf("Count = %d\n", count);
    for(int i = 0; i < scomms; i++) {
      if(expired[ie] != i)
      {
        if(dest != i) { // A little performance
          memcpy(&comms[dest], &comms[i], sizeof(struct command_spec));
          //printf("Source: %d, Destination: %d\n", i, dest);
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
void KeyHandler_Twitch_UpdateLights(char* bytes) {
  
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
        printf("BPM change: %g -> %g (%g), exp=%g, diff=%g\n", fBPM, cBPM, decBPM, exp, diff);
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
