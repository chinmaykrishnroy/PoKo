#pragma once
inline unsigned splashWatchdogFeeds=0;
inline void esp_task_wdt_reset() { ++splashWatchdogFeeds; }
