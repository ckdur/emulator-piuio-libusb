#include "libusbi.h"
#include "piuio_emu.h"
#include "piuio.h"
#include "piuio_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char bytes_piuio[16] = { // From PIUIO
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 1P
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 2P
    0xFF, 0xFF, // Coins, service
    0xFF, 0xFF, // Frontal buttons in LX mode
    0xFF, 0xFF, 0xFF, 0xFF}; // Probably unused
unsigned char bytes_piuiob[2] = {0xFF, 0xFF}; // From a real PIUIObutton only

int npiuio = 0;
libusb_device_handle** piuio;
libusb_context *piuio_ctx = NULL;
unsigned int is_piuiob = 0;
unsigned int is_lxio = 0;
void finish_piuio(void){
    if(npiuio <= 0) return;
    for(int i = 0; i < npiuio; i++) {
        true_libusb_close(piuio[i]);
    }
    free(piuio);
    piuio = NULL;
    npiuio = 0;
}

void init_piuio(void){
    if(npiuio > 0) return;
    // Init a separate context
    libusb_context *ctx = piuio_ctx;
    libusb_device **device_list = NULL;
    ssize_t num_devices;
    int r;
    /*r = true_libusb_init_context(&ctx, NULL, 0);
    if (r < 0) {
        fprintf(stderr, "Failed to initialize libusb: %s\n", libusb_error_name(r));
        return;
    }*/

    // Get the list of USB devices
    num_devices = true_libusb_get_device_list(ctx, &device_list);
    if (num_devices < 0) {
        fprintf(stderr, "Failed to get device list: %s\n", libusb_error_name((int)num_devices));
        libusb_exit(ctx);
        return;
    }

    PRINTF("Number of USB devices found: %zd\n", num_devices);

    // Iterate through the list of devices
    for (ssize_t i = 0; i < num_devices; ++i) {
        libusb_device *device = device_list[i];
        struct libusb_device_descriptor desc;

        // Get device descriptor
        r = true_libusb_get_device_descriptor(device, &desc);
        if (r < 0) {
            fprintf(stderr, "Failed to get device descriptor: %s\n", libusb_error_name(r));
            continue;
        }
        PRINTF("Probing: Vendor ID=0x%04x, Product ID=0x%04x\n", desc.idVendor, desc.idProduct);

        // Check if it matches the target device
        if ((desc.idVendor == PIULXIO_VENDOR_ID && (desc.idProduct == PIULXIO_PRODUCT_ID || desc.idProduct == PIULXIO_PRODUCT_ID_2)) ||
            (desc.idVendor == PIUIO_VENDOR_ID && desc.idProduct == PIUIO_PRODUCT_ID) ||
            (desc.idVendor == PIUIOBUTTON_VENDOR_ID && desc.idProduct == PIUIOBUTTON_PRODUCT_ID)
            ) {
            PRINTF("Device found: Vendor ID=0x%04x, Product ID=0x%04x\n", desc.idVendor, desc.idProduct);
            libusb_device_handle *dev_handle = NULL;
            r = true_libusb_open(device, &dev_handle);
            if(r < 0) continue;

            if (true_libusb_kernel_driver_active(dev_handle, 0)) {
                true_libusb_detach_kernel_driver(dev_handle, 0);
            }

            r = true_libusb_claim_interface(dev_handle, 0);
            if(r < 0) {true_libusb_close(dev_handle); continue;}

            // It was successful, so we can save this handle
            if(npiuio == 0) {
                piuio = malloc(sizeof(libusb_device_handle *));
                npiuio = 1;
            }
            else {
                npiuio++;
                piuio = realloc(piuio, npiuio*sizeof(libusb_device_handle *));
            }
            piuio[npiuio-1] = dev_handle;
            // is_piuiob: a real PIUIO button board is present
            // is_lxio: a real LXIO is present (its front buttons replace the button board)
            if(desc.idVendor == PIUIOBUTTON_VENDOR_ID && desc.idProduct == PIUIOBUTTON_PRODUCT_ID)
                is_piuiob = 1;
            if(desc.idVendor == PIULXIO_VENDOR_ID && desc.idProduct == PIULXIO_PRODUCT_ID)
                is_lxio = 1;
            if(desc.idVendor == PIULXIO_VENDOR_ID && desc.idProduct == PIULXIO_PRODUCT_ID_2)
                is_lxio = 1;
        }
    }
    PRINTF("PIUIO init done\n");

    // Free the device list
    libusb_free_device_list(device_list, 1);
    piuio_ctx = ctx;
}

