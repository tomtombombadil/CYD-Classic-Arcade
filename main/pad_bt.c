// Bluepad32 "custom platform": receives controller reports on the Bluetooth
// task (core 0) and keeps a copy per controller for the game task (core 1).
// Written in C because Bluepad32's headers are C.
#include "pad.h"

#include <string.h>

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <freertos/FreeRTOS.h>
#include <uni.h>

#include "sdkconfig.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "sdkconfig must set CONFIG_BLUEPAD32_PLATFORM_CUSTOM"
#endif

static pad_state_t s_pads[PAD_MAX];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// Stick pushed past this counts as the d-pad (range -512..511)
#define STICK_DIR 170

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
    logi("pad: connected %s\n", d->name);
}

static void on_disconnected(uni_hid_device_t* d) {
    int i = slot_of(d);
    logi("pad: disconnected %d\n", i);
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
    logi("pad: ready %d %s\n", i, model);
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
    if (gp->buttons & BUTTON_A) b |= PAD_A;
    if (gp->buttons & BUTTON_B) b |= PAD_B;
    if (gp->buttons & BUTTON_X) b |= PAD_X;
    if (gp->buttons & BUTTON_Y) b |= PAD_Y;
    if (gp->buttons & (BUTTON_SHOULDER_L | BUTTON_TRIGGER_L)) b |= PAD_L;
    if (gp->buttons & (BUTTON_SHOULDER_R | BUTTON_TRIGGER_R)) b |= PAD_R;
    if (gp->misc_buttons & MISC_BUTTON_START) b |= PAD_START;
    if (gp->misc_buttons & MISC_BUTTON_SELECT) b |= PAD_SELECT;
    if (gp->misc_buttons & MISC_BUTTON_SYSTEM) b |= PAD_HOME;
    taskENTER_CRITICAL(&s_lock);
    s_pads[i].buttons = b;
    s_pads[i].lx = (int16_t)gp->axis_x;
    s_pads[i].ly = (int16_t)gp->axis_y;
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
#ifdef CONFIG_ESP_CONSOLE_UART
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif
#endif
    btstack_init();
    uni_platform_set_custom(&s_platform);
    uni_init(0, NULL);
    btstack_run_loop_execute();  // never returns
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
