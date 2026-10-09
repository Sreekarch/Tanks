// input.cpp — keyboard/mouse implementation of InputSource.
#include "input.h"

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
