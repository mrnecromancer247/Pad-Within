#pragma once

// ---------------------------------------------------------------------------
// Runtime configuration loaded from PadWithin.ini (next to the DLL).
// All fields have sane defaults so the mod works with no ini present.
// ---------------------------------------------------------------------------
struct Config
{
    // --- general ---
    bool enableLog = false;  // write PadWithin.log (off by default)
    bool passthrough = false;// DIAG: don't synthesize; pass native dinput through
                             // and dump the raw 80-byte buffer (to capture the
                             // working native layout of a real DirectInput pad).

    // --- controller selection ---
    // Which controller to use when several are present. -1 = auto (pick the one
    // actually sending input). Otherwise the SDL device index.
    int  controllerIndex = -1;

    // --- sensitivity / feel ---
    // Movement (left stick) has no speed multiplier — only deadzone/calibration
    // below. Camera (right stick) sensitivity is a percentage: 50 is the
    // formula's baseline (1.0x), shipped default is 65 (a bit faster).
    int   cameraSensitivity = 65;    // percent; 50 = 1.0x multiplier (baseline)
    float moveDeadzone     = 0.15f;  // radial deadzone, left stick, 0..1
    float cameraDeadzone   = 0.15f;  // radial deadzone, right stick, 0..1
    // Outer calibration: if a worn/loose stick never quite reaches full
    // physical deflection, lower this below 100 (e.g. 90) so that deflection
    // gets treated as 100% instead of the Prince refusing to run at "full"
    // push. Percent, 0..100. 100 = off (use raw SDL range as-is).
    int   moveMaxRange     = 100;
    int   cameraMaxRange   = 100;
    float triggerThreshold = 0.20f;  // only used if triggers act as buttons
    // Cross-axis suppression: if one axis' magnitude is below this fraction of
    // the other's, snap it to zero. Fixes WW's binding-detection screen always
    // grabbing X due to mechanical stick tolerance leaking a hair of X while
    // pushing "pure" vertical. 0 disables. Doesn't affect true diagonal input.
    float axisSnapRatio    = 0.15f;

    // --- axis inversion ---
    bool invertMoveY   = false;
    bool invertCameraY = false;
    bool invertCameraX = false;

    // --- trigger -> Z axis direction ---
    // If false: RT -> Z(+), LT -> Z(-).  If true: swapped.
    bool swapTriggers = false;

    // --- axis routing (which DIJOYSTATE fields the sticks/triggers write) ---
    // Some games read the right stick on Z/Rz instead of Rx/Ry. Toggle if the
    // camera stick behaves wrong.
    //   cameraOnZRz=false: right stick -> lRx/lRy, triggers -> lZ
    //   cameraOnZRz=true : right stick -> lZ/lRz,  triggers -> lRx (combined)
    bool cameraOnZRz = false;

    // --- VID/PID spoof: present a consistent controller identity to the game
    // regardless of which real pad is plugged in. This is NOT what fixes the
    // axis-detection bug (AxisSnapRatio above is) — it solves a different
    // annoyance: WW keys its saved bindings (Profile.DAT) to device VID/PID,
    // so without this, switching real controllers resets your bindings every
    // time. With a consistent spoofed identity, bindings persist across pads.
    bool spoofVidPid = false;
    int  spoofVID    = 0x045e;   // Microsoft
    int  spoofPID    = 0x0007;   // matches an actual Gamepads.DAT entry

    // --- button map: rgbButtons index (0..31) for each SDL button ---
    // Values are WW "Btn N" minus 1. -1 = unmapped.
    int btnA  = 0;   // Btn1  Roll/Jump
    int btnB  = 1;   // Btn2  Pickup/Climb
    int btnX  = 2;   // Btn3  Sword Attack
    int btnY  = 3;   // Btn4  Grab/2nd weapon
    int btnLB = 4;   // Btn5  Rewind/Slowdown
    int btnRB = 5;   // Btn6  Walling/Block
    int btnStart = 7;   // Btn8  Start
    int btnBack  = 11;  // Btn12 Navigation Map
    int btnLS = 8;   // Btn9  (free / Walk toggle)
    int btnRS = 9;   // Btn10 Reset Camera
};

// Global config instance.
extern Config g_cfg;

// Load PadWithin.ini from the DLL's own directory. Missing file / keys keep
// defaults. Safe to call again to reload.
void Config_Load(const char* iniName);
