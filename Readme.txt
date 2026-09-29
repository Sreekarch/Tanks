TANKSHOOTER v0.1 — cover-based tank shooter prototype
====================================================

BUILD (Windows, macOS, Linux — same commands everywhere):

    cmake -B build
    cmake --build build

The game binary lands at build/tankshooter (build/tankshooter.exe on
Windows). raylib 5.5 is downloaded and built automatically the first
time you configure — no manual dependency setup.

CONTROLS (v0.1):

    W / S ......... drive forward / reverse
    A / D ......... turn hull left / right
    Mouse ......... aim turret (gunner view) / look (drone view)
    TAB ........... toggle drone view <-> gunner view
    WASD (drone) .. fly the drone
    Q / E (drone) . descend / ascend
    ESC ........... quit

WHAT'S IN v0.1:
- Drivable player tank (hull + rotating turret, code-built meshes)
- Broken-down village arena: buildings, ruined walls, rubble (collision)
- Two camera modes: first-person gunner and free-fly drone (TAB to switch)

ROADMAP:
- v0.2: shooting, projectiles, hit detection, destructible cover
- v0.3: HUD drone/gunner toggle button
- v0.4: enemy tanks (advance / seek cover / peek / fire AI)
- v0.5: HUD, win/lose states
- later: fog of war, more maps
