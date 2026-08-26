// Native unit tests for the ring-buffer index arithmetic behind
// CircularLog (src/util/RingMath.h) — the one piece of this firmware pure
// enough to test off-device. Run with:
//
//   pio test -e native
//
// RingMath.h has zero Arduino/FS dependency by design, so it's included
// directly here rather than through anything in src/hardware or the
// on-device CircularLog wrapper.

#include <unity.h>
#include "../../src/util/RingMath.h"

void setUp(void) {}
void tearDown(void) {}

// --- afterAppend ------------------------------------------------------

void test_after_append_increments_count_until_full(void) {
    ringmath::AppendResult r = ringmath::afterAppend(0, 0, 5);
    TEST_ASSERT_EQUAL_UINT32(1, r.count);
    TEST_ASSERT_EQUAL_UINT32(1, r.writeIndex);
}

void test_after_append_count_caps_at_max_records(void) {
    // Ring not yet full: count keeps growing.
    ringmath::AppendResult r = ringmath::afterAppend(3, 3, 5);
    TEST_ASSERT_EQUAL_UINT32(4, r.count);
    TEST_ASSERT_EQUAL_UINT32(4, r.writeIndex);
}

void test_after_append_wraps_write_index_when_full(void) {
    // Ring already full (count == maxRecords): count stays capped, the
    // write index wraps around to overwrite the oldest record.
    ringmath::AppendResult r = ringmath::afterAppend(5, 4, 5);
    TEST_ASSERT_EQUAL_UINT32(5, r.count);
    TEST_ASSERT_EQUAL_UINT32(0, r.writeIndex);
}

// --- slotForLogicalIndex ------------------------------------------------

void test_slot_for_logical_index_before_first_wrap(void) {
    // 3 records written into a 5-slot ring starting at slot 0: they live
    // at physical slots 0,1,2 in that (oldest-to-newest) order.
    TEST_ASSERT_EQUAL_UINT32(0, ringmath::slotForLogicalIndex(0, 3, 3, 5));
    TEST_ASSERT_EQUAL_UINT32(1, ringmath::slotForLogicalIndex(1, 3, 3, 5));
    TEST_ASSERT_EQUAL_UINT32(2, ringmath::slotForLogicalIndex(2, 3, 3, 5));
}

void test_slot_for_logical_index_after_wrap(void) {
    // Ring is full (count==maxRecords==5) with the next write landing at
    // slot 2 — meaning slot 2 currently holds the oldest surviving record.
    TEST_ASSERT_EQUAL_UINT32(2, ringmath::slotForLogicalIndex(0, 5, 2, 5)); // oldest
    TEST_ASSERT_EQUAL_UINT32(3, ringmath::slotForLogicalIndex(1, 5, 2, 5));
    TEST_ASSERT_EQUAL_UINT32(4, ringmath::slotForLogicalIndex(2, 5, 2, 5));
    TEST_ASSERT_EQUAL_UINT32(0, ringmath::slotForLogicalIndex(3, 5, 2, 5));
    TEST_ASSERT_EQUAL_UINT32(1, ringmath::slotForLogicalIndex(4, 5, 2, 5)); // newest
}

// --- planDownsample -----------------------------------------------------

void test_downsample_plan_returns_everything_when_under_cap(void) {
    // Fewer stored/matching records than the output cap: no striding needed.
    ringmath::DownsamplePlan plan = ringmath::planDownsample(50, 40, 300);
    TEST_ASSERT_EQUAL_UINT32(40, plan.matchCount);
    TEST_ASSERT_EQUAL_UINT32(1, plan.stride);
    TEST_ASSERT_EQUAL_UINT32(0, plan.oldestWantedLogicalIndex);
}

void test_downsample_plan_strides_when_over_cap(void) {
    // The ESP32 7-day/60s-resolution ring (10,080 records) against the
    // /api/history 300-point output cap.
    ringmath::DownsamplePlan plan = ringmath::planDownsample(10080, 10080, 300);
    TEST_ASSERT_EQUAL_UINT32(10080, plan.matchCount);
    TEST_ASSERT_EQUAL_UINT32(33, plan.stride); // 10080 / 300, truncated
    TEST_ASSERT_EQUAL_UINT32(0, plan.oldestWantedLogicalIndex);
}

void test_downsample_plan_desired_range_smaller_than_stored_history(void) {
    // A "1 Hour" request (60 records at 60s resolution) against a ring
    // that's been running for days — only the most recent 60 should match.
    ringmath::DownsamplePlan plan = ringmath::planDownsample(60, 10080, 300);
    TEST_ASSERT_EQUAL_UINT32(60, plan.matchCount);
    TEST_ASSERT_EQUAL_UINT32(1, plan.stride);
    TEST_ASSERT_EQUAL_UINT32(10020, plan.oldestWantedLogicalIndex);
}

void test_downsample_plan_desired_range_larger_than_stored_history(void) {
    // A "7 Days" request on a device that's only been up for 48h worth of
    // records (2,880 on ESP8266) — matchCount is capped by what's stored,
    // never claims data that was never recorded.
    ringmath::DownsamplePlan plan = ringmath::planDownsample(604800 / 60, 2880, 300);
    TEST_ASSERT_EQUAL_UINT32(2880, plan.matchCount);
    TEST_ASSERT_EQUAL_UINT32(9, plan.stride); // 2880 / 300
    TEST_ASSERT_EQUAL_UINT32(0, plan.oldestWantedLogicalIndex);
}

void test_downsample_plan_empty_ring(void) {
    ringmath::DownsamplePlan plan = ringmath::planDownsample(3600, 0, 300);
    TEST_ASSERT_EQUAL_UINT32(0, plan.matchCount);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_after_append_increments_count_until_full);
    RUN_TEST(test_after_append_count_caps_at_max_records);
    RUN_TEST(test_after_append_wraps_write_index_when_full);

    RUN_TEST(test_slot_for_logical_index_before_first_wrap);
    RUN_TEST(test_slot_for_logical_index_after_wrap);

    RUN_TEST(test_downsample_plan_returns_everything_when_under_cap);
    RUN_TEST(test_downsample_plan_strides_when_over_cap);
    RUN_TEST(test_downsample_plan_desired_range_smaller_than_stored_history);
    RUN_TEST(test_downsample_plan_desired_range_larger_than_stored_history);
    RUN_TEST(test_downsample_plan_empty_ring);

    return UNITY_END();
}
