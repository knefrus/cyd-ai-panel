#pragma once

#define WIFI_SSID     "your-wifi"
#define WIFI_PASS     "your-password"

// where the panel pulls data from, leave empty if a PC pushes it instead
#define DATA_URL      "http://1.2.3.4:8787/panel.json?key=your-key"
#define PULL_EVERY_S  60

#define HOSTNAME      "ai-panel"
#define PANEL_TOKEN   ""
#define TZ_POSIX      "MSK-3"

// 0 light, 1 dark, 2 dark at night
#define THEME_MODE    2
#define NIGHT_FROM    22
#define NIGHT_TO      8
#define BRIGHT_DAY    255
#define BRIGHT_NIGHT  40

#define SCREEN_ROTATION        1
#define TOUCH_OFFSET_ROTATION  0
// flip touches if taps land mirrored
#define TOUCH_FLIP_X           0
#define TOUCH_FLIP_Y           0
