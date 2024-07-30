#define BTSTACK_FILE__ "demo.c"

#include <stdint.h>
#include <stdio.h>
// #include <stdlib.h>
#include "pico/stdlib.h"
#include <string.h>
#include <inttypes.h>

#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pio_usb.h"

#include "hog_keyboard_demo.h"

#include "btstack.h"

#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"

// from USB HID Specification 1.1, Appendix B.1
const uint8_t hid_descriptor_keyboard_boot_mode[] = {

    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xa1, 0x01, // Collection (Application)

    0x85, 0x01, // Report ID 1

    // Modifier byte

    0x75, 0x01, //   Report Size (1)
    0x95, 0x08, //   Report Count (8)
    0x05, 0x07, //   Usage Page (Key codes)
    0x19, 0xe0, //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xe7, //   Usage Maxium (Keyboard Right GUI)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x81, 0x02, //   Input (Data, Variable, Absolute)

    // Reserved byte

    0x75, 0x01, //   Report Size (1)
    0x95, 0x08, //   Report Count (8)
    0x81, 0x03, //   Input (Constant, Variable, Absolute)

    // LED report + padding

    0x95, 0x05, //   Report Count (5)
    0x75, 0x01, //   Report Size (1)
    0x05, 0x08, //   Usage Page (LEDs)
    0x19, 0x01, //   Usage Minimum (Num Lock)
    0x29, 0x05, //   Usage Maxium (Kana)
    0x91, 0x02, //   Output (Data, Variable, Absolute)

    0x95, 0x01, //   Report Count (1)
    0x75, 0x03, //   Report Size (3)
    0x91, 0x03, //   Output (Constant, Variable, Absolute)

    // Keycodes

    0x95, 0x06, //   Report Count (6)
    0x75, 0x08, //   Report Size (8)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0xff, //   Logical Maximum (1)
    0x05, 0x07, //   Usage Page (Key codes)
    0x19, 0x00, //   Usage Minimum (Reserved (no event indicated))
    0x29, 0xff, //   Usage Maxium (Reserved)
    0x81, 0x00, //   Input (Data, Array)

    0xc0, // End collection
};

static usb_device_t *usb_device = NULL;

// static btstack_timer_source_t heartbeat;
static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_packet_callback_registration_t sm_event_callback_registration;
static uint8_t battery = 100;
static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;
static uint8_t protocol_mode = 1;

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

const uint8_t adv_data[] = {
    // Flags general discoverable, BR/EDR not supported
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // Name
    0x0d, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'H', 'I', 'D', ' ', 'K', 'e', 'y', 'b', 'o', 'a', 'r', 'd',
    // 16-bit Service UUIDs
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xff, ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8,
    // Appearance HID - Keyboard (Category 15, Sub-Category 1)
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC1, 0x03,
};
const uint8_t adv_data_len = sizeof(adv_data);

void core1_main()
{
    sleep_ms(10);

    // To run USB SOF interrupt in core1, create alarm pool in core1.
    static pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
    config.alarm_pool = (void *)alarm_pool_create(2, 1);
    usb_device = pio_usb_host_init(&config);

    //// Call pio_usb_host_add_port to use multi port
    // const uint8_t pin_dp2 = 8;
    // pio_usb_host_add_port(pin_dp2);

    while (true)
    {
        pio_usb_host_task();
        // sleep_ms(5000);
        // printf("core1!\n");
    }
}

static void le_keyboard_setup(void)
{

    multicore_reset_core1();
    // all USB task run in core1
    multicore_launch_core1(core1_main);

    l2cap_init();

    // setup SM: Display only
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);

    // setup ATT server
    att_server_init(profile_data, NULL, NULL);

    // setup battery service
    battery_service_server_init(battery);

    // setup device information service
    device_information_service_server_init();

    // setup HID Device service
    hids_device_init(0, hid_descriptor_keyboard_boot_mode, sizeof(hid_descriptor_keyboard_boot_mode));

    // setup advertisements
    uint16_t adv_int_min = 0x0030;
    uint16_t adv_int_max = 0x0030;
    uint8_t adv_type = 0;
    bd_addr_t null_addr;
    memset(null_addr, 0, 6);
    gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(adv_data_len, (uint8_t *)adv_data);
    gap_advertisements_enable(1);

    // register for HCI events
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // register for SM events
    sm_event_callback_registration.callback = &packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    // register for HIDS
    hids_device_register_packet_handler(packet_handler);
}

