/* =============================================================================
 * button_manager.c — Button Debouncer & Keyboard Matrix Scanner Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 5 — Input Layer Architecture
 * ============================================================================= */

#include "button_manager.h"
#include "event_bus.h"
#include <string.h>

static vhl_button_info_t s_button_registry[VHL_BUTTON_MAX_BUTTONS];
static uint8_t           s_matrix_history[VHL_KEYBOARD_MATRIX_ROWS][VHL_KEYBOARD_MATRIX_COLS];
static vhl_button_state_t s_matrix_state[VHL_KEYBOARD_MATRIX_ROWS][VHL_KEYBOARD_MATRIX_COLS];

vhl_err_t vhl_button_manager_init(void)
{
    memset(s_button_registry, 0, sizeof(s_button_registry));
    memset(s_matrix_history, 0, sizeof(s_matrix_history));
    memset(s_matrix_state, 0, sizeof(s_matrix_state));

    /* Initialize Prototype Push Buttons */
    s_button_registry[BUTTON_ID_KEY_C4]   = (vhl_button_info_t){ .buttonId = BUTTON_ID_KEY_C4,   .midiNote = 60, .state = BUTTON_STATE_RELEASED };
    s_button_registry[BUTTON_ID_KEY_D4]   = (vhl_button_info_t){ .buttonId = BUTTON_ID_KEY_D4,   .midiNote = 62, .state = BUTTON_STATE_RELEASED };
    s_button_registry[BUTTON_ID_KEY_E4]   = (vhl_button_info_t){ .buttonId = BUTTON_ID_KEY_E4,   .midiNote = 64, .state = BUTTON_STATE_RELEASED };
    s_button_registry[BUTTON_ID_KEY_F4]   = (vhl_button_info_t){ .buttonId = BUTTON_ID_KEY_F4,   .midiNote = 65, .state = BUTTON_STATE_RELEASED };
    s_button_registry[BUTTON_ID_KEY_G4]   = (vhl_button_info_t){ .buttonId = BUTTON_ID_KEY_G4,   .midiNote = 67, .state = BUTTON_STATE_RELEASED };

    s_button_registry[BUTTON_ID_UI_UP]     = (vhl_button_info_t){ .buttonId = BUTTON_ID_UI_UP,     .midiNote = 0,  .state = BUTTON_STATE_RELEASED };
    s_button_registry[BUTTON_ID_UI_DOWN]   = (vhl_button_info_t){ .buttonId = BUTTON_ID_UI_DOWN,   .midiNote = 0,  .state = BUTTON_STATE_RELEASED };
    s_button_registry[BUTTON_ID_UI_SELECT] = (vhl_button_info_t){ .buttonId = BUTTON_ID_UI_SELECT, .midiNote = 0,  .state = BUTTON_STATE_RELEASED };
    s_button_registry[BUTTON_ID_UI_BACK]   = (vhl_button_info_t){ .buttonId = BUTTON_ID_UI_BACK,   .midiNote = 0,  .state = BUTTON_STATE_RELEASED };
    s_button_registry[BUTTON_ID_REC_TOGGLE]= (vhl_button_info_t){ .buttonId = BUTTON_ID_REC_TOGGLE,.midiNote = 0,  .state = BUTTON_STATE_RELEASED };

    return VHL_OK;
}

vhl_midi_note_t vhl_input_map_matrix_to_note(uint8_t row, uint8_t col)
{
    if (row >= VHL_KEYBOARD_MATRIX_ROWS || col >= VHL_KEYBOARD_MATRIX_COLS) {
        return 0;
    }
    // Standard mapping: Row 0 Col 0 = C3 (48), each cell = +1 pitch
    return (vhl_midi_note_t)(48 + (row * VHL_KEYBOARD_MATRIX_COLS) + col);
}

