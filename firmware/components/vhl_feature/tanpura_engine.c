/* =============================================================================
 * tanpura_engine.c — Tanpura Drone Subsystem Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#include "tanpura_engine.h"
#include <string.h>

vhl_err_t tanpura_engine_init(tanpura_engine_t* pEngine)
{
    if (!pEngine) return VHL_ERR_INVALID_ARG;
    memset(pEngine, 0, sizeof(tanpura_engine_t));
    pEngine->tuning          = TANPURA_TUNING_PA;
    pEngine->rootKey         = 60; // C4
    pEngine->pluckIntervalMs = 1500; // 1.5 sec between string plucks
    return VHL_OK;
}

vhl_err_t tanpura_engine_configure(tanpura_engine_t* pEngine, tanpura_tuning_t tuning, uint8_t rootKey, uint32_t pluckIntervalMs)
{
    if (!pEngine || rootKey > 127) return VHL_ERR_INVALID_ARG;
    pEngine->tuning          = tuning;
    pEngine->rootKey         = rootKey;
    pEngine->pluckIntervalMs = (pluckIntervalMs >= 500) ? pluckIntervalMs : 1500;
    return VHL_OK;
}

vhl_err_t tanpura_engine_start(tanpura_engine_t* pEngine)
{
    if (!pEngine) return VHL_ERR_INVALID_ARG;
    pEngine->isPlaying     = true;
    pEngine->currentString = 0;
    pEngine->elapsedMs     = 0;
    return VHL_OK;
}

vhl_err_t tanpura_engine_stop(tanpura_engine_t* pEngine)
{
    if (!pEngine) return VHL_ERR_INVALID_ARG;
    pEngine->isPlaying = false;
    return VHL_OK;
}

vhl_err_t tanpura_engine_step_timing(tanpura_engine_t* pEngine, uint32_t deltaMs)
{
    if (!pEngine || !pEngine->isPlaying) return VHL_OK;

    pEngine->elapsedMs += deltaMs;
    if (pEngine->elapsedMs >= pEngine->pluckIntervalMs) {
        pEngine->elapsedMs %= pEngine->pluckIntervalMs;
        pEngine->currentString = (pEngine->currentString + 1) % 4; // 4 strings
    }

    return VHL_OK;
}
