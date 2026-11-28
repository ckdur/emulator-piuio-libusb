// Host test for the autoplay commands of the PIUIO shim's remote-control port
// (libusb/twitch.c), the channel the profile web's live pages use (IMPER-85):
// `req 9` on, `req 10` off, `req 12` -> "a <available> <on>", and autoplay
// goes off when the client quits (`Q`) or the connection drops.
//
//   gcc -std=gnu17 -Wall -I libusb -o /tmp/test_twitch_autoplay tests/test_twitch_autoplay.c libusb/twitch.c -lm
//   /tmp/test_twitch_autoplay

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#include "piuio_emu.h"
#include "twitch.h"
#include "autoplay.h"

// what piuio_emu.c / autoplay.c define in the real library
unsigned char bytes_l[16];
unsigned char bytes_f[16];
unsigned char bytes_t[4];
unsigned char bytes_tb[2];
unsigned char bytes_tf[2];
unsigned char bytes_ts[8];
int auto_available = 1;
int auto_1 = -1;
int auto_2 = -1;
const char* game_name = "IT UP (REV2 / %d)";
const char* game_ver = "1999-2002";

static int fails = 0;
static void check(const char *what, int ok) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) fails++;
}

static void polls(int n) {
    struct timespec ts = {0, 2000000};
    for(int i = 0; i < n; i++) {
        KeyHandler_Twitch_Poll();
        nanosleep(&ts, NULL);
    }
}

static int port;

static int client(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) { perror("connect"); exit(2); }
    polls(5);                       // accepted
    return s;
}

// send one line, poll the shim, return everything that came back
static const char* ask(int s, const char* line) {
    static char out[256];
    send(s, line, strlen(line), 0);
    polls(10);
    struct timeval tv = {0, 200000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int len = 0, n;
    while((n = recv(s, out + len, sizeof(out) - 1 - len, 0)) > 0) len += n;   // until 200 ms of quiet
    out[len] = 0;
    return out;
}

int main(void) {
    char env[16];
    port = 20000 + (getpid() % 20000);
    snprintf(env, sizeof(env), "%d", port);
    setenv("_PIUIOEMU_TWITCH_PORT", env, 1);
    KeyHandler_Twitch_Init();

    int s = client();
    check("state before: available, off", strcmp(ask(s, "req 12\n"), "a 1 0\n") == 0);
    check("req 9 answers ok", strcmp(ask(s, "req 9\n"), "ok\n") == 0);
    check("req 9 turns autoplay on", auto_1 == 0 && auto_2 == 0);
    check("state: on", strcmp(ask(s, "req 12\n"), "a 1 1\n") == 0);
    check("req 10 answers off", strcmp(ask(s, "req 10\n"), "off\n") == 0);
    check("req 10 turns autoplay off", auto_1 == -1 && auto_2 == -1);
    ask(s, "req 9\n");
    check("on again", auto_1 == 0);
    ask(s, "req 10\n");
    // two requests in one packet: both answered, in order (the first used to be lost)
    check("req 9 + req 12 in one packet", strcmp(ask(s, "req 9\nreq 12\n"), "ok\na 1 1\n") == 0 && auto_1 == 0);
    // a forked child that runs the exit handlers (phoenix's helpers end with
    // exit(), and the game's handler calls libusb_exit): the parent's client
    // connection and autoplay stay (IMPER-88)
    pid_t child = fork();
    if(child == 0) { KeyHandler_Twitch_Exit(); _exit(0); }
    waitpid(child, NULL, 0);
    check("a child's exit path: autoplay still on", auto_1 == 0 && auto_2 == 0);
    check("a child's exit path: the client is still served", strcmp(ask(s, "req 12\n"), "a 1 1\n") == 0);
    close(s);                       // the page (piuweb) vanished
    polls(10);
    check("connection dropped: autoplay off", auto_1 == -1 && auto_2 == -1);

    s = client();
    ask(s, "req 9\n");
    check("second client turns it on", auto_1 == 0);
    check("Q answers Q", strcmp(ask(s, "Q\n"), "Q\n") == 0);
    check("client quit: autoplay off", auto_1 == -1 && auto_2 == -1);
    close(s);
    polls(5);

    // Front buttons: `b` is a button board (one red per player), `f` an LXIO
    // front panel (UL and UR apart, IMPER-113); both let go when the client leaves
    s = client();
    ask(s, "b FFEF\n");
    check("b: button board byte (red P2)", bytes_tb[0] == 0xEF && bytes_tb[1] == 0xFF && bytes_tf[0] == 0xFF && bytes_tf[1] == 0xFF);
    ask(s, "b FFFF\nf FDFE\n");
    check("f: LX front buttons, UL of P1 and UR of P2 alone", bytes_tf[0] == 0xFE && bytes_tf[1] == 0xFD && bytes_tb[0] == 0xFF);
    // the status line: TEST is the first of the third group, SERVICE the third
    memset(bytes_f, 0xFF, sizeof(bytes_f));
    bytes_f[8] = (unsigned char)~0x02;
    check("req 2: TEST (0x02) is field 1 of the third group", strcmp(ask(s, "req 2\n"), "11111 11111 01111\n") == 0);
    bytes_f[8] = (unsigned char)~0x40;
    check("req 2: SERVICE (0x40) is field 3", strcmp(ask(s, "req 2\n"), "11111 11111 11011\n") == 0);
    bytes_f[8] = 0xFF;
    close(s);
    polls(10);
    check("connection dropped: front buttons released", bytes_tf[0] == 0xFF && bytes_tf[1] == 0xFF && bytes_tb[0] == 0xFF);

    // A client that is gone before the answers (reset: a killed tool, a page
    // closed with replies unread): the shim's send() to it must not raise
    // SIGPIPE, which would end the game (IMPER-169: Phoenix died this way). The
    // game's own SIGPIPE handling stays as it was (here: the default, fatal).
    for(int round = 0; round < 3; round++) {
        s = client();
        struct linger lg = {1, 0};
        setsockopt(s, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        const char* many = "req 9\nreq 12\nreq 12\nreq 12\n";
        send(s, many, strlen(many), 0);
        close(s);                    // RST, with the requests still to be answered
        polls(20);
    }
    check("reset before the replies: the process lives, autoplay off", auto_1 == -1 && auto_2 == -1);
    struct sigaction sp;
    sigaction(SIGPIPE, NULL, &sp);
    check("SIGPIPE left as the program had it", sp.sa_handler == SIG_DFL);
    s = client();
    check("the next client is served", strcmp(ask(s, "req 12\n"), "a 1 0\n") == 0);
    close(s);
    polls(5);

    auto_available = 0;             // a game with no known flag
    s = client();
    check("no autoplay in this game: a 0 0", strcmp(ask(s, "req 12\n"), "a 0 0\n") == 0);
    close(s);
    polls(5);

    KeyHandler_Twitch_Exit();
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
