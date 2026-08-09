/* =============================================================================
 * system_manager.h — Global System Runtime State Machine
 * Project: Digital Harmonium Workstation Firmware
 * Stage 2 — System Runtime Engine
 * ============================================================================= */

#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include "vhl_types.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VHL_STATE_HISTORY_CAPACITY (32U)
#define VHL_MAX_STATE_CALLBACKS    (8U)

/* Primary Global System States */
typedef enum {
    SYSTEM_STATE_BOOT = 0,      /* Initial boot initialization */
    SYSTEM_STATE_IDLE,          /* System ready, no active performance */
    SYSTEM_STATE_PERFORMING,    /* Live musical synthesis active */
    SYSTEM_STATE_LOADING_INST,  /* SD-to-Flash VHL installation in progress */
    SYSTEM_STATE_ERROR,         /* System error / fault condition */
    SYSTEM_STATE_MAX_STATES
} vhl_system_state_t;

/* Independent Subsystem Feature Flags */
typedef enum {
    FEATURE_FLAG_NONE       = 0,
    FEATURE_FLAG_RECORDING  = (1U << 0),  /* Live performance recording active */
    FEATURE_FLAG_PLAYBACK   = (1U << 1),  /* Performance replayer active */
    FEATURE_FLAG_TABLA      = (1U << 2),  /* Tabla rhythm engine active */
    FEATURE_FLAG_TANPURA    = (1U << 3),  /* Tanpura drone engine active */
    FEATURE_FLAG_METRONOME  = (1U << 4)   /* Metronome click active */
} vhl_feature_flags_t;

/* Requesting Source Module Identifiers */
typedef enum {
    MODULE_ID_SYSTEM = 0,
    MODULE_ID_UI,
    MODULE_ID_INPUT,
    MODULE_ID_AUDIO,
    MODULE_ID_LOADER,
    MODULE_ID_RECORDER,
    MODULE_ID_DIAGNOSTICS
} vhl_module_id_t;

/* Transition History Record Structure */
typedef struct {
    uint32_t           transitionId;   /* Monotonically increasing counter */
    vhl_module_id_t    sourceModule;   /* Requesting module ID */
    vhl_system_state_t previousState;  /* Source state */
    vhl_system_state_t newState;       /* Target state */
    uint32_t           timestampMs;    /* System timestamp */
    vhl_err_t          reason;         /* Result / Reason code */
} vhl_state_transition_log_t;

/* Observer Callback Signature (Must execute in <= 10 us, non-blocking) */
typedef void (*vhl_state_change_cb_t)(vhl_system_state_t oldState, 
                                      vhl_system_state_t newState, 
                                      vhl_module_id_t sourceModule);

/* -----------------------------------------------------------------------------
 * Public API Contracts
 * ----------------------------------------------------------------------------- */

/** Initialize SystemManager, clear transition history, set state to BOOT */
vhl_err_t vhl_system_init(void);

/** Thread-safe reader for current global state */
vhl_system_state_t vhl_system_get_state(void);

/** 
 * Single Authority State Change Request API.
 * Only SystemManager validates, applies, logs, and broadcasts the state change.
 */
vhl_err_t vhl_system_request_state_change(vhl_system_state_t targetState,
                                          vhl_module_id_t sourceModule,
                                          vhl_err_t reason);

/** Subsystem Feature Flag Accessors */
uint32_t  vhl_system_get_feature_flags(void);
vhl_err_t vhl_system_enable_feature(vhl_feature_flags_t feature);
vhl_err_t vhl_system_disable_feature(vhl_feature_flags_t feature);
bool      vhl_system_is_feature_active(vhl_feature_flags_t feature);

/** Observer Callback Subscription */
vhl_err_t vhl_system_register_state_callback(vhl_state_change_cb_t cb);

/** Diagnostic Accessor: Copy circular transition history log buffer */
size_t    vhl_system_get_transition_history(vhl_state_transition_log_t* outBuffer, size_t maxEntries);

/** Diagnostic Accessor: Total transition count since boot */
uint32_t  vhl_system_get_transition_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_MANAGER_H */
