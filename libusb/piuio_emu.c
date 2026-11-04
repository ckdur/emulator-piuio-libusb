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
#include "twitch.h"
#include "autoplay.h"

#define min(a, b) ((a)<(b)?(a):(b))
// Full USB string descriptors (bLength, bDescriptorType=3, UTF-16LE text).
// hidapi skips the 2 byte header, so returning bare text loses characters.
static const uint8_t string0[] = {4, LIBUSB_DT_STRING, 0x09, 0x04}; // English (US)
static uint8_t string1[2 + 2*32];
static uint8_t string2[2 + 2*32];
static void make_string_desc(uint8_t* out, const char* str) {
    int n = (int)strlen(str);
    if(n > 32) n = 32;
    out[0] = (uint8_t)(2 + 2*n);
    out[1] = LIBUSB_DT_STRING;
    for(int i = 0; i < n; i++) { out[2+2*i] = (uint8_t)str[i]; out[3+2*i] = 0; }
}

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

// Function to submit (enqueue) an item to the queue. Caller holds piuioemu_queue_mutex
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

// Function to remove a specific item from the queue. Caller holds piuioemu_queue_mutex
// Returns 1 if it was found (and removed)
static int remove_from_queue(Queue* queue, struct libusb_transfer * data) {
    Node *temp = queue->front, *prev = NULL;

    // Search for the node to be removed
    while (temp != NULL && temp->data != data) {
        prev = temp;
        temp = temp->next;
    }

    // If the data was not found
    if (temp == NULL) {
        PRINTF("Item %p not found in the queue.\n", data);
        return 0;
    }

    // Remove the node
    if (prev == NULL) queue->front = temp->next;
    else prev->next = temp->next;
    if (temp == queue->rear) queue->rear = prev; // Update rear if needed
    free(temp);
    return 1;
}

unsigned char bytes_j[4] = {0xFF, 0xFF, 0xFF, 0xFF}; // From joysticks
unsigned char bytes_jb[2] = {0xFF, 0xFF};
unsigned char bytes_p[4] = {0xFF, 0xFF, 0xFF, 0xFF}; // From keyboard
unsigned char bytes_pb[2] = {0xFF, 0xFF};
unsigned char bytes_t[4] = {0xFF, 0xFF, 0xFF, 0xFF}; // From twitch
unsigned char bytes_tb[2] = {0xFF, 0xFF};

