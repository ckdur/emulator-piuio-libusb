#include "piuio_emu.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <glob.h>
//#include <uchar.h>

#include "libusbi.h"
#include "keyboards.h"

#define min(a, b) ((a)<(b)?(a):(b))
int16_t string0[] = PIULXIO_CTRL_300;
unsigned short string1[] = PIULXIO_STR_MANUFACTURER;
unsigned short string2[] = PIULXIO_STR_PRODUCT;

const struct libusb_device_descriptor piulxio_dev_desc = {
    .bLength = 18,
    .bDescriptorType = LIBUSB_DT_DEVICE,
    .idVendor = PIULXIO_VENDOR_ID,
    .idProduct = PIULXIO_PRODUCT_ID,
    .bcdUSB = 0x110,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 0x8,
    .bcdDevice = 0x100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 0,
    .bNumConfigurations = 1
};

// TODO: Check the accuracy of this descriptor
const struct libusb_device_descriptor piuio_dev_desc = {
    .bLength = 18,
    .bDescriptorType = LIBUSB_DT_DEVICE,
    .idVendor = PIUIO_VENDOR_ID,
    .idProduct = PIUIO_PRODUCT_ID,
    .bcdUSB = 0x100,
    .bDeviceClass = 0xFF,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 0x8,
    .bcdDevice = 0x100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 0,
    .bNumConfigurations = 1
};

// TODO: Check the accuracy of this descriptor
const struct libusb_device_descriptor piuiobutton_dev_desc = {
    .bLength = 18,
    .bDescriptorType = LIBUSB_DT_DEVICE,
    .idVendor = PIUIOBUTTON_VENDOR_ID,
    .idProduct = PIUIOBUTTON_PRODUCT_ID,
    .bcdUSB = 0x110,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 0x8,
    .bcdDevice = 0x100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 0,
    .bNumConfigurations = 1
};

const struct libusb_endpoint_descriptor piulxio_endp_desc[2] = {
    {
        .bLength = 7,
        .bDescriptorType = LIBUSB_DT_ENDPOINT,
        .bEndpointAddress = 0x80 | PIULXIO_ENDPOINT_IN,
        .bmAttributes = 0x03,
        .wMaxPacketSize = PIULXIO_DESC_MAX_PACKET_SIZE,
        .bInterval = 1,
        .bRefresh = 0,
        .bSynchAddress = 0,
        .extra = NULL,
        .extra_length = 0
    },
    {
        .bLength = 7,
        .bDescriptorType = LIBUSB_DT_ENDPOINT,
        .bEndpointAddress = PIULXIO_ENDPOINT_OUT,
        .bmAttributes = 0x03,
        .wMaxPacketSize = PIULXIO_DESC_MAX_PACKET_SIZE,
        .bInterval = 1,
        .bRefresh = 0,
        .bSynchAddress = 0,
        .extra = NULL,
        .extra_length = 0
    }
};

const struct libusb_interface_descriptor piulxio_int = {
    .bLength = 8,
    .bDescriptorType = LIBUSB_DT_INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = 0x03,
    .bInterfaceSubClass = 0x00,
    .bInterfaceProtocol = 0x00,
    .iInterface = 0,
    .endpoint = piulxio_endp_desc
};

const struct libusb_interface piulxio_interface = {
    .altsetting = &piulxio_int,
    .num_altsetting = 1
};

const struct libusb_config_descriptor piulxio_config_desc = {
    .bLength = 9,
    .bDescriptorType = LIBUSB_DT_CONFIG,
    .wTotalLength = 9+9+9+7+7,
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0xC0,
    .MaxPower = 0xbb, // To identify it
    .interface = &piulxio_interface,
    .extra = NULL,
    .extra_length = 0
};

static const uint8_t piulxio_hid_report_desc[] = {
	0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
	0x09, 0x01,        // Usage (0x01)
	0xA1, 0x01,        // Collection (Application)
	0x09, 0x02,        //   Usage (0x02)
	0x15, 0x00,        //   Logical Minimum (0)
	0x25, 0xFF,        //   Logical Maximum (-1)
	0x75, 0x08,        //   Report Size (8)
	0x95, 0x10,        //   Report Count (16)
	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0x09, 0x03,        //   Usage (0x03)
	0x15, 0x00,        //   Logical Minimum (0)
	0x25, 0xFF,        //   Logical Maximum (-1)
	0x75, 0x08,        //   Report Size (8)
	0x95, 0x10,        //   Report Count (16)
	0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0xC0,              // End Collection
};

