/* =============================================================================
 * tabla_engine.h — Tabla Rhythm Subsystem Infrastructure
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#ifndef TABLA_ENGINE_H
#define TABLA_ENGINE_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TABLA_TAAL_TEENTAAL = 0, /* 16 beats */
    TABLA_TAAL_KEHERWA  = 1, /* 8 beats */
    TABLA_TAAL_DADRA    = 2  /* 6 beats */
} tabla_taal_t;

typedef struct {
    tabla_taal_t currentTaal;
    uint16_t     bpm;
    uint8_t      currentBeat;
    uint8_t      totalBeats;
    bool         isPlaying;
    uint32_t     elapsedMs;
} tabla_engine_t;

vhl_err_t tabla_engine_init(tabla_engine_t* pEngine);
vhl_err_t tabla_engine_configure(tabla_engine_t* pEngine, tabla_taal_t taal, uint16_t bpm);
vhl_err_t tabla_engine_start(tabla_engine_t* pEngine);
vhl_err_t tabla_engine_stop(tabla_engine_t* pEngine);
vhl_err_t tabla_engine_step_timing(tabla_engine_t* pEngine, uint32_t deltaMs);

#ifdef __cplusplus
}
#endif

#endif /* TABLA_ENGINE_H */