// static void send_report(int modifier, int keycode)
// {
//     // setup HID message: A1 = Input Report, Report ID, Payload
//     uint8_t message[] = {0xa1, REPORT_ID, modifier, 0, keycode, 0, 0, 0, 0, 0};
//     hid_device_send_interrupt_message(hid_cid, &message[0], sizeof(message));
// }
// HID Report sending
static void send_report( uint8_t report[8],uint16_t message_size)
{
    // uint8_t report[] = {  modifier, 0, keycode, 0, 0, 0, 0, 0};
    switch (protocol_mode)
    {
    case 0:
        hids_device_send_boot_keyboard_input_report(con_handle, report, message_size);
        break;
    case 1:
        hids_device_send_input_report(con_handle, report, message_size);
        break;
    default:
        break;
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet))
    {
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        con_handle = HCI_CON_HANDLE_INVALID;
        printf("Disconnected\n");
        break;
    case SM_EVENT_JUST_WORKS_REQUEST:
        printf("Just Works requested\n");
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;
    case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
        printf("Confirming numeric comparison: %" PRIu32 "\n", sm_event_numeric_comparison_request_get_passkey(packet));
        sm_numeric_comparison_confirm(sm_event_passkey_display_number_get_handle(packet));
        break;
    case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
        printf("Display Passkey: %" PRIu32 "\n", sm_event_passkey_display_number_get_passkey(packet));
        break;
    case HCI_EVENT_HIDS_META:
        switch (hci_event_hids_meta_get_subevent_code(packet))
        {
        case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
            con_handle = hids_subevent_input_report_enable_get_con_handle(packet);
            printf("Report Characteristic Subscribed %u\n", hids_subevent_input_report_enable_get_enable(packet));
            break;
        case HIDS_SUBEVENT_BOOT_KEYBOARD_INPUT_REPORT_ENABLE:
            con_handle = hids_subevent_boot_keyboard_input_report_enable_get_con_handle(packet);
            printf("Boot Keyboard Characteristic Subscribed %u\n", hids_subevent_boot_keyboard_input_report_enable_get_enable(packet));
            break;
        case HIDS_SUBEVENT_PROTOCOL_MODE:
            protocol_mode = hids_subevent_protocol_mode_get_protocol_mode(packet);
            printf("Protocol Mode: %s mode\n", hids_subevent_protocol_mode_get_protocol_mode(packet) ? "Report" : "Boot");
            break;
        case HIDS_SUBEVENT_CAN_SEND_NOW:
            // typing_can_send_now();
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }
}

/* @section Main Application Setup
 *
 * @text Listing MainConfiguration shows main application code.
 * To run a HID Device service you need to initialize the SDP, and to create and register HID Device record with it.
 * At the end the Bluetooth stack is started.
 */

/* LISTING_START(MainConfiguration): Setup HID Device */

int btstack_main(int argc, const char *argv[]);
int btstack_main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    stdio_init_all();
    sleep_ms(1000);
    printf("OK!!!!!!");
    le_keyboard_setup();
    // turn on!
    hci_power_control(HCI_POWER_ON);
    uint8_t message[] = { 0, 0, 0, 0, 0, 0, 0, 0};
    while (true)
    {
        if (usb_device != NULL)
        {
            for (int dev_idx = 0; dev_idx < PIO_USB_DEVICE_CNT; dev_idx++)
            {
                usb_device_t *device = &usb_device[dev_idx];
                if (!device->connected)
                {
                    continue;
                }
                // Print received packet to EPs
                for (int ep_idx = 0; ep_idx < PIO_USB_DEV_EP_CNT; ep_idx++)
                {
                    endpoint_t *ep = pio_usb_get_endpoint(device, ep_idx);
                    if (ep == NULL)
                    {
                        break;
                    }
                    uint8_t temp[64];
                    int len = pio_usb_get_in_data(ep, temp, sizeof(temp));

                    if (len > 0)
                    {
                        printf("%04x:%04x EP 0x%02x:\t", device->vid, device->pid,
                               ep->ep_num);
                        for (int i = 0; i < len; i++)
                        {
                            message[i] = temp[i];
                            printf("%02x ", temp[i]);
                        }
                        printf("\n");
                        send_report(&message[0],sizeof(message));
                    }
                }
            }
        }
        stdio_flush();
        sleep_us(10);
    }

    return 0;
}
/* LISTING_END */
/* EXAMPLE_END */