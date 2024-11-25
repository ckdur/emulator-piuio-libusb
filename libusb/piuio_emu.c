#include "piuio_emu.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
//#include <uchar.h>

#include "libusbi.h"
#include "keyboards.h"
#include "piuio.h"

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
    .bMaxPacketSize0 = 0x10,
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
    .bcdUSB = 0x200,
    .bDeviceClass = 0x0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .bcdDevice = 0x0,
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
        .wMaxPacketSize = PIULXIO_ENDPOINT_PACKET_SIZE,
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
        .wMaxPacketSize = PIULXIO_ENDPOINT_PACKET_SIZE,
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
    .bmAttributes = 0xa0,
    .MaxPower = 0xbb, // To identify it
    .interface = &piulxio_interface,
    .extra = NULL,
    .extra_length = 0
};

const struct libusb_endpoint_descriptor piuio_endp_desc[2] = {
    {
        .bLength = 7,
        .bDescriptorType = LIBUSB_DT_ENDPOINT,
        .bEndpointAddress = 0x80 | PIUIO_ENDPOINT_IN,
        .bmAttributes = 0x02,
        .wMaxPacketSize = PIUIO_ENDPOINT_PACKET_SIZE,
        .bInterval = 0,
        .bRefresh = 0,
        .bSynchAddress = 0,
        .extra = NULL,
        .extra_length = 0
    },
    {
        .bLength = 7,
        .bDescriptorType = LIBUSB_DT_ENDPOINT,
        .bEndpointAddress = PIUIO_ENDPOINT_OUT,
        .bmAttributes = 0x02,
        .wMaxPacketSize = PIUIO_ENDPOINT_PACKET_SIZE,
        .bInterval = 0,
        .bRefresh = 0,
        .bSynchAddress = 0,
        .extra = NULL,
        .extra_length = 0
    }
};

const struct libusb_interface_descriptor piuio_int = {
    .bLength = 9,
    .bDescriptorType = LIBUSB_DT_INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = 0xFF,
    .bInterfaceSubClass = 0x00,
    .bInterfaceProtocol = 0x00,
    .iInterface = 0,
    .endpoint = piuio_endp_desc
};

const struct libusb_interface piuio_interface = {
    .altsetting = &piuio_int,
    .num_altsetting = 1
};

const struct libusb_config_descriptor piuio_config_desc = {
    .bLength = 9,
    .bDescriptorType = LIBUSB_DT_CONFIG,
    .wTotalLength = 0x20,
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0xC0,
    .MaxPower = 0xbb, // To identify it
    .interface = &piuio_interface,
    .extra = NULL,
    .extra_length = 0
};

const struct libusb_interface piuiobutton_interface = {
    .altsetting = &piulxio_int,
    .num_altsetting = 1
};

const struct libusb_config_descriptor piuiobutton_config_desc = {
    .bLength = 9,
    .bDescriptorType = LIBUSB_DT_CONFIG,
    .wTotalLength = 9+9+9+7+7,
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0xa0,
    .MaxPower = 0xbb, // To identify it
    .interface = &piuiobutton_interface,
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

// Define the structure for a queue node
typedef struct Node {
    struct libusb_transfer * data;
    struct Node* next;
} Node;

// Define the queue structure
typedef struct Queue {
    Node* front;
    Node* rear;
} Queue;

// Function to create a new queue
static Queue* createQueue(void) {
    Queue* queue = (Queue*)malloc(sizeof(Queue));
    queue->front = NULL;
    queue->rear = NULL;
    return queue;
}

// Function to submit (enqueue) an item to the queue
static void submit_to_queue(Queue* queue, struct libusb_transfer * data) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    newNode->data = data;
    newNode->next = NULL;

    if (queue->rear == NULL) {
        // If the queue is empty, the new node is both the front and rear
        queue->front = newNode;
        queue->rear = newNode;
        return;
    }

    // Add the new node at the end of the queue
    queue->rear->next = newNode;
    queue->rear = newNode;
}

