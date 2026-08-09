/* =============================================================================
 * tabla_engine.c — Tabla Rhythm Subsystem Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#include "tabla_engine.h"
#include <string.h>

vhl_err_t tabla_engine_init(tabla_engine_t* pEngine)
{
    if (!pEngine) return VHL_ERR_INVALID_ARG;
    memset(pEngine, 0, sizeof(tabla_engine_t));
    pEngine->bpm        = 120;
    pEngine->totalBeats = 16; // Teentaal
    return VHL_OK;
}

vhl_err_t tabla_engine_configure(tabla_engine_t* pEngine, tabla_taal_t taal, uint16_t bpm)
{
    if (!pEngine || bpm < 30 || bpm > 300) return VHL_ERR_INVALID_ARG;
    pEngine->currentTaal = taal;
    pEngine->bpm         = bpm;

    switch (taal) {
        case TABLA_TAAL_TEENTAAL: pEngine->totalBeats = 16; break;
        case TABLA_TAAL_KEHERWA:  pEngine->totalBeats = 8;  break;
        case TABLA_TAAL_DADRA:    pEngine->totalBeats = 6;  break;
        default:                  pEngine->totalBeats = 16; break;
    }
    return VHL_OK;
}

vhl_err_t tabla_engine_start(tabla_engine_t* pEngine)
{
    if (!pEngine) return VHL_ERR_INVALID_ARG;
    pEngine->isPlaying   = true;
    pEngine->currentBeat = 0;
    pEngine->elapsedMs   = 0;
    return VHL_OK;
}

vhl_err_t tabla_engine_stop(tabla_engine_t* pEngine)
{
    if (!pEngine) return VHL_ERR_INVALID_ARG;
    pEngine->isPlaying = false;
    return VHL_OK;
}

vhl_err_t tabla_engine_step_timing(tabla_engine_t* pEngine, uint32_t deltaMs)
{
    if (!pEngine || !pEngine->isPlaying) return VHL_OK;

    pEngine->elapsedMs += deltaMs;
    uint32_t msPerBeat = 60000U / pEngine->bpm;

    if (pEngine->elapsedMs >= msPerBeat) {
        pEngine->elapsedMs %= msPerBeat;
        pEngine->currentBeat = (pEngine->currentBeat + 1) % pEngine->totalBeats;
    }

    return VHL_OK;
}
