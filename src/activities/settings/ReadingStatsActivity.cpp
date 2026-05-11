#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ReadingStatsStore.h"

namespace {
constexpr int CELL = 12;
constexpr int GAP = 2;
constexpr int MIN_VIEW_YEAR = 2020;

Color heatColor(uint32_t sec, uint32_t maxSec) {
  if (sec == 0) {
    return Color::White;
  }
  if (maxSec == 0) {
    return Color::LightGray;
  }
  const uint32_t q = (sec * 4U + maxSec - 1U) / maxSec;
  if (q >= 4U) {
    return Color::Black;
  }
  if (q == 3U) {
    return Color::DarkGray;
  }
  return Color::LightGray;
}
}  // namespace

bool ReadingStatsActivity::isLeapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

int ReadingStatsActivity::daysInMonth(int year, int month1To12) {
  static constexpr int md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int d = md[month1To12 - 1];
  if (month1To12 == 2 && isLeapYear(year)) {
    d = 29;
  }
  return d;
}

void ReadingStatsActivity::clampViewToMaxToday() {
  const time_t now = time(nullptr);
  if (!ReadingStatsStore::wallClockValid(now)) {
    return;
  }
  struct tm tmNow{};
  localtime_r(&now, &tmNow);
  const int cy = tmNow.tm_year + 1900;
  const int cm = tmNow.tm_mon + 1;
  if (viewYear > cy || (viewYear == cy && viewMonth > cm)) {
    viewYear = cy;
    viewMonth = cm;
  }
}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  ReadingStatsStore::instance().ensureLoaded();

  const time_t now = time(nullptr);
  if (ReadingStatsStore::wallClockValid(now)) {
    struct tm tmNow{};
    localtime_r(&now, &tmNow);
    viewYear = tmNow.tm_year + 1900;
    viewMonth = tmNow.tm_mon + 1;
  } else {
    viewYear = MIN_VIEW_YEAR;
    viewMonth = 1;
  }
  requestUpdate();
}

void ReadingStatsActivity::onExit() { Activity::onExit(); }

void ReadingStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    viewMonth -= 1;
    if (viewMonth < 1) {
      viewMonth = 12;
      viewYear -= 1;
    }
    if (viewYear < MIN_VIEW_YEAR) {
      viewYear = MIN_VIEW_YEAR;
      viewMonth = 1;
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    viewMonth += 1;
    if (viewMonth > 12) {
      viewMonth = 1;
      viewYear += 1;
    }
    clampViewToMaxToday();
    requestUpdate();
  }
}

void ReadingStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS),
                 nullptr);

  char monthLine[32];
  snprintf(monthLine, sizeof(monthLine), "%04d-%02d", viewYear, viewMonth);
  renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + metrics.headerHeight + 6, monthLine, true);

  const time_t now = time(nullptr);
  const bool clockOk = ReadingStatsStore::wallClockValid(now);

  if (!clockOk) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_READING_STATS_NO_CLOCK), true);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int dim = daysInMonth(viewYear, viewMonth);
  struct tm first{};
  first.tm_year = viewYear - 1900;
  first.tm_mon = viewMonth - 1;
  first.tm_mday = 1;
  first.tm_hour = 12;
  first.tm_min = 0;
  first.tm_sec = 0;
  mktime(&first);
  const int wday = first.tm_wday;
  const int monFirst = (wday + 6) % 7;

  auto& store = ReadingStatsStore::instance();
  uint32_t maxSec = 0;
  uint32_t monthTotalSec = 0;
  for (int dom = 1; dom <= dim; dom++) {
    const std::string key = ReadingStatsStore::dayKeyFromYmd(viewYear, viewMonth, dom);
    const uint32_t s = store.secondsForDayKey(key);
    monthTotalSec += s;
    if (s > maxSec) {
      maxSec = s;
    }
  }

  char totalLine[64];
  const unsigned mins = monthTotalSec / 60U;
  snprintf(totalLine, sizeof(totalLine), tr(STR_READING_STATS_MONTH_TOTAL), mins);
  renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + metrics.headerHeight + 26, totalLine, true);

  const int lastIdx = monFirst + dim - 1;
  const int nCols = lastIdx / 7 + 1;
  const int gridW = nCols * (CELL + GAP) - GAP;
  const int gridH = 7 * (CELL + GAP) - GAP;

  const int gy = metrics.topPadding + metrics.headerHeight + 52;
  const int gx = (pageWidth - gridW) / 2;

  for (int dom = 1; dom <= dim; dom++) {
    const int idx = monFirst + dom - 1;
    const int col = idx / 7;
    const int row = idx % 7;
    const int x = gx + col * (CELL + GAP);
    const int y = gy + row * (CELL + GAP);

    const std::string key = ReadingStatsStore::dayKeyFromYmd(viewYear, viewMonth, dom);
    const uint32_t sec = store.secondsForDayKey(key);
    const Color c = heatColor(sec, maxSec);
    renderer.fillRectDither(x, y, CELL, CELL, c);
    renderer.drawRect(x, y, CELL, CELL, true);
  }

  const int legendY = std::min(gy + gridH + 10, pageHeight - metrics.buttonHintsHeight - 36);
  renderer.drawCenteredText(SMALL_FONT_ID, legendY, tr(STR_READING_STATS_LEGEND), true);

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_READING_STATS_PREV_MONTH), tr(STR_READING_STATS_NEXT_MONTH));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
