#pragma once

// Toggle normal Pixel output without touching the saved RGB, brightness, or
// target mask. The generic form keeps this behavior testable without FastLED.
template <typename Engine, typename PreferencesType, typename Mode>
void togglePixelSolidOff(
    Engine& engine,
    PreferencesType& preferences,
    Mode offMode,
    Mode solidMode
) {
    engine.setMode(engine.getMode() == offMode ? solidMode : offMode);
    engine.saveToPreferences(preferences);
}
