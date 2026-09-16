// The measurements the design is drawn to.
//
// The window is laid out at 1180x820 and everything here is in those units.
// A larger window gives its extra width to the rail and its extra height to the
// orbit; a smaller one takes it back in the same order.
#pragma once

namespace eightd {

inline constexpr double kDesignW = 1180, kDesignH = 820;
inline constexpr double kTopBar  = 56;   // the app's own title bar
inline constexpr double kStageW  = 520;   // the left column, padding included
inline constexpr double kCardR   = 22;    // card corner
inline constexpr double kTileR   = 14;    // choice tile corner
inline constexpr double kOrbit   = 430;   // the orbit at design size

// control ids
enum : int {
    kIdTabStudio = 101, kIdTabAbout, kIdTabAccount,
    kIdSource = 110, kIdSwitch = 111,
    kIdMinimise = 120, kIdClose = 121,
    kIdPreset = 200,             // .. kIdPreset + presets
    kIdSavePreset = 240,
    kIdPrev = 250, kIdPlayPause, kIdNext,
    kIdMode = 300,               // .. +8
    kIdDir = 310,
    kIdSpeed = 320, kIdDistance, kIdIntensity, kIdSmooth,
    kIdReverb = 330, kIdRoom, kIdDamping, kIdWidth,
    kIdDelay = 340, kIdDelayTime, kIdFeedback,
    kIdCharacter = 350,          // .. +3
    kIdAmount = 355,
    kIdBass = 360, kIdMid, kIdTreble, kIdOutput,
    kIdCapture = 370, kIdQuality = 372, kIdPause = 376, kIdReset = 377,
    kIdLatencyMenu = 380, kIdDeviceMenu = 381,
    kIdAbout = 500,
    kIdAccount = 600,
    kIdTour = 700,
};

} // namespace eightd
