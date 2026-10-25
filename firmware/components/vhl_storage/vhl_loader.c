/* =============================================================================
 * vhl_loader.c — VHL Binary Package Loader & O(1) Region Lookup Engine Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 / VHL v2.0 Runtime Infrastructure
 * ============================================================================= */

#include "vhl_loader.h"
#include <string.h>

/* Legacy v1 Header layout for fallback compatibility with factory packages */
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];
    uint8_t  version_major;
    uint8_t  version_minor;
    uint16_t flags;
    uint16_t num_instruments;
    uint16_t total_region_count;
    uint16_t total_sample_count;
    uint32_t target_sample_rate;
    uint16_t dma_block_size;
    uint32_t offset_instrument_table;
    uint32_t offset_region_table;
    uint32_t offset_sample_directory;
    uint32_t offset_lookup_table;
    uint32_t offset_pcm_data;
} VHLFileHeader_v1;

vhl_err_t vhl_loader_init(vhl_loader_t* pLoader, const void* pVhlBinaryData, size_t dataSize)
{
    if (!pLoader || !pVhlBinaryData || dataSize < sizeof(VHLFileHeader)) {
        return VHL_ERR_INVALID_ARG;
    }

    memset(pLoader, 0, sizeof(vhl_loader_t));
    const uint8_t* pBase = (const uint8_t*)pVhlBinaryData;

    // Validate Magic prefix 'V', 'H', 'L'
    if (pBase[0] != 'V' || pBase[1] != 'H' || pBase[2] != 'L') {
        return VHL_ERR_INVALID_STATE;
    }

    // Check version: '2' (VHL v2.0) or '1' (VHL v1.0 fallback)
    if (pBase[3] == '2') {
        const VHLFileHeader* pHeader = (const VHLFileHeader*)pBase;

        if (pHeader->version_major != 2) {
            return VHL_ERR_INVALID_STATE;
        }

        if (pHeader->total_file_size > dataSize || pHeader->total_file_size < sizeof(VHLFileHeader)) {
            return VHL_ERR_INVALID_ARG;
        }

        // Validate Section Offsets
        if (pHeader->offset_instrument_table >= dataSize ||
            pHeader->offset_region_table >= dataSize ||
            pHeader->offset_sample_directory >= dataSize ||
            pHeader->offset_lookup_table >= dataSize ||
            pHeader->offset_pcm_data > dataSize) {
            return VHL_ERR_INVALID_ARG;
        }

        pLoader->pHeader      = pHeader;
        pLoader->pKeyMap      = pHeader->offset_keybed_map ? (const VHLKeyMapEntry*)(pBase + pHeader->offset_keybed_map) : NULL;
        pLoader->pTuningTable = pHeader->offset_tuning_table ? (const VHLTuningTable*)(pBase + pHeader->offset_tuning_table) : NULL;
        pLoader->pInstruments = (const VHLInstrumentEntry*)(pBase + pHeader->offset_instrument_table);
        pLoader->pRegions     = (const VHLRegion*)(pBase + pHeader->offset_region_table);
        pLoader->pSamples     = (const VHLSampleEntry*)(pBase + pHeader->offset_sample_directory);
        pLoader->pLookupBase  = pBase + pHeader->offset_lookup_table;
        pLoader->pPcmBase     = pBase + pHeader->offset_pcm_data;
        pLoader->pModulators  = pHeader->offset_modulator_table ? (const VHLModulator*)(pBase + pHeader->offset_modulator_table) : NULL;
        pLoader->totalBinarySize = dataSize;
        pLoader->version      = 2;
        pLoader->isLoaded     = true;

        return VHL_OK;
    } else if (pBase[3] == '1') {
        // Fallback for factory-embedded v1 package
        const VHLFileHeader_v1* pHeader1 = (const VHLFileHeader_v1*)pBase;
        pLoader->pHeader      = (const VHLFileHeader*)pBase; /* Header mapped */
        pLoader->pInstruments = (const VHLInstrumentEntry*)(pBase + pHeader1->offset_instrument_table);
        pLoader->pRegions     = (const VHLRegion*)(pBase + pHeader1->offset_region_table);
        pLoader->pSamples     = (const VHLSampleEntry*)(pBase + pHeader1->offset_sample_directory);
        pLoader->pLookupBase  = pBase + pHeader1->offset_lookup_table;
        pLoader->pPcmBase     = pBase + pHeader1->offset_pcm_data;
        pLoader->totalBinarySize = dataSize;
        pLoader->version      = 1;
        pLoader->isLoaded     = true;

        return VHL_OK;
    }

    return VHL_ERR_INVALID_STATE;
}

