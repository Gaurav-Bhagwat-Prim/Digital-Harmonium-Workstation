/* =============================================================================
 * main.c — System Entry Point & Stage 5 Input Layer Unit Test Harness
 * Project: Digital Harmonium Workstation Firmware
 * Stage 5 — Input Layer & Event Producers
 * ============================================================================= */

#include <stdio.h>
#include <assert.h>
#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "system_manager.h"
#include "task_manager.h"
#include "event_bus.h"
#include "button_manager.h"
#include "slider_filter.h"

int main(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 5 Input Layer Test\n");
    printf("=============================================================\n");

    // Initialize System Runtime, Tasks, Event Bus & Input Engine
    vhl_err_t err = vhl_system_init();
    assert(err == VHL_OK);

    err = vhl_event_bus_init();
    assert(err == VHL_OK);

    err = vhl_button_manager_init();
    assert(err == VHL_OK);

    err = vhl_slider_filter_init();
    assert(err == VHL_OK);

    printf("[TEST 1 PASS] Button Manager & Slider Filter engines initialized cleanly.\n");

    // Test 2: Keyboard Matrix Row/Col Mapping & Monotonic Timestamped Key Event Generation
    // Map Row 1, Col 2 -> MIDI Note (48 + 1*8 + 2 = 58)
    vhl_midi_note_t mappedNote = vhl_input_map_matrix_to_note(1, 2);
    assert(mappedNote == 58);

    // Simulate 4-sample debouncing pass for Matrix key press (Row 1, Col 2) @ timestamp = 500 ms
    vhl_button_update_matrix(1, 2, true, 500); // Sample 1 (history 0x01)
    vhl_button_update_matrix(1, 2, true, 503); // Sample 2 (history 0x03)
    vhl_button_update_matrix(1, 2, true, 505); // Sample 3 (history 0x07)
    vhl_button_update_matrix(1, 2, true, 508); // Sample 4 (history 0x0F -> Debounced PRESS!)

    // Pop key event from xKeyEventQueue and verify payload fields & timestampMs
    vhl_key_event_t keyOut;
    err = vhl_event_bus_pop_key_event(&keyOut, 10);
    assert(err == VHL_OK);
    assert(keyOut.note == 58);
    assert(keyOut.velocity == 100);
    assert(keyOut.action == KEY_ACTION_PRESS);
    assert(keyOut.timestampMs == 508);

    printf("[TEST 2 PASS] Keyboard Matrix Row/Col Note Mapping & Monotonic Timestamped Key Event generated cleanly.\n");

    // Test 3: Mechanical Switch Contact Bounce Suppression (Simulated noise 1, 0, 1, 1, 0, 1, 1, 1, 1)
    // Prototype button BUTTON_ID_KEY_C4 (Note 60)
    vhl_button_update_single(BUTTON_ID_KEY_C4, true,  600);
    vhl_button_update_single(BUTTON_ID_KEY_C4, false, 602); // Bounce 0
    vhl_button_update_single(BUTTON_ID_KEY_C4, true,  605);
    vhl_button_update_single(BUTTON_ID_KEY_C4, true,  608);  // History 0x0B -> Not debounced!
    vhl_button_update_single(BUTTON_ID_KEY_C4, true,  610);
    vhl_button_update_single(BUTTON_ID_KEY_C4, true,  612);  // History 0x0F -> Debounced PRESS @ 612 ms!

    vhl_key_event_t bounceKeyOut;
    err = vhl_event_bus_pop_key_event(&bounceKeyOut, 10);
    assert(err == VHL_OK);
    assert(bounceKeyOut.note == 60);
    assert(bounceKeyOut.timestampMs == 612);

    // Ensure no second duplicate event was posted
    vhl_key_event_t dummy;
    err = vhl_event_bus_pop_key_event(&dummy, 0);
    assert(err == VHL_ERR_TIMEOUT);

    printf("[TEST 3 PASS] 10 ms Software Debouncing engine suppressed switch contact bounce cleanly.\n");

    // Test 4: UI Navigation Button Timestamped Event Generation
    vhl_button_update_single(BUTTON_ID_UI_UP, true, 700);
    vhl_button_update_single(BUTTON_ID_UI_UP, true, 702);
    vhl_button_update_single(BUTTON_ID_UI_UP, true, 705);
    vhl_button_update_single(BUTTON_ID_UI_UP, true, 708); // Debounced PRESS @ 708 ms!

    vhl_ui_cmd_t uiCmdOut;
    err = vhl_event_bus_pop_ui_cmd(&uiCmdOut, 10);
    assert(err == VHL_OK);
    assert(uiCmdOut.commandId == VHL_UI_CMD_NAV_UP);
    assert(uiCmdOut.timestampMs == 708);

    printf("[TEST 4 PASS] UI Navigation Button press generated timestamped UI command cleanly.\n");

    // Test 5: ADC Slider EMA Smoothing & 2-LSB Hysteresis Filter Verification
    // Feed noisy ADC potentiometer readings around raw ADC 2048 (±15 LSB noise)
    uint8_t ccVal1 = vhl_slider_update(SLIDER_CHAN_VOLUME, 2048, 800);
    uint8_t ccVal2 = vhl_slider_update(SLIDER_CHAN_VOLUME, 2055, 802); // Small noise spike (+7 LSB)
    uint8_t ccVal3 = vhl_slider_update(SLIDER_CHAN_VOLUME, 2040, 805); // Small noise spike (-8 LSB)
    (void)ccVal1; (void)ccVal2; (void)ccVal3;

    vhl_slider_state_t sliderState;
    err = vhl_slider_get_state(SLIDER_CHAN_VOLUME, &sliderState);
    assert(err == VHL_OK);
    assert(sliderState.normalizedValue == (2048 >> 5)); // 64 CC

    // Feed large genuine slider movement: raw ADC 4000
    for (int i = 0; i < 20; ++i) {
        vhl_slider_update(SLIDER_CHAN_VOLUME, 4000, 810 + i);
    }
    err = vhl_slider_get_state(SLIDER_CHAN_VOLUME, &sliderState);
    assert(err == VHL_OK);
    assert(sliderState.normalizedValue > 120); // Scaled smoothly to max range

    printf("[TEST 5 PASS] Analog Slider EMA & 2-LSB Hysteresis filter suppressed ADC noise cleanly.\n");

    printf("=============================================================\n");
    printf("Stage 5 Acceptance Gate Passed 100%% Cleanly.\n");
    printf("=============================================================\n");
    return 0;
}

void app_main(void)
{
    main();
}
