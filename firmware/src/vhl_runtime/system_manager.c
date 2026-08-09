/* =============================================================================
 * system_manager.c — Global System Runtime State Machine Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 2 — System Runtime Engine
 * ============================================================================= */

#include "system_manager.h"
#include <string.h>

/* -----------------------------------------------------------------------------
 * Static Internal Storage (Owned Exclusively by SystemManager)
 * ----------------------------------------------------------------------------- */

static volatile vhl_system_state_t s_current_state = SYSTEM_STATE_BOOT;
static volatile uint32_t           s_feature_flags  = FEATURE_FLAG_NONE;

static uint32_t                   s_transition_counter = 0;
static vhl_state_transition_log_t s_history_ring[VHL_STATE_HISTORY_CAPACITY];
static size_t                     s_history_head  = 0;
static size_t                     s_history_count = 0;

static vhl_state_change_cb_t      s_callbacks[VHL_MAX_STATE_CALLBACKS];
static size_t                     s_callback_count = 0;

/* -----------------------------------------------------------------------------
 * Global State Transition Matrix
 * ----------------------------------------------------------------------------- */

static const bool s_transition_matrix[SYSTEM_STATE_MAX_STATES][SYSTEM_STATE_MAX_STATES] = {
    /* From BOOT */
    [SYSTEM_STATE_BOOT] = {
        [SYSTEM_STATE_BOOT]         = true,   /* No-op reset allowed */
        [SYSTEM_STATE_IDLE]         = true,   /* Boot complete */
        [SYSTEM_STATE_PERFORMING]   = false,
        [SYSTEM_STATE_LOADING_INST] = false,
        [SYSTEM_STATE_ERROR]        = true    /* Boot failure */
    },
    /* From IDLE */
    [SYSTEM_STATE_IDLE] = {
        [SYSTEM_STATE_BOOT]         = false,
        [SYSTEM_STATE_IDLE]         = true,
        [SYSTEM_STATE_PERFORMING]   = true,   /* Note press / play trigger */
        [SYSTEM_STATE_LOADING_INST] = true,   /* User selects instrument */
        [SYSTEM_STATE_ERROR]        = true    /* Hardware fault */
    },
    /* From PERFORMING */
    [SYSTEM_STATE_PERFORMING] = {
        [SYSTEM_STATE_BOOT]         = false,
        [SYSTEM_STATE_IDLE]         = true,   /* All notes released */
        [SYSTEM_STATE_PERFORMING]   = true,
        [SYSTEM_STATE_LOADING_INST] = true,   /* Inst selection stops play */
        [SYSTEM_STATE_ERROR]        = true    /* Glitch / underrun fault */
    },
    /* From LOADING_INST */
    [SYSTEM_STATE_LOADING_INST] = {
        [SYSTEM_STATE_BOOT]         = false,
        [SYSTEM_STATE_IDLE]         = true,   /* Load complete */
        [SYSTEM_STATE_PERFORMING]   = false,  /* Forbidden while loading! */
        [SYSTEM_STATE_LOADING_INST] = false,
        [SYSTEM_STATE_ERROR]        = true    /* CRC or Flash program fail */
    },
    /* From ERROR */
    [SYSTEM_STATE_ERROR] = {
        [SYSTEM_STATE_BOOT]         = true,   /* Soft reboot request */
        [SYSTEM_STATE_IDLE]         = true,   /* Error acknowledged */
        [SYSTEM_STATE_PERFORMING]   = false,
        [SYSTEM_STATE_LOADING_INST] = false,
        [SYSTEM_STATE_ERROR]        = true
    }
};

static bool is_transition_allowed(vhl_system_state_t from, vhl_system_state_t to)
{
    if (from >= SYSTEM_STATE_MAX_STATES || to >= SYSTEM_STATE_MAX_STATES) {
        return false;
    }
    return s_transition_matrix[from][to];
}

/* -----------------------------------------------------------------------------
 * Public API Implementation
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_system_init(void)
{
    s_current_state      = SYSTEM_STATE_BOOT;
    s_feature_flags      = FEATURE_FLAG_NONE;
    s_transition_counter = 0;
    s_history_head       = 0;
    s_history_count      = 0;
    s_callback_count     = 0;

    memset((void*)s_history_ring, 0, sizeof(s_history_ring));
    memset((void*)s_callbacks, 0, sizeof(s_callbacks));

    return VHL_OK;
}

vhl_system_state_t vhl_system_get_state(void)
{
    return s_current_state;
}

vhl_err_t vhl_system_request_state_change(vhl_system_state_t targetState,
                                          vhl_module_id_t sourceModule,
                                          vhl_err_t reason)
{
    if (targetState >= SYSTEM_STATE_MAX_STATES) {
        return VHL_ERR_INVALID_ARG;
    }

    vhl_system_state_t fromState = s_current_state;

    // Check against transition matrix
    if (!is_transition_allowed(fromState, targetState)) {
        return VHL_ERR_INVALID_STATE;
    }

    // Apply state change
    s_current_state = targetState;
    s_transition_counter++;

    // Write transition record into 32-entry circular history buffer
    vhl_state_transition_log_t log_entry;
    log_entry.transitionId  = s_transition_counter;
    log_entry.sourceModule  = sourceModule;
    log_entry.previousState = fromState;
    log_entry.newState      = targetState;
    log_entry.timestampMs   = 0; // Simulated timestamp for host test
    log_entry.reason        = reason;

    s_history_ring[s_history_head] = log_entry;
    s_history_head = (s_history_head + 1) % VHL_STATE_HISTORY_CAPACITY;
    if (s_history_count < VHL_STATE_HISTORY_CAPACITY) {
        s_history_count++;
    }

    // Broadcast to registered observers (non-blocking execution contract enforced)
    for (size_t i = 0; i < s_callback_count; ++i) {
        if (s_callbacks[i]) {
            s_callbacks[i](fromState, targetState, sourceModule);
        }
    }

    return VHL_OK;
}

uint32_t vhl_system_get_feature_flags(void)
{
    return s_feature_flags;
}

vhl_err_t vhl_system_enable_feature(vhl_feature_flags_t feature)
{
    s_feature_flags |= (uint32_t)feature;
    return VHL_OK;
}

vhl_err_t vhl_system_disable_feature(vhl_feature_flags_t feature)
{
    s_feature_flags &= ~((uint32_t)feature);
    return VHL_OK;
}

bool vhl_system_is_feature_active(vhl_feature_flags_t feature)
{
    return (s_feature_flags & (uint32_t)feature) != 0;
}

vhl_err_t vhl_system_register_state_callback(vhl_state_change_cb_t cb)
{
    if (!cb) {
        return VHL_ERR_INVALID_ARG;
    }
    if (s_callback_count >= VHL_MAX_STATE_CALLBACKS) {
        return VHL_ERR_NO_MEM;
    }

    s_callbacks[s_callback_count++] = cb;
    return VHL_OK;
}

size_t vhl_system_get_transition_history(vhl_state_transition_log_t* outBuffer, size_t maxEntries)
{
    if (!outBuffer || maxEntries == 0) {
        return 0;
    }

    size_t copyCount = (s_history_count < maxEntries) ? s_history_count : maxEntries;

    // Read backwards from head to get most recent history
    for (size_t i = 0; i < copyCount; ++i) {
        size_t idx = (s_history_head + VHL_STATE_HISTORY_CAPACITY - 1 - i) % VHL_STATE_HISTORY_CAPACITY;
        outBuffer[i] = s_history_ring[idx];
    }

    return copyCount;
}

uint32_t vhl_system_get_transition_count(void)
{
    return s_transition_counter;
}
