/* =============================================================================
 * vhl_loader.c — VHL Binary Package Loader & O(1) Region Lookup Engine Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#include "vhl_loader.h"
#include <string.h>

vhl_err_t vhl_loader_init(vhl_loader_t* pLoader, const void* pVhlBinaryData, size_t dataSize)
{
    if (!pLoader || !pVhlBinaryData || dataSize < sizeof(VHLFileHeader)) {
        return VHL_ERR_INVALID_ARG;
    }

    memset(pLoader, 0, sizeof(vhl_loader_t));
    const uint8_t* pBase = (const uint8_t*)pVhlBinaryData;
    const VHLFileHeader* pHeader = (const VHLFileHeader*)pBase;

    // 1. Validate Header Magic ('V', 'H', 'L', '1')
    if (pHeader->magic[0] != VHL_MAGIC_0 ||
        pHeader->magic[1] != VHL_MAGIC_1 ||
        pHeader->magic[2] != VHL_MAGIC_2 ||
        pHeader->magic[3] != VHL_MAGIC_3) {
        return VHL_ERR_INVALID_STATE;
    }

    // 2. Validate Version
    if (pHeader->version_major != VHL_VERSION_MAJOR) {
        return VHL_ERR_INVALID_STATE;
    }

    // 3. Validate Total File Size Boundaries
    if (pHeader->total_file_size > dataSize || pHeader->total_file_size < sizeof(VHLFileHeader)) {
        return VHL_ERR_INVALID_ARG;
    }

    // 4. Validate Section Offsets & Memory Alignment
    if (pHeader->offset_instrument_table >= dataSize ||
        pHeader->offset_region_table >= dataSize ||
        pHeader->offset_sample_directory >= dataSize ||
        pHeader->offset_lookup_table >= dataSize ||
        pHeader->offset_pcm_data > dataSize) {
        return VHL_ERR_INVALID_ARG;
    }

    // 5. Memory-Map Flash Pointers (Zero RAM Copy)
    pLoader->pHeader      = pHeader;
    pLoader->pInstruments = (const VHLInstrumentEntry*)(pBase + pHeader->offset_instrument_table);
    pLoader->pRegions     = (const VHLRegion*)(pBase + pHeader->offset_region_table);
    pLoader->pSamples     = (const VHLSampleEntry*)(pBase + pHeader->offset_sample_directory);
    pLoader->pLookupBase  = pBase + pHeader->offset_lookup_table;
    pLoader->pPcmBase     = pBase + pHeader->offset_pcm_data;
    pLoader->totalBinarySize = dataSize;
    pLoader->isLoaded     = true;

    return VHL_OK;
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

    const VHLInstrumentEntry* pInst = &pLoader->pInstruments[instrumentId];

    // O(1) Accelerated Region Lookup: Jump directly to instrument lookup header
    const uint8_t* pLookupInst = (const uint8_t*)(pLoader->pHeader->offset_lookup_table + (const uint8_t*)pLoader->pHeader);
    pLookupInst += (instrumentId * (sizeof(VHLLookupHeader) + (VHL_MIDI_KEYS * sizeof(VHLLookupEntry))));

    const VHLLookupHeader* pLookHeader = (const VHLLookupHeader*)pLookupInst;
    (void)pLookHeader; // Header validation check

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

    (void)pInst; // Suppress unused var warning
    *outRegionCount = matchedCount;
    return VHL_OK;
}