// The pulling state. We need to buffer it to avoid race conditions
unsigned char poll_bytes_piuio[16] = { // From PIUIO
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 1P
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 2P
    0xFF, 0xFF, // Coins, service
    0xFF, 0xFF, // Frontal buttons in LX mode
    0xFF, 0xFF, 0xFF, 0xFF}; // Probably unused
unsigned char poll_bytes_piuiob[2] = {0xFF, 0xFF}; // From PIUIObutton

// Reads the real IOs, and only that: bytes_piuio is what a real PIUIO / LXIO
// says (LX layout), bytes_piuiob what a real button board says. Turning one
// into the other (LX front buttons <-> button board, pads as buttons) is
// done when an emulated device answers the game: piuio_input.h.
//
// A read that fails never changes the state (a zeroed buffer would be "every
// button pressed", coin and SERVICE included: IMPER-116); a device that is
// gone lets go of everything.
void poll_piuio(void){
    for(int k = 0; k < npiuio; k++) { 
        libusb_device_handle *dev_handle = piuio[k];
        libusb_device *dev;
        dev = dev_handle->dev;

        if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
            dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {

            // A game that speaks LXIO itself: its report goes out as it is
            // (pure bypass, IMPER-113). A game that speaks PIUIO: LX output =
            // PIUIO lamp bytes 0..3 without mux bits, the rest is 0.
            unsigned char datout[16];
            int transferred = 0;
            memcpy(datout, bytes_l, 16);
            if(!(piuioemu_mode & (WITH_PIULXIO | WITH_PIULXIO_2))) {
                datout[0] &= 0xFC;
                datout[2] &= 0xFC;
            }
            PRINTF("lxio: Out endp\n");
            true_libusb_interrupt_transfer(dev_handle,
                PIULXIO_ENDPOINT_OUT, datout, 16,
                &transferred, 1000);
            PRINTF("lxio: In endp\n");
            unsigned char datin[16];
            transferred = 0;
            int r = true_libusb_interrupt_transfer(dev_handle,
                0x80 | PIULXIO_ENDPOINT_IN, datin, 16,
                &transferred, 1000);
            switch(piuio_read_result(r, transferred, 1)) {
                case PIUIO_READ_OK:
                    memcpy(poll_bytes_piuio, datin, transferred > 16 ? 16 : transferred);
                    break;
                case PIUIO_READ_RELEASE:
                    memset(poll_bytes_piuio, 0xFF, sizeof(poll_bytes_piuio));
                    break;
                default: // Keep the last known state on timeouts
                    break;
            }

#ifdef DEBUG
            PRINTF("LXIO poll: ");
            for(int i = 0; i < 16; i++) {
                PRINTF("%2x, ", (uint32_t)poll_bytes_piuio[i]);
            }
            PRINTF("\n");
#endif
        }
        
        if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
            dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {

            int i;
            int ok = 0, gone = 0;
            unsigned char coin1 = 0xFF, coin2 = 0xFF;

            for (i = 0; i < 4 && !gone; i++)
            {
                unsigned char datin[8];
                unsigned char datout[8];
                memset(datin, 0xFF, 8);
                memset(datout, 0, 8);
                datout[0] = (bytes_l[0] & 0xFC) | (i & 0x3);
                datout[1] = bytes_l[1];
                datout[2] = (bytes_l[2] & 0xFC) | (i & 0x3);
                datout[3] = bytes_l[3];
                true_libusb_control_transfer(
                    dev_handle, USB_DIR_OUT | USB_TYPE_VENDOR, PIUIO_CTL_REQ, 
                    0, 0, datout, 8, 10000);
                int r = true_libusb_control_transfer(
                    dev_handle, USB_DIR_IN | USB_TYPE_VENDOR, PIUIO_CTL_REQ, 
                    0, 0, datin, 8, 10000);
                switch(piuio_read_result(r, r, 4)) {
                    case PIUIO_READ_OK:
                        poll_bytes_piuio[i+0] = datin[0];
                        poll_bytes_piuio[i+4] = datin[2];
                        coin1 &= datin[1];
                        coin2 &= datin[3];
                        ok++;
                        break;
                    case PIUIO_READ_RELEASE:
                        gone = 1;
                        break;
                    default: // This sensor group keeps its last state
                        break;
                }
            }
            if(gone) {
                memset(poll_bytes_piuio, 0xFF, 10); // 10.. are not the PIUIO's
            }
            else if(ok) {
                // Coins, TEST and SERVICE come with every answer
                poll_bytes_piuio[8] = coin1;
                poll_bytes_piuio[9] = coin2;
            }
        }
        
        if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
            dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
                
            // The game itself talks to the button board with the PIUIO vendor
            // request (0x40 / 0xC0, 0xAE, 8 bytes), not with interrupt
            // transfers: first the lamps, then the buttons. Do the same, with
            // the lamps the game last wrote to the emulated board.
            unsigned char dat[8];
            if(bytes_lb_set) {
                memcpy(dat, bytes_lb, 8);
                true_libusb_control_transfer(dev_handle,
                    USB_DIR_OUT | USB_TYPE_VENDOR, PIUIO_CTL_REQ,
                    0, 0, dat, 8, 1000);
            }
            memset(dat, 0xFF, sizeof(dat));
            int r = true_libusb_control_transfer(dev_handle,
                USB_DIR_IN | USB_TYPE_VENDOR, PIUIO_CTL_REQ,
                0, 0, dat, 8, 1000);
            switch(piuio_read_result(r, r, 2)) {
                case PIUIO_READ_OK:
                    memcpy(poll_bytes_piuiob, dat, 2);
                    break;
                case PIUIO_READ_RELEASE:
                    memset(poll_bytes_piuiob, 0xFF, sizeof(poll_bytes_piuiob));
                    break;
                default:
                    break;
            }
        }

        if(piuioemu_mode & EMU_PROPAGATE) {
            // Propagate the sensor state to all sensors
            unsigned char prop1 = 0xFF;
            unsigned char prop2 = 0xFF;
            for (int i = 0; i < 4; i++) {
                prop1 &= poll_bytes_piuio[i+0];
                prop2 &= poll_bytes_piuio[i+4];
            }
            for (int i = 0; i < 4; i++) {
                poll_bytes_piuio[i+0] = prop1;
                poll_bytes_piuio[i+4] = prop2;
            }
        }
    }

    memcpy(bytes_piuio, poll_bytes_piuio, sizeof(poll_bytes_piuio));
    memcpy(bytes_piuiob, poll_bytes_piuiob, sizeof(poll_bytes_piuiob));
}

void API_EXPORTED libusb_piuio_get_status(int* status);
void API_EXPORTED libusb_piuio_get_status(int* status) {
    PRINTF("piuio_emu: libusb_piuio_get_status %d\n", __LINE__);
    *status = 0;

    for(int k = 0; k < npiuio; k++) { 
        libusb_device_handle *dev_handle = piuio[k];
        libusb_device *dev;
        dev = dev_handle->dev;
        if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
            dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
            *status |= WITH_PIULXIO;
        }
        if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2 && 
            dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
            *status |= WITH_PIULXIO_2;
        }
        if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
            dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
            *status |= WITH_PIUIO;
        }
        if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
            dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
            *status |= WITH_PIUIOBUTTON;
        }
    }
}