static const uint8_t piulxio_hid_desc[] = {
	9,                                      // bLength
	0x21,                                   // bDescriptorType
	0x11, 0x01,                             // bcdHID
	0,                                      // bCountryCode
	1,                                      // bNumDescriptors
	0x22,                                   // bDescriptorType (0x22 = Report)
	sizeof(piulxio_hid_report_desc), 0,	// wDescriptorLength
};

char bytes_j[4] = {0xFF, 0xFF, 0xFF, 0xFF};
char bytes_jb[2] = {0xFF, 0xFF};
char bytes_p[4] = {0xFF, 0xFF, 0xFF, 0xFF};
char bytes_pb[2] = {0xFF, 0xFF};
char bytes_t[4] = {0xFF, 0xFF, 0xFF, 0xFF};
char bytes_tb[2] = {0xFF, 0xFF};
char bytes_l[4] = {0x00, 0x00, 0x00, 0x00};
char bytes_f[4] = {0xFF, 0xFF, 0xFF, 0xFF};
char bytes_fb[2] = {0xFF, 0xFF};
static void init_piuio_emu(void) {
    init_keyboards();
}

static void poll_piuio_emu(void) {
    poll_keyboards();
}

int g_init = 0;
int true_libusb_init_context(libusb_context **ctx, const struct libusb_init_option options[], int num_options);
int API_EXPORTED libusb_init_context(libusb_context **ctx, const struct libusb_init_option options[], int num_options) {
    printf("piuio_emu: libusb_init_context %d\n", __LINE__);
    if(!g_init) {
        init_piuio_emu();
        g_init = 1;
    }
    return true_libusb_init_context(ctx, options, num_options);
}

ssize_t true_libusb_get_device_list(libusb_context *ctx,
	libusb_device ***list);
ssize_t API_EXPORTED libusb_get_device_list(libusb_context *ctx,
	libusb_device ***list)

{
    printf("piuio_emu: libusb_get_device_list %d\n", __LINE__);
    // Call the original one
    ssize_t len = true_libusb_get_device_list(ctx, list);

    // Add three new devices
    struct libusb_device **ret = calloc(3+len+1, sizeof(struct libusb_device*));
    ret[0] = malloc(sizeof(struct libusb_device));
    ret[0]->device_descriptor.idProduct = PIULXIO_PRODUCT_ID;
    ret[0]->device_descriptor.idVendor = PIULXIO_VENDOR_ID;
    ret[1] = malloc(sizeof(struct libusb_device));
    ret[1]->device_descriptor.idProduct = PIUIO_PRODUCT_ID;
    ret[1]->device_descriptor.idVendor = PIUIO_VENDOR_ID;
    ret[2] = malloc(sizeof(struct libusb_device));
    ret[2]->device_descriptor.idProduct = PIUIOBUTTON_PRODUCT_ID;
    ret[2]->device_descriptor.idVendor = PIUIOBUTTON_VENDOR_ID;

    // Copy the rest just as it is
    memcpy(ret+3, *list, len*sizeof(struct libusb_device*));
    free(*list);
    *list = ret;
    return 3+len;
}

int API_EXPORTED true_libusb_get_device_descriptor(libusb_device *dev,
	struct libusb_device_descriptor *desc);
int API_EXPORTED libusb_get_device_descriptor(libusb_device *dev,
	struct libusb_device_descriptor *desc) {
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        memcpy(desc, &piulxio_dev_desc, sizeof(struct libusb_device_descriptor));
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        memcpy(desc, &piuio_dev_desc, sizeof(struct libusb_device_descriptor));
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        memcpy(desc, &piuiobutton_dev_desc, sizeof(struct libusb_device_descriptor));
        return 0;
    }

    return true_libusb_get_device_descriptor(dev, desc);
}

int API_EXPORTED true_libusb_get_config_descriptor(libusb_device *dev,
	uint8_t config_index, struct libusb_config_descriptor **config);
