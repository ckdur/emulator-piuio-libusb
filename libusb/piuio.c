#include "libusbi.h"
#include "piuio_emu.h"
#include "piuio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int npiuio = 0;
libusb_device_handle** piuio;
libusb_context *piuio_ctx = NULL;
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

        // Check if it matches the target device
        if ((desc.idVendor == PIULXIO_VENDOR_ID && (desc.idProduct == PIULXIO_PRODUCT_ID || desc.idProduct == PIULXIO_PRODUCT_ID_2)) ||
            (desc.idVendor == PIUIO_VENDOR_ID && desc.idProduct == PIUIO_PRODUCT_ID) ||
            (desc.idVendor == PIUIOBUTTON_VENDOR_ID && desc.idProduct == PIUIOBUTTON_VENDOR_ID)
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
        }
    }

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
void poll_piuio(void){
    for(int k = 0; k < npiuio; k++) { 
        libusb_device_handle *dev_handle = piuio[k];
        libusb_device *dev;
        dev = dev_handle->dev;

        if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
            dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {

            unsigned char datout[16];
            memset(datout, 0, 16);
            datout[14] = 0xFF;
            datout[15] = 0xFF;
            int transferred;
            memcpy(datout, bytes_l, 4);
            true_libusb_interrupt_transfer(dev_handle,
                PIULXIO_ENDPOINT_OUT, datout, 16,
                &transferred, 1000);
            true_libusb_interrupt_transfer(dev_handle,
                0x80 | PIULXIO_ENDPOINT_IN, poll_bytes_piuio, 16,
                &transferred, 1000);
            
                poll_bytes_piuiob[0] = 0xFF;
                poll_bytes_piuiob[1] = 0xFF;
            
            // The LXIO can also pull states of the piuiob
            if((~poll_bytes_piuio[10]) & 0x03) poll_bytes_piuiob[0] &= 0xFE; // Red button on either UL/UR
            if((~poll_bytes_piuio[10]) & 0x04) poll_bytes_piuiob[0] &= 0xF7; // Green on Center
            if((~poll_bytes_piuio[10]) & 0x08) poll_bytes_piuiob[0] &= 0xFD; // Left on DL
            if((~poll_bytes_piuio[10]) & 0x10) poll_bytes_piuiob[0] &= 0xFB; // Right on DR
            if((~poll_bytes_piuio[11]) & 0x03) poll_bytes_piuiob[0] &= 0xEF; // Red button on either UL/UR
            if((~poll_bytes_piuio[11]) & 0x04) poll_bytes_piuiob[0] &= 0x7F; // Green on Center
            if((~poll_bytes_piuio[11]) & 0x08) poll_bytes_piuiob[0] &= 0xDF; // Left on DL
            if((~poll_bytes_piuio[11]) & 0x10) poll_bytes_piuiob[0] &= 0xBF; // Right on DR
        }
        
        if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
            dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {

            int i;

            poll_bytes_piuio[8] = 0xFF;
            poll_bytes_piuio[9] = 0xFF;
            for (i = 0; i < 4; i++)
            {
                unsigned char datin[8];
                unsigned char datout[8];
                memset(datin, 0, 8);
                memset(datout, 0, 8);
                datout[0] = (bytes_l[0] & 0xFC) | (i & 0x3);
                datout[1] = bytes_l[1];
                datout[2] = (bytes_l[2] & 0xFC) | (i & 0x3);
                datout[3] = bytes_l[3];
                true_libusb_control_transfer(
                    dev_handle, USB_DIR_OUT | USB_TYPE_VENDOR, PIUIO_CTL_REQ, 
                    0, 0, datout, 8, 10000);
                true_libusb_control_transfer(
                    dev_handle, USB_DIR_IN | USB_TYPE_VENDOR, PIUIO_CTL_REQ, 
                    0, 0, datin, 8, 10000);
                poll_bytes_piuio[i+0] = datin[0];
                poll_bytes_piuio[i+4] = datin[2];
                poll_bytes_piuio[8] &= datin[1];
                poll_bytes_piuio[9] &= datin[3];
            }
        }
        
        if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
            dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
                
            unsigned char dat[16];
            int transferred;
            true_libusb_interrupt_transfer(dev_handle,
                PIUIOBUTTON_ENDPOINT_OUT, dat, 16,
                &transferred, 1000);
            memcpy(poll_bytes_piuiob, dat, 2);
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

        if(piuioemu_mode & EMU_PIUIO_BUTTON) {
            // Emulate the piuio button with the regular pad
            poll_bytes_piuiob[0] = 0xFF;
            int i;
            for (i = 0; i < 4; i++) {
                if((~poll_bytes_piuio[i+0]) & 0x03) poll_bytes_piuiob[0] &= 0xFE; // Red button on either UL/UR
                if((~poll_bytes_piuio[i+0]) & 0x04) poll_bytes_piuiob[0] &= 0xF7; // Green on Center
                if((~poll_bytes_piuio[i+0]) & 0x08) poll_bytes_piuiob[0] &= 0xFD; // Left on DL
                if((~poll_bytes_piuio[i+0]) & 0x10) poll_bytes_piuiob[0] &= 0xFB; // Right on DR
                if((~poll_bytes_piuio[i+4]) & 0x03) poll_bytes_piuiob[0] &= 0xEF; // Red button on either UL/UR
                if((~poll_bytes_piuio[i+4]) & 0x04) poll_bytes_piuiob[0] &= 0x7F; // Green on Center
                if((~poll_bytes_piuio[i+4]) & 0x08) poll_bytes_piuiob[0] &= 0xDF; // Left on DL
                if((~poll_bytes_piuio[i+4]) & 0x10) poll_bytes_piuiob[0] &= 0xBF; // Right on DR
            }
            
            poll_bytes_piuiob[1] = 0xFF;
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
