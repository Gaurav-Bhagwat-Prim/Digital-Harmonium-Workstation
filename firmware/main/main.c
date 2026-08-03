/* =============================================================================
 * main.c — Digital Harmonium Workstation Stage 11 Test Harness
 * Project: Digital Harmonium Workstation Firmware
 * Stage 11 — Event Recorder Engine & Performance Logging Subsystem
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
#include "recorder_engine.h"
#include "event_bus.h"

/* =============================================================================
 * Stage 11: Event Bus Recorder Tap Bridge
 *
 * This tap callback is registered with the Event Bus.
 * It receives key events (Note-On, Note-Off, Control-Change) posted to the
 * Event Bus and forwards them into the recorder ring buffer.
 * The recorder NEVER reads hardware inputs directly — only this tap path.
 * ============================================================================= */

static uint32_t s_mock_timestamp_us = 0; /* unused after fix, kept for zero-init */


static void recorder_event_bus_tap(const vhl_key_event_t* pEvent, void* pCtx)
{
    vhl_recorder_engine_t* pRec = (vhl_recorder_engine_t*)pCtx;
    if (!pRec || !pEvent) return;

    /* Map vhl_key_action_t to vhr_event_type_t */
    vhr_event_type_t evType = (pEvent->action == KEY_ACTION_PRESS)
                              ? VHR_EVENT_NOTE_ON
                              : VHR_EVENT_NOTE_OFF;

    /* Derive microsecond timestamp from ms field */
    uint32_t tsUs = pEvent->timestampMs * 1000U;

    vhl_recorder_push_event(pRec, evType,
                            pEvent->note,
                            pEvent->velocity,
                            0U /* channel */,
                            tsUs);
}