int API_EXPORTED libusb_get_config_descriptor(libusb_device *dev,
	uint8_t config_index, struct libusb_config_descriptor **config)
{
    printf("piuio_emu: libusb_get_config_descriptor %d\n", __LINE__);
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }

    return true_libusb_get_config_descriptor(dev, config_index, config);
}

int API_EXPORTED true_libusb_get_active_config_descriptor(libusb_device *dev,
	struct libusb_config_descriptor **config);
int API_EXPORTED libusb_get_active_config_descriptor(libusb_device *dev,
	struct libusb_config_descriptor **config) {
    printf("piuio_emu: libusb_get_active_config_descriptor %d\n", __LINE__);
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }
    
    return true_libusb_get_active_config_descriptor(dev, config);
}

void API_EXPORTED true_libusb_free_config_descriptor(
	struct libusb_config_descriptor *config);
void API_EXPORTED libusb_free_config_descriptor(
	struct libusb_config_descriptor *config)
{
    printf("piuio_emu: libusb_free_config_descriptor %d\n", __LINE__);
	if (!config)
		return;
    
    if(config->MaxPower == 0xbb) {
        free(config);
        return;
    }

    true_libusb_free_config_descriptor(config);
}

int API_EXPORTED true_libusb_open(libusb_device *dev,
	libusb_device_handle **dev_handle);
int API_EXPORTED libusb_open(libusb_device *dev,
	libusb_device_handle **dev_handle)
{
    printf("piuio_emu: libusb_open %d\n", __LINE__);
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        *dev_handle = malloc(sizeof(struct libusb_device_handle));
        memset(*dev_handle, 0, sizeof(struct libusb_device_handle));
        (*dev_handle)->dev = dev;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        *dev_handle = malloc(sizeof(struct libusb_device_handle));
        memset(*dev_handle, 0, sizeof(struct libusb_device_handle));
        (*dev_handle)->dev = dev;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        *dev_handle = malloc(sizeof(struct libusb_device_handle));
        memset(*dev_handle, 0, sizeof(struct libusb_device_handle));
        (*dev_handle)->dev = dev;
        return 0;
    }
    return true_libusb_open(dev, dev_handle);
}

void API_EXPORTED true_libusb_close(libusb_device_handle *dev_handle);
void API_EXPORTED libusb_close(libusb_device_handle *dev_handle){
    printf("piuio_emu: libusb_close %d\n", __LINE__);

    libusb_device *dev;
    dev = dev_handle->dev;
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        free(dev_handle);
        return;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        free(dev_handle);
        return;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        free(dev_handle);
        return;
    }
    true_libusb_close(dev_handle);
}

static int piulxio_helper_process_data_in(uint8_t* bytes, int size) {
    poll_piuio_emu();
    int ret = min(16, size);

    // p1 stuff
    bytes_f[0] = bytes[0] = bytes[1] = bytes[2] = bytes[3] = bytes_j[0] & bytes_p[0] & bytes_t[0];
    // p2 stuff
    bytes_f[2] = bytes[4] = bytes[5] = bytes[6] = bytes[7] = bytes_j[2] & bytes_p[2] & bytes_t[2];
    // coin stuff
    bytes_f[1] = bytes[8] = bytes_j[1] & bytes_p[1] & bytes_t[1];
    bytes_f[3] = bytes[9] = bytes_j[3] & bytes_p[3] & bytes_t[3];
    
    bytes[10] = bytes_f[3];
    bytes[11] = bytes_f[3];
    bytes[12] = bytes_f[3];
    bytes[13] = bytes_f[3];
    bytes[14] = bytes_f[3];
    bytes[15] = bytes_f[3];

    return ret;
}

static int piulxio_helper_process_data_out(uint8_t* data, int size) {
    int ret = min(16, size);

    bytes_l[0] = data[0];
    bytes_l[1] = data[1];
    bytes_l[2] = data[2];
    bytes_l[3] = data[3];

    return ret;
}

static int piuio_helper_process_data_in(uint8_t* bytes, int size) {
    poll_piuio_emu();
    int ret = min(8, size);

    bytes_f[0] = bytes[0] = bytes_j[0] & bytes_p[0] & bytes_t[0];
    bytes_f[1] = bytes[1] = bytes_j[1] & bytes_p[1] & bytes_t[1];
    bytes_f[2] = bytes[2] = bytes_j[2] & bytes_p[2] & bytes_t[2];
    bytes_f[3] = bytes[3] = bytes_j[3] & bytes_p[3] & bytes_t[3];

    return ret;
}

