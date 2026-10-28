#pragma once

// ─────────────────────────────────────────────────────────────
//  CoreInputEvent — Typed input events dispatched to IApp
//  Decouples button hardware from application logic.
//  ButtonInput emits raw GPIO events; the main loop translates
//  them to InputEvent values for app handleInput() implementations.
// ─────────────────────────────────────────────────────────────

enum class InputEvent : uint8_t {
    Left,          ///< Single press of the LEFT (BOOT/DOWN) button
    Right,         ///< Single press of the RIGHT (KEY/UP) button
    LeftDouble,    ///< Double-click LEFT  — conventional "Back"
    RightDouble,   ///< Double-click RIGHT — conventional "Enter/Action"
    LeftHolding,   ///< Repeating hold LEFT  (volume down, slow adjust)
    RightHolding,  ///< Repeating hold RIGHT (volume up,  slow adjust)
    LongLeft,      ///< Long-press LEFT  (currently unused by most apps)
    LongRight,     ///< Long-press RIGHT (refresh / secondary action)
    Enter,         ///< Logical "confirm" — alias for RightDouble in some flows
    Back,          ///< Logical "cancel/exit" — alias for LeftDouble in some flows
};
