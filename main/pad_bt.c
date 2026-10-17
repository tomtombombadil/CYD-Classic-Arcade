// Bluepad32 "custom platform": receives controller reports on the Bluetooth
// task (core 0) and keeps a copy per controller for the game task (core 1).
// Written in C because Bluepad32's headers are C.
#include "pad.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_tlv_esp32.h>
#include <classic/btstack_link_key_db_tlv.h>
#include <freertos/FreeRTOS.h>
#include <uni.h>

#include "sdkconfig.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "sdkconfig must set CONFIG_BLUEPAD32_PLATFORM_CUSTOM"
#endif

static pad_state_t s_pads[PAD_MAX];
static volatile bool s_ready = false;

void devlog_c(const char* text);    // main/devlog.h (C++)

static void logf_(const char* fmt, ...) {
    char text[100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    devlog_c(text);
}
static volatile bool s_swap_ab = false;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// Stick pushed past this counts as the d-pad (range -512..511)
#define STICK_DIR 170

// ---- Pairings are kept ----
// Bluepad32 deletes a controller's link key whenever one connection attempt
// fails ("so that the connection can start again"): a controller that pages
// the board while it is still starting, or is busy searching, then loses its
// pairing and needs pairing mode again (Tom, v0.14.0: "reboots drop the
// controller pairing"). BTstack's key store (NVS) is wrapped so such deletes
// are ignored; a key goes only when the controller refuses it (it was paired
// elsewhere since) or with Forget Controllers.
#if defined(ENABLE_CLASSIC)
static const btstack_link_key_db_t* s_keys;
static btstack_link_key_db_t s_kept_keys;
static bool s_forgetting;
static btstack_packet_callback_registration_t s_hci_cb;
static volatile int s_paired;
#endif

#if defined(ENABLE_CLASSIC)
static void count_keys(void) {
    btstack_link_key_iterator_t it;
    bd_addr_t addr;
    link_key_t key;
    link_key_type_t type;
    int n = 0;
    if (s_keys->iterator_init(&it)) {
        while (s_keys->iterator_get_next(&it, addr, key, &type)) n++;
        s_keys->iterator_done(&it);
    }
    s_paired = n;
}

static void keys_open(void) {}

static void keys_put(bd_addr_t addr, link_key_t key, link_key_type_t type) {
    s_keys->put_link_key(addr, key, type);
    count_keys();
}

static void keys_delete(bd_addr_t addr) {
    if (!s_forgetting) {
        logf_("BT: kept the pairing of %s (a connection attempt failed)", bd_addr_to_str(addr));
        return;
    }
    s_keys->delete_link_key(addr);
    count_keys();
}

static void on_hci_event(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_AUTHENTICATION_COMPLETE_EVENT)
        return;
    const uint8_t status = hci_event_authentication_complete_get_status(packet);
    if (status != ERROR_CODE_PIN_OR_KEY_MISSING && status != ERROR_CODE_AUTHENTICATION_FAILURE) return;
    hci_connection_t* c = hci_connection_for_handle(hci_event_authentication_complete_get_connection_handle(packet));
    if (!c) return;
    // The controller refused our key: it was paired with something else
    logf_("BT: %s refused its key (paired elsewhere since): forgotten", bd_addr_to_str(c->address));
    s_keys->delete_link_key(c->address);
    count_keys();
}

static void keep_pairings(void) {
    s_keys = btstack_link_key_db_tlv_get_instance(btstack_tlv_esp32_get_instance(), NULL);
    s_kept_keys = *s_keys;
    s_kept_keys.open = keys_open;
    s_kept_keys.put_link_key = keys_put;
    s_kept_keys.delete_link_key = keys_delete;
    hci_set_link_key_db(&s_kept_keys);
    s_hci_cb.callback = on_hci_event;
    hci_add_event_handler(&s_hci_cb);
}

static void forget_now(void* ctx) {
    (void)ctx;
    logf_("BT: Forget Controllers");
    s_forgetting = true;
    uni_bt_del_keys_unsafe();
    s_forgetting = false;
    count_keys();
}
static btstack_context_callback_registration_t s_forget_cb = {.callback = forget_now};
#endif

static int slot_of(uni_hid_device_t* d) {
    int i = uni_hid_device_get_idx_for_instance(d);
    return (i >= 0 && i < PAD_MAX) ? i : -1;
}

static void on_init(int argc, const char** argv) {
    (void)argc;
    (void)argv;
}

static void on_init_complete(void) {
    // Look for controllers in pairing mode and let paired ones reconnect.
    // Pairings are kept (BTstack stores them in NVS).
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
    uni_bt_list_keys_unsafe();
#if defined(ENABLE_CLASSIC)
    count_keys();
    logf_("BT: started, %d controller(s) paired", s_paired);
#else
    logf_("BT: started (BLE)");
#endif
    s_ready = true;
}

static uni_error_t on_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    (void)addr;
    (void)name;
    (void)rssi;
    // Game controllers only: no keyboards or mice
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD)
        return UNI_ERROR_IGNORE_DEVICE;
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_MICE) == UNI_BT_COD_MINOR_MICE)
        return UNI_ERROR_IGNORE_DEVICE;
    return UNI_ERROR_SUCCESS;
}

static void on_connected(uni_hid_device_t* d) {
    logf_("BT: connected %s (%s)", d->name, bd_addr_to_str(d->conn.btaddr));
}

