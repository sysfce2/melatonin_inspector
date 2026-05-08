#pragma once
#include "juce_gui_basics/juce_gui_basics.h"

// Paint timings rely on ComponentListener::componentPainted and
// TimedDiagnostic, both introduced in JUCE 8.0.13. The rest of the inspector
// works fine on older JUCE — when this macro is 0 we just hide the timings
// panel and elide the related code. We accept either an explicit version
// bump or the presence of the new header (so the timings also activate on
// JUCE develop branches whose build number hasn't been bumped yet).
#if (defined(JUCE_VERSION) && JUCE_VERSION > 0x8000c) \
    || (defined(__has_include) && __has_include(<juce_core/time/juce_TimedDiagnostic.h>))
    #define MELATONIN_HAS_PAINT_DIAGNOSTICS 1
#else
    #define MELATONIN_HAS_PAINT_DIAGNOSTICS 0
#endif

#if MELATONIN_HAS_PAINT_DIAGNOSTICS

namespace melatonin
{
    // Rolling window of paint-cycle measurements for a single component.
    // Each sample bundles its timing with the cache-state recorded for that
    // paint, so a future contributor adding a new metric can't accidentally
    // desync a parallel array.
    //
    // 64 slots covers ~1 second at 60fps. Each bar in the Preview histogram
    // takes 3px (2px bar + 1px gap), so the buffer fits comfortably wider than
    // the histogram region — older samples scroll off the left.
    struct PaintDiagnosticsHistory
    {
        enum class CacheState : uint8_t
        {
            none = 0,    // no cache involvement this paint
            hit = 1,     // read from cache without re-rendering
            refresh = 2  // cache was invalidated and re-rendered
        };

        struct Sample
        {
            double seconds = 0.0;
            CacheState cache = CacheState::none;
        };

        struct Metric
        {
            static constexpr int capacity = 64;

            // Ring buffer. writeIdx points at the next slot to write; the
            // oldest valid sample is at (writeIdx - filled + capacity) % capacity.
            std::array<Sample, capacity> samples {};
            int writeIdx = 0;
            int filled = 0;
            double max = 0.0;
            double sum = 0.0;

            void push (double seconds, CacheState cache)
            {
                if (filled == capacity)
                    sum -= samples[(size_t) writeIdx].seconds;
                else
                    ++filled;

                samples[(size_t) writeIdx] = { seconds, cache };
                sum += seconds;
                writeIdx = (writeIdx + 1) % capacity;

                if (seconds > max)
                    max = seconds;
            }

            void clear()
            {
                samples.fill ({});
                writeIdx = 0;
                filled = 0;
                max = 0.0;
                sum = 0.0;
            }

            [[nodiscard]] double average() const
            {
                return filled > 0 ? sum / (double) filled : 0.0;
            }
        };

        Metric total;             // paintDuration + children + paintOverChildren + applyEffect
        Metric exclusive;         // paint() + paintOverChildren() + applyEffect() (no children)
        Metric paintMethod;       // paint() only
        Metric paintOverChildren; // paintOverChildren() only
        Metric applyEffect;       // ImageEffectFilter::applyEffect()

        [[nodiscard]] bool empty() const noexcept { return total.filled == 0; }

        void clear()
        {
            total.clear();
            exclusive.clear();
            paintMethod.clear();
            paintOverChildren.clear();
            applyEffect.clear();
        }

        void capture (const juce::ComponentPaintDiagnostics& d)
        {
            const auto totalSec = d.totalPaintDuration.get<juce::Seconds>();
            const auto paintSec = d.paintDuration.get<juce::Seconds>();
            const auto paintOverSec = d.paintOverChildrenDuration.get<juce::Seconds>();
            const auto applySec = d.applyEffectDuration.get<juce::Seconds>();

            CacheState state = CacheState::none;
            if (d.readFromCache && d.wroteToCache)
                state = CacheState::refresh;
            else if (d.readFromCache)
                state = CacheState::hit;

            // on a hit no paint methods run, so attribute the blit cost
            // (totalSec) to exclusive — it's the component's own self-render,
            // not children's
            const double exclusiveSec = (state == CacheState::hit)
                ? totalSec
                : paintSec + paintOverSec + applySec;

            total.push (totalSec, state);
            paintMethod.push (paintSec, state);
            paintOverChildren.push (paintOverSec, state);
            applyEffect.push (applySec, state);
            exclusive.push (exclusiveSec, state);
        }
    };
}

#endif // MELATONIN_HAS_PAINT_DIAGNOSTICS
