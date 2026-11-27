#ifndef PIUIO_INPUT_H
#define PIUIO_INPUT_H

// How the inputs of every source are mixed into what an emulated IO answers.
// No dependency besides <stdint.h>, so that tests/test_piuio_input.c can
// check it on the host. Everything is active low (0xFF = nothing pressed).
//
// Front buttons exist in two formats:
//
//  - LX (the LXIO front panel, 5 buttons per player): one byte per player,
//    bytes 10 (P1) and 11 (P2) of the LXIO IN report, bit 0 UL, 1 UR,
//    2 centre, 3 DL, 4 DR.
//  - board (the PIUIO button board 0x0D2F:0x1010, 4 buttons per player):
//    byte 0 of its answer, bit 0 red, 1 left, 2 right, 3 green; bits 4..7
//    the same for P2.
//
// The board has ONE red button per player, the LXIO has two (UL and UR).
// Rules (IMPER-113):
//  - a source that speaks LX keeps UL and UR apart, all the way to an LXIO
//    game: a real LXIO is passed through byte for byte;
//  - a board source (a real button board, a "red" key) can only mean both:
//    red -> UL+UR for an LXIO game;
//  - towards a button-board game either UL or UR is red.

#include <stdint.h>

#define PIUIO_LX_UL 0x01
#define PIUIO_LX_UR 0x02
#define PIUIO_LX_CN 0x04
#define PIUIO_LX_DL 0x08
#define PIUIO_LX_DR 0x10

#define PIUIO_BOARD_RED   0x01
#define PIUIO_BOARD_LEFT  0x02
#define PIUIO_BOARD_RIGHT 0x04
#define PIUIO_BOARD_GREEN 0x08

// Byte 8 of the LX layout (byte 1 of a PIUIO answer)
#define PIUIO_SYS_TEST    0x02
#define PIUIO_SYS_COIN    0x04
#define PIUIO_SYS_SERVICE 0x40
#define PIUIO_SYS_CLEAR   0x80

// Button board byte 0 -> the LX byte of one player (0 = P1, 1 = P2)
static inline uint8_t piuio_board_to_lx(uint8_t board, int player)
{
    uint8_t b = player ? (uint8_t)(board >> 4) : board;
    uint8_t lx = 0xFF;
    if(!(b & PIUIO_BOARD_RED))   lx &= (uint8_t)~(PIUIO_LX_UL | PIUIO_LX_UR); // the one red button
    if(!(b & PIUIO_BOARD_LEFT))  lx &= (uint8_t)~PIUIO_LX_DL;
    if(!(b & PIUIO_BOARD_RIGHT)) lx &= (uint8_t)~PIUIO_LX_DR;
    if(!(b & PIUIO_BOARD_GREEN)) lx &= (uint8_t)~PIUIO_LX_CN;
    return lx;
}

// The two LX bytes -> button board byte 0
static inline uint8_t piuio_lx_to_board(uint8_t lx1, uint8_t lx2)
{
    uint8_t board = 0xFF;
    if((~lx1) & (PIUIO_LX_UL | PIUIO_LX_UR)) board &= (uint8_t)~PIUIO_BOARD_RED;
    if((~lx1) & PIUIO_LX_DL) board &= (uint8_t)~PIUIO_BOARD_LEFT;
    if((~lx1) & PIUIO_LX_DR) board &= (uint8_t)~PIUIO_BOARD_RIGHT;
    if((~lx1) & PIUIO_LX_CN) board &= (uint8_t)~PIUIO_BOARD_GREEN;
    if((~lx2) & (PIUIO_LX_UL | PIUIO_LX_UR)) board &= (uint8_t)~(PIUIO_BOARD_RED << 4);
    if((~lx2) & PIUIO_LX_DL) board &= (uint8_t)~(PIUIO_BOARD_LEFT << 4);
    if((~lx2) & PIUIO_LX_DR) board &= (uint8_t)~(PIUIO_BOARD_RIGHT << 4);
    if((~lx2) & PIUIO_LX_CN) board &= (uint8_t)~(PIUIO_BOARD_GREEN << 4);
    return board;
}

// The pads as a button board (_PIUIOEMU_EMU_PIUIO_BUTTON): pads[0..3] the
// four sensor bytes of P1, pads[4..7] of P2, bits 0-4 UL UR centre DL DR.
// A panel counts as pressed with any of its sensors, and it is the panel of
// the same name on the front: the same mapping as the LX front buttons.
static inline uint8_t piuio_pads_to_board(const uint8_t pads[8])
{
    uint8_t p1 = pads[0] & pads[1] & pads[2] & pads[3];
    uint8_t p2 = pads[4] & pads[5] & pads[6] & pads[7];
    return piuio_lx_to_board(p1, p2);
}

