#pragma once
// -----------------------------------------------------------------------------
// CircularLog.h
//
// A small, bounded, flash-backed ring buffer of fixed-size binary records.
// Used for historical environment data (section 20: "Historical Data").
//
// Rationale for a binary ring file instead of JSON-on-flash:
//   - Fixed record size means O(1) append/overwrite with no re-parsing of the
//     whole file, which matters on ESP8266's slower flash and smaller heap.
//   - The file itself is capped at (headerSize + recordSize * maxRecords)
//     bytes and never grows further — old points are overwritten in place,
//     so history can never consume unbounded flash.
//
// File layout:
//   [Header: magic, recordSize, maxRecords, count, writeIndex]
//   [record 0][record 1]...[record maxRecords-1]
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <FS.h>

class CircularLog {
public:
    // path: LittleFS path, e.g. "/history/env.bin"
    // recordSize: size in bytes of one record
    // maxRecords: capacity of the ring; oldest records are overwritten
    CircularLog(const char *path, size_t recordSize, uint32_t maxRecords);

    // Mounts/validates the backing file, creating it if absent or if the
    // header doesn't match (e.g. recordSize changed between firmware
    // versions).
    bool begin();

    // Appends one record, overwriting the oldest when the ring is full.
    bool append(const void *data);

    // Reads up to `maxOut` most recent records, oldest-first, into `out`
    // (which must hold maxOut * recordSize bytes). Returns the number of
    // records actually written.
    uint32_t readRecent(void *out, uint32_t maxOut);

    // Reads a downsampled view of the most recent `desiredRecent` records
    // (or fewer if the log doesn't have that many yet), emitting at most
    // `maxOutputPoints` of them, oldest-first, evenly strided. Used to keep
    // /api/history responses small and O(maxOutputPoints) flash reads
    // regardless of how large the requested time range is (section 30/34).
    uint32_t readRecentDownsampled(uint32_t desiredRecent, uint32_t maxOutputPoints, void *out);

    uint32_t count() const { return count_; }
    uint32_t capacity() const { return maxRecords_; }

    // Wipes all stored history (used by factory reset).
    bool clear();

private:
    struct Header {
        uint32_t magic;
        uint32_t recordSize;
        uint32_t maxRecords;
        uint32_t count;
        uint32_t writeIndex;
    };

    static constexpr uint32_t MAGIC = 0x454E5648; // "ENVH"

    String path_;
    size_t recordSize_;
    uint32_t maxRecords_;
    uint32_t count_ = 0;
    uint32_t writeIndex_ = 0;
    bool ready_ = false;

    bool readHeader(Header &hdr);
    bool writeHeader();
    size_t headerBytes() const { return sizeof(Header); }
};