static int piuio_helper_process_data_out(uint8_t* data, int size) {
    int ret = min(8, size);

    bytes_l[0] = data[0];
    bytes_l[1] = data[1];
    bytes_l[2] = data[2];
    bytes_l[3] = data[3];

    return ret;
}

static int piuiobutton_helper_process_data(uint8_t* bytes, int size) {
    poll_piuio_emu();
    int ret = min(0, size);
    bytes_fb[0] = bytes[0] = bytes_jb[0] & bytes_pb[0] & bytes_tb[0];
    bytes_fb[1] = bytes[1] = bytes_jb[1] & bytes_pb[1] & bytes_tb[1];
    return ret;
}

static int piulxio_libusb_control_transfer(
	uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength) {
    if (request_type == USB_DIR_IN && bRequest == PIULXIO_GET_DESCRIPTOR)
    {
        // From do_control_to_usb: can be 0x300, or param_2 | 0xff | 0x300
        if (wValue == 0x300)
        { // Get the control descriptor
            int ret = min(4, wLength);
            memcpy(data, string0, ret);
            return ret;
        }
        else if (wValue == 0x301)
        {
            int ret = min(sizeof(string1), wLength);
            memcpy(data, string1, ret);
            return ret;
        }
        else if (wValue == 0x302)
        {
            int ret = min(sizeof(string2), wLength);
            memcpy(data, string2, ret);
            return ret;
        }
        else if (wValue == 0x2200)
        {
            // HID report descriptor
            int ret = min(sizeof(piulxio_hid_report_desc), wLength);
            memcpy(data, piulxio_hid_report_desc, ret);
            return ret;
        }
        else if (wValue == 0x2100)
        {
            // HID desc
            int ret = min(sizeof(piulxio_hid_desc), wLength);
            memcpy(data, piulxio_hid_desc, ret);
            return ret;
        }
    }
    else if (wIndex == 0 && request_type == 0xA1 && bRequest == PIULXIO_HID_SET_REPORT)
    {
        // printf("push data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piulxio_helper_process_data_out(data, wLength);
    }
    else if (wIndex == 0 && request_type == 0x21 && bRequest == PIULXIO_HID_SET_REPORT)
    {
        // printf("recv data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piulxio_helper_process_data_in(data, wLength);
    }
    printf("Unknown request: %x %x %x %x %x\n", request_type, bRequest, wValue, wIndex, wLength);
    return 0;
}

static int piuio_libusb_control_transfer(
	uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength) {
    if (request_type == USB_DIR_IN && bRequest == PIULXIO_GET_DESCRIPTOR)
    {
        // TODO: Fill acordingly
    }
    else if ((request_type & USB_DIR_OUT) && (request_type & USB_TYPE_VENDOR) && (bRequest & PIUIO_CTL_REQ))
    {
        // printf("push data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piuio_helper_process_data_out(data, wLength);
    }
    else if ((request_type & USB_DIR_IN) && (request_type & USB_TYPE_VENDOR) && (bRequest & PIUIO_CTL_REQ))
    {
        // printf("recv data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piuio_helper_process_data_in(data, wLength);
    }
    printf("Unknown request: %x %x %x %x %x\n", request_type, bRequest, wValue, wIndex, wLength);
    return 0;
}

static int piuiobutton_libusb_control_transfer(
	uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength) {
    if (request_type == USB_DIR_IN && bRequest == PIULXIO_GET_DESCRIPTOR)
    {
        // TODO: Fill acordingly
    }
    else 
    {
        return piuiobutton_helper_process_data(data, wLength);
    }
    printf("Unknown request: %x %x %x %x %x\n", request_type, bRequest, wValue, wIndex, wLength);
    return 0;
}

int API_EXPORTED true_libusb_control_transfer(libusb_device_handle *dev_handle,
	uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength, unsigned int timeout);
