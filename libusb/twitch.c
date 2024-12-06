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
  //  printf("\n mutex init has failed for twitch server\n"); 
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
  printf("Twitch server listening on port %d\n", net_port);
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
        printf("Timeout\n");
        isListen = 0;
        close(newsockfd);
        return; // Try in the next iteration to do the accept
      }
#endif
    }
    
    if(buffer[0] == 'Q') {
      printf("Quiting the client\n");
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
      //printf("Here is the message (%d): %s\n", n, buffer);
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

static unsigned long GetTime(double b)
{
  if(b < 0) return 0;
  return (unsigned long) ((b) * 60000000.0 / fBPM );
}

static unsigned long GetTime2(double b, double BPM)
{
  if(b < 0) return 0;
  return (unsigned long) ((b) * 60000000.0 / BPM );
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
unsigned long delay = 0;
int poll_change_lights = 0;
int poll_change_steps = 0;

int HandleBuffer(int deploy_full) {
  if(siz <= 0) return 0;
  //printf("Entering: HandleBuffer\n");
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
      char* p1s = malloc(1);
      char* p2s = malloc(1);
      int pscount = 1;
      p1s[0] = 0xFF;
      p2s[0] = 0xFF;
      struct command_spec spec;
      spec.p1 = 0xFF;
      spec.p2 = 0xFF;
      spec.s1 = 0xFF;
      spec.s2 = 0xFF;
      spec.isHold = 0;
      // To determinate the beat, we need to get the current time
      // add the delay time
      // and also assume for now that we are going to aligh
      // at 1/4 of the beat
      // So, the beat has to be a multiple of 1/4
      unsigned long t = GetCurrentTime() + delay;
      unsigned long addTime = 0;
      unsigned long addTimeHold = 0;
      char notadd = 0;
      double defAddBeat = 0.25;
      
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

          if(strcmp(fs, "req")  == 0) {
            notadd = 1;
            char* conv = f2+1;
            int k;
            int arg = sscanf(conv, "%d", &k);
            if(arg == 1 && k < STATE_REQUEST_END) {
              req = k;
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
        
          if(strcmp(fs, "coin") == 0) {
            spec.s1 &= ~0x4;
          }
          if(strcmp(fs, "coin2") == 0) {
            spec.s2 &= ~0x4;
          }
        
          if(strcmp(fs, "service") == 0) {
            spec.s1 &= ~0x2;
          }
          if(strcmp(fs, "test") == 0) {
            spec.s1 &= ~0x40;
          }
          if(strcmp(fs, "clear") == 0) {
            spec.s1 &= ~0x80;
          }

          if(strcmp(fs, "p1upleft") == 0) {
            spec.p1 &= ~0x1;
          }
          if(strcmp(fs, "p1upright") == 0) {
            spec.p1 &= ~0x2;
          }
          if(strcmp(fs, "p1center") == 0) {
            spec.p1 &= ~0x4;
          }
          if(strcmp(fs, "p1downleft") == 0) {
            spec.p1 &= ~0x8;
          }
          if(strcmp(fs, "p1downright") == 0) {
            spec.p1 &= ~0x10;
          }

          if(strcmp(fs, "p2upleft") == 0) {
            spec.p2 &= ~0x1;
          }
          if(strcmp(fs, "p2upright") == 0) {
            spec.p2 &= ~0x2;
          }
          if(strcmp(fs, "p2center") == 0) {
            spec.p2 &= ~0x4;
          }
          if(strcmp(fs, "p2downleft") == 0) {
            spec.p2 &= ~0x8;
          }
          if(strcmp(fs, "p2downright") == 0) {
            spec.p2 &= ~0x10;
          }
          int len = strlen(fs);
          for(int i = 0; i < len; i++) {
            if(fs[i] == 'q') {
              p1s[pscount-1] &= ~0x1;
            }
            if(fs[i] == 'e') {
              p1s[pscount-1] &= ~0x2;
            }
            if(fs[i] == 's') {
              p1s[pscount-1] &= ~0x4;
            }
            if(fs[i] == 'z') {
              p1s[pscount-1] &= ~0x8;
            }
            if(fs[i] == 'c') {
              p1s[pscount-1] &= ~0x10;
            }

            if(fs[i] == 'r' || fs[i] == '7') {
              p2s[pscount-1] &= ~0x1;
            }
            if(fs[i] == 'y' || fs[i] == '9') {
              p2s[pscount-1] &= ~0x2;
            }
            if(fs[i] == 'g' || fs[i] == '5') {
              p2s[pscount-1] &= ~0x4;
            }
            if(fs[i] == 'v' || fs[i] == '1') {
              p2s[pscount-1] &= ~0x8;
            }
            if(fs[i] == 'n' || fs[i] == '3') {
              p2s[pscount-1] &= ~0x10;
            }

            if(fs[i] == '-') {
              pscount++;
              p1s = realloc(p1s, pscount);
              p2s = realloc(p1s, pscount);
              p1s[pscount-1] = 0xFF;
              p2s[pscount-1] = 0xFF;
            }
          }
        }
        
        else { 
          // Search for the rest
          
          // x/x
          if('0' <= fs[0] && fs[0] <= '9' &&
             '0' <= fs[2] && fs[2] <= '9' &&
             fs[1] == '/') {

            // Get the current beat
            double beat = GetBeat(t);
            // Get how much do we need to add
            double addBeat = (double)(fs[0] - '0')/(double)(fs[2] - '0');
            // Also get the align
            // The 2 is just for forcing 1/4
            double beatAlign = 0.25;
            if(fs[2] == '2') beatAlign = 0.25;
            else beatAlign = 1.0/(double)(fs[2] - '0');

            // Finally, get the time this is supposed to press
            double nextalign = ceil(beat / beatAlign) * beatAlign;
            double finalbeat = nextalign + addBeat - beat;
            
            addTime = GetTime(finalbeat) + t;
            defAddBeat = addBeat;
          }
          
          // hxxx.xxx
          else if(fs[0] == 'h') {
            unsigned long holdtime = 0;
            int arg = 0;
            if(fs[1] == 'b') {
              // Time is in beats
              double beat = 0.0;
              arg = sscanf(fs+2, "%lg", &beat);
              if(arg == 1) {
                holdtime = GetTime2(beat, fBPM);
              }
            }
            else {
              double time_in_seconds = 0.0;
              arg = sscanf(fs+1, "%lg", &time_in_seconds);
              if(arg == 1) {
                holdtime = (unsigned long)(time_in_seconds * 1000000.0);
              }
            }
            if(arg == 1) {
              addTimeHold += holdtime;
              spec.isHold = 1;
            }
          }
          
          // xxx.xxx
          else {
            unsigned long steptime = 0;
            int arg = 0;
            if(fs[0] == 'b') {
              // Time is in beats
              double beat = 0.0;
              arg = sscanf(fs+1, "%lg", &beat);
              if(arg == 1) {
                steptime = GetTime2(beat, fBPM);
                defAddBeat = beat;
              }
            }
            else {
              double time_in_seconds = 0.0;
              arg = sscanf(fs, "%lg", &time_in_seconds);
              if(arg == 1) {
                steptime = (unsigned long)(time_in_seconds * 1000000.0);
              }
            }
            if(arg == 1) {
              addTime = steptime;
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
      spec.time = t + addTime;
      spec.timeEnd = spec.time + addTimeHold;
      
      // if this is true, we add the command
      if(strcmp(bufaux, "nothing") != 0 && !notadd && pscount > 0) {
        for(int k = 0; k < pscount; k++) {
          spec.p1 = p1s[k];
          spec.p2 = p2s[k];
          
          check_comms_capacity(scomms + 1);
          memcpy(&comms[scomms], &spec, sizeof(struct command_spec));
          scomms++;
          
          printf("Added command at %ld (%d:%d): %.2x %.2x %ld %ld (%d)\n", 
            t, scomms, cap_comms, spec.p1, spec.p2, spec.time, spec.timeEnd, spec.isHold);
          
          spec.time += GetTime2(defAddBeat, fBPM);
        }
      }
      
      bufaux = f + 1;
      if(bufaux >= bufend) {
        // Just drop it, nothing else to see here
        siz = 0;
        buf[siz] = 0;
        break;
      }
      free(p1s);
      free(p2s);
    }
  }
  //printf("Current state of the buf: %s\n", buf);
  return req;
}

static void OnUpdateBPM(unsigned long bef, double bBPM) {
  // In this function, we want to update the beat of all the stuff, as the reference updated
  
  double beats = GetBeat2(tlastchange - bef, bBPM);
  printf("The number of beats to update: %g\n", beats);

  // NOTE: Here was a procedure to change all beats. It does not exist anymore
}

int auto_2=0;
unsigned long lastAutoplayChange = 0;
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
    if((time - tlastVoted) > 4000000)
      directionAnarchy = 0;
  }
  
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
    //printf("Sent the light status\n");
  }
  if(req == STATE_REQUEST_LIGHTS_RAW) {
    snprintf(msg, sizeof(msg), "%.2x%.2x%.2x%.2x\n", 
      bytes_l[3], bytes_l[2], bytes_l[1], bytes_l[0]);

    send(newsockfd, msg, strlen(msg), 0);
    //printf("Sent the light raw status\n");
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
    //printf("Sent the step status\n");
  }
  if(req == STATE_REQUEST_STEP_RAW) {
    snprintf(msg, sizeof(msg), "%.2x%.2x%.2x%.2x %.2x%.2x%.2x%.2x %.2x%.2x%.2x%.2x\n", 
      bytes_f[3], bytes_f[2], bytes_f[1], bytes_f[0],
      bytes_f[7], bytes_f[6], bytes_f[5], bytes_f[4],
      bytes_f[11], bytes_f[10], bytes_f[9], bytes_f[8]);

    send(newsockfd, msg, strlen(msg), 0);
    //printf("Sent the step raw status\n");
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
