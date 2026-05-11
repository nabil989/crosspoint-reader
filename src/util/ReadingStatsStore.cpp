#include "ReadingStatsStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <ctime>

ReadingStatsStore& ReadingStatsStore::instance() {
  static ReadingStatsStore inst;
  return inst;
}

void ReadingStatsStore::ensureLoaded() {
  if (loaded) {
    return;
  }
  loadFromFile();
}

bool ReadingStatsStore::wallClockValid(time_t t) { return t > MIN_VALID_EPOCH && t < MAX_VALID_EPOCH; }

void ReadingStatsStore::formatDayKey(time_t t, char out[9]) {
  struct tm localTm{};
  localtime_r(&t, &localTm);
  snprintf(out, 9, "%04d%02d%02d", localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
}

std::string ReadingStatsStore::dayKeyFromYmd(int year, int month1To12, int day) {
  char key[9];
  snprintf(key, sizeof(key), "%04d%02d%02d", year, month1To12, day);
  return key;
}

bool ReadingStatsStore::loadFromFile() {
  loaded = true;
  days_.clear();
  if (!Storage.exists(FILE_PATH)) {
    return true;
  }
  String json = Storage.readFile(FILE_PATH);
  if (json.isEmpty()) {
    return false;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json);
  if (err) {
    LOG_ERR("READSTAT", "Parse error: %s", err.c_str());
    return false;
  }
  const uint8_t ver = doc["v"] | static_cast<uint8_t>(0);
  if (ver != FILE_VERSION && ver != 0) {
    LOG_ERR("READSTAT", "Unsupported version %u", ver);
    return false;
  }
  JsonObjectConst daysObj = doc["days"];
  if (!daysObj.isNull()) {
    for (JsonPairConst p : daysObj) {
      const char* k = p.key().c_str();
      if (std::strlen(k) == 8) {
        days_[k] = static_cast<uint32_t>(p.value().as<unsigned long>());
      }
    }
  }
  return true;
}

void ReadingStatsStore::trimOldEntries() {
  const time_t now = time(nullptr);
  if (!wallClockValid(now)) {
    return;
  }
  struct tm localTm{};
  localtime_r(&now, &localTm);
  localTm.tm_hour = 0;
  localTm.tm_min = 0;
  localTm.tm_sec = 0;
  const time_t todayMidnight = mktime(&localTm);
  if (todayMidnight == static_cast<time_t>(-1)) {
    return;
  }
  const time_t cutoff = todayMidnight - static_cast<time_t>(400) * 86400;
  char cutoffKey[9];
  formatDayKey(cutoff, cutoffKey);
  for (auto it = days_.begin(); it != days_.end();) {
    if (it->first < cutoffKey) {
      it = days_.erase(it);
    } else {
      ++it;
    }
  }
}

bool ReadingStatsStore::saveToFile() const {
  JsonDocument doc;
  doc["v"] = FILE_VERSION;
  JsonObject daysObj = doc["days"].to<JsonObject>();
  for (const auto& e : days_) {
    daysObj[e.first] = e.second;
  }
  String json;
  serializeJson(doc, json);
  Storage.mkdir("/.crosspoint");
  if (!Storage.writeFile(FILE_PATH, json)) {
    LOG_ERR("READSTAT", "Failed to write %s", FILE_PATH);
    return false;
  }
  return true;
}

void ReadingStatsStore::accrueSessionSegment(time_t& segmentWall, uint32_t& lastMillis) {
  ensureLoaded();
  const uint32_t nowMs = millis();
  const uint32_t deltaSec = static_cast<uint32_t>((nowMs - lastMillis) / 1000);
  if (deltaSec == 0) {
    return;
  }

  const time_t wallNow = time(nullptr);
  if (!wallClockValid(wallNow)) {
    lastMillis = nowMs;
    segmentWall = wallNow;
    return;
  }

  if (!wallClockValid(segmentWall)) {
    segmentWall = wallNow - static_cast<time_t>(deltaSec);
    if (!wallClockValid(segmentWall)) {
      segmentWall = wallNow;
    }
  }

  time_t t = segmentWall;
  uint32_t remain = deltaSec;

  while (remain > 0) {
    struct tm localTm{};
    localtime_r(&t, &localTm);
    struct tm nextMid = localTm;
    nextMid.tm_hour = 0;
    nextMid.tm_min = 0;
    nextMid.tm_sec = 0;
    nextMid.tm_mday += 1;
    const time_t nextMidnight = mktime(&nextMid);
    uint32_t secUntilMidnight = UINT32_MAX;
    if (nextMidnight != static_cast<time_t>(-1) && nextMidnight > t) {
      secUntilMidnight = static_cast<uint32_t>(nextMidnight - t);
    }
    uint32_t chunk = remain < secUntilMidnight ? remain : secUntilMidnight;
    if (chunk == 0) {
      chunk = remain;
    }

    char key[9];
    formatDayKey(t, key);
    const uint64_t sum = static_cast<uint64_t>(days_[key]) + chunk;
    days_[key] = sum > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(sum);
    dirty = true;

    t += static_cast<time_t>(chunk);
    remain -= chunk;
  }

  segmentWall = t;
  lastMillis = nowMs;
}

bool ReadingStatsStore::saveIfDirty() {
  if (!dirty) {
    return true;
  }
  trimOldEntries();
  if (saveToFile()) {
    dirty = false;
    return true;
  }
  return false;
}

uint32_t ReadingStatsStore::secondsForDayKey(const std::string& key) const {
  const auto it = days_.find(key);
  return it == days_.end() ? 0 : it->second;
}
