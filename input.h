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
