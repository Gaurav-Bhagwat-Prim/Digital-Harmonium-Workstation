/* =============================================================================
 * tanpura_engine.h — Tanpura Drone Subsystem Infrastructure
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#ifndef TANPURA_ENGINE_H
#define TANPURA_ENGINE_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TANPURA_TUNING_PA = 0, /* Pa-Sa-Sa-Sa */
    TANPURA_TUNING_MA = 1, /* Ma-Sa-Sa-Sa */
    TANPURA_TUNING_NI = 2  /* Ni-Sa-Sa-Sa */
} tanpura_tuning_t;

typedef struct {
    tanpura_tuning_t tuning;
    uint8_t          rootKey;     /* MIDI Key (e.g. 60 = C4) */
    uint8_t          currentString;
    bool             isPlaying;
    uint32_t         pluckIntervalMs;
    uint32_t         elapsedMs;
} tanpura_engine_t;

vhl_err_t tanpura_engine_init(tanpura_engine_t* pEngine);
vhl_err_t tanpura_engine_configure(tanpura_engine_t* pEngine, tanpura_tuning_t tuning, uint8_t rootKey, uint32_t pluckIntervalMs);
vhl_err_t tanpura_engine_start(tanpura_engine_t* pEngine);
vhl_err_t tanpura_engine_stop(tanpura_engine_t* pEngine);
vhl_err_t tanpura_engine_step_timing(tanpura_engine_t* pEngine, uint32_t deltaMs);

#ifdef __cplusplus
}
#endif

#endif /* TANPURA_ENGINE_H */