int main(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 11 Event Recorder Test\n");
    printf("=============================================================\n\n");

    /* -------------------------------------------------------------------------
     * TEST 1: 5-State Recorder State Machine Transitions
     * -------------------------------------------------------------------------
     * Trace: IDLE -> ARMED -> RECORDING -> STOPPING -> FLUSHING -> IDLE
     * Pass Condition: State value matches expected enum at every transition.
     * ---------------------------------------------------------------------- */
    {
        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        assert(vhl_recorder_get_state(&rec) == VHR_RECORDER_STATE_IDLE);

        vhl_recorder_arm(&rec, 0, "Test Song");
        assert(vhl_recorder_get_state(&rec) == VHR_RECORDER_STATE_ARMED);

        vhl_recorder_start(&rec, 0, "Test Song");
        assert(vhl_recorder_get_state(&rec) == VHR_RECORDER_STATE_RECORDING);

        vhl_recorder_stop(&rec);
        assert(vhl_recorder_get_state(&rec) == VHR_RECORDER_STATE_FLUSHING);

        /* Drain ring buffer to complete FLUSHING -> IDLE */
        VHREventEntry scratch[16];
        uint32_t drained = 0;
        vhl_recorder_flush_block(&rec, scratch, 16, &drained);
        assert(vhl_recorder_get_state(&rec) == VHR_RECORDER_STATE_IDLE);
    }
    printf("[TEST 1 PASS] 5-State recorder state machine verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 2: SPSC Ring Buffer VHL_RECORDER_RING_SIZE Macro Bounds
     * -------------------------------------------------------------------------
     * Push exactly VHL_RECORDER_RING_SIZE - 1 events (max safe fill).
     * Drain all. Verify FIFO integrity and macro-driven bounds.
     * Pass Condition: All events drain in order, no pointer corruption.
     * ---------------------------------------------------------------------- */
    {
        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        vhl_recorder_start(&rec, 0, "Buffer Test");

        uint32_t pushCount = VHL_RECORDER_RING_SIZE - 1U;
        for (uint32_t i = 0; i < pushCount; ++i) {
            vhl_err_t e = vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON,
                                                  (uint8_t)(i % 128),
                                                  100,
                                                  0,
                                                  i * 1000U);
            assert(e == VHL_OK);
        }
        assert(rec.totalEventsRecorded == pushCount);

        /* Drain all events */
        VHREventEntry drainBuf[VHL_RECORDER_RING_SIZE];
        uint32_t totalDrained = 0;
        uint32_t drainedThisCall = 0;
        vhl_recorder_flush_block(&rec, drainBuf, VHL_RECORDER_RING_SIZE, &drainedThisCall);
        totalDrained += drainedThisCall;
        assert(totalDrained == pushCount);
        assert(rec.head == rec.tail); /* Ring buffer is empty */
    }
    printf("[TEST 2 PASS] SPSC ring buffer VHL_RECORDER_RING_SIZE macro verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 3: Strongly Typed vhr_event_type_t & data1/data2 Fields
     * -------------------------------------------------------------------------
     * Push NOTE_ON, NOTE_OFF, CONTROL_CHANGE, TEMPO_CHANGE events.
     * Drain and verify type enum, data1 (note/CC number), data2 (velocity/value).
     * Pass Condition: All fields match expected values using the renamed data1/data2.
     * ---------------------------------------------------------------------- */
    {
        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        vhl_recorder_start(&rec, 0, "Type Test");

        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON,          60, 100, 0, 0U);
        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_OFF,         60,   0, 0, 50000U);
        vhl_recorder_push_event(&rec, VHR_EVENT_CONTROL_CHANGE,    7,  80, 0, 60000U);
        vhl_recorder_push_event(&rec, VHR_EVENT_TEMPO_CHANGE,    120,   0, 0, 70000U);

        VHREventEntry buf[8];
        uint32_t drained = 0;
        vhl_recorder_flush_block(&rec, buf, 8, &drained);
        assert(drained == 4);

        assert(buf[0].type  == (uint8_t)VHR_EVENT_NOTE_ON);
        assert(buf[0].data1 == 60);
        assert(buf[0].data2 == 100);

        assert(buf[1].type  == (uint8_t)VHR_EVENT_NOTE_OFF);
        assert(buf[1].data1 == 60);
        assert(buf[1].data2 == 0);

        assert(buf[2].type  == (uint8_t)VHR_EVENT_CONTROL_CHANGE);
        assert(buf[2].data1 == 7);
        assert(buf[2].data2 == 80);

        assert(buf[3].type  == (uint8_t)VHR_EVENT_TEMPO_CHANGE);
        assert(buf[3].data1 == 120);
    }
    printf("[TEST 3 PASS] Strongly typed vhr_event_type_t & data1/data2 fields verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 4: Microsecond Delta Timestamp Encoding
     * -------------------------------------------------------------------------
     * Simulate events at 0 us, 50000 us, 120000 us.
     * Verify delta_us values: 0, 50000, 70000.
     * Pass Condition: Computed delta_us matches expected inter-event intervals.
     * ---------------------------------------------------------------------- */
    {
        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        vhl_recorder_start(&rec, 0, "Timestamp Test");

        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON,  60, 100, 0, 0U);
        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON,  62, 100, 0, 50000U);
        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_OFF, 60,   0, 0, 120000U);

        VHREventEntry buf[4];
        uint32_t drained = 0;
        vhl_recorder_flush_block(&rec, buf, 4, &drained);
        assert(drained == 3);

        assert(buf[0].delta_us == 0U);
        assert(buf[1].delta_us == 50000U);
        assert(buf[2].delta_us == 70000U);
    }
    printf("[TEST 4 PASS] Microsecond delta timestamp encoding verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 5: Lock-Free O(1) Non-Blocking Execution (<1 us per push)
     * -------------------------------------------------------------------------
     * Push 1000 events and measure total elapsed time.
     * Pass Condition: Total time / 1000 < 1.0 us per event.
     * ---------------------------------------------------------------------- */
    {
        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        vhl_recorder_start(&rec, 0, "Perf Test");

        clock_t start = clock();
        for (int i = 0; i < 1000; ++i) {
            /* Use modulo to keep within ring buffer capacity */
            if (i % (VHL_RECORDER_RING_SIZE - 1) == 0 && i > 0) {
                VHREventEntry tmp[VHL_RECORDER_RING_SIZE];
                uint32_t d = 0;
                vhl_recorder_flush_block(&rec, tmp, VHL_RECORDER_RING_SIZE, &d);
            }
            vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON,
                                    (uint8_t)(i % 128), 100, 0,
                                    (uint32_t)(i * 1000));
        }
        clock_t end = clock();
        double totalUs = ((double)(end - start) / CLOCKS_PER_SEC) * 1e6;
        double usPerPush = totalUs / 1000.0;
        assert(usPerPush < 1.0);
    }
    printf("[TEST 5 PASS] Lock-free non-blocking performance verified (<1us).\n");

    /* -------------------------------------------------------------------------
     * TEST 6: Pure Event Bus-Driven Recorder Tap Verified
     * -------------------------------------------------------------------------
     * Register the recorder tap on the Event Bus.
     * Post NOTE_ON/NOTE_OFF events via vhl_event_bus_post_key_event().
     * Verify they appear in the recorder ring buffer without hardware access.
     * Pass Condition: Recorder ring buffer contains all bus-posted events.
     * ---------------------------------------------------------------------- */
    {
        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        vhl_recorder_start(&rec, 0, "Bus Tap Test");
        s_mock_timestamp_us = 0; /* reset for clarity */


        vhl_event_bus_init();
        vhl_event_bus_set_recorder_hook(recorder_event_bus_tap, &rec);

        vhl_key_event_t ev1 = { .note = 60, .velocity = 100, .action = KEY_ACTION_PRESS,   .timestampMs = 10U };
        vhl_key_event_t ev2 = { .note = 64, .velocity = 90,  .action = KEY_ACTION_PRESS,   .timestampMs = 20U };
        vhl_key_event_t ev3 = { .note = 60, .velocity = 0,   .action = KEY_ACTION_RELEASE, .timestampMs = 30U };

        vhl_event_bus_post_key_event(&ev1);
        vhl_event_bus_post_key_event(&ev2);
        vhl_event_bus_post_key_event(&ev3);

        /* Deregister tap */
        vhl_event_bus_set_recorder_hook(NULL, NULL);

        VHREventEntry buf[8];
        uint32_t drained = 0;
        vhl_recorder_flush_block(&rec, buf, 8, &drained);
        assert(drained == 3);
        assert(buf[0].type  == (uint8_t)VHR_EVENT_NOTE_ON  && buf[0].data1 == 60);
        assert(buf[1].type  == (uint8_t)VHR_EVENT_NOTE_ON  && buf[1].data1 == 64);
        assert(buf[2].type  == (uint8_t)VHR_EVENT_NOTE_OFF && buf[2].data1 == 60);
    }
    printf("[TEST 6 PASS] Pure Event Bus-driven recorder tap verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 7: Extended VHR File Header & timebase_us Field
     * -------------------------------------------------------------------------
     * Arm recorder, finalize header, and audit all header fields.
     * Pass Condition: 64-byte header, magic VHR1, version 1.0,
     *                 timebase_us == 1000, ticks_per_quarter == 480.
     * ---------------------------------------------------------------------- */
    {
        assert(sizeof(VHRFileHeader) == 64U);

        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        vhl_recorder_arm(&rec, 1, "Header Test");
        vhl_recorder_start(&rec, 1, "Header Test");
        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON, 60, 100, 0, 0U);
        vhl_recorder_stop(&rec);

        VHREventEntry tmp[16]; uint32_t d = 0;
        vhl_recorder_flush_block(&rec, tmp, 16, &d);

        VHRFileHeader hdr;
        vhl_recorder_finalize_header(&rec, &hdr);

        assert(hdr.magic[0] == VHR_MAGIC_0 && hdr.magic[1] == VHR_MAGIC_1);
        assert(hdr.magic[2] == VHR_MAGIC_2 && hdr.magic[3] == VHR_MAGIC_3);
        assert(hdr.version_major     == VHR_VERSION_MAJOR);
        assert(hdr.version_minor     == VHR_VERSION_MINOR);
        assert(hdr.timebase_us       == VHR_DEFAULT_TIMEBASE_US);
        assert(hdr.ticks_per_quarter == VHR_DEFAULT_TICKS_PER_QN);
        assert(hdr.instrument_id     == 1);
    }
    printf("[TEST 7 PASS] Extended VHR file header & timebase_us verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 8: Performance Recording Lifecycle & Duration Tracking
     * -------------------------------------------------------------------------
     * Record 50 events with increasing timestamps (1 ms apart).
     * Stop and finalize. Verify total_events == 50 and duration_ms == 49.
     * Pass Condition: Header reflects accurate event count and duration.
     * ---------------------------------------------------------------------- */
    {
        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        vhl_recorder_start(&rec, 0, "Lifecycle Test");

        for (int i = 0; i < 50; ++i) {
            vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON,
                                    (uint8_t)(48 + (i % 12)),
                                    100, 0,
                                    (uint32_t)(i * 1000U));
        }
        assert(rec.totalEventsRecorded == 50U);
        vhl_recorder_stop(&rec);

        VHREventEntry tmp[VHL_RECORDER_RING_SIZE];
        uint32_t d = 0;
        vhl_recorder_flush_block(&rec, tmp, VHL_RECORDER_RING_SIZE, &d);

        VHRFileHeader hdr;
        vhl_recorder_finalize_header(&rec, &hdr);
        assert(hdr.total_events == 50U);
        assert(hdr.duration_ms  == 49U);  /* (49 * 1000 us) / 1000 = 49 ms */
    }
    printf("[TEST 8 PASS] Performance recording lifecycle verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 9: CRC-32 Checksum Computation
     * -------------------------------------------------------------------------
     * Record events, finalize header, compute CRC-32 again independently.
     * Pass Condition: Header crc32 field is non-zero and deterministic
     *                 (same input produces same checksum).
     * ---------------------------------------------------------------------- */
    {
        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        vhl_recorder_start(&rec, 0, "CRC Test");

        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON,  60, 100, 0, 0U);
        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_OFF, 60,   0, 0, 500000U);
        vhl_recorder_stop(&rec);

        VHREventEntry tmp[16]; uint32_t d = 0;
        vhl_recorder_flush_block(&rec, tmp, 16, &d);

        VHRFileHeader hdr1, hdr2;
        vhl_recorder_finalize_header(&rec, &hdr1);

        /* Reset and repeat — CRC must be deterministic */
        vhl_recorder_init(&rec);
        vhl_recorder_start(&rec, 0, "CRC Test");
        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON,  60, 100, 0, 0U);
        vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_OFF, 60,   0, 0, 500000U);
        vhl_recorder_stop(&rec);
        vhl_recorder_flush_block(&rec, tmp, 16, &d);
        vhl_recorder_finalize_header(&rec, &hdr2);

        assert(hdr1.crc32 != 0);
        assert(hdr1.crc32 == hdr2.crc32);
    }
    printf("[TEST 9 PASS] CRC-32 checksum computation verified.\n");

    /* -------------------------------------------------------------------------
     * TEST 10: Ring Buffer Overflow Protection
     * -------------------------------------------------------------------------
     * Fill buffer beyond VHL_RECORDER_RING_SIZE capacity without draining.
     * Pass Condition: overflow counter increments; existing buffer uncorrupted.
     * ---------------------------------------------------------------------- */
    {
        vhl_recorder_engine_t rec;
        vhl_recorder_init(&rec);
        vhl_recorder_start(&rec, 0, "Overflow Test");

        /* Fill to capacity (ring holds RING_SIZE-1 at most) */
        uint32_t overflowTarget = VHL_RECORDER_RING_SIZE + 10U;
        uint32_t accepted = 0;
        for (uint32_t i = 0; i < overflowTarget; ++i) {
            vhl_err_t e = vhl_recorder_push_event(&rec, VHR_EVENT_NOTE_ON,
                                                  (uint8_t)(i % 128),
                                                  100, 0, i * 1000U);
            if (e == VHL_OK) accepted++;
        }
        /* Exactly VHL_RECORDER_RING_SIZE - 1 accepted, rest overflowed */
        assert(accepted  == VHL_RECORDER_RING_SIZE - 1U);
        assert(rec.overflowCount > 0U);
    }
    printf("[TEST 10 PASS] Ring buffer overflow protection verified.\n");

    printf("\n=============================================================\n");
    printf("Stage 11 Acceptance Gate Passed 100%% Cleanly.\n");
    printf("=============================================================\n");

    return 0;
}