// Function to remove a specific item from the queue
static void remove_from_queue(Queue* queue, struct libusb_transfer * data) {
    if (queue->front == NULL) {
        printf("Queue is empty, cannot remove.\n");
        return;
    }

    Node *temp = queue->front, *prev = NULL;

    // Check if the front node is the one to be removed
    if (temp != NULL && temp->data == data) {
        queue->front = temp->next; // Update the front
        if (queue->front == NULL) queue->rear = NULL; // Update rear if queue becomes empty
        free(temp);
        return;
    }

    // Search for the node to be removed
    while (temp != NULL && temp->data != data) {
        prev = temp;
        temp = temp->next;
    }

    // If the data was not found
    if (temp == NULL) {
        printf("Item %p not found in the queue.\n", data);
        return;
    }

    // Remove the node
    prev->next = temp->next;
    if (temp == queue->rear) queue->rear = prev; // Update rear if needed
    free(temp);
}

unsigned char bytes_j[4] = {0xFF, 0xFF, 0xFF, 0xFF}; // From joysticks
unsigned char bytes_jb[2] = {0xFF, 0xFF};
unsigned char bytes_p[4] = {0xFF, 0xFF, 0xFF, 0xFF}; // From keyboard
unsigned char bytes_pb[2] = {0xFF, 0xFF};
unsigned char bytes_t[4] = {0xFF, 0xFF, 0xFF, 0xFF}; // From twitch (TODO)
unsigned char bytes_tb[2] = {0xFF, 0xFF};