// TEST <-> SERVICE (_PIUIOEMU_SWAP_TEST_SERVICE, IMPER-115)
static inline uint8_t piuio_swap_test_service(uint8_t sys)
{
    uint8_t out = sys | PIUIO_SYS_TEST | PIUIO_SYS_SERVICE;
    if(!(sys & PIUIO_SYS_TEST))    out &= (uint8_t)~PIUIO_SYS_SERVICE;
    if(!(sys & PIUIO_SYS_SERVICE)) out &= (uint8_t)~PIUIO_SYS_TEST;
    return out;
}

// Every input source of one moment
struct piuio_sources {
    uint8_t io[16];      // real PIUIO / LXIO, in the LXIO IN report layout:
                         // 0..3 P1 sensors, 4..7 P2 sensors, 8/9 coins, TEST,
                         // SERVICE, 10/11 front buttons (LX), 12..15 unused
    uint8_t board[2];    // real button board, as it answers
    uint8_t pad[4];      // keyboard, joystick and network pads, as a PIUIO
                         // answer: [0] P1 panels, [1] = io[8], [2] P2, [3] = io[9]
    uint8_t sensor[8];   // network pads, one byte per sensor like io[0..7]
    uint8_t fboard[2];   // front buttons of those sources, board format
    uint8_t flx[2];      // front buttons of those sources, LX format (P1, P2)
};

static inline void piuio_sources_clear(struct piuio_sources *s)
{
    for(unsigned i = 0; i < sizeof(*s); i++) ((uint8_t *)s)[i] = 0xFF;
}

// The state of the machine as an LXIO reports it (16 bytes). With a real LXIO
// and nothing else pressed this is its report, untouched. It is also the
// physical state the TEST hold and the status feed look at: TEST and SERVICE
// are never swapped here.
static inline void piuio_mix_lx(const struct piuio_sources *s, uint8_t out[16])
{
    for(int i = 0; i < 4; i++) {
        out[i]   = s->io[i]   & s->pad[0] & s->sensor[i];
        out[4+i] = s->io[4+i] & s->pad[2] & s->sensor[4+i];
    }
    out[8] = s->io[8] & s->pad[1];
    out[9] = s->io[9] & s->pad[3];
    uint8_t board = s->board[0] & s->fboard[0];
    out[10] = s->io[10] & s->flx[0] & piuio_board_to_lx(board, 0);
    out[11] = s->io[11] & s->flx[1] & piuio_board_to_lx(board, 1);
    for(int i = 12; i < 16; i++) out[i] = s->io[i];
}

// The state of the machine as a button board reports it (2 bytes). pad_emu:
// the pads press the buttons too (see piuio_pad_emu).
static inline void piuio_mix_board(const struct piuio_sources *s, int pad_emu, uint8_t out[2])
{
    out[0] = s->board[0] & s->fboard[0] &
             piuio_lx_to_board(s->io[10] & s->flx[0], s->io[11] & s->flx[1]);
    out[1] = s->board[1] & s->fboard[1];
    if(pad_emu) {
        uint8_t pads[8];
        for(int i = 0; i < 4; i++) {
            pads[i]   = s->io[i]   & s->pad[0] & s->sensor[i];
            pads[4+i] = s->io[4+i] & s->pad[2] & s->sensor[4+i];
        }
        out[0] &= piuio_pads_to_board(pads);
    }
}

// _PIUIOEMU_EMU_PIUIO_BUTTON (Prime JE: its menus take the button board
// only) lets the pads press the buttons, for machines that have no front
// buttons at all. A machine with a real button board or a real LXIO has
// them, and its pads stay pads (IMPER-114).
static inline int piuio_pad_emu(int emu_requested, int real_board, int real_lxio)
{
    return emu_requested && !real_board && !real_lxio;
}

// A failed read of a real IO must not look like a press: a zeroed buffer is
// "everything pressed", coin and SERVICE included (IMPER-116). The last state
// is kept while a read fails, and everything is let go once the device is
// gone (unplugged), or has not answered for a while (piuio.c, hot-plug):
// nobody can be known to be pressing it then.
enum piuio_read_result {
    PIUIO_READ_OK = 0,      // use the new data
    PIUIO_READ_KEEP,        // transient failure: keep the last state
    PIUIO_READ_RELEASE,     // no such device any more: nothing pressed
};

// r: what the libusb transfer returned (control: byte count or a negative
// error; interrupt: 0 with got = bytes transferred). need: bytes required.
#define PIUIO_ERR_NO_DEVICE (-4)    // LIBUSB_ERROR_NO_DEVICE
static inline enum piuio_read_result piuio_read_result(int r, int got, int need)
{
    if(r == PIUIO_ERR_NO_DEVICE) return PIUIO_READ_RELEASE;
    if(r < 0 || got < need) return PIUIO_READ_KEEP;
    return PIUIO_READ_OK;
}

#endif
