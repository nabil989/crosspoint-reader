#pragma once
#include <cstdint>
#include <ctime>
#include <map>
#include <string>

// Persists EPUB reader session time (seconds per calendar day) on the SD card.
// Uses wall-clock splits when recording monotonic session duration so sessions
// crossing local midnight attribute seconds to the correct day.
class ReadingStatsStore {
 public:
  static ReadingStatsStore& instance();

  void ensureLoaded();

  // Adds elapsed monotonic time since lastMillis to daily buckets, advancing
  // segmentWall along the local-time axis. Updates lastMillis to millis().
  void accrueSessionSegment(time_t& segmentWall, uint32_t& lastMillis);

  bool saveIfDirty();

  uint32_t secondsForDayKey(const std::string& key) const;

  const std::map<std::string, uint32_t>& days() const { return days_; }

  static bool wallClockValid(time_t t);
  static void formatDayKey(time_t t, char out[9]);
  static std::string dayKeyFromYmd(int year, int month1To12, int day);

 private:
  ReadingStatsStore() = default;

  bool loadFromFile();
  bool saveToFile() const;
  void trimOldEntries();
  static constexpr const char* FILE_PATH = "/.crosspoint/reading_stats.json";
  static constexpr uint8_t FILE_VERSION = 1;
  static constexpr time_t MIN_VALID_EPOCH = 1704067200;
  static constexpr time_t MAX_VALID_EPOCH = 4102444800;

  bool loaded = false;
  bool dirty = false;
  std::map<std::string, uint32_t> days_;
};