int API_EXPORTED libusb_control_transfer(libusb_device_handle *dev_handle,
	uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength, unsigned int timeout)
{
    printf("piuio_emu: libusb_control_transfer %d\n", __LINE__);

    libusb_device *dev;
    dev = dev_handle->dev;
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        return piulxio_libusb_control_transfer(
            bmRequestType, bRequest, wValue, wIndex,
            data, wLength);
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        return piuio_libusb_control_transfer(
            bmRequestType, 
            bRequest, wValue, wIndex,
            data, wLength);
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        return piuiobutton_libusb_control_transfer(
            bmRequestType, bRequest, wValue, wIndex,
            data, wLength);
    }

    return true_libusb_control_transfer(dev_handle,
	    bmRequestType, bRequest, wValue, wIndex,
	    data, wLength, timeout);
}

int API_EXPORTED true_libusb_interrupt_transfer(libusb_device_handle *dev_handle,
	unsigned char endpoint, unsigned char *data, int length,
	int *transferred, unsigned int timeout);
int API_EXPORTED libusb_interrupt_transfer(libusb_device_handle *dev_handle,
	unsigned char endpoint, unsigned char *data, int length,
	int *transferred, unsigned int timeout)
{
    // printf("piuio_emu: libusb_interrupt_transfer %d\n", __LINE__);

    libusb_device *dev;
    dev = dev_handle->dev;
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        if((endpoint & 0x7F) == PIULXIO_ENDPOINT_IN) {
            *transferred = piulxio_helper_process_data_in(data, length);
            return *transferred;
        }
        else if(endpoint == PIULXIO_ENDPOINT_OUT) {
            *transferred = piulxio_helper_process_data_out(data, length);
            return *transferred;
        }
        else {
            printf("PIULXIO: Unknown endpoint %d\n", (int)endpoint);
            *transferred = 0;
            return 0;
        }
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        printf("PIUIO (libusb_interrupt_transfer): Unknown endpoint %d\n", (int)endpoint);
        *transferred = 0;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        if((endpoint & 0x7F) == PIUIOBUTTON_ENDPOINT_IN) {
            *transferred = piuiobutton_helper_process_data(data, length);
            return *transferred;
        }
        else if(endpoint == PIUIOBUTTON_ENDPOINT_OUT) {
            *transferred = piuiobutton_helper_process_data(data, length);
            return *transferred;
        }
        else {
            printf("PIUIOBUTTON (libusb_interrupt_transfer): Unknown endpoint %d\n", (int)endpoint);
            *transferred = 0;
            return 0;
        }
    }
	return true_libusb_interrupt_transfer(dev_handle,
        endpoint, data, length, transferred, timeout);
}

static int dummy_response(libusb_device_handle *dev_handle) {

    libusb_device *dev;
    dev = dev_handle->dev;

    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        return 0;
    }
    return -1;
}

/*struct libusb_transfer * LIBUSB_CALL libusb_alloc_transfer(int iso_packets) {
    printf("CKDUR: libusb_alloc_transfer (1.0) %d\n", __LINE__);
    struct libusb_transfer * tr = malloc(sizeof(struct libusb_transfer));
    memset(tr, 0, sizeof(struct libusb_transfer));
    return tr;
}*/

struct libusb_transfer * piulxio_current_transfer = NULL;
struct libusb_transfer * piuio_current_transfer = NULL;
struct libusb_transfer * piuiobutton_current_transfer = NULL;
int API_EXPORTED true_libusb_submit_transfer(struct libusb_transfer *transfer);
int API_EXPORTED libusb_submit_transfer(struct libusb_transfer *transfer) {

    if(transfer == NULL) goto true_libusb_submit_transfer_call;

    libusb_device *dev;
    dev = transfer->dev_handle->dev;

    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        piulxio_current_transfer = transfer;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        piuio_current_transfer = transfer;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        piuiobutton_current_transfer = transfer;
        return 0;
    }

true_libusb_submit_transfer_call:
    return true_libusb_submit_transfer(transfer);
}

int API_EXPORTED true_libusb_cancel_transfer(struct libusb_transfer *transfer);
int API_EXPORTED libusb_cancel_transfer(struct libusb_transfer *transfer) {

    if(transfer == NULL) goto true_libusb_cancel_transfer_call;

    libusb_device *dev;
    dev = transfer->dev_handle->dev;

    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        if(piulxio_current_transfer) piulxio_current_transfer = NULL;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        if(piuio_current_transfer) piuio_current_transfer = NULL;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        if(piuiobutton_current_transfer) piuiobutton_current_transfer = NULL;
        return 0;
    }
