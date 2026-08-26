#include "CircularLog.h"
#include "hardware/FSCompat.h"
#include "Logger.h"
#include "RingMath.h"

CircularLog::CircularLog(const char *path, size_t recordSize, uint32_t maxRecords)
    : path_(path), recordSize_(recordSize), maxRecords_(maxRecords) {}

bool CircularLog::readHeader(Header &hdr) {
    File f = LittleFS.open(path_, "r");
    if (!f) return false;
    size_t n = f.read(reinterpret_cast<uint8_t *>(&hdr), sizeof(Header));
    f.close();
    return n == sizeof(Header);
}

bool CircularLog::writeHeader() {
    // Rewriting only the header of an existing file in place, preserving the
    // record region that follows it.
    File f = LittleFS.open(path_, "r+");
    if (!f) return false;
    Header hdr{MAGIC, (uint32_t)recordSize_, maxRecords_, count_, writeIndex_};
    f.seek(0);
    size_t n = f.write(reinterpret_cast<uint8_t *>(&hdr), sizeof(Header));
    f.close();
    return n == sizeof(Header);
}

bool CircularLog::begin() {
    Header hdr;
    bool valid = readHeader(hdr) &&
                 hdr.magic == MAGIC &&
                 hdr.recordSize == recordSize_ &&
                 hdr.maxRecords == maxRecords_;

    if (valid) {
        count_ = hdr.count;
        writeIndex_ = hdr.writeIndex;
        ready_ = true;
        return true;
    }

    // (Re)create the file: header + zeroed record region.
    LittleFS.remove(path_);
    File f = LittleFS.open(path_, "w");
    if (!f) {
        Logger::error("CircularLog", "Failed to create " + path_);
        return false;
    }

    Header hdr0{MAGIC, (uint32_t)recordSize_, maxRecords_, 0, 0};
    f.write(reinterpret_cast<uint8_t *>(&hdr0), sizeof(Header));

    uint8_t zeroBuf[32] = {0};
    size_t remaining = recordSize_ * maxRecords_;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(zeroBuf) ? remaining : sizeof(zeroBuf);
        f.write(zeroBuf, chunk);
        remaining -= chunk;
    }
    f.close();

    count_ = 0;
    writeIndex_ = 0;
    ready_ = true;
    return true;
}

bool CircularLog::append(const void *data) {
    if (!ready_) return false;

    File f = LittleFS.open(path_, "r+");
    if (!f) return false;

    size_t offset = headerBytes() + (size_t)writeIndex_ * recordSize_;
    f.seek(offset);
    f.write(reinterpret_cast<const uint8_t *>(data), recordSize_);
    f.close();

    ringmath::AppendResult next = ringmath::afterAppend(count_, writeIndex_, maxRecords_);
    count_ = next.count;
    writeIndex_ = next.writeIndex;

    return writeHeader();
}

uint32_t CircularLog::readRecent(void *out, uint32_t maxOut) {
    if (!ready_ || count_ == 0) return 0;

    File f = LittleFS.open(path_, "r");
    if (!f) return 0;

    uint32_t n = count_ < maxOut ? count_ : maxOut;
    // Oldest logical index among the n most recent records:
    uint32_t oldestLogicalIndex = count_ - n;

    uint8_t *dst = reinterpret_cast<uint8_t *>(out);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t slot = ringmath::slotForLogicalIndex(oldestLogicalIndex + i, count_, writeIndex_, maxRecords_);
        size_t offset = headerBytes() + (size_t)slot * recordSize_;
        f.seek(offset);
        f.read(dst + (size_t)i * recordSize_, recordSize_);
    }
    f.close();
    return n;
}

uint32_t CircularLog::readRecentDownsampled(uint32_t desiredRecent, uint32_t maxOutputPoints, void *out) {
    if (!ready_ || count_ == 0 || maxOutputPoints == 0) return 0;

    ringmath::DownsamplePlan plan = ringmath::planDownsample(desiredRecent, count_, maxOutputPoints);
    if (plan.matchCount == 0) return 0;

    File f = LittleFS.open(path_, "r");
    if (!f) return 0;

    uint8_t *dst = reinterpret_cast<uint8_t *>(out);
    uint32_t outCount = 0;

    for (uint32_t i = 0; i < plan.matchCount && outCount < maxOutputPoints; i += plan.stride) {
        uint32_t logicalIndex = plan.oldestWantedLogicalIndex + i;
        uint32_t slot = ringmath::slotForLogicalIndex(logicalIndex, count_, writeIndex_, maxRecords_);
        size_t offset = headerBytes() + (size_t)slot * recordSize_;
        f.seek(offset);
        f.read(dst + (size_t)outCount * recordSize_, recordSize_);
        outCount++;
    }

    f.close();
    return outCount;
}

bool CircularLog::clear() {
    LittleFS.remove(path_);
    ready_ = false;
    count_ = 0;
    writeIndex_ = 0;
    return begin();
}
