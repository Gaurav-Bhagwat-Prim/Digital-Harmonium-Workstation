// =============================================================================
// VhlDocument.cpp — VHL Core Library: Decoded Document Model Implementation
// =============================================================================

#include "VhlDocument.h"
#include <algorithm>
#include <numeric>

namespace Vhl {
namespace Tools {
namespace Core {

void VhlDocument::computeStatistics()
{
    stats = ComputedStats{};

    if (samples.empty() && regions.empty())
        return;

    // 1. Sample statistics
    std::vector<SampleStats> allSampleStats;
    allSampleStats.reserve(samples.size());

    for (size_t i = 0; i < samples.size(); ++i)
    {
        const auto& s = samples[i];
        SampleStats st;
        st.sampleId = static_cast<uint16_t>(i);
        st.name = "Sample_" + std::to_string(i);
        st.totalFrames = s.total_frames;
        st.durationSec = s.sample_rate > 0 ? (float)s.total_frames / (float)s.sample_rate : 0.0f;
        st.pcmSizeBytes = s.total_frames * s.channels * 2;
        st.isLooped = (s.flags & 0x01) != 0;
        st.loopLengthFrames = (st.isLooped && s.loop_end_frame > s.loop_start_frame)
                              ? (s.loop_end_frame - s.loop_start_frame) : 0;

        stats.totalPcmBytes += st.pcmSizeBytes;
        stats.totalPcmDurationSec += st.durationSec;

        if (st.isLooped)
            stats.loopedSampleCount++;
        else
            stats.oneShotSampleCount++;

        allSampleStats.push_back(st);
    }

    if (!samples.empty())
    {
        stats.avgSampleDurationSec = stats.totalPcmDurationSec / static_cast<float>(samples.size());
    }

    // Sort largest samples by PCM size (descending)
    std::sort(allSampleStats.begin(), allSampleStats.end(),
              [](const SampleStats& a, const SampleStats& b) {
                  return a.pcmSizeBytes > b.pcmSizeBytes;
              });

    size_t topN = std::min<size_t>(5, allSampleStats.size());
    stats.largestSamples.assign(allSampleStats.begin(), allSampleStats.begin() + topN);

    // 2. Region statistics & averages
    double sumAttack = 0.0;
    double sumRelease = 0.0;
    double sumCutoff = 0.0;

    for (const auto& r : regions)
    {
        sumAttack  += r.vol_env.attack_ms;
        sumRelease += r.vol_env.release_ms;
        sumCutoff  += r.filter.cutoff_hz;

        if (r.key_lo < stats.minKeyCovered) stats.minKeyCovered = r.key_lo;
        if (r.key_hi > stats.maxKeyCovered) stats.maxKeyCovered = r.key_hi;
    }

    if (!regions.empty())
    {
        double n = static_cast<double>(regions.size());
        stats.avgVolAttackMs    = static_cast<float>(sumAttack / n);
        stats.avgVolReleaseMs   = static_cast<float>(sumRelease / n);
        stats.avgFilterCutoffHz = static_cast<float>(sumCutoff / n);
    }

    // 3. Active key coverage from lookup tables
    std::vector<bool> keyCovered(128, false);
    for (const auto& lkTable : lookupTables)
    {
        for (int k = 0; k < 128 && k < (int)lkTable.size(); ++k)
        {
            if (lkTable[k].region_count > 0)
                keyCovered[k] = true;
        }
    }

    stats.activeKeysCount = 0;
    for (int k = 0; k < 128; ++k)
    {
        if (keyCovered[k])
            stats.activeKeysCount++;
    }
}

} // namespace Core
} // namespace Tools
} // namespace Vhl
