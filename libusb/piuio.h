#ifndef PIUIO_H
#define PIUIO_H

extern unsigned char bytes_piuio[16];
extern unsigned char bytes_piuiob[2];

void init_piuio(void);
void poll_piuio(void);
void finish_piuio(void);

extern libusb_context *piuio_ctx;

#endif // PIUIO_H