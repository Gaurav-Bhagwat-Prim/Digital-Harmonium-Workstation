/* =============================================================================
 * metronome_engine.h — Metronome Subsystem Infrastructure
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#ifndef METRONOME_ENGINE_H
#define METRONOME_ENGINE_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t bpm;
    uint8_t  beatsPerMeasure;
    uint8_t  currentBeat;
    bool     isPlaying;
    uint32_t elapsedMs;
} metronome_engine_t;

vhl_err_t metronome_engine_init(metronome_engine_t* pEngine);
vhl_err_t metronome_engine_configure(metronome_engine_t* pEngine, uint16_t bpm, uint8_t beatsPerMeasure);
vhl_err_t metronome_engine_start(metronome_engine_t* pEngine);
vhl_err_t metronome_engine_stop(metronome_engine_t* pEngine);
vhl_err_t metronome_engine_step_timing(metronome_engine_t* pEngine, uint32_t deltaMs);

#ifdef __cplusplus
}
#endif

#endif /* METRONOME_ENGINE_H */
