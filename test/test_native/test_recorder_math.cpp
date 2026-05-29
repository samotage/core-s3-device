// Native Unity tests for the recorder-standalone-experience pure-math layer.
//
// These tests cover the PRD-anchored predicates without pulling in any
// firmware-only dependencies (M5Unified, SD, ESP-IDF). They build for the
// native env and run on the dev host.

#include <unity.h>
#include "recorder_math.h"
#include <algorithm>
#include <cstring>

using namespace RecorderMath;

void setUp(void)    {}
void tearDown(void) {}

// --- LiPo percentage from voltage (PRD FR2) -------------------------------

static void test_lipo_full() {
    TEST_ASSERT_EQUAL_UINT8(100, BatteryPercentFromMv(4200));
    TEST_ASSERT_EQUAL_UINT8(100, BatteryPercentFromMv(4500));  // clamped
}

static void test_lipo_empty() {
    TEST_ASSERT_EQUAL_UINT8(0, BatteryPercentFromMv(0));     // zero = bad read
    TEST_ASSERT_EQUAL_UINT8(0, BatteryPercentFromMv(2900)); // below curve
    TEST_ASSERT_EQUAL_UINT8(0, BatteryPercentFromMv(3000)); // anchor low
}

static void test_lipo_anchors() {
    // Hit each known curve anchor exactly.
    TEST_ASSERT_EQUAL_UINT8(5,  BatteryPercentFromMv(3300));
    TEST_ASSERT_EQUAL_UINT8(10, BatteryPercentFromMv(3500));
    TEST_ASSERT_EQUAL_UINT8(30, BatteryPercentFromMv(3700));
    TEST_ASSERT_EQUAL_UINT8(50, BatteryPercentFromMv(3800));
    TEST_ASSERT_EQUAL_UINT8(70, BatteryPercentFromMv(3900));
    TEST_ASSERT_EQUAL_UINT8(85, BatteryPercentFromMv(4050));
}

static void test_lipo_interpolation_is_monotonic() {
    // Across the discharge range, percentage must never decrease as voltage rises.
    uint8_t last = 0;
    for (uint16_t mv = 3000; mv <= 4200; mv += 25) {
        uint8_t p = BatteryPercentFromMv(mv);
        TEST_ASSERT_TRUE(p >= last);
        last = p;
    }
}

// --- Est-recording-time math (PRD FR15) -----------------------------------

static void test_estimated_minutes_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, EstimatedMinutes(0));
    // Below one second of audio rounds to 0 minutes.
    TEST_ASSERT_EQUAL_UINT32(0, EstimatedMinutes(31999));
}

static void test_estimated_minutes_one_hour() {
    // ~115 MB/hour at 16 kHz mono 16-bit.
    // 60 minutes * 60 sec * 32000 bytes/sec = 115_200_000 bytes.
    TEST_ASSERT_EQUAL_UINT32(60, EstimatedMinutes(115200000ULL));
}

static void test_estimated_minutes_two_minutes() {
    // 2 * 60 * 32000 = 3_840_000.
    TEST_ASSERT_EQUAL_UINT32(2, EstimatedMinutes(3840000ULL));
}

static void test_estimated_minutes_large_card() {
    // 14 GB free should give very many minutes — bounds check only.
    uint64_t fourteen_gb = 14ULL * 1024ULL * 1024ULL * 1024ULL;
    uint32_t minutes = EstimatedMinutes(fourteen_gb);
    TEST_ASSERT_TRUE(minutes > 7000);    // >116 hours
    TEST_ASSERT_TRUE(minutes < 9000);    // sanity upper bound
}

// --- Storage-full predicate (PRD FR33) ------------------------------------

static void test_storage_full_below_threshold() {
    TEST_ASSERT_TRUE(IsStorageFull(0));
    TEST_ASSERT_TRUE(IsStorageFull(STORAGE_FULL_THRESHOLD_BYTES - 1));
}

