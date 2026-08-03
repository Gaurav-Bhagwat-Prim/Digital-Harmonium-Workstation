/* =============================================================================
 * main.c — Digital Harmonium Workstation Stage 12 Test Harness
 * Project: Digital Harmonium Workstation Firmware
 * Stage 12 — VHR Performance Playback Replayer Engine
 * ============================================================================= */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "vhr_format.h"
#include "vhr_player.h"
#include "event_bus.h"

/* =============================================================================
 * Test Helper — CRC-32 (same polynomial as recorder and player)
 * Used by the test harness to build valid mock .vhr buffers.
 * ============================================================================= */

static uint32_t test_crc32(const uint8_t* pData, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= pData[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1U) ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
        }
    }
    return ~crc;
}

/* =============================================================================
 * Test Helper — Build a valid in-memory .vhr binary
 *
 * Fills pBuf->header with correct magic, version, fields, and computes CRC-32
 * over the event stream bytes (not the header). Matches the recorder's
 * finalize_header() behavior exactly.
 * ============================================================================= */

#define MAX_TEST_EVENTS 32U

typedef struct {
    VHRFileHeader header;
    VHREventEntry events[MAX_TEST_EVENTS];
} test_vhr_buf_t;

static void build_test_vhr(test_vhr_buf_t*      pBuf,
                             const VHREventEntry* pEvts,
                             uint32_t             numEvts,
                             uint8_t              instrId,
                             const char*          title)
{
    memset(pBuf, 0, sizeof(*pBuf));

    pBuf->header.magic[0]         = VHR_MAGIC_0;
    pBuf->header.magic[1]         = VHR_MAGIC_1;
    pBuf->header.magic[2]         = VHR_MAGIC_2;
    pBuf->header.magic[3]         = VHR_MAGIC_3;
    pBuf->header.version_major    = VHR_VERSION_MAJOR;
    pBuf->header.version_minor    = VHR_VERSION_MINOR;
    pBuf->header.total_events     = numEvts;
    pBuf->header.instrument_id    = instrId;
    pBuf->header.timebase_us      = VHR_DEFAULT_TIMEBASE_US;
    pBuf->header.ticks_per_quarter= VHR_DEFAULT_TICKS_PER_QN;
    pBuf->header.total_file_size  = (uint32_t)sizeof(VHRFileHeader) +
                                    numEvts * (uint32_t)sizeof(VHREventEntry);

    if (title) {
        strncpy(pBuf->header.title, title, sizeof(pBuf->header.title) - 1U);
    }

    if (numEvts > 0U && pEvts) {
        memcpy(pBuf->events, pEvts, numEvts * sizeof(VHREventEntry));
    }

    /* CRC-32 over event stream bytes, matching recorder finalize_header() */
    pBuf->header.crc32 = test_crc32((const uint8_t*)pBuf->events,
                                     numEvts * sizeof(VHREventEntry));
}

static size_t vhr_buf_size(uint32_t numEvts) {
    return sizeof(VHRFileHeader) + numEvts * sizeof(VHREventEntry);
}

/* =============================================================================
 * Stage 12: Unit Tests
 * ============================================================================= */