unsigned char bytes_piuio[16] = { // From PIUIO
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 1P
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 2P
    0xFF, 0xFF, // Coins, service
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Probably unused
unsigned char bytes_piuiob[2] = {0xFF, 0xFF}; // From PIUIObutton

unsigned char bytes_l[4] = {0x00, 0x00, 0x00, 0x00};
unsigned char bytes_f[4] = {0xFF, 0xFF, 0xFF, 0xFF};
unsigned char bytes_f_prev[4] = {0xFF, 0xFF, 0xFF, 0xFF};
unsigned char bytes_fb[2] = {0xFF, 0xFF};

usbi_mutex_t piuioemu_mutex;
usbi_mutex_t piuioemu_poll_mutex;
Queue* piuioemu_queue;
#define WITH_PIULXIO 0x1
#define WITH_PIUIO 0x2
#define WITH_PIUIOBUTTON 0x4
int piuioemu_mode = WITH_PIUIO | WITH_PIUIOBUTTON;
static void init_piuio_emu(void) {
    usbi_mutex_init(&piuioemu_mutex);
    usbi_mutex_init(&piuioemu_poll_mutex);
    init_keyboards();
    piuioemu_queue = createQueue();
    const char *val;
    int r;

    if ((val = getenv("_PIUIOEMU_WITH_PIULXIO"))) {
        r = strtol(val, NULL, 10);
        if(r) {
            piuioemu_mode |= WITH_PIULXIO;
            piuioemu_mode &= ~WITH_PIUIO;
            piuioemu_mode &= ~WITH_PIUIOBUTTON;
        }
        else {
            piuioemu_mode &= ~WITH_PIULXIO;
        }
    }

    if ((val = getenv("_PIUIOEMU_WITH_PIUIO"))) {
        r = strtol(val, NULL, 10);
        if(r) {
            piuioemu_mode |= WITH_PIUIO;
        }
        else {
            piuioemu_mode &= ~WITH_PIUIO;
        }
    }

    if ((val = getenv("_PIUIOEMU_WITH_PIUIOBUTTON"))) {
        r = strtol(val, NULL, 10);
        if(r) {
            piuioemu_mode |= WITH_PIUIOBUTTON;
        }
        else {
            piuioemu_mode &= ~WITH_PIUIOBUTTON;
        }
    }
}

time_t start_time = 0;
bool condition_met = false;
static void poll_piuio_emu(void) {
    poll_keyboards();
    poll_piuio();

    // Detect if service and test has been held 5 seconds
    // Test is bit 9, service is bit 14
    //    bit 9                    bit 14
    if(!(bytes_f[1] & 0x02)/* && !(bytes_f[1] & 0x40)*/) {
        if(!condition_met) {
            // Start timing if condition is met for the first time
            start_time = time(NULL);
            condition_met = true;
            printf("TEST hold ...\n");
        }
        else if (time(NULL) - start_time >= 5) {
            // If condition is continuously met for 5 seconds, exit the loop
            printf("TEST held 5 seconds. Exiting ...\n");
            exit(0);
            abort();
            *((int*)NULL) = 0; // crash it
            return;
        }
    }
    else {
        condition_met = false;
    }
    memcpy(bytes_f_prev, bytes_f, 4);
}

int g_init = 0;
int API_EXPORTED libusb_init_context(libusb_context **ctx, const struct libusb_init_option options[], int num_options) {
    printf("piuio_emu: libusb_init_context %d\n", __LINE__);
    int r = true_libusb_init_context(ctx, options, num_options);
    if(!g_init) {
        if(ctx) piuio_ctx = *ctx;
        init_piuio_emu();
        g_init = 1;
    }
    finish_piuio();
    init_piuio();
    
    return r;
}

ssize_t API_EXPORTED libusb_get_device_list(libusb_context *ctx,
	libusb_device ***list)
{
    printf("piuio_emu: libusb_get_device_list %d\n", __LINE__);
    // Call the original one
    ssize_t len = 0; //= true_libusb_get_device_list(ctx, list);

    ssize_t filtered_len = 0;
    if(piuioemu_mode & WITH_PIULXIO) filtered_len++;
    if(piuioemu_mode & WITH_PIUIO) filtered_len++;
    if(piuioemu_mode & WITH_PIUIOBUTTON) filtered_len++;
    struct libusb_device **ret = calloc(filtered_len+len+1, sizeof(struct libusb_device*));
    struct libusb_device **p = ret;

    // Add three new devices
    if(piuioemu_mode & WITH_PIULXIO) {
        printf("  Adding PIULXIO\n");
        struct libusb_device *dev = malloc(sizeof(struct libusb_device));
        memset(dev, 0, sizeof(struct libusb_device));
        dev->device_descriptor.idProduct = PIULXIO_PRODUCT_ID;
        dev->device_descriptor.idVendor = PIULXIO_VENDOR_ID;
        dev->refcnt = 1;
        *p = libusb_ref_device(dev);
        p++;
    }
    if(piuioemu_mode & WITH_PIUIO) {
        printf("  Adding PIUIO\n");
        struct libusb_device *dev = malloc(sizeof(struct libusb_device));
        memset(dev, 0, sizeof(struct libusb_device));
        dev->device_descriptor.idProduct = PIUIO_PRODUCT_ID;
        dev->device_descriptor.idVendor = PIUIO_VENDOR_ID;
        dev->refcnt = 1;
        *p = libusb_ref_device(dev);
        p++;
    }
    if(piuioemu_mode & WITH_PIUIOBUTTON) {
        printf("  Adding PIUIOBUTTON\n");
        struct libusb_device *dev = malloc(sizeof(struct libusb_device));
        memset(dev, 0, sizeof(struct libusb_device));
        dev->device_descriptor.idProduct = PIUIOBUTTON_PRODUCT_ID;
        dev->device_descriptor.idVendor = PIUIOBUTTON_VENDOR_ID;
        dev->refcnt = 1;
        *p = libusb_ref_device(dev);
        p++;
    }

    /*for(int k = 0; k < len; k++) {
        if(!
           (((*list)[k]->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
            (*list)[k]->device_descriptor.idVendor == PIULXIO_VENDOR_ID) ||
           ((*list)[k]->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
            (*list)[k]->device_descriptor.idVendor == PIUIO_VENDOR_ID) || 
           ((*list)[k]->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
            (*list)[k]->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID))) {
            *p = (*list)[k];
            p++;
            filtered_len++;
        }
    }*/

    // Copy the rest just as it is
    //memcpy(ret+3, *list, len*sizeof(struct libusb_device*));
    *list = ret;
    return filtered_len;
}

void API_EXPORTED libusb_free_device_list(libusb_device **list,
	int unref_devices) {

    /*for(libusb_device **p = list; *p != NULL; p++) {
        free(*p);
    }*/
    true_libusb_free_device_list(list, unref_devices);
}

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

int API_EXPORTED libusb_get_config_descriptor(libusb_device *dev,
	uint8_t config_index, struct libusb_config_descriptor **config)
{
    printf("piuio_emu: libusb_get_config_descriptor %d\n", __LINE__);
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        printf("  Requested for PIULXIO\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        printf("  Requested for PIUIO\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piuio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piuio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        printf("  Requested for PIUIOBUTTON\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piuiobutton_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piuiobutton_interface;
        return 0;
    }

    return true_libusb_get_config_descriptor(dev, config_index, config);
}

int API_EXPORTED libusb_get_active_config_descriptor(libusb_device *dev,
	struct libusb_config_descriptor **config) {
    printf("piuio_emu: libusb_get_active_config_descriptor %d\n", __LINE__);
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        printf("  Requested for PIULXIO\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        printf("  Requested for PIUIO\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piuio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piuio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        printf("  Requested for PIUIOBUTTON\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piuiobutton_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piuiobutton_interface;
        return 0;
    }
    
    return true_libusb_get_active_config_descriptor(dev, config);
}

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

int API_EXPORTED libusb_open(libusb_device *dev,
	libusb_device_handle **dev_handle)
{
    printf("piuio_emu: libusb_open %d\n", __LINE__);
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        printf("  Opened for PIULXIO\n");
        *dev_handle = malloc(sizeof(struct libusb_device_handle));
        memset(*dev_handle, 0, sizeof(struct libusb_device_handle));
        (*dev_handle)->dev = dev;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        printf("  Opened for PIUIO\n");
        *dev_handle = malloc(sizeof(struct libusb_device_handle));
        memset(*dev_handle, 0, sizeof(struct libusb_device_handle));
        (*dev_handle)->dev = dev;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        printf("  Opened for PIUIOBUTTON\n");
        *dev_handle = malloc(sizeof(struct libusb_device_handle));
        memset(*dev_handle, 0, sizeof(struct libusb_device_handle));
        (*dev_handle)->dev = dev;
        return 0;
    }
    return true_libusb_open(dev, dev_handle);
}

void API_EXPORTED libusb_close(libusb_device_handle *dev_handle){
    printf("piuio_emu: libusb_close %d\n", __LINE__);

    libusb_device *dev;
    dev = dev_handle->dev;
    
    if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        printf("  Closed for PIULXIO\n");
        free(dev_handle);
        return;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        printf("  Closed for PIUIO\n");
        free(dev_handle);
        return;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        printf("  Closed for PIUIOBUTTON\n");
        free(dev_handle);
        return;
    }
    true_libusb_close(dev_handle);
}

static int piulxio_helper_process_data_in(uint8_t* bytes, int size) {
    usbi_mutex_lock(&piuioemu_poll_mutex);
    poll_piuio_emu();
    int ret = min(16, size);
    memset(bytes, 0xFF, size);

    // p1 stuff
    for(int i = 0; i < 4; i++) {
        bytes[i] = bytes_piuio[i] & bytes_j[0] & bytes_p[0] & bytes_t[0];
    }
    //printf("%02x %02x %02x %02x %02x\n", bytes[0], bytes_piuio[0], bytes_j[0], bytes_p[0], bytes_t[0]);
    bytes_f[0] = bytes[0] & bytes[1] & bytes[2] & bytes[3];
    // p2 stuff
    for(int i = 4; i < 8; i++) {
        bytes[i] = bytes_piuio[i] & bytes_j[2] & bytes_p[2] & bytes_t[2];
    }
    bytes_f[2] = bytes[4] & bytes[5] & bytes[6] & bytes[7];
    if(size <= 8) return ret;
    // coin stuff
    bytes_f[1] = bytes[8] = bytes_piuio[8] & bytes_j[1] & bytes_p[1] & bytes_t[1];
    bytes_f[3] = bytes[9] = bytes_piuio[9] & bytes_j[3] & bytes_p[3] & bytes_t[3];
    
    // Frontal buttons
    bytes_fb[0] = bytes_piuiob[0] & bytes_jb[0] & bytes_pb[0] & bytes_tb[0];
    bytes_fb[1] = bytes_piuiob[1] & bytes_jb[1] & bytes_pb[1] & bytes_tb[1];
        // From the info of the PIUIOBUTTON, we need to construct the LXIO version
    bytes[10] = ((bytes_fb[0] & 0x01)? 0xFF:0xFC) & // UL/UR both on red button
                ((bytes_fb[0] & 0x02)? 0xFF:0xF7) & // DL on left button
                ((bytes_fb[0] & 0x04)? 0xFF:0xEF) & // DR on right button
                ((bytes_fb[0] & 0x08)? 0xFF:0xFB); // Center on green button
    bytes[11] = ((bytes_fb[0] & 0x10)? 0xFF:0xFC) & // UL/UR both on red button
                ((bytes_fb[0] & 0x20)? 0xFF:0xF7) & // DL on left button
                ((bytes_fb[0] & 0x40)? 0xFF:0xEF) & // DR on right button
                ((bytes_fb[0] & 0x80)? 0xFF:0xFB); // Center on green button
    // Probably unused stuff
    for(int i = 12; i < 16; i++) {

        bytes[i] = bytes_piuio[i] & bytes_f[3]; // Only for testing if actually does something?
    }
    usbi_mutex_unlock(&piuioemu_poll_mutex);

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
    usbi_mutex_lock(&piuioemu_poll_mutex);
    poll_piuio_emu();
    memset(bytes, 0xFF, size);
    int ret = min(8, size);

    // Request the state of the sensonrs
    int s1 = bytes_l[0] & 0x3;
    int s2 = bytes_l[2] & 0x3;

    // Fill accordingly
    bytes_f[0] = bytes[0] = bytes_piuio[s1+0] & bytes_j[0] & bytes_p[0] & bytes_t[0];
    bytes_f[1] = bytes[1] = bytes_piuio[8] & bytes_j[1] & bytes_p[1] & bytes_t[1];
    bytes_f[2] = bytes[2] = bytes_piuio[s2+4] &  bytes_j[2] & bytes_p[2] & bytes_t[2];
    bytes_f[3] = bytes[3] = bytes_piuio[9] & bytes_j[3] & bytes_p[3] & bytes_t[3];

    usbi_mutex_unlock(&piuioemu_poll_mutex);
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
    usbi_mutex_lock(&piuioemu_poll_mutex);
    poll_piuio_emu();
    int ret = min(16, size);
    memset(bytes, 0xFF, size);
    bytes_fb[0] = bytes[0] = bytes_piuiob[0] & bytes_jb[0] & bytes_pb[0] & bytes_tb[0];
    bytes_fb[1] = bytes[1] = bytes_piuiob[1] & bytes_jb[1] & bytes_pb[1] & bytes_tb[1];
    usbi_mutex_unlock(&piuioemu_poll_mutex);
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
    else if ((request_type & USB_DIR_IN) && (request_type & USB_TYPE_VENDOR) && (bRequest & PIUIO_CTL_REQ))
    {
        // printf("recv data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piuio_helper_process_data_in(data, wLength);
    }
    else if ((request_type & USB_DIR_OUT) && (request_type & USB_TYPE_VENDOR) && (bRequest & PIUIO_CTL_REQ))
    {
        // printf("push data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piuio_helper_process_data_out(data, wLength);
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

int API_EXPORTED libusb_control_transfer(libusb_device_handle *dev_handle,
	uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength, unsigned int timeout)
{
    //printf("piuio_emu: libusb_control_transfer %d\n", __LINE__);

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
        if((endpoint & 0x7F) == PIUIO_ENDPOINT_IN) {
            *transferred = piuio_helper_process_data_in(data, length);
            return *transferred;
        }
        else if(endpoint == PIUIO_ENDPOINT_OUT) {
            *transferred = piuio_helper_process_data_out(data, length);
            return *transferred;
        }
        else {
            printf("PIUIO (libusb_interrupt_transfer): Unknown endpoint %d\n", (int)endpoint);
            *transferred = 0;
            return 0;
        }
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



int API_EXPORTED libusb_submit_transfer(struct libusb_transfer *transfer) {

    if(transfer == NULL) goto true_libusb_submit_transfer_call;

    if(dummy_response(transfer->dev_handle) == 0) {
        submit_to_queue(piuioemu_queue, transfer);
        return 0;
    }

true_libusb_submit_transfer_call:
    return true_libusb_submit_transfer(transfer);
}

int API_EXPORTED libusb_cancel_transfer(struct libusb_transfer *transfer) {

    if(transfer == NULL) goto true_libusb_cancel_transfer_call;

    if(dummy_response(transfer->dev_handle) == 0) {
        remove_from_queue(piuioemu_queue, transfer);
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

int API_EXPORTED libusb_handle_events(libusb_context *ctx) {
    struct timeval tv;
    usbi_mutex_lock(&piuioemu_mutex);
    Queue* queue = piuioemu_queue;

    if (queue->front == NULL) {
        goto end_handle_events;
    }

    // Calculate the size of the queue
    int count = 0;
    Node* temp = queue->front;
    while (temp != NULL) {
        count++;
        temp = temp->next;
    }

    // Create an array to store the data
    struct libusb_transfer ** eventList = malloc(count * sizeof(struct libusb_transfer *));

    // Dequeue elements into the array
    int index = 0;
    while (queue->front != NULL) {
        temp = queue->front;
        eventList[index++] = temp->data;

        // Move the front to the next node
        queue->front = queue->front->next;

        // Free the old front node
        free(temp);
    }

    // Reset rear to NULL when the queue is empty
    queue->rear = NULL;

     // Process the list
    for (int i = 0; i < count; i++) {
        struct libusb_transfer *transfer = eventList[i];
        if(transfer->dev_handle == NULL) continue;

        libusb_device *dev;
        dev = transfer->dev_handle->dev;

        if(dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID && 
                dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
            if(transfer->type != LIBUSB_TRANSFER_TYPE_INTERRUPT) continue;
            transfer->status = LIBUSB_TRANSFER_COMPLETED;

            if((transfer->endpoint & 0x7F) == PIULXIO_ENDPOINT_IN)
                transfer->actual_length = piulxio_helper_process_data_in(transfer->buffer, transfer->length);
            else if(transfer->endpoint == PIULXIO_ENDPOINT_OUT)
                transfer->actual_length = piulxio_helper_process_data_out(transfer->buffer, transfer->length);
            else
                printf("PIULXIO (libusb_handle_events): Unknown endpoint %d\n", (int)transfer->endpoint);

            if (transfer->callback)
                transfer->callback(transfer);
            continue;
        }
        
        if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
                dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
            if(transfer->type != LIBUSB_TRANSFER_TYPE_INTERRUPT) continue;
            transfer->status = LIBUSB_TRANSFER_COMPLETED;
            if((transfer->endpoint & 0x7F) == PIUIO_ENDPOINT_IN)
                transfer->actual_length = piuio_helper_process_data_in(transfer->buffer, transfer->length);
            else if(transfer->endpoint == PIUIO_ENDPOINT_OUT)
                transfer->actual_length = piuio_helper_process_data_out(transfer->buffer, transfer->length);
            else
                printf("PIUIO (libusb_handle_events): Unknown endpoint %d\n", (int)transfer->endpoint);
            if (transfer->callback)
                transfer->callback(transfer);
            continue;
        }
        
        if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
                dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
            if(transfer->type != LIBUSB_TRANSFER_TYPE_INTERRUPT) continue;
            transfer->status = LIBUSB_TRANSFER_COMPLETED;
            if((transfer->endpoint & 0x7F) == PIUIOBUTTON_ENDPOINT_IN)
                transfer->actual_length = piuiobutton_helper_process_data(transfer->buffer, transfer->length);
            else if(transfer->endpoint == PIUIOBUTTON_ENDPOINT_OUT)
                transfer->actual_length = piuiobutton_helper_process_data(transfer->buffer, transfer->length);
            else
                printf("PIUIOBUTTON (libusb_handle_events): Unknown endpoint %d\n", (int)transfer->endpoint);
            if (transfer->callback) 
                transfer->callback(transfer);
            continue;
        }
    }

    free(eventList);

end_handle_events:
    memset(&tv, 0, sizeof(struct timeval));
    int r = 0;
    r = libusb_handle_events_timeout_completed(ctx, &tv, NULL);

    usbi_mutex_unlock(&piuioemu_mutex);

    return r; //true_libusb_handle_events(ctx);
}

int API_EXPORTED libusb_kernel_driver_active(libusb_device_handle *dev_handle,
	int interface_number) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_kernel_driver_active(dev_handle, interface_number);
}


int API_EXPORTED libusb_detach_kernel_driver(libusb_device_handle *dev_handle,
	int interface_number) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_detach_kernel_driver(dev_handle, interface_number);
}

int API_EXPORTED libusb_set_configuration(libusb_device_handle *dev_handle, int configuration) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_set_configuration(dev_handle, configuration);
}


int API_EXPORTED libusb_claim_interface(libusb_device_handle *dev_handle,
	int interface_number) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_detach_kernel_driver(dev_handle, interface_number);
}

int API_EXPORTED libusb_release_interface(libusb_device_handle *dev_handle,
	int interface_number) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_release_interface(dev_handle, interface_number);
}

void API_EXPORTED libusb_exit(libusb_context *ctx) {
    printf("piuio_emu: libusb_exit %d\n", __LINE__);
    finish_piuio();
}