unsigned char bytes_l[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
unsigned char bytes_f[16] = { // As LX
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 1P
    0xFF, 0xFF, 0xFF, 0xFF, // Sensor status 2P
    0xFF, 0xFF, // Coins, service
    0xFF, 0xFF, // Frontal buttons in LX mode
    0xFF, 0xFF, 0xFF, 0xFF}; // Probably unused
unsigned char bytes_fb[2] = {0xFF, 0xFF};

#include <pthread.h>
usbi_mutex_t piuioemu_mutex;
// Protects piuioemu_queue / piuioemu_cancelled. Never held while calling callbacks
static pthread_mutex_t piuioemu_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t piuioemu_queue_cond = PTHREAD_COND_INITIALIZER;
static Queue* piuioemu_cancelled;

// LXIO IN report pacing. The game (hidapi) keeps an IN transfer always pending and
// queues every completed report (up to 30), but only pops one per IO tick, right
// after its OUT write. Completing IN transfers as fast as they are submitted fills
// that queue, so, like the firmware, we complete one IN per OUT. While the game is
// not writing at all (init, read-only hosts) an IN only completes on input changes.
#define LX_HOST_IDLE_US 1000000
static unsigned int lx_out_seq = 0;      // Incremented on every LX OUT report
static unsigned int lx_in_seq = 0;       // lx_out_seq value already answered
static int lx_host_writes = 0;
static struct timespec lx_last_out;
static uint8_t lx_last_in[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
//usbi_mutex_t piuioemu_poll_mutex;
Queue* piuioemu_queue;
int g_init = 0;
int piuioemu_mode = WITH_PIUIO | WITH_PIUIOBUTTON;
pthread_t thread_poll;
void* thread_poll_func(void* a);
int thread_poll_done = 0;

static int file_exists(const char* name){
  if(name == NULL) return 0;
  FILE* test = fopen(name, "rb");
  if(test == NULL) {
    return 0;
  }
  fclose(test);
  return 1;
}

static void init_piuio_emu(void) {
    usbi_mutex_init(&piuioemu_mutex);
    //usbi_mutex_init(&piuioemu_poll_mutex);
    if(file_exists("/SAVE/.keyboard"))
        init_keyboards();

    KeyHandler_Twitch_Init();
    check_autoplay();
    piuioemu_queue = createQueue();
    piuioemu_cancelled = createQueue();
    make_string_desc(string1, "ANDAMIRO");
    make_string_desc(string2, "PIU HID V1.00");
    const char *val;
    int r;

    if ((val = getenv("_PIUIOEMU_WITH_PIULXIO"))) {
        r = strtol(val, NULL, 10);
        if(r == 1) {
            piuioemu_mode |= WITH_PIULXIO;
            piuioemu_mode &= ~WITH_PIUIO;
            piuioemu_mode &= ~WITH_PIUIOBUTTON;
        }
        else if(r == 2) {
            piuioemu_mode |= WITH_PIULXIO_2;
            piuioemu_mode &= ~WITH_PIUIO;
            piuioemu_mode &= ~WITH_PIUIOBUTTON;
        }
        else {
            piuioemu_mode &= (~WITH_PIULXIO) & (~WITH_PIULXIO_2);
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

    if ((val = getenv("_PIUIOEMU_EMU_PIUIO_BUTTON"))) {
        r = strtol(val, NULL, 10);
        if(r) {
            piuioemu_mode |= EMU_PIUIO_BUTTON;
        }
        else {
            piuioemu_mode &= ~EMU_PIUIO_BUTTON;
        }
    }

    if ((val = getenv("_PIUIOEMU_PROPAGATE"))) {
        r = strtol(val, NULL, 10);
        if(r) {
            piuioemu_mode |= EMU_PROPAGATE;
        }
        else {
            piuioemu_mode &= ~EMU_PROPAGATE;
        }
    }

    //finish_piuio();
    init_piuio();

    thread_poll_done = 0;
    pthread_create(&thread_poll, NULL, thread_poll_func, NULL);
}

// The TEST button held for 5 seconds kills the process (that is how a game
// hands control back to the boot script, which relaunches the menu). The
// menu itself must NOT be killed that way: exiting the menu only makes the
// boot script relaunch it, in a pointless loop. The menu tells us it is the
// menu through libusb_piuio_report_menu() and we then leave the process
// alive on a TEST hold.
static int piuioemu_menu_process = 0;
void API_EXPORTED libusb_piuio_report_menu(void) {
    piuioemu_menu_process = 1;
    PRINTF("piuio_emu: menu process reported, TEST-hold exit disabled\n");
}

time_t start_time = 0;
bool condition_met = false;
void* thread_poll_func(void* a) {
    (void)a;
    while(!thread_poll_done) {
        poll_keyboards();
        poll_piuio();
        KeyHandler_Twitch_Poll();
        update_autoplay();

        // Detect if service and test has been held 5 seconds
        // Test is bit 9, service is bit 14
        //    bit 9                    bit 14
        if(!(bytes_f[8] & 0x02)/* && !(bytes_f[1] & 0x40)*/) {
            if(!condition_met) {
                // Start timing if condition is met for the first time
                start_time = time(NULL);
                condition_met = true;
                PRINTF("TEST hold ...\n");
            }
            else if (time(NULL) - start_time >= 5) {
                if(piuioemu_menu_process) {
                    // The menu reported itself. Keep it running.
                    PRINTF("TEST held 5 seconds, but this is the menu: staying alive\n");
                    condition_met = false;
                }
                else {
                    // If condition is continuously met for 5 seconds, exit the loop
                    thread_poll_done = 1;
                    PRINTF("TEST held 5 seconds. Exiting ...\n");
                    exit(0);
                    abort();
                    *((int*)NULL) = 0; // crash it
                    return NULL;
                }
            }
        }
        else {
            condition_met = false;
        }
        // Real IOs already block on their transfers. This only avoids burning a
        // whole core when there is nothing real to poll
        usleep(500);
    }
    return NULL;
}

static void poll_piuio_emu(void) {
    // Nothing
}

int API_EXPORTED libusb_init_context(libusb_context **ctx, const struct libusb_init_option options[], int num_options) {
    PRINTF("piuio_emu: libusb_init_context %d\n", __LINE__);
    int r = true_libusb_init_context(ctx, options, num_options);
    if(!g_init) {
        if(ctx) piuio_ctx = *ctx;
        init_piuio_emu();
        g_init = 1;
    }
    
    return r;
}

static struct libusb_device *fake_lxio = NULL;
static struct libusb_device *fake_piuio = NULL;
static struct libusb_device *fake_piuiobutton = NULL;
static struct libusb_device *fake_device(struct libusb_device **slot, uint16_t vid, uint16_t pid, uint8_t address) {
    if(*slot == NULL) {
        struct libusb_device *dev = calloc(1, sizeof(struct libusb_device));
        dev->bus_number = 0xFF; // No real bus uses this
        dev->device_address = address;
        dev->refcnt = 1; // Our own reference, never released
        *slot = dev;
    }
    (*slot)->device_descriptor.idVendor = vid;
    (*slot)->device_descriptor.idProduct = pid;
    return *slot;
}

ssize_t API_EXPORTED libusb_get_device_list(libusb_context *ctx,
	libusb_device ***list)
{
    PRINTF("piuio_emu: libusb_get_device_list %d\n", __LINE__);
    // Call the original one
    libusb_device **true_list;
    ssize_t true_len = true_libusb_get_device_list(ctx, &true_list);
    if(true_len < 0) true_len = 0;

    ssize_t filtered_len = 0;
    if((piuioemu_mode & WITH_PIULXIO) || (piuioemu_mode & WITH_PIULXIO_2)) filtered_len++;
    if(piuioemu_mode & WITH_PIUIO) filtered_len++;
    if(piuioemu_mode & WITH_PIUIOBUTTON) filtered_len++;
    struct libusb_device **ret = calloc(filtered_len+true_len+1, sizeof(struct libusb_device*));
    struct libusb_device **p = ret;

    // Emulated devices are created once and live forever (refcnt never reaches 0).
    // Each one gets its own bus/address: hidapi builds its device "path" from
    // bus:address:interface and opens every interface matching that path.
    if((piuioemu_mode & WITH_PIULXIO) || (piuioemu_mode & WITH_PIULXIO_2)) {
        PRINTF("  Adding PIULXIO\n");
        *p++ = libusb_ref_device(fake_device(&fake_lxio, PIULXIO_VENDOR_ID,
            (piuioemu_mode & WITH_PIULXIO_2) ? PIULXIO_PRODUCT_ID_2 : PIULXIO_PRODUCT_ID, 1));
    }
    if(piuioemu_mode & WITH_PIUIO) {
        PRINTF("  Adding PIUIO\n");
        *p++ = libusb_ref_device(fake_device(&fake_piuio, PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID, 2));
    }
    if(piuioemu_mode & WITH_PIUIOBUTTON) {
        PRINTF("  Adding PIUIOBUTTON\n");
        *p++ = libusb_ref_device(fake_device(&fake_piuiobutton, PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID, 3));
    }

    // Copy the rest just as it is
    if(true_len > 0) {
        memcpy(&ret[filtered_len], true_list, true_len*sizeof(struct libusb_device*));
        free(true_list);
    }
    *list = ret;
    return filtered_len+true_len;
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
    
    if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
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
    PRINTF("piuio_emu: libusb_get_config_descriptor %d\n", __LINE__);
    
    if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        PRINTF("  Requested for PIULXIO\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        PRINTF("  Requested for PIUIO\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piuio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piuio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        PRINTF("  Requested for PIUIOBUTTON\n");
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
    PRINTF("piuio_emu: libusb_get_active_config_descriptor %d\n", __LINE__);
    
    if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        PRINTF("  Requested for PIULXIO\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piulxio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piulxio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        PRINTF("  Requested for PIUIO\n");
        *config = malloc(sizeof(struct libusb_config_descriptor));
        memset(*config, 0, sizeof(struct libusb_config_descriptor));
        memcpy(*config, &piuio_config_desc, sizeof(struct libusb_config_descriptor));
        (*config)->interface = &piuio_interface;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        PRINTF("  Requested for PIUIOBUTTON\n");
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
    PRINTF("piuio_emu: libusb_free_config_descriptor %d\n", __LINE__);
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
    PRINTF("piuio_emu: libusb_open %d\n", __LINE__);
    
    if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        if(!((piuioemu_mode & WITH_PIULXIO) || (piuioemu_mode & WITH_PIULXIO_2))) {
            // Requested the LXIO, but is not enabled
            return LIBUSB_ERROR_NO_DEVICE;
        }
        PRINTF("  Opened for PIULXIO\n");
        *dev_handle = malloc(sizeof(struct libusb_device_handle));
        memset(*dev_handle, 0, sizeof(struct libusb_device_handle));
        (*dev_handle)->dev = dev;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        if(!(piuioemu_mode & WITH_PIUIO)) {
            // Requested the PIUIO, but is not enabled
            return LIBUSB_ERROR_NO_DEVICE;
        }
        PRINTF("  Opened for PIUIO\n");
        *dev_handle = malloc(sizeof(struct libusb_device_handle));
        memset(*dev_handle, 0, sizeof(struct libusb_device_handle));
        (*dev_handle)->dev = dev;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        if(!(piuioemu_mode & WITH_PIUIOBUTTON)) {
            // Requested the PIUIO, but is not enabled
            return LIBUSB_ERROR_NO_DEVICE;
        }
        PRINTF("  Opened for PIUIOBUTTON\n");
        *dev_handle = malloc(sizeof(struct libusb_device_handle));
        memset(*dev_handle, 0, sizeof(struct libusb_device_handle));
        (*dev_handle)->dev = dev;
        return 0;
    }
    return true_libusb_open(dev, dev_handle);
}

void API_EXPORTED libusb_close(libusb_device_handle *dev_handle){
    PRINTF("piuio_emu: libusb_close %d\n", __LINE__);

    libusb_device *dev;
    dev = dev_handle->dev;
    
    if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        PRINTF("  Closed for PIULXIO\n");
        free(dev_handle);
        return;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        PRINTF("  Closed for PIUIO\n");
        free(dev_handle);
        return;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        PRINTF("  Closed for PIUIOBUTTON\n");
        free(dev_handle);
        return;
    }
    true_libusb_close(dev_handle);
}

static int piulxio_helper_process_data_in(uint8_t* bytes, int size) {
    //usbi_mutex_lock(&piuioemu_poll_mutex);
    poll_piuio_emu();
    int ret = min(16, size);
    memset(bytes, 0xFF, size);

    // p1 stuff
    for(int i = 0; i < 4; i++) {
        bytes_f[i] = bytes[i] = bytes_piuio[i] & bytes_j[0] & bytes_p[0] & bytes_t[0];
    }
    // p2 stuff
    for(int i = 4; i < 8; i++) {
        bytes_f[i] = bytes[i] = bytes_piuio[i] & bytes_j[2] & bytes_p[2] & bytes_t[2];
    }
    if(size <= 8) return ret;
    // coin stuff
    bytes_f[8] = bytes[8] = bytes_piuio[8] & bytes_j[1] & bytes_p[1] & bytes_t[1];
    bytes_f[9] = bytes[9] = bytes_piuio[9] & bytes_j[3] & bytes_p[3] & bytes_t[3];
    
    // Frontal buttons. Two formats exist:
    //  - LX bytes 10/11 (P1/P2), active low, panel order UL,UR,CN,DL,DR
    //  - PIUIO button board byte 0: bit0 red (UL/UR), bit1 left (DL), bit2 right (DR),
    //    bit3 green (CN), bits 4..7 the same for P2
    // bytes_piuiob holds the board format, coming either from a real button board or
    // derived from a real LXIO / the pads. Only convert it when it does not already
    // come from a real LXIO, otherwise e.g. UL would come back as UL+UR.
    {
        uint8_t fb = bytes_jb[0] & bytes_pb[0] & bytes_tb[0];
        if(!is_lxio) fb &= bytes_piuiob[0];
        bytes_fb[0] = bytes_piuiob[0] & bytes_jb[0] & bytes_pb[0] & bytes_tb[0];
        bytes_fb[1] = bytes_piuiob[1] & bytes_jb[1] & bytes_pb[1] & bytes_tb[1];
        bytes[10] = bytes_piuio[10] &
                    ((fb & 0x01)? 0xFF:0xFC) & // UL/UR both on red button
                    ((fb & 0x02)? 0xFF:0xF7) & // DL on left button
                    ((fb & 0x04)? 0xFF:0xEF) & // DR on right button
                    ((fb & 0x08)? 0xFF:0xFB) ; // Center on green button
        bytes[11] = bytes_piuio[11] &
                    ((fb & 0x10)? 0xFF:0xFC) & // UL/UR both on red button
                    ((fb & 0x20)? 0xFF:0xF7) & // DL on left button
                    ((fb & 0x40)? 0xFF:0xEF) & // DR on right button
                    ((fb & 0x80)? 0xFF:0xFB) ; // Center on green button
    }
    bytes_f[10] = bytes[10];
    bytes_f[11] = bytes[11];
    // Probably unused stuff
    for(int i = 12; i < 16; i++) {

        bytes_f[i] = bytes[i] = bytes_piuio[i]; // Only for testing if actually does something?
    }
    //usbi_mutex_unlock(&piuioemu_poll_mutex);

    return ret;
}

static int lx_skip_next = 0; // A reply came late: skip one so the game's queue drains
static void lx_complete_pending_in_now(void);
static void lx_note_out(void) {
    pthread_mutex_lock(&piuioemu_queue_mutex);
    lx_host_writes = 1;
    clock_gettime(CLOCK_MONOTONIC, &lx_last_out);
    if(lx_skip_next) lx_skip_next = 0;
    else lx_out_seq++;
    pthread_cond_broadcast(&piuioemu_queue_cond);
    pthread_mutex_unlock(&piuioemu_queue_mutex);
    // Answer right away, before the game even starts its 1 ms hid_read wait
    lx_complete_pending_in_now();
}

static int piulxio_helper_process_data_out(uint8_t* data, int size) {
    int ret = min(16, size);
    if(ret < 16) {
        // hidapi always writes the full report, but be safe with short ones
        uint8_t full[16] = {0};
        memcpy(full, data, ret);
        memcpy(bytes_l, full, 16);
        lx_note_out();
        return ret;
    }

    bytes_l[0] = data[0];
    bytes_l[1] = data[1];
    bytes_l[2] = data[2];
    bytes_l[3] = data[3];
    bytes_l[4] = data[4];
    bytes_l[5] = data[5];
    bytes_l[6] = data[6];
    bytes_l[7] = data[7];
    bytes_l[8] = data[8];
    bytes_l[9] = data[9];
    bytes_l[10] = data[10];
    bytes_l[11] = data[11];
    bytes_l[12] = data[12];
    bytes_l[13] = data[13];
    bytes_l[14] = data[14];
    bytes_l[15] = data[15];

    lx_note_out();
    return ret;
}

static int piuio_helper_process_data_in(uint8_t* bytes, int size) {
    // Request the state of the sensonrs
    int s1 = bytes_l[0] & 0x3;
    int s2 = bytes_l[2] & 0x3;

    //usbi_mutex_lock(&piuioemu_poll_mutex);
    //static int calls = 0;
    //calls = (calls+1) % 4;
    //if(calls == 0) // Only poll on s1 == 0
    poll_piuio_emu(); 
    memset(bytes, 0xFF, size);
    int ret = min(8, size);

    // Fill accordingly
    bytes_f[s1+0] = bytes[0] = bytes_piuio[s1+0] & bytes_j[0] & bytes_p[0] & bytes_t[0];
    bytes_f[s2+4] = bytes[2] = bytes_piuio[s2+4] & bytes_j[2] & bytes_p[2] & bytes_t[2];
    bytes_f[8] = bytes[1] = bytes_piuio[8] & bytes_j[1] & bytes_p[1] & bytes_t[1];
    bytes_f[9] = bytes[3] = bytes_piuio[9] & bytes_j[3] & bytes_p[3] & bytes_t[3];

    // Only mutex on s1 == 0
    //usbi_mutex_unlock(&piuioemu_poll_mutex);
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
    //usbi_mutex_lock(&piuioemu_poll_mutex);
    //poll_piuio_emu();
    int ret = min(16, size);
    memset(bytes, 0xFF, size);
    bytes_fb[0] = bytes[0] = bytes_piuiob[0] & bytes_jb[0] & bytes_pb[0] & bytes_tb[0];
    bytes_fb[1] = bytes[1] = bytes_piuiob[1] & bytes_jb[1] & bytes_pb[1] & bytes_tb[1];
    //usbi_mutex_unlock(&piuioemu_poll_mutex);
    return ret;
}

static int piulxio_libusb_control_transfer(
	uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength) {
    if (request_type == USB_DIR_IN && bRequest == PIULXIO_GET_DESCRIPTOR)
    {
        // From do_control_to_usb: can be 0x300, or param_2 | 0xff | 0x300
        if (wValue == 0x300)
        { // Supported languages
            int ret = min(string0[0], wLength);
            memcpy(data, string0, ret);
            return ret;
        }
        else if (wValue == 0x301)
        {
            int ret = min(string1[0], wLength);
            memcpy(data, string1, ret);
            return ret;
        }
        else if (wValue == 0x302)
        {
            int ret = min(string2[0], wLength);
            memcpy(data, string2, ret);
            return ret;
        }
        else if ((wValue >> 8) == LIBUSB_DT_STRING)
        {
            return LIBUSB_ERROR_PIPE; // No such string (the real one has no serial)
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
    else if (wIndex == 0 && request_type == 0x21 && bRequest == PIULXIO_HID_SET_REPORT)
    {
        // SET_REPORT(Output): hidapi's hid_write() fallback when there is no OUT endpoint
        // PRINTF("push data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piulxio_helper_process_data_out(data, wLength);
    }
    else if (wIndex == 0 && request_type == 0xA1 && bRequest == PIULXIO_HID_GET_REPORT)
    {
        // GET_REPORT(Input)
        // PRINTF("recv data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piulxio_helper_process_data_in(data, wLength);
    }
    PRINTF("Unknown request: %x %x %x %x %x\n", request_type, bRequest, wValue, wIndex, wLength);
    return 0;
}

static int piuio_libusb_control_transfer(
	uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength) {
    (void)wValue;
    (void)wIndex;
    (void)data;
    (void)wLength;
    if (request_type == USB_DIR_IN && bRequest == PIULXIO_GET_DESCRIPTOR)
    {
        // TODO: Fill acordingly
    }
    else if (request_type == (USB_DIR_IN | USB_TYPE_VENDOR) && bRequest == PIUIO_CTL_REQ)
    {
        // PRINTF("recv data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piuio_helper_process_data_in(data, wLength);
    }
    else if (request_type == (USB_DIR_OUT | USB_TYPE_VENDOR) && bRequest == PIUIO_CTL_REQ)
    {
        // PRINTF("push data %x %x %x %x %x\r\n", request_type, bRequest, wValue, wIndex, wLength);
        return piuio_helper_process_data_out(data, wLength);
    }
    PRINTF("Unknown request: %x %x %x %x %x\n", request_type, bRequest, wValue, wIndex, wLength);
    return 0;
}

static int piuiobutton_libusb_control_transfer(
	uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength) {
    (void)wValue;
    (void)wIndex;
    (void)data;
    (void)wLength;
    if (request_type == USB_DIR_IN && bRequest == PIULXIO_GET_DESCRIPTOR)
    {
        // TODO: Fill acordingly
    }
    else if (request_type & USB_DIR_IN)
    {
        // The game reads the button board with 0xC0/0xAE (8 bytes)
        return piuiobutton_helper_process_data(data, wLength);
    }
    else
    {
        // Lamp write from the game (0x40/0xAE): accept it, nothing to drive
        return wLength;
    }
    PRINTF("Unknown request: %x %x %x %x %x\n", request_type, bRequest, wValue, wIndex, wLength);
    return 0;
}

int API_EXPORTED libusb_control_transfer(libusb_device_handle *dev_handle,
	uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	unsigned char *data, uint16_t wLength, unsigned int timeout)
{
    //PRINTF("piuio_emu: libusb_control_transfer %d\n", __LINE__);

    libusb_device *dev;
    dev = dev_handle->dev;
    
    if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
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
    // PRINTF("piuio_emu: libusb_interrupt_transfer %d\n", __LINE__);

    libusb_device *dev;
    dev = dev_handle->dev;
    
    if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        if((endpoint & 0x7F) == PIULXIO_ENDPOINT_IN) {
            *transferred = piulxio_helper_process_data_in(data, length);
            return LIBUSB_SUCCESS;
        }
        else if(endpoint == PIULXIO_ENDPOINT_OUT) {
            *transferred = piulxio_helper_process_data_out(data, length);
            return LIBUSB_SUCCESS;
        }
        else {
            PRINTF("PIULXIO: Unknown endpoint %d\n", (int)endpoint);
            *transferred = 0;
            return 0;
        }
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        if((endpoint & 0x7F) == PIUIO_ENDPOINT_IN) {
            *transferred = piuio_helper_process_data_in(data, length);
            return LIBUSB_SUCCESS;
        }
        else if(endpoint == PIUIO_ENDPOINT_OUT) {
            *transferred = piuio_helper_process_data_out(data, length);
            return LIBUSB_SUCCESS;
        }
        else {
            PRINTF("PIUIO (libusb_interrupt_transfer): Unknown endpoint %d\n", (int)endpoint);
            *transferred = 0;
            return 0;
        }
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        if((endpoint & 0x7F) == PIUIOBUTTON_ENDPOINT_IN) {
            *transferred = piuiobutton_helper_process_data(data, length);
            return LIBUSB_SUCCESS;
        }
        else if(endpoint == PIUIOBUTTON_ENDPOINT_OUT) {
            *transferred = piuiobutton_helper_process_data(data, length);
            return LIBUSB_SUCCESS;
        }
        else {
            PRINTF("PIUIOBUTTON (libusb_interrupt_transfer): Unknown endpoint %d\n", (int)endpoint);
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

    if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
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
    PRINTF("CKDUR: libusb_alloc_transfer (1.0) %d\n", __LINE__);
    struct libusb_transfer * tr = malloc(sizeof(struct libusb_transfer));
    memset(tr, 0, sizeof(struct libusb_transfer));
    return tr;
}*/



// Transfers taken out of the queue by the handle_events pass that is running now.
// A cancel arriving for one of them is applied when its callback resubmits it.
#define MAX_BATCH 64
static struct libusb_transfer *batch[MAX_BATCH];
static int batch_cancel[MAX_BATCH];
static int batch_len = 0;
// Same for the transfer being completed from the game's write path
static struct libusb_transfer *sync_inflight = NULL;
static int sync_cancel = 0;

int API_EXPORTED libusb_submit_transfer(struct libusb_transfer *transfer) {

    if(transfer == NULL) goto true_libusb_submit_transfer_call;

    if(dummy_response(transfer->dev_handle) == 0) {
        pthread_mutex_lock(&piuioemu_queue_mutex);
        int cancelled = 0;
        for(int i = 0; i < batch_len; i++)
            if(batch[i] == transfer && batch_cancel[i]) { cancelled = 1; batch_cancel[i] = 0; }
        if(transfer == sync_inflight && sync_cancel) { cancelled = 1; sync_cancel = 0; }
        submit_to_queue(cancelled ? piuioemu_cancelled : piuioemu_queue, transfer);
        pthread_cond_broadcast(&piuioemu_queue_cond);
        pthread_mutex_unlock(&piuioemu_queue_mutex);
        return 0;
    }

true_libusb_submit_transfer_call:
    return true_libusb_submit_transfer(transfer);
}

int API_EXPORTED libusb_cancel_transfer(struct libusb_transfer *transfer) {

    if(transfer == NULL) goto true_libusb_cancel_transfer_call;

    if(dummy_response(transfer->dev_handle) == 0) {
        int r = LIBUSB_ERROR_NOT_FOUND;
        pthread_mutex_lock(&piuioemu_queue_mutex);
        if(remove_from_queue(piuioemu_queue, transfer)) {
            // Completed as LIBUSB_TRANSFER_CANCELLED by the next libusb_handle_events
            submit_to_queue(piuioemu_cancelled, transfer);
            r = 0;
        }
        else {
            for(int i = 0; i < batch_len; i++)
                if(batch[i] == transfer) { batch_cancel[i] = 1; r = 0; }
            if(transfer == sync_inflight) { sync_cancel = 1; r = 0; }
        }
        pthread_cond_broadcast(&piuioemu_queue_cond);
        pthread_mutex_unlock(&piuioemu_queue_mutex);
        return r;
    }
true_libusb_cancel_transfer_call:
    return true_libusb_cancel_transfer(transfer);
}

static int is_dev(libusb_device *dev, uint16_t vid, uint16_t pid) {
    return dev->device_descriptor.idVendor == vid && dev->device_descriptor.idProduct == pid;
}

static int is_lxio_dev(libusb_device *dev) {
    return is_dev(dev, PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID) || is_dev(dev, PIULXIO_VENDOR_ID, PIULXIO_PRODUCT_ID_2);
}

static int64_t elapsed_us(const struct timespec *from) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)(now.tv_sec - from->tv_sec) * 1000000 + (now.tv_nsec - from->tv_nsec) / 1000;
}

// Decides if a pending LXIO IN transfer completes now. Returns 1 and fills the
// buffer if it does. See the pacing notes at lx_out_seq
static int lx_complete_in(struct libusb_transfer *transfer) {
    int answer = 0;
    pthread_mutex_lock(&piuioemu_queue_mutex);
    if(lx_out_seq != lx_in_seq) {
        lx_in_seq = lx_out_seq; // Several OUTs before an IN are answered once
        answer = 1;
        // The game only waits 1 ms after its write. A later reply stays queued and
        // the game would read one report behind from now on.
        if(elapsed_us(&lx_last_out) > 900) lx_skip_next = 1;
    }
    else if(lx_host_writes && elapsed_us(&lx_last_out) > LX_HOST_IDLE_US) {
        lx_host_writes = 0;
    }
    int idle = !lx_host_writes;
    pthread_mutex_unlock(&piuioemu_queue_mutex);
    if(!answer && !idle) return 0;

    uint8_t tmp[64];
    int len = transfer->length > (int)sizeof(tmp) ? (int)sizeof(tmp) : transfer->length;
    int n = piulxio_helper_process_data_in(tmp, len);
    if(!answer && memcmp(tmp, lx_last_in, n) == 0) return 0; // Idle host: only changes
    memcpy(transfer->buffer, tmp, n);
    memcpy(lx_last_in, tmp, n);
    transfer->actual_length = n;
    return 1;
}

static int process_emulated_transfer(struct libusb_transfer *transfer) {
    libusb_device *dev = transfer->dev_handle->dev;
    if(transfer->type != LIBUSB_TRANSFER_TYPE_INTERRUPT) {
        PRINTF("piuio_emu: unsupported async transfer type %d\n", (int)transfer->type);
        transfer->status = LIBUSB_TRANSFER_ERROR;
        transfer->actual_length = 0;
        return 1;
    }
    transfer->status = LIBUSB_TRANSFER_COMPLETED;

    if(is_lxio_dev(dev)) {
        if((transfer->endpoint & 0x7F) == PIULXIO_ENDPOINT_IN)
            return lx_complete_in(transfer);
        else if(transfer->endpoint == PIULXIO_ENDPOINT_OUT)
            transfer->actual_length = piulxio_helper_process_data_out(transfer->buffer, transfer->length);
        else {
            PRINTF("PIULXIO (libusb_handle_events): Unknown endpoint %d\n", (int)transfer->endpoint);
            transfer->status = LIBUSB_TRANSFER_STALL;
        }
        return 1;
    }

    if(is_dev(dev, PIUIO_VENDOR_ID, PIUIO_PRODUCT_ID)) {
        if((transfer->endpoint & 0x7F) == PIUIO_ENDPOINT_IN)
            transfer->actual_length = piuio_helper_process_data_in(transfer->buffer, transfer->length);
        else if(transfer->endpoint == PIUIO_ENDPOINT_OUT)
            transfer->actual_length = piuio_helper_process_data_out(transfer->buffer, transfer->length);
        else {
            PRINTF("PIUIO (libusb_handle_events): Unknown endpoint %d\n", (int)transfer->endpoint);
            transfer->status = LIBUSB_TRANSFER_STALL;
        }
        return 1;
    }

    if(is_dev(dev, PIUIOBUTTON_VENDOR_ID, PIUIOBUTTON_PRODUCT_ID)) {
        if((transfer->endpoint & 0x7F) == PIUIOBUTTON_ENDPOINT_IN)
            transfer->actual_length = piuiobutton_helper_process_data(transfer->buffer, transfer->length);
        else if(transfer->endpoint == PIUIOBUTTON_ENDPOINT_OUT)
            transfer->actual_length = transfer->length; // Lamps, nothing to drive
        else {
            PRINTF("PIUIOBUTTON (libusb_handle_events): Unknown endpoint %d\n", (int)transfer->endpoint);
            transfer->status = LIBUSB_TRANSFER_STALL;
        }
        return 1;
    }
    transfer->status = LIBUSB_TRANSFER_NO_DEVICE;
    return 1;
}

// Called from the game's write path: if the IN transfer of the read thread is
// sitting in our queue, complete it from here (hidapi's callback only takes its own
// mutex and resubmits). If it is being processed by libusb_handle_events right now,
// that pass picks up the new OUT through lx_out_seq instead.
static void lx_complete_pending_in_now(void) {
    struct libusb_transfer *found = NULL;
    pthread_mutex_lock(&piuioemu_queue_mutex);
    for(Node *n = piuioemu_queue->front; n != NULL; n = n->next) {
        struct libusb_transfer *t = n->data;
        if(t->dev_handle && is_lxio_dev(t->dev_handle->dev) &&
           t->type == LIBUSB_TRANSFER_TYPE_INTERRUPT && (t->endpoint & 0x80)) { found = t; break; }
    }
    if(found) {
        remove_from_queue(piuioemu_queue, found);
        sync_inflight = found;
        sync_cancel = 0;
    }
    pthread_mutex_unlock(&piuioemu_queue_mutex);
    if(!found) return;

    found->status = LIBUSB_TRANSFER_COMPLETED;
    if(!lx_complete_in(found)) {
        // Nothing to answer (e.g. skipped to drain): put it back (or cancel it)
        pthread_mutex_lock(&piuioemu_queue_mutex);
        submit_to_queue(sync_cancel ? piuioemu_cancelled : piuioemu_queue, found);
        sync_inflight = NULL;
        sync_cancel = 0;
        pthread_cond_broadcast(&piuioemu_queue_cond);
        pthread_mutex_unlock(&piuioemu_queue_mutex);
        return;
    }
    if(found->callback) found->callback(found); // Normally resubmits it
    pthread_mutex_lock(&piuioemu_queue_mutex);
    sync_inflight = NULL;
    sync_cancel = 0;
    pthread_mutex_unlock(&piuioemu_queue_mutex);
}

int API_EXPORTED libusb_handle_events(libusb_context *ctx) {
    struct timeval tv;
    int completed = 0;
    int pending = 0;
    usbi_mutex_lock(&piuioemu_mutex);

    // Take everything that is queued now. Callbacks may resubmit (or cancel) while
    // we process, which only touches the queues under piuioemu_queue_mutex.
    pthread_mutex_lock(&piuioemu_queue_mutex);
    batch_len = 0;
    while(piuioemu_queue->front != NULL && batch_len < MAX_BATCH) {
        Node *n = piuioemu_queue->front;
        piuioemu_queue->front = n->next;
        if(piuioemu_queue->front == NULL) piuioemu_queue->rear = NULL;
        batch_cancel[batch_len] = 0;
        batch[batch_len++] = n->data;
        free(n);
    }
    int count = batch_len;
    pthread_mutex_unlock(&piuioemu_queue_mutex);

    for(int i = 0; i < count; i++) {
        struct libusb_transfer *transfer = batch[i];
        if(transfer->dev_handle == NULL) continue;

        pthread_mutex_lock(&piuioemu_queue_mutex);
        int cancel = batch_cancel[i];
        batch_cancel[i] = 0;
        pthread_mutex_unlock(&piuioemu_queue_mutex);
        if(cancel) {
            transfer->status = LIBUSB_TRANSFER_CANCELLED;
            transfer->actual_length = 0;
        }
        else if(!process_emulated_transfer(transfer)) {
            // Not ready yet, keep it pending (in its original order)
            pthread_mutex_lock(&piuioemu_queue_mutex);
            submit_to_queue(piuioemu_queue, transfer);
            pthread_mutex_unlock(&piuioemu_queue_mutex);
            pending++;
            continue;
        }
        completed++;
        if (transfer->callback)
            transfer->callback(transfer);
    }

    if(!completed && pending) {
        // Nothing to report yet: sleep until the game writes (or 1 ms) instead of spinning
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += 1000000;
        if(until.tv_nsec >= 1000000000) { until.tv_sec++; until.tv_nsec -= 1000000000; }
        pthread_mutex_lock(&piuioemu_queue_mutex);
        if(lx_out_seq == lx_in_seq && piuioemu_cancelled->front == NULL)
            pthread_cond_timedwait(&piuioemu_queue_cond, &piuioemu_queue_mutex, &until);
        pthread_mutex_unlock(&piuioemu_queue_mutex);
    }

    // Transfers cancelled while they were queued (also the ones cancelled while we waited)
    pthread_mutex_lock(&piuioemu_queue_mutex);
    batch_len = 0;
    Node *c = piuioemu_cancelled->front;
    piuioemu_cancelled->front = piuioemu_cancelled->rear = NULL;
    pthread_mutex_unlock(&piuioemu_queue_mutex);
    while(c != NULL) {
        Node *next = c->next;
        struct libusb_transfer *transfer = c->data;
        free(c);
        transfer->status = LIBUSB_TRANSFER_CANCELLED;
        transfer->actual_length = 0;
        completed++;
        if (transfer->callback)
            transfer->callback(transfer);
        c = next;
    }
    usbi_mutex_unlock(&piuioemu_mutex);

    // Real devices
    memset(&tv, 0, sizeof(struct timeval));
    if(!count) tv.tv_usec = 10000; // No emulated transfers at all: let libusb block a bit
    return libusb_handle_events_timeout_completed(ctx, &tv, NULL);
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
    return true_libusb_claim_interface(dev_handle, interface_number);
}

int API_EXPORTED libusb_release_interface(libusb_device_handle *dev_handle,
	int interface_number) {

    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_release_interface(dev_handle, interface_number);
}

int API_EXPORTED libusb_set_interface_alt_setting(libusb_device_handle *dev_handle,
	int interface_number, int alternate_setting)
{
    if(dummy_response(dev_handle) == 0) return 0;
    return true_libusb_set_interface_alt_setting(dev_handle, interface_number, alternate_setting);
}

int API_EXPORTED libusb_get_configuration(libusb_device_handle *dev_handle,
	int *config)
{
    libusb_device *dev;
    dev = dev_handle->dev;

    if((dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID || dev->device_descriptor.idProduct == PIULXIO_PRODUCT_ID_2) && 
       dev->device_descriptor.idVendor == PIULXIO_VENDOR_ID) {
        *config = piulxio_config_desc.bConfigurationValue;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIO_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIO_VENDOR_ID) {
        *config = piuio_config_desc.bConfigurationValue;
        return 0;
    }
    
    if(dev->device_descriptor.idProduct == PIUIOBUTTON_PRODUCT_ID && 
       dev->device_descriptor.idVendor == PIUIOBUTTON_VENDOR_ID) {
        *config = piuiobutton_config_desc.bConfigurationValue;
        return 0;
    }
    return true_libusb_get_configuration(dev_handle, config);
}

void API_EXPORTED libusb_exit(libusb_context *ctx) {
    (void)ctx;
    PRINTF("piuio_emu: libusb_exit %d\n", __LINE__);
    thread_poll_done = 1;
    pthread_join(thread_poll, NULL);
    
    finish_piuio();
    KeyHandler_Twitch_Exit();
}

int API_EXPORTED XNextEvent(void *display, void *event_return);
int API_EXPORTED XNextEvent(void *display, void *event_return) {
    return XNextEventEMU(display, event_return);
}

