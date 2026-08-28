#pragma once
namespace browser::css::constants {
// Magic numbers promoted from inline sites (Wave 5 hygiene)
// Layout
inline constexpr float kCharWidthFactor = 0.6f; // inline.cpp:21
inline constexpr float kFormControlCharWidth = 7.0f; // block.cpp:288
inline constexpr float kFormControlPadding = 20.0f; // block.cpp:288
inline constexpr float kMarkerWidthFactor = 1.5f; // block.cpp:348
inline constexpr float kColumnMinWidth = 20.0f; // block.cpp:212
inline constexpr float kOptionRowHeight = 20.0f; // painter.cpp:246
// Chrome
inline constexpr int kResizeDebounceMs = 100; // window.cpp:668
inline constexpr int kTitlebarDragBorder = 6; // window.cpp:795
inline constexpr float kDropdownWidth = 300.0f; // chrome/window.cpp et al
inline constexpr float kDropdownItemHeight = 28.0f;
inline constexpr float kScrollbarStep = 30.0f; // event_handler.cpp:1889
inline constexpr float kZoomStep = 1.25f; // event_handler.cpp:1229
inline constexpr float kZoomMin = 0.25f;
inline constexpr float kZoomMax = 5.0f;
inline constexpr int kFontSizeMin = 8;
inline constexpr int kFontSizeMax = 64;
// Grid
inline constexpr int kGridPlacementSafety = 10000; // layout/grid.cpp:381
inline constexpr int kGridSpanClamp = 1000; // css/grid.cpp:232
} // namespace browser::css::constants
