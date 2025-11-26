#include "tusb_config.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "bsp/board.h"
#include "pio_usb.h"
#include "cdc_stdio_lib.h"

// GPIO mapping
const uint host_pin_dp = 2;



// Host HID
static uint8_t host_hid_instance = 0;
static uint8_t host_hid_boot_mode = 0;

// Device HID
static uint8_t dev_hid_instance = 0;

// Function prototypes
void hexdump(const void *data, size_t len);
void core1_main(void);

// TinyUSB Host HID Callbacks
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len);
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance);
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    // Use Interface Association Descriptor (IAD) for CDC
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = 0xCafe,
    .idProduct = 0x4005,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  return desc_hid_report;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)
#define EPNUM_HID 0x83
#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82

uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    TUD_CDC_DESCRIPTOR(0, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(2, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10)
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index; // for multiple configurations
  return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const *string_desc_arr[] = {
    (char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "TinyUSB",            // 1: Manufacturer
    "HID-Proxy",          // 2: Product
    "914784",             // 3: Serials, should use chip ID
    "TinyUSB CDC",        // 4: CDC Interface
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;

  uint8_t chr_count;

  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    // Convert ASCII string into UTF-16
    if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
      return NULL;

    const char *str = string_desc_arr[index];

    // Cap at max char
    chr_count = strlen(str);
    if (chr_count > 31)
      chr_count = 31;

    for (uint8_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

  return _desc_str;
}


int main(void) {
    board_init();

    // Initialize TinyUSB device stack before stdio
    tusb_init();

    // Initialize USB CDC stdio (for debugging)
    cdc_stdio_lib_init();

    stdio_init_all();

    printf("USB HID proxy\n");

    multicore_launch_core1(core1_main);

    while (true) {
        tud_task();
    }

    return 0;
}

void core1_main(void) {
    // Configure PIO-USB for host mode
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = host_pin_dp;

    // Configure TinyUSB to use PIO-USB
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    // Initialize TinyUSB host stack
    tuh_init(1);

    while(true) {
        // Run TinyUSB host task (which handles PIO-USB internally)
        tuh_task();
    }
}

// TinyUSB Host HID callback: invoked when HID device is mounted
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_report;
    (void)desc_len;
    printf("HID device mounted: addr %d, instance %d\n", dev_addr, instance);

    // Request to receive reports
    tuh_hid_receive_report(dev_addr, instance);
}

// TinyUSB Host HID callback: invoked when HID device is unmounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("HID device unmounted: addr %d, instance %d\n", dev_addr, instance);
}

// TinyUSB Host HID callback: invoked when HID report is received
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    printf("HID report from device %d, instance %d, len %d\n", dev_addr, instance, len);
    hexdump(report, len);

    // Forward report to USB device (to host PC)
    tud_hid_report(0, report, len);

    // Continue requesting reports
    tuh_hid_receive_report(dev_addr, instance);
}

// TinyUSB Device HID callback: invoked when host PC requests a report
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    // We don't handle GET_REPORT in this simple passthrough
    return 0;
}

// TinyUSB Device HID callback: invoked when host PC sends a report (e.g., LED state)
void tud_hid_report_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
    (void)instance;
    printf("HID report from host PC, len %d\n", len);
    hexdump(report, len);
    // Note: This would forward to physical keyboard if needed
    // tuh_hid_set_report(1, 0, 0, HID_REPORT_TYPE_OUTPUT, report, len);
}

// TinyUSB Device HID callback: invoked when host PC sends SET_REPORT (e.g., LED state)
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void)instance;
    printf("HID SET_REPORT from host PC: report_id %d, type %d, len %d\n", report_id, report_type, bufsize);
    hexdump(buffer, bufsize);
    // Forward LED state to physical keyboard
    // Device address 1 is typically the first connected device
    tuh_hid_set_report(1, 0, report_id, report_type, (void*)buffer, bufsize);
}

void hexdump(const void *data, size_t len) {
    const uint8_t *data_ptr = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) {
        printf("%02x ", data_ptr[i]);
    }
    printf("\n");
}