static void test_storage_not_full_at_threshold() {
    TEST_ASSERT_FALSE(IsStorageFull(STORAGE_FULL_THRESHOLD_BYTES));
    TEST_ASSERT_FALSE(IsStorageFull(STORAGE_FULL_THRESHOLD_BYTES + 1));
}

static void test_storage_not_full_plenty() {
    TEST_ASSERT_FALSE(IsStorageFull(1024ULL * 1024ULL * 1024ULL));
}

// --- Critical-battery predicate (PRD FR31) --------------------------------

static void test_critical_battery_zero_read_ignored() {
    // FR31 NOTE: a 0 mV read is a bad sample, NOT a critical battery — don't
    // shut down on noise.
    TEST_ASSERT_FALSE(IsCriticalBattery(0));
}

static void test_critical_battery_below_voltage() {
    TEST_ASSERT_TRUE(IsCriticalBattery(CRITICAL_BATTERY_VOLTAGE_MV));
    TEST_ASSERT_TRUE(IsCriticalBattery(CRITICAL_BATTERY_VOLTAGE_MV - 1));
}

static void test_critical_battery_below_percent_via_voltage() {
    // At ~3300 mV the curve gives 5% which is <= CRITICAL_BATTERY_PERCENT (8).
    TEST_ASSERT_TRUE(IsCriticalBattery(3300));
    TEST_ASSERT_TRUE(IsCriticalBattery(3400));  // ~7.5% via interp
}

static void test_critical_battery_healthy() {
    TEST_ASSERT_FALSE(IsCriticalBattery(3800));  // 50%
    TEST_ASSERT_FALSE(IsCriticalBattery(4200));  // 100%
}

// --- Oldest-N selection (PRD FR16) ----------------------------------------

static void test_oldest_n_caps_at_count() {
    TEST_ASSERT_EQUAL_INT(3, OldestN(3, 10));   // fewer files than N
    TEST_ASSERT_EQUAL_INT(0, OldestN(0, 10));   // empty SD
}

static void test_oldest_n_takes_n_when_plenty() {
    TEST_ASSERT_EQUAL_INT(10, OldestN(50, 10));
    TEST_ASSERT_EQUAL_INT(10, OldestN(10, 10));
}

static void test_oldest_n_filename_sort_picks_lowest_numbers() {
    // Representative recordings; sort + take first N.
    const char* names[] = {
        "REC_005.wav",
        "REC_001.wav",
        "REC_003.wav",
        "REC_010.wav",
        "REC_002.wav",
    };
    size_t count = sizeof(names) / sizeof(names[0]);
    std::sort(names, names + count, [](const char* a, const char* b) {
        return strcmp(a, b) < 0;
    });
    // After sort, REC_001 / REC_002 / REC_003 lead.
    TEST_ASSERT_EQUAL_STRING("REC_001.wav", names[0]);
    TEST_ASSERT_EQUAL_STRING("REC_002.wav", names[1]);
    TEST_ASSERT_EQUAL_STRING("REC_003.wav", names[2]);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_lipo_full);
    RUN_TEST(test_lipo_empty);
    RUN_TEST(test_lipo_anchors);
    RUN_TEST(test_lipo_interpolation_is_monotonic);
    RUN_TEST(test_estimated_minutes_zero);
    RUN_TEST(test_estimated_minutes_one_hour);
    RUN_TEST(test_estimated_minutes_two_minutes);
    RUN_TEST(test_estimated_minutes_large_card);
    RUN_TEST(test_storage_full_below_threshold);
    RUN_TEST(test_storage_not_full_at_threshold);
    RUN_TEST(test_storage_not_full_plenty);
    RUN_TEST(test_critical_battery_zero_read_ignored);
    RUN_TEST(test_critical_battery_below_voltage);
    RUN_TEST(test_critical_battery_below_percent_via_voltage);
    RUN_TEST(test_critical_battery_healthy);
    RUN_TEST(test_oldest_n_caps_at_count);
    RUN_TEST(test_oldest_n_takes_n_when_plenty);
    RUN_TEST(test_oldest_n_filename_sort_picks_lowest_numbers);
    return UNITY_END();
}
