#pragma once

#include "activities/Activity.h"

class ReadingStatsActivity final : public Activity {
 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStats", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  int viewYear = 2024;
  int viewMonth = 1;

  void clampViewToMaxToday();
  static int daysInMonth(int year, int month1To12);
  static bool isLeapYear(int year);
};