true_libusb_cancel_transfer_call:
    return true_libusb_cancel_transfer(transfer);
}

/*void LIBUSB_CALL libusb_free_transfer(struct libusb_transfer *transfer) {
    printf("CKDUR: libusb_free_transfer (1.0) %d\n", __LINE__);
    if(transfer) {
        free(transfer);
    }
}*/

int API_EXPORTED true_libusb_handle_events(libusb_context *ctx);
int API_EXPORTED libusb_handle_events(libusb_context *ctx){
    //printf("CKDUR: libusb_handle_events (1.0) %d\n", __LINE__);
    if(piulxio_current_transfer) {
        struct libusb_transfer *transfer = piulxio_current_transfer;
        piulxio_current_transfer = NULL;
        // printf("Processing transfers to endp=%d %x\n", (int)transfer->endpoint, (int)transfer->length);
        if(transfer->dev_handle == NULL) return 0;
        //if(transfer->dev_handle->a0 != 0) return 0;
        if(transfer->type != LIBUSB_TRANSFER_TYPE_INTERRUPT) return 0;
        transfer->status = LIBUSB_TRANSFER_COMPLETED;
        if((transfer->endpoint & 0x7F) == PIULXIO_ENDPOINT_IN) {
            transfer->actual_length = piulxio_helper_process_data_in(transfer->buffer, transfer->length);
        }
        else if(transfer->endpoint == PIULXIO_ENDPOINT_OUT) {
            transfer->actual_length = piulxio_helper_process_data_out(transfer->buffer, transfer->length);
        }
        else {
            printf("PIULXIO (libusb_handle_events): Unknown endpoint %d\n", (int)transfer->endpoint);
            return -1;
        }
        transfer->callback(transfer);
    }

    if(piuio_current_transfer) {
        struct libusb_transfer *transfer = piuio_current_transfer;
        piuio_current_transfer = NULL;
        printf("PIUIO (libusb_handle_events): Unknown endpoint %d\n", (int)transfer->endpoint);
        transfer->callback(transfer);
    }

    if(piuiobutton_current_transfer) {
        struct libusb_transfer *transfer = piuiobutton_current_transfer;
        piuiobutton_current_transfer = NULL;
        if(transfer->dev_handle == NULL) return 0;
        //if(transfer->dev_handle->a0 != 0) return 0;
        if(transfer->type != LIBUSB_TRANSFER_TYPE_INTERRUPT) return 0;
        transfer->status = LIBUSB_TRANSFER_COMPLETED;
        if((transfer->endpoint & 0x7F) == PIUIOBUTTON_ENDPOINT_IN) {
            transfer->actual_length = piulxio_helper_process_data_in(transfer->buffer, transfer->length);
        }
        else if(transfer->endpoint == PIUIOBUTTON_ENDPOINT_OUT) {
            transfer->actual_length = piulxio_helper_process_data_out(transfer->buffer, transfer->length);
        }
        else {
            printf("PIUIOBUTTON (libusb_handle_events): Unknown endpoint %d\n", (int)transfer->endpoint);
        }
        transfer->callback(transfer);
    }

    return true_libusb_handle_events(ctx);
}

int API_EXPORTED true_libusb_kernel_driver_active(libusb_device_handle *dev_handle,
	int interface_number);
int API_EXPORTED libusb_kernel_driver_active(libusb_device_handle *dev_handle,
	int interface_number) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_kernel_driver_active(dev_handle, interface_number);
}

int API_EXPORTED true_libusb_detach_kernel_driver(libusb_device_handle *dev_handle,
	int interface_number);
int API_EXPORTED libusb_detach_kernel_driver(libusb_device_handle *dev_handle,
	int interface_number) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_detach_kernel_driver(dev_handle, interface_number);
}

int API_EXPORTED true_libusb_claim_interface(libusb_device_handle *dev_handle,
	int interface_number);
int API_EXPORTED libusb_claim_interface(libusb_device_handle *dev_handle,
	int interface_number) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_detach_kernel_driver(dev_handle, interface_number);
}

int API_EXPORTED true_libusb_release_interface(libusb_device_handle *dev_handle,
	int interface_number);
int API_EXPORTED libusb_release_interface(libusb_device_handle *dev_handle,
	int interface_number) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_release_interface(dev_handle, interface_number);
}

