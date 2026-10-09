// input.cpp — keyboard/mouse and gamepad implementations of InputSource.
#include "input.h"

#include <cmath>

InputState KeyboardMouseInput::poll() {
        InputState in;
        bool w = IsKeyDown(KEY_W), a = IsKeyDown(KEY_A);
        bool s = IsKeyDown(KEY_S), d = IsKeyDown(KEY_D);
        bool up = IsKeyDown(KEY_UP), down = IsKeyDown(KEY_DOWN);
        bool left = IsKeyDown(KEY_LEFT), right = IsKeyDown(KEY_RIGHT);
        in.throttle = ((w || up) ? 1.0f : 0.0f) - ((s || down) ? 1.0f : 0.0f);
        in.steer    = ((d || right) ? 1.0f : 0.0f) - ((a || left) ? 1.0f : 0.0f);
        Vector2 md = GetMouseDelta();
        // Discard deltas from the first few frames: hiding/capturing the
        // cursor can warp the pointer and inject one bogus jump (it would
        // otherwise yaw the gun on launch).
        if (pollCount++ < 5) md = Vector2{ 0.0f, 0.0f };
        in.lookDX = md.x;
        in.lookDY = md.y;
        in.fire = IsMouseButtonDown(MOUSE_LEFT_BUTTON) || IsKeyDown(KEY_SPACE);
        // Edge-triggered keys (pressed && !was-down-last-poll).
        bool tab = IsKeyDown(KEY_TAB);
        in.toggleCam = tab && !prevTab; prevTab = tab;
        in.replay = edge(KEY_R, prevR);
        in.confirm = edge(KEY_ENTER, prevEnter);
        in.cancel = edge(KEY_ESCAPE, prevEsc);
        in.toMap = edge(KEY_M, prevM);
        in.orderFollow = edge(KEY_F, prevF);
        in.orderHold = edge(KEY_H, prevH);
        in.orderMove = edgeMouse(MOUSE_BUTTON_RIGHT, prevRMB);
        in.selectAlly = 0;
        for (int i = 0; i < 9; ++i) {
            if (edge(KEY_ONE + i, prevDigit[i])) { in.selectAlly = i + 1; break; }
        }
        // Drone camera keys.
        in.droneYaw    = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
        in.dronePitch  = (down ? 1.0f : 0.0f) - (up ? 1.0f : 0.0f);
        in.droneFwd    = (w ? 1.0f : 0.0f) - (s ? 1.0f : 0.0f);
        in.droneStrafe = (d ? 1.0f : 0.0f) - (a ? 1.0f : 0.0f);
        in.droneUp     = (IsKeyDown(KEY_E) ? 1.0f : 0.0f) -
                         (IsKeyDown(KEY_Q) ? 1.0f : 0.0f);
        in.droneFast = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        return in;
    }

bool KeyboardMouseInput::edge(int key, bool &prev) {
        bool down = IsKeyDown(key);
        bool hit = down && !prev;
        prev = down;
        return hit;
    }

bool KeyboardMouseInput::edgeMouse(int btn, bool &prev) {
        bool down = IsMouseButtonDown(btn);
        bool hit = down && !prev;
        prev = down;
        return hit;
    }

// ---------------------------------------------------------------------------
// Gamepad: Xbox-style layout. Sticks fill the same fields the keyboard
// does (drive + drone), so game logic can't tell the sources apart.
// ---------------------------------------------------------------------------

InputState GamepadInput::poll() {
    InputState in;
    int pad = findPad();
    if (pad < 0) return in;
    auto axis = [pad](int a) {
        float v = GetGamepadAxisMovement(pad, a);
        return (fabsf(v) < 0.18f) ? 0.0f : v;  // deadzone
    };
    float lx = axis(GAMEPAD_AXIS_LEFT_X), ly = axis(GAMEPAD_AXIS_LEFT_Y);
    float rx = axis(GAMEPAD_AXIS_RIGHT_X), ry = axis(GAMEPAD_AXIS_RIGHT_Y);
    // Driving: left stick (stick-up reads -1 -> throttle +1), scaled by
    // the configured drive sensitivity (below 1 = gentler, slower tank).
    in.throttle = -ly * driveSens;
    in.steer = lx * driveSens;
    // Turret: right stick, scaled to mouse-pixel equivalents so the
    // game's TURRET_SENS applies unchanged.
    in.lookDX = rx * aimSens;
    in.lookDY = ry * aimSens;
    // Fire: RT as a button, or the analog trigger pulled past a third.
    in.fire = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) ||
              GetGamepadAxisMovement(pad, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.35f;
    // Face buttons (raylib's Pressed is already edge-triggered).
    in.toggleCam = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP);     // Y
    in.confirm   = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);   // A
    in.cancel    = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);  // B
    in.replay    = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);   // X
    in.toMap     = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_MIDDLE_LEFT);       // Back
    // Orders on the D-pad; left/right cycles the selected ally 1..9
    // (the game clamps to the number of allies alive).
    in.orderHold   = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_FACE_UP);
    in.orderFollow = IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) {
        allySel = allySel % 9 + 1;
        in.selectAlly = allySel;
    }
    if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) {
        allySel = (allySel + 7) % 9 + 1;
        in.selectAlly = allySel;
    }
    // Drone camera mirrors the sticks; bumpers climb/sink, L3 = fast.
    in.droneYaw = rx;
    in.dronePitch = ry;
    in.droneFwd = -ly * driveSens;
    in.droneStrafe = lx * driveSens;
    in.droneUp = (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) ? 1.0f : 0.0f) -
                 (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1) ? 1.0f : 0.0f);
    in.droneFast = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_THUMB);
    return in;
}

int GamepadInput::findPad() const {
    if (IsGamepadAvailable(gamepad)) return gamepad;
    for (int i = 0; i < 4; ++i) {
        if (IsGamepadAvailable(i)) return i;
    }
    return -1;
}

const char *GamepadInput::activeName() const {
    int pad = findPad();
    return (pad >= 0) ? GetGamepadName(pad) : nullptr;
}

void MergeInput(InputState &base, const InputState &over) {
    auto pick = [](float a, float b) { return fabsf(b) > fabsf(a) ? b : a; };
    base.throttle = pick(base.throttle, over.throttle);
    base.steer = pick(base.steer, over.steer);
    base.lookDX += over.lookDX;  // mouse and stick can both nudge the gun
    base.lookDY += over.lookDY;
    base.fire = base.fire || over.fire;
    base.toggleCam = base.toggleCam || over.toggleCam;
    base.replay = base.replay || over.replay;
    base.confirm = base.confirm || over.confirm;
    base.cancel = base.cancel || over.cancel;
    base.toMap = base.toMap || over.toMap;
    if (over.selectAlly != 0) base.selectAlly = over.selectAlly;
    base.orderFollow = base.orderFollow || over.orderFollow;
    base.orderHold = base.orderHold || over.orderHold;
    base.orderMove = base.orderMove || over.orderMove;
    base.droneYaw = pick(base.droneYaw, over.droneYaw);
    base.dronePitch = pick(base.dronePitch, over.dronePitch);
    base.droneFwd = pick(base.droneFwd, over.droneFwd);
    base.droneStrafe = pick(base.droneStrafe, over.droneStrafe);
    base.droneUp = pick(base.droneUp, over.droneUp);
    base.droneFast = base.droneFast || over.droneFast;
}
