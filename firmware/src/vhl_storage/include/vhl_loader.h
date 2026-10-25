/* =============================================================================
 * vhl_loader.h — VHL Binary Package Loader & O(1) Region Lookup Engine
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 / VHL v2.0 Runtime Infrastructure
 * ============================================================================= */

#ifndef VHL_LOADER_H
#define VHL_LOADER_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "vhl_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum matched regions per key lookup */
#define VHL_MAX_MATCHED_REGIONS (8U)

typedef struct {
    const VHLFileHeader*      pHeader;
    const VHLKeyMapEntry*     pKeyMap;
    const VHLTuningTable*     pTuningTable;
    const VHLInstrumentEntry* pInstruments;
    const VHLRegion*          pRegions;
    const VHLSampleEntry*     pSamples;
    const uint8_t*            pLookupBase;
    const uint8_t*            pPcmBase;
    const VHLModulator*       pModulators;
    size_t                    totalBinarySize;
    bool                      isLoaded;
    uint8_t                   version; /* 1 or 2 */
} vhl_loader_t;

/* -----------------------------------------------------------------------------
 * Public VHL Loader & Region Lookup API Contracts
 * ----------------------------------------------------------------------------- */

/** Initialize and validate VHL binary file from Flash/RAM memory mapping */
vhl_err_t vhl_loader_init(vhl_loader_t* pLoader, const void* pVhlBinaryData, size_t dataSize);

/** Retrieve instrument entry by 0-based instrument ID */
vhl_err_t vhl_loader_get_instrument(const vhl_loader_t* pLoader, uint8_t instrumentId, const VHLInstrumentEntry** outInstrument);

/** Retrieve sample entry by sample ID */
vhl_err_t vhl_loader_get_sample(const vhl_loader_t* pLoader, uint16_t sampleId, const VHLSampleEntry** outSample);

/** Perform O(1) region lookup for given instrument ID, MIDI key, and velocity */
vhl_err_t vhl_loader_get_regions_for_key(const vhl_loader_t* pLoader,
                                         uint8_t instrumentId,
                                         uint8_t key,
                                         uint8_t vel,
                                         const VHLRegion* outRegions[],
                                         uint8_t* outRegionCount);

/** Retrieve physical keybed mapping entry (VHL v2 only) */
vhl_err_t vhl_loader_get_keymap(const vhl_loader_t* pLoader, uint8_t physicalKeyIdx, const VHLKeyMapEntry** outKeyMap);

/** Retrieve microtonal tuning table (VHL v2 only) */
vhl_err_t vhl_loader_get_tuning(const vhl_loader_t* pLoader, const VHLTuningTable** outTuning);

/** Retrieve modulator entry (VHL v2 only) */
vhl_err_t vhl_loader_get_modulator(const vhl_loader_t* pLoader, uint16_t modIdx, const VHLModulator** outMod);

#ifdef __cplusplus
}
#endif

#endif /* VHL_LOADER_H */