int main(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 12 Playback Replayer Test\n");
    printf("=============================================================\n\n");

    /* -------------------------------------------------------------------------
     * TEST 1: Player Initialization & IDLE State
     * -------------------------------------------------------------------------
     * Call vhr_player_init(). Verify all fields are zeroed/default.
     * Pass: state == IDLE, cursor == 0, accumulator == 0, speed == 1.0f.
     * ---------------------------------------------------------------------- */
    {
        vhl_vhr_player_t player;
        vhr_player_init(&player);

        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_IDLE);
        assert(player.cursorIdx       == 0U);
        assert(player.accumulatedUs   == 0U);
        assert(player.cursorTimeUs    == 0U);
        assert(player.eventsFireCount == 0U);

        /* speed clamped and defaulted */
        float diff = player.speedMultiplier - VHR_PLAYER_SPEED_DEFAULT;
        if (diff < 0.0f) diff = -diff;
        assert(diff < 1e-6f);
    }
    printf("[TEST 1 PASS] Player initialization & IDLE state verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 2: VHR Header Load & CRC-32 Validation (over event stream)
     * -------------------------------------------------------------------------
     * Build valid mock VHR with 3 events. Call vhr_player_load().
     * Pass: LOADING → READY, totalEvents == 3.
     * ---------------------------------------------------------------------- */
    {
        VHREventEntry evts[3] = {
            { .delta_us = 0U,      .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 100, .channel = 0 },
            { .delta_us = 50000U,  .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 64, .data2 = 90,  .channel = 0 },
            { .delta_us = 70000U,  .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,   .channel = 0 },
        };

        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 3U, 1U, "Load Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);

        vhl_err_t err = vhr_player_load(&player, &buf, vhr_buf_size(3U));
        assert(err == VHL_OK);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_READY);
        assert(player.totalEvents == 3U);
        assert(player.cursorIdx   == 0U);
    }
    printf("[TEST 2 PASS] VHR header load & CRC-32 validation verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 3: CRC Mismatch Rejection
     * -------------------------------------------------------------------------
     * Corrupt header.crc32 by +1. Call vhr_player_load().
     * Pass: returns VHL_ERR_VHL_CRC_MISMATCH, state == IDLE.
     * ---------------------------------------------------------------------- */
    {
        VHREventEntry evts[2] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 80, .channel = 0 },
            { .delta_us = 10000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,  .channel = 0 },
        };

        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 2U, 0U, "CRC Reject Test");
        buf.header.crc32 += 1U; /* Corrupt CRC by +1 */

        vhl_vhr_player_t player;
        vhr_player_init(&player);

        vhl_err_t err = vhr_player_load(&player, &buf, vhr_buf_size(2U));
        assert(err == VHL_ERR_VHL_CRC_MISMATCH);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_IDLE);
    }
    printf("[TEST 3 PASS] CRC mismatch rejection verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 4: Single Large Tick → All Events Fire → FINISHED
     * -------------------------------------------------------------------------
     * 3 events at delta: 0, 50000, 70000 µs. Total = 120000 µs.
     * play(). tick(200000) — more than enough for all events.
     * Pass: eventsFireCount == 3, state == FINISHED.
     * ---------------------------------------------------------------------- */
    {
        VHREventEntry evts[3] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 100, .channel = 0 },
            { .delta_us = 50000U, .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 64, .data2 = 90,  .channel = 0 },
            { .delta_us = 70000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,   .channel = 0 },
        };

        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 3U, 0U, "Single Tick Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(3U));
        vhr_player_play(&player);

        vhr_player_tick(&player, 200000U);

        assert(player.eventsFireCount == 3U);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_FINISHED);
    }
    printf("[TEST 4 PASS] Single-tick full event replay verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 5: Multi-Tick Partial Advance — Only Due Events Fire Per Tick
     * -------------------------------------------------------------------------
     * Events at delta: 0, 50000, 70000 µs.
     * tick(30000): event[0] fires (t=0 ≤ 30000). eventsFireCount == 1.
     * tick(30000): accum=60000; event[1] fires (t=50000 ≤ 60000). count == 2.
     * tick(100000): accum=160000; event[2] fires (t=120000 ≤ 160000). count == 3.
     * ---------------------------------------------------------------------- */
    {
        VHREventEntry evts[3] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 100, .channel = 0 },
            { .delta_us = 50000U, .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 62, .data2 = 90,  .channel = 0 },
            { .delta_us = 70000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,   .channel = 0 },
        };

        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 3U, 0U, "Multi-Tick Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(3U));
        vhr_player_play(&player);

        vhr_player_tick(&player, 30000U);
        assert(player.eventsFireCount == 1U);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_PLAYING);

        vhr_player_tick(&player, 30000U);
        assert(player.eventsFireCount == 2U);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_PLAYING);

        vhr_player_tick(&player, 100000U);
        assert(player.eventsFireCount == 3U);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_FINISHED);
    }
    printf("[TEST 5 PASS] Multi-tick partial advance timing verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 6: Event Bus Re-Injection — NOTE_ON/NOTE_OFF Arrive at Key Queue
     * -------------------------------------------------------------------------
     * Load VHR with NOTE_ON (note=48, vel=80) and NOTE_OFF (note=48, vel=0).
     * Drain Event Bus before test. play() + tick(large).
     * Pop events from bus and verify note/action match exactly.
     * Pass: Both replayed events arrive at the Event Bus key queue unchanged.
     * ---------------------------------------------------------------------- */
    {
        VHREventEntry evts[2] = {
            { .delta_us = 0U,    .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 48, .data2 = 80, .channel = 0 },
            { .delta_us = 5000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 48, .data2 = 0,  .channel = 0 },
        };

        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 2U, 0U, "Bus Inject Test");

        /* Clear Event Bus queue before test */
        vhl_event_bus_init();
        /* Deregister any recorder tap so played events don't loop back */
        vhl_event_bus_set_recorder_hook(NULL, NULL);

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(2U));
        vhr_player_play(&player);
        vhr_player_tick(&player, 100000U);

        assert(player.eventsFireCount == 2U);

        /* Pop events from the Event Bus and verify */
        vhl_key_event_t popped1, popped2;
        vhl_err_t e1 = vhl_event_bus_pop_key_event(&popped1, 0U);
        vhl_err_t e2 = vhl_event_bus_pop_key_event(&popped2, 0U);

        assert(e1 == VHL_OK);
        assert(popped1.action   == KEY_ACTION_PRESS);
        assert(popped1.note     == 48U);
        assert(popped1.velocity == 80U);

        assert(e2 == VHL_OK);
        assert(popped2.action   == KEY_ACTION_RELEASE);
        assert(popped2.note     == 48U);
    }
    printf("[TEST 6 PASS] Event Bus re-injection (NOTE_ON/NOTE_OFF) verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 7: Pause / Resume with Timing Continuity
     * -------------------------------------------------------------------------
     * Events at delta: 0, 50000 µs. play(). tick(30000) → event[0] fires.
     * pause(). tick(1000000) — nothing fires (wall clock frozen).
     * play() resume. tick(30000) → accum grows to 60000; event[1] fires.
     * Pass: eventsFireCount == 1 after pause-tick, == 2 after resume-tick.
     * ---------------------------------------------------------------------- */
    {
        VHREventEntry evts[2] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON, .data1 = 55, .data2 = 100, .channel = 0 },
            { .delta_us = 50000U, .type = (uint8_t)VHR_EVENT_NOTE_ON, .data1 = 57, .data2 = 90,  .channel = 0 },
        };

        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 2U, 0U, "Pause Resume Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(2U));
        vhr_player_play(&player);

        vhr_player_tick(&player, 30000U); /* event[0] at t=0 fires. accum=30000 */
        assert(player.eventsFireCount == 1U);

        vhr_player_pause(&player);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_PAUSED);

        vhr_player_tick(&player, 1000000U); /* 1 second passes — nothing fires (paused) */
        assert(player.eventsFireCount == 1U);
        assert(player.accumulatedUs   == 30000U); /* Accumulator frozen during pause */

        vhr_player_play(&player); /* Resume */
        vhr_player_tick(&player, 30000U); /* accum=60000; event[1] at t=50000 fires */
        assert(player.eventsFireCount == 2U);
    }
    printf("[TEST 7 PASS] Pause/Resume with timing continuity verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 8: Stop & Reset to Position 0
     * -------------------------------------------------------------------------
     * Load, play, fire events, call stop(). Verify full reset.
     * Pass: cursorIdx==0, accumulatedUs==0, state==READY.
     * ---------------------------------------------------------------------- */
    {
        VHREventEntry evts[3] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 100, .channel = 0 },
            { .delta_us = 10000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,   .channel = 0 },
            { .delta_us = 5000U,  .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 62, .data2 = 100, .channel = 0 },
        };

        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 3U, 0U, "Stop Reset Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(3U));
        vhr_player_play(&player);
        vhr_player_tick(&player, 50000U); /* Fire all events */
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_FINISHED);

        vhr_player_stop(&player);

        assert(player.cursorIdx      == 0U);
        assert(player.accumulatedUs  == 0U);
        assert(player.cursorTimeUs   == 0U);
        assert(player.eventsFireCount == 0U);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_READY);
    }
    printf("[TEST 8 PASS] Stop & reset to position 0 verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 9: Speed Multiplier 2× (Double Speed)
     * -------------------------------------------------------------------------
     * One event at delta_us = 100000 (100 ms). set_speed(2.0f). play().
     * tick(50000) = only 50 ms elapsed, but 50000 * 2.0 = 100000 µs.
     * The event should fire because its absolute time (100000 µs) is met.
     * Pass: eventsFireCount == 1 after only 50ms of elapsed time.
     * ---------------------------------------------------------------------- */
    {
        VHREventEntry evts[1] = {
            { .delta_us = 100000U, .type = (uint8_t)VHR_EVENT_NOTE_ON, .data1 = 60, .data2 = 100, .channel = 0 },
        };

        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 1U, 0U, "Speed 2x Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(1U));
        vhr_player_set_speed(&player, 2.0f);
        vhr_player_play(&player);

        vhr_player_tick(&player, 50000U); /* 50ms * 2.0 = 100ms virtual time */

        assert(player.eventsFireCount == 1U);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_FINISHED);

        /* Verify speed clamp: below minimum is clamped to 0.25f */
        vhr_player_set_speed(&player, 0.01f);
        float diff = player.speedMultiplier - VHR_PLAYER_SPEED_MIN;
        if (diff < 0.0f) diff = -diff;
        assert(diff < 1e-6f);

        /* Verify speed clamp: above maximum is clamped to 4.0f */
        vhr_player_set_speed(&player, 99.0f);
        diff = player.speedMultiplier - VHR_PLAYER_SPEED_MAX;
        if (diff < 0.0f) diff = -diff;
        assert(diff < 1e-6f);
    }
    printf("[TEST 9 PASS] Speed multiplier 2x & speed clamp [0.25, 4.0] verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 10: Tick Performance Budget (<1 ms for 16 simultaneous events)
     * -------------------------------------------------------------------------
     * Load 16 events all at delta_us = 0 (fire simultaneously on first tick).
     * Measure tick() execution time averaged over 200 load-play-tick cycles.
     * Pass: average tick time (including inject) < 1ms.
     * ---------------------------------------------------------------------- */
    {
        VHREventEntry evts[16];
        memset(evts, 0, sizeof(evts));
        for (int i = 0; i < 16; ++i) {
            evts[i].delta_us = 0U;
            evts[i].type     = (uint8_t)((i % 2 == 0) ? VHR_EVENT_NOTE_ON : VHR_EVENT_NOTE_OFF);
            evts[i].data1    = (uint8_t)(48 + i);
            evts[i].data2    = 100U;
        }

        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 16U, 0U, "Perf Test");

        clock_t totalStart = clock();
        for (int iter = 0; iter < 200; ++iter) {
            vhl_vhr_player_t player;
            vhr_player_init(&player);
            vhr_player_load(&player, &buf, vhr_buf_size(16U));
            vhr_player_play(&player);
            vhr_player_tick(&player, 1U);  /* 1µs tick fires all 16 events (delta=0) */
        }
        clock_t totalEnd = clock();

        double totalMs   = ((double)(totalEnd - totalStart) / (double)CLOCKS_PER_SEC) * 1000.0;
        double msPerIter = totalMs / 200.0;

        /* Each iteration: load + play + tick(16 events) < 1ms */
        assert(msPerIter < 1.0);
    }
    printf("[TEST 10 PASS] Tick performance budget (<1ms / 16 events) verified.\n");

    printf("\n=============================================================\n");
    printf("Stage 12 Acceptance Gate Passed 100%% Cleanly.\n");
    printf("=============================================================\n");

    return 0;
}
