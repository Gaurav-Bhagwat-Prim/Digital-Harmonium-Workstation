/* =============================================================================
 * main.c — System Entry Point & Stage 2 Unit Test Harness
 * Project: Digital Harmonium Workstation Firmware
 * Stage 2 — System Runtime Engine & Global State Machine
 * ============================================================================= */

#include <stdio.h>
#include <assert.h>
#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "system_manager.h"

static uint32_t s_callback_fire_count = 0;

static void test_observer_cb(vhl_system_state_t oldState, vhl_system_state_t newState, vhl_module_id_t sourceModule)
{
    s_callback_fire_count++;
}

int main(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 2 Runtime State Test\n");
    printf("=============================================================\n");

    // Test 1: System Initialization
    vhl_err_t err = vhl_system_init();
    assert(err == VHL_OK);
    assert(vhl_system_get_state() == SYSTEM_STATE_BOOT);
    printf("[TEST 1 PASS] SystemInit initialized state to BOOT cleanly.\n");

    // Test 2: Callback Registration
    err = vhl_system_register_state_callback(test_observer_cb);
    assert(err == VHL_OK);

    // Test 3: Valid State Transition Sequence
    // BOOT -> IDLE (by MODULE_ID_SYSTEM)
    err = vhl_system_request_state_change(SYSTEM_STATE_IDLE, MODULE_ID_SYSTEM, VHL_OK);
    assert(err == VHL_OK);
    assert(vhl_system_get_state() == SYSTEM_STATE_IDLE);
    assert(s_callback_fire_count == 1);

    // IDLE -> PERFORMING (by MODULE_ID_INPUT)
    err = vhl_system_request_state_change(SYSTEM_STATE_PERFORMING, MODULE_ID_INPUT, VHL_OK);
    assert(err == VHL_OK);
    assert(vhl_system_get_state() == SYSTEM_STATE_PERFORMING);
    assert(s_callback_fire_count == 2);

    // PERFORMING -> LOADING_INST (by MODULE_ID_UI)
    err = vhl_system_request_state_change(SYSTEM_STATE_LOADING_INST, MODULE_ID_UI, VHL_OK);
    assert(err == VHL_OK);
    assert(vhl_system_get_state() == SYSTEM_STATE_LOADING_INST);
    assert(s_callback_fire_count == 3);

    printf("[TEST 2 & 3 PASS] Single Authority state transition requests & callbacks verified.\n");

    // Test 4: Forbidden Transition Matrix Enforcement
    // LOADING_INST -> PERFORMING is strictly forbidden!
    err = vhl_system_request_state_change(SYSTEM_STATE_PERFORMING, MODULE_ID_AUDIO, VHL_ERR_FAIL);
    assert(err == VHL_ERR_INVALID_STATE);
    assert(vhl_system_get_state() == SYSTEM_STATE_LOADING_INST); // State unchanged
    printf("[TEST 4 PASS] Forbidden transition (LOADING -> PERFORMING) rejected with VHL_ERR_INVALID_STATE.\n");

    // Complete loading: LOADING_INST -> IDLE
    err = vhl_system_request_state_change(SYSTEM_STATE_IDLE, MODULE_ID_LOADER, VHL_OK);
    assert(err == VHL_OK);

    // Test 5: Feature Flag Independent Operation
    assert(!vhl_system_is_feature_active(FEATURE_FLAG_RECORDING));
    vhl_system_enable_feature(FEATURE_FLAG_RECORDING);
    vhl_system_enable_feature(FEATURE_FLAG_TABLA);
    assert(vhl_system_is_feature_active(FEATURE_FLAG_RECORDING));
    assert(vhl_system_is_feature_active(FEATURE_FLAG_TABLA));
    assert(!vhl_system_is_feature_active(FEATURE_FLAG_TANPURA));
    vhl_system_disable_feature(FEATURE_FLAG_RECORDING);
    assert(!vhl_system_is_feature_active(FEATURE_FLAG_RECORDING));
    printf("[TEST 5 PASS] Subsystem feature flags toggled independently of global state.\n");

    // Test 6: 1,000 Rapid Transitions & Circular Ring Buffer Verification
    for (int i = 0; i < 1000; ++i) {
        vhl_system_request_state_change(SYSTEM_STATE_PERFORMING, MODULE_ID_INPUT, VHL_OK);
        vhl_system_request_state_change(SYSTEM_STATE_IDLE, MODULE_ID_AUDIO, VHL_OK);
    }

    uint32_t totalTransitions = vhl_system_get_transition_count();
    assert(totalTransitions > 2000);

    vhl_state_transition_log_t history[32];
    size_t historyCount = vhl_system_get_transition_history(history, 32);
    assert(historyCount == 32);
    assert(history[0].transitionId == totalTransitions);
    assert(history[0].newState == SYSTEM_STATE_IDLE);

    printf("[TEST 6 PASS] 1,000 rapid state transitions executed cleanly. Total counter: %u, Ring Log verified.\n", totalTransitions);

    printf("=============================================================\n");
    printf("Stage 2 Acceptance Gate Passed 100%% Cleanly.\n");
    printf("=============================================================\n");
    return 0;
}

void app_main(void)
{
    main();
}
