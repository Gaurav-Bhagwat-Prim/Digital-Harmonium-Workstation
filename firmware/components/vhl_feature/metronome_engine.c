/* =============================================================================
 * metronome_engine.c — Metronome Subsystem Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#include "metronome_engine.h"
#include <string.h>

vhl_err_t metronome_engine_init(metronome_engine_t* pEngine)
{
    if (!pEngine) return VHL_ERR_INVALID_ARG;
    memset(pEngine, 0, sizeof(metronome_engine_t));
    pEngine->bpm             = 120;
    pEngine->beatsPerMeasure = 4;
    return VHL_OK;
}

vhl_err_t metronome_engine_configure(metronome_engine_t* pEngine, uint16_t bpm, uint8_t beatsPerMeasure)
{
    if (!pEngine || bpm < 30 || bpm > 300 || beatsPerMeasure == 0) return VHL_ERR_INVALID_ARG;
    pEngine->bpm             = bpm;
    pEngine->beatsPerMeasure = beatsPerMeasure;
    return VHL_OK;
}

vhl_err_t metronome_engine_start(metronome_engine_t* pEngine)
{
    if (!pEngine) return VHL_ERR_INVALID_ARG;
    pEngine->isPlaying   = true;
    pEngine->currentBeat = 0;
    pEngine->elapsedMs   = 0;
    return VHL_OK;
}

vhl_err_t metronome_engine_stop(metronome_engine_t* pEngine)
{
    if (!pEngine) return VHL_ERR_INVALID_ARG;
    pEngine->isPlaying = false;
    return VHL_OK;
}

vhl_err_t metronome_engine_step_timing(metronome_engine_t* pEngine, uint32_t deltaMs)
{
    if (!pEngine || !pEngine->isPlaying) return VHL_OK;

    pEngine->elapsedMs += deltaMs;
    uint32_t msPerBeat = 60000U / pEngine->bpm;

    if (pEngine->elapsedMs >= msPerBeat) {
        pEngine->elapsedMs %= msPerBeat;
        pEngine->currentBeat = (pEngine->currentBeat + 1) % pEngine->beatsPerMeasure;
    }

    return VHL_OK;
}