static void on_disconnected(uni_hid_device_t* d) {
    int i = slot_of(d);
    logf_("BT: controller %d disconnected", i + 1);
    if (i < 0) return;
    taskENTER_CRITICAL(&s_lock);
    memset(&s_pads[i], 0, sizeof(s_pads[i]));
    taskEXIT_CRITICAL(&s_lock);
}

static uni_error_t on_ready(uni_hid_device_t* d) {
    int i = slot_of(d);
    if (i < 0) return UNI_ERROR_IGNORE_DEVICE;
    const char* model = uni_guess_controller_name(d->vendor_id, d->product_id);
    if (!model || !model[0]) model = d->name[0] ? d->name : "Controller";
    taskENTER_CRITICAL(&s_lock);
    memset(&s_pads[i], 0, sizeof(s_pads[i]));
    s_pads[i].connected = 1;
    strncpy(s_pads[i].name, model, sizeof(s_pads[i].name) - 1);
    taskEXIT_CRITICAL(&s_lock);
    // Player LED on controllers that have one
    if (d->report_parser.set_player_leds) d->report_parser.set_player_leds(d, 1 << i);
    logf_("BT: controller %d ready: %s", i + 1, model);
    return UNI_ERROR_SUCCESS;
}

static void on_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) return;
    int i = slot_of(d);
    if (i < 0) return;
    const uni_gamepad_t* gp = &ctl->gamepad;
    uint32_t b = 0;
    if ((gp->dpad & DPAD_UP) || gp->axis_y < -STICK_DIR) b |= PAD_UP;
    if ((gp->dpad & DPAD_DOWN) || gp->axis_y > STICK_DIR) b |= PAD_DOWN;
    if ((gp->dpad & DPAD_LEFT) || gp->axis_x < -STICK_DIR) b |= PAD_LEFT;
    if ((gp->dpad & DPAD_RIGHT) || gp->axis_x > STICK_DIR) b |= PAD_RIGHT;
    if (gp->axis_ry < -STICK_DIR) b |= PAD_RUP;
    if (gp->axis_ry > STICK_DIR) b |= PAD_RDOWN;
    if (gp->axis_rx < -STICK_DIR) b |= PAD_RLEFT;
    if (gp->axis_rx > STICK_DIR) b |= PAD_RRIGHT;
    const bool swap = s_swap_ab;
    if (gp->buttons & BUTTON_A) b |= swap ? PAD_B : PAD_A;
    if (gp->buttons & BUTTON_B) b |= swap ? PAD_A : PAD_B;
    if (gp->buttons & BUTTON_X) b |= swap ? PAD_Y : PAD_X;
    if (gp->buttons & BUTTON_Y) b |= swap ? PAD_X : PAD_Y;
    if (gp->buttons & (BUTTON_SHOULDER_L | BUTTON_TRIGGER_L)) b |= PAD_L;
    if (gp->buttons & (BUTTON_SHOULDER_R | BUTTON_TRIGGER_R)) b |= PAD_R;
    if (gp->misc_buttons & MISC_BUTTON_START) b |= PAD_START;
    if (gp->misc_buttons & MISC_BUTTON_SELECT) b |= PAD_SELECT;
    if (gp->misc_buttons & MISC_BUTTON_SYSTEM) b |= PAD_HOME;
    taskENTER_CRITICAL(&s_lock);
    s_pads[i].buttons = b;
    s_pads[i].lx = (int16_t)gp->axis_x;
    s_pads[i].ly = (int16_t)gp->axis_y;
    s_pads[i].rx = (int16_t)gp->axis_rx;
    s_pads[i].ry = (int16_t)gp->axis_ry;
    s_pads[i].reports++;
    taskEXIT_CRITICAL(&s_lock);
}

static const uni_property_t* get_property(uni_property_idx_t idx) {
    (void)idx;
    return NULL;
}

static void on_oob_event(uni_platform_oob_event_t event, void* data) {
    (void)event;
    (void)data;
}

static struct uni_platform s_platform = {
    .name = "cyd-arcade",
    .init = on_init,
    .on_init_complete = on_init_complete,
    .on_device_discovered = on_discovered,
    .on_device_connected = on_connected,
    .on_device_disconnected = on_disconnected,
    .on_device_ready = on_ready,
    .on_oob_event = on_oob_event,
    .on_controller_data = on_data,
    .get_property = get_property,
};

void pad_bt_run(void) {
    // No btstack_stdio_init(): it would take the serial port's input, which
    // carries the ROM loader (main/serial_link.cpp).
    btstack_init();
#if defined(ENABLE_CLASSIC)
    keep_pairings();
#endif
    uni_platform_set_custom(&s_platform);
    uni_init(0, NULL);
    btstack_run_loop_execute();  // never returns
}

bool pad_bt_ready(void) {
    return s_ready;
}

void pad_set_swap_ab(bool swap) {
    s_swap_ab = swap;
}

void pad_forget_all(void) {
    // Queued on the Bluetooth task, in order (Bluepad32 keeps 8 such calls)
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) uni_bt_disconnect_device_safe(i);
#if defined(ENABLE_CLASSIC)
    btstack_run_loop_execute_on_main_thread(&s_forget_cb);
#else
    uni_bt_del_keys_safe();
#endif
}

int pad_paired_count(void) {
#if defined(ENABLE_CLASSIC)
    return s_paired;
#else
    return -1;
#endif
}

void pad_get(int idx, pad_state_t* out) {
    if (idx < 0 || idx >= PAD_MAX) {
        memset(out, 0, sizeof(*out));
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    *out = s_pads[idx];
    taskEXIT_CRITICAL(&s_lock);
}
