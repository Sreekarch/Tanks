#pragma once
// input.h — input is separated behind InputSource so a gamepad source
// can be added later without touching game logic. Game code reads one
// InputState per frame; only KeyboardMouseInput touches raylib input.

#include "raylib.h"

struct InputState {
    float throttle = 0.0f, steer = 0.0f;  // drive: W/S+Up/Down, A/D+Left/Right
    float lookDX = 0.0f, lookDY = 0.0f;   // mouse deltas (gunner turret+elevation)
    bool fire = false;                    // LMB held or Space held
    // Edge-triggered (true only on the frame pressed):
    bool toggleCam = false;               // TAB
    bool replay = false;                  // R
    bool confirm = false;                 // Enter
    bool cancel = false;                  // ESC
    bool toMap = false;                   // M in setup
    int selectAlly = 0;                   // 0=none, else 1..9 (digit pressed)
    bool orderFollow = false;             // F
    bool orderHold = false;               // H
    bool orderMove = false;               // RMB pressed (with mouseGroundValid)
    // Drone camera (keyboard for now):
    float droneYaw = 0.0f, dronePitch = 0.0f;              // -1..1 from arrows
    float droneFwd = 0.0f, droneStrafe = 0.0f, droneUp = 0.0f;  // WASD/QE
    bool droneFast = false;               // shift
};

class InputSource {
public:
    virtual ~InputSource() = default;
    virtual InputState poll() = 0;
};

class KeyboardMouseInput : public InputSource {
public:
    InputState poll() override;
private:
    static bool edge(int key, bool &prev);
    static bool edgeMouse(int btn, bool &prev);
    bool prevTab = false, prevR = false, prevEnter = false, prevM = false;
    bool prevF = false, prevH = false, prevEsc = false, prevRMB = false;
    bool prevDigit[9] = {};
    int pollCount = 0;
};

// Gamepad (Xbox-style layout): left stick drives, right stick aims the
// turret, RT fires, face buttons mirror Enter/ESC/R/TAB. Returns an
// empty InputState when no gamepad is connected, so it can be polled
// and merged every frame next to KeyboardMouseInput at no cost.
class GamepadInput : public InputSource {
public:
    explicit GamepadInput(int index = 0) : gamepad(index) {}
    InputState poll() override;
    // Name of the active pad (raylib's reported name), or nullptr.
    const char *activeName() const;
    // Stick sensitivities, set from Config at startup.
    float driveSens = 0.6f;  // left stick scale (throttle/steer, drone move)
    float aimSens = 10.0f;   // right stick scale (mouse-pixels per frame)
private:
    // Preferred slot if live, else the first connected pad 0..3, else -1.
    int findPad() const;
    int gamepad = 0;
    int allySel = 1;  // D-pad left/right cycles the ally selection 1..9
};

// Overlay `over` onto `base`: analog fields take the larger deflection,
// buttons/edges OR together, look deltas add. Lets keyboard+mouse and
// gamepad drive the same InputState.
void MergeInput(InputState &base, const InputState &over);