vhl_err_t vhl_loader_get_instrument(const vhl_loader_t* pLoader, uint8_t instrumentId, const VHLInstrumentEntry** outInstrument)
{
    if (!pLoader || !pLoader->isLoaded || !outInstrument) {
        return VHL_ERR_INVALID_ARG;
    }

    if (instrumentId >= pLoader->pHeader->num_instruments) {
        return VHL_ERR_INVALID_ARG;
    }

    *outInstrument = &pLoader->pInstruments[instrumentId];
    return VHL_OK;
}

vhl_err_t vhl_loader_get_sample(const vhl_loader_t* pLoader, uint16_t sampleId, const VHLSampleEntry** outSample)
{
    if (!pLoader || !pLoader->isLoaded || !outSample) {
        return VHL_ERR_INVALID_ARG;
    }

    if (sampleId >= pLoader->pHeader->total_sample_count) {
        return VHL_ERR_INVALID_ARG;
    }

    *outSample = &pLoader->pSamples[sampleId];
    return VHL_OK;
}

vhl_err_t vhl_loader_get_regions_for_key(const vhl_loader_t* pLoader,
                                         uint8_t instrumentId,
                                         uint8_t key,
                                         uint8_t vel,
                                         const VHLRegion* outRegions[],
                                         uint8_t* outRegionCount)
{
    if (!pLoader || !pLoader->isLoaded || !outRegions || !outRegionCount || key >= VHL_MIDI_KEYS) {
        return VHL_ERR_INVALID_ARG;
    }

    *outRegionCount = 0;

    if (instrumentId >= pLoader->pHeader->num_instruments) {
        return VHL_ERR_INVALID_ARG;
    }

    // O(1) Accelerated Region Lookup: Jump directly to instrument lookup header
    const uint8_t* pLookupInst = pLoader->pLookupBase;
    pLookupInst += (instrumentId * (sizeof(VHLLookupHeader) + (VHL_MIDI_KEYS * sizeof(VHLLookupEntry))));

    const VHLLookupHeader* pLookHeader = (const VHLLookupHeader*)pLookupInst;
    (void)pLookHeader;

    const VHLLookupEntry* pEntries = (const VHLLookupEntry*)(pLookupInst + sizeof(VHLLookupHeader));
    const VHLLookupEntry* pKeyEntry = &pEntries[key];

    if (pKeyEntry->region_start_idx == VHL_LOOKUP_NONE || pKeyEntry->region_count == 0) {
        return VHL_OK; // No matching regions for this key
    }

    uint16_t startIdx = pKeyEntry->region_start_idx;
    uint8_t count = pKeyEntry->region_count;
    uint8_t matchedCount = 0;

    // Filter regions matching velocity range [vel_lo..vel_hi]
    for (uint8_t i = 0; i < count && matchedCount < VHL_MAX_MATCHED_REGIONS; ++i) {
        uint16_t regIdx = startIdx + i;
        if (regIdx < pLoader->pHeader->total_region_count) {
            const VHLRegion* pReg = &pLoader->pRegions[regIdx];
            if (vel >= pReg->vel_lo && vel <= pReg->vel_hi) {
                outRegions[matchedCount++] = pReg;
            }
        }
    }

    *outRegionCount = matchedCount;
    return VHL_OK;
}

vhl_err_t vhl_loader_get_keymap(const vhl_loader_t* pLoader, uint8_t physicalKeyIdx, const VHLKeyMapEntry** outKeyMap)
{
    if (!pLoader || !pLoader->isLoaded || !outKeyMap || !pLoader->pKeyMap) {
        return VHL_ERR_INVALID_ARG;
    }

    if (physicalKeyIdx >= pLoader->pHeader->num_physical_keys) {
        return VHL_ERR_INVALID_ARG;
    }

    *outKeyMap = &pLoader->pKeyMap[physicalKeyIdx];
    return VHL_OK;
}

vhl_err_t vhl_loader_get_tuning(const vhl_loader_t* pLoader, const VHLTuningTable** outTuning)
{
    if (!pLoader || !pLoader->isLoaded || !outTuning || !pLoader->pTuningTable) {
        return VHL_ERR_INVALID_ARG;
    }

    *outTuning = pLoader->pTuningTable;
    return VHL_OK;
}

vhl_err_t vhl_loader_get_modulator(const vhl_loader_t* pLoader, uint16_t modIdx, const VHLModulator** outMod)
{
    if (!pLoader || !pLoader->isLoaded || !outMod || !pLoader->pModulators) {
        return VHL_ERR_INVALID_ARG;
    }

    if (modIdx >= pLoader->pHeader->total_modulator_count) {
        return VHL_ERR_INVALID_ARG;
    }

    *outMod = &pLoader->pModulators[modIdx];
    return VHL_OK;
}
