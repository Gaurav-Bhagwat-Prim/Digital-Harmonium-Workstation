/* =============================================================================
 * main.c — System Entry Point & Stage 3 Task Architecture Unit Test Harness
 * Project: Digital Harmonium Workstation Firmware
 * Stage 3 — FreeRTOS Task Architecture & Dual-Core Affinity
 * ============================================================================= */

#include <stdio.h>
#include <assert.h>
#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "system_manager.h"
#include "task_manager.h"

int main(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 3 Task Engine Test\n");
    printf("=============================================================\n");

    // Initialize System Runtime & Task Engine
    vhl_err_t err = vhl_system_init();
    assert(err == VHL_OK);

    err = vhl_tasks_init();
    assert(err == VHL_OK);
    printf("[TEST 1 PASS] Tasks Engine & Static Memory initialized cleanly.\n");

    // Test 2: Dual-Core Affinity Verification
    // AudioTask MUST be pinned to Core 1 (Priority 7)
    assert(vhl_task_get_core_affinity(VHL_TASK_ID_AUDIO) == 1);
    
    vhl_task_info_t audioInfo;
    err = vhl_task_get_info(VHL_TASK_ID_AUDIO, &audioInfo);
    assert(err == VHL_OK);
    assert(audioInfo.coreId == 1);
    assert(audioInfo.priority == VHL_TASK_PRIO_AUDIO);
    assert(audioInfo.stackSizeBytes == VHL_STACK_SIZE_AUDIO);

    // All background tasks MUST be pinned to Core 0
    assert(vhl_task_get_core_affinity(VHL_TASK_ID_INPUT) == 0);
    assert(vhl_task_get_core_affinity(VHL_TASK_ID_UI) == 0);
    assert(vhl_task_get_core_affinity(VHL_TASK_ID_LOADER) == 0);
    assert(vhl_task_get_core_affinity(VHL_TASK_ID_STORAGE) == 0);
    assert(vhl_task_get_core_affinity(VHL_TASK_ID_DIAG) == 0);

    printf("[TEST 2 PASS] Core 1 Affinity pinned exclusively to TASK_AUDIO; Core 0 pinned to all background tasks.\n");

    // Test 3: Static Stack Reservations Audit
    size_t totalStackBytes = 0;
    for (int i = 0; i < VHL_TASK_MAX_TASKS; ++i) {
        vhl_task_info_t info;
        vhl_task_get_info(i, &info);
        totalStackBytes += info.stackSizeBytes;
        assert(info.stackHighWaterMarkBytes > 0);
        assert(info.stackHighWaterMarkBytes <= info.stackSizeBytes);
    }
    assert(totalStackBytes == 30720);
    printf("[TEST 3 PASS] Static Stack Allocation audited cleanly (30,720 Bytes total static stack memory reserved).\n");

    // Test 4: Task Watchdog & Heartbeat Policy Verification
    // Publish heartbeats for all tasks @ timestamp = 100 ms
    for (int i = 0; i < VHL_TASK_MAX_TASKS; ++i) {
        err = vhl_task_send_heartbeat(i, 100);
        assert(err == VHL_OK);
    }

    // Audit heartbeats @ timestamp = 150 ms (No stalls expected)
    err = vhl_task_audit_heartbeats(150);
    assert(err == VHL_OK);

    // Audit heartbeats @ timestamp = 300 ms (AudioTask timeout is 100 ms -> should detect stall)
    err = vhl_task_audit_heartbeats(300);
    assert(err == VHL_ERR_TIMEOUT);

    vhl_task_info_t stalledAudioInfo;
    vhl_task_get_info(VHL_TASK_ID_AUDIO, &stalledAudioInfo);
    assert(stalledAudioInfo.isStalled == true);

    printf("[TEST 4 PASS] Heartbeat publishing & Task stall detection verified cleanly.\n");

    printf("=============================================================\n");
    printf("Stage 3 Acceptance Gate Passed 100%% Cleanly.\n");
    printf("=============================================================\n");
    return 0;
}

void app_main(void)
{
    main();
}