vhl_err_t vhl_button_update_matrix(uint8_t row, uint8_t col, bool rawState, uint32_t timestampMs)
{
    if (row >= VHL_KEYBOARD_MATRIX_ROWS || col >= VHL_KEYBOARD_MATRIX_COLS) {
        return VHL_ERR_INVALID_ARG;
    }

    uint8_t mask = (uint8_t)((1U << VHL_INPUT_DEBOUNCE_SAMPLES) - 1U);
    uint8_t history = s_matrix_history[row][col];

    history = ((history << 1) | (rawState ? 1U : 0U)) & mask;
    s_matrix_history[row][col] = history;

    vhl_button_state_t prevState = s_matrix_state[row][col];
    vhl_button_state_t newState  = prevState;

    if (history == mask) {
        newState = BUTTON_STATE_PRESSED;
    } else if (history == 0U) {
        newState = BUTTON_STATE_RELEASED;
    }

    // On state change, emit timestamped key event
    if (newState != prevState) {
        s_matrix_state[row][col] = newState;

        vhl_key_event_t keyEv;
        keyEv.note        = vhl_input_map_matrix_to_note(row, col);
        keyEv.velocity    = (newState == BUTTON_STATE_PRESSED) ? 100 : 0;
        keyEv.action      = (newState == BUTTON_STATE_PRESSED) ? KEY_ACTION_PRESS : KEY_ACTION_RELEASE;
        keyEv.timestampMs = timestampMs;

        vhl_event_bus_post_key_event(&keyEv);
    }

    return VHL_OK;
}

vhl_err_t vhl_button_update_single(vhl_button_id_t buttonId, bool rawState, uint32_t timestampMs)
{
    if (buttonId >= VHL_BUTTON_MAX_BUTTONS) {
        return VHL_ERR_INVALID_ARG;
    }

    vhl_button_info_t* pBtn = &s_button_registry[buttonId];
    uint8_t mask = (uint8_t)((1U << VHL_INPUT_DEBOUNCE_SAMPLES) - 1U);
    uint8_t history = pBtn->debounceHistory;

    history = ((history << 1) | (rawState ? 1U : 0U)) & mask;
    pBtn->debounceHistory = history;

    vhl_button_state_t prevState = pBtn->state;
    vhl_button_state_t newState  = prevState;

    if (history == mask) {
        newState = BUTTON_STATE_PRESSED;
    } else if (history == 0U) {
        newState = BUTTON_STATE_RELEASED;
    }

    if (newState != prevState) {
        pBtn->state = newState;
        pBtn->lastChangeMs = timestampMs;

        // Determine event target (Musical key vs UI button)
        if (pBtn->midiNote > 0) {
            vhl_key_event_t keyEv;
            keyEv.note        = pBtn->midiNote;
            keyEv.velocity    = (newState == BUTTON_STATE_PRESSED) ? 100 : 0;
            keyEv.action      = (newState == BUTTON_STATE_PRESSED) ? KEY_ACTION_PRESS : KEY_ACTION_RELEASE;
            keyEv.timestampMs = timestampMs;

            vhl_event_bus_post_key_event(&keyEv);
        } else if (newState == BUTTON_STATE_PRESSED) {
            vhl_ui_cmd_t uiCmd;
            uiCmd.screenId    = 0;
            uiCmd.timestampMs = timestampMs;

            switch (buttonId) {
                case BUTTON_ID_UI_UP:      uiCmd.commandId = VHL_UI_CMD_NAV_UP; break;
                case BUTTON_ID_UI_DOWN:    uiCmd.commandId = VHL_UI_CMD_NAV_DOWN; break;
                case BUTTON_ID_UI_SELECT:  uiCmd.commandId = VHL_UI_CMD_NAV_SELECT; break;
                case BUTTON_ID_UI_BACK:    uiCmd.commandId = VHL_UI_CMD_NAV_BACK; break;
                case BUTTON_ID_REC_TOGGLE: uiCmd.commandId = VHL_UI_CMD_REC_TOGGLE; break;
                default: break;
            }

            vhl_event_bus_post_ui_cmd(&uiCmd);
        }
    }

    return VHL_OK;
}

vhl_button_state_t vhl_button_get_state(vhl_button_id_t buttonId)
{
    if (buttonId >= VHL_BUTTON_MAX_BUTTONS) {
        return BUTTON_STATE_RELEASED;
    }
    return s_button_registry[buttonId].state;
}
