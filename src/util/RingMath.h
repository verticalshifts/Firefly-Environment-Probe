#pragma once
// -----------------------------------------------------------------------------
// RingMath.h
//
// The pure index arithmetic behind CircularLog, pulled out into a header
// with zero Arduino/FS dependency specifically so it can be unit-tested on
// the host (`pio test -e native`) without a physical ESP32/ESP8266 board.
// CircularLog.cpp is the only caller on-device; test/test_ringmath exercises
// it natively. Keep this file free of Arduino.h / FS.h includes.
// -----------------------------------------------------------------------------

#include <stdint.h>

namespace ringmath {

struct AppendResult {
    uint32_t count;
    uint32_t writeIndex;
};

// New (count, writeIndex) after appending one record to a ring of capacity
// maxRecords that currently holds `count` records with the next write going
// to physical slot `writeIndex`.
inline AppendResult afterAppend(uint32_t count, uint32_t writeIndex, uint32_t maxRecords) {
    AppendResult r;
    r.writeIndex = (maxRecords == 0) ? 0 : (writeIndex + 1) % maxRecords;
    r.count = (count < maxRecords) ? count + 1 : maxRecords;
    return r;
}

// Physical slot holding logical index `logicalIndex` (0 = the oldest record
// currently stored), given the ring's current count/writeIndex/capacity.
inline uint32_t slotForLogicalIndex(uint32_t logicalIndex, uint32_t count, uint32_t writeIndex, uint32_t maxRecords) {
    if (maxRecords == 0) return 0;
    return (writeIndex + maxRecords - count + logicalIndex) % maxRecords;
}

struct DownsamplePlan {
    uint32_t matchCount;              // how many of the most recent records qualify
    uint32_t stride;                  // step between emitted records (>=1)
    uint32_t oldestWantedLogicalIndex; // logical index (0=oldest ever) to start from
};

// Plans an evenly-strided read of the most recent `desiredRecent` records
// (capped at what's actually stored), emitting at most `maxOutputPoints` of
// them. Used by CircularLog::readRecentDownsampled to keep /api/history
// responses bounded regardless of how large the requested range is.
inline DownsamplePlan planDownsample(uint32_t desiredRecent, uint32_t count, uint32_t maxOutputPoints) {
    DownsamplePlan plan;
    plan.matchCount = (desiredRecent < count) ? desiredRecent : count;

    uint32_t stride = (maxOutputPoints == 0) ? plan.matchCount : (plan.matchCount / maxOutputPoints);
    plan.stride = (stride < 1) ? 1 : stride;

    plan.oldestWantedLogicalIndex = count - plan.matchCount;
    return plan;
}

} // namespace ringmath
