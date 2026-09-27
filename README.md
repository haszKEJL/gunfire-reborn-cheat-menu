# ScarletAim

A C++ assist for Gunfire Reborn on Steam by haszKEJL.

It includes an aimbot with Legit and Silent modes, enemy and item ESP, model glow, a triggerbot, and combat controls. The menu uses Dear ImGui and DirectX 11. English, Polish (without diacritics), and Russian are available; settings are saved between sessions.

## Download and run

Download the Windows x64 ZIP from [Releases](https://github.com/haszKEJL/gunfire-reborn-cheat-menu/releases), extract it, start Gunfire Reborn in windowed or borderless mode, then run `Uruchom.cmd`. No compiler or Python is required.

| Default key | Action |
| --- | --- |
| Insert | Open or close the menu |
| F9 | Disable all features |
| End | Exit and restore hooks and materials |

## Compatibility

Windows x64, DirectX 11, and the Steam version of Gunfire Reborn. The runtime resolves game methods by name and signature instead of fixed offsets; a game update that changes those methods may still require an update to ScarletAim. Manual mapping is the only loading method.

The current release was built for Steam build 25361614. Some weapons and character skills may use unsupported attack paths. Hidden portals can appear in Item ESP only after the client has created their objects. The new portal, speed, and skill paths still need live gameplay validation.

## Build

Requires Windows x64, C++17, and MinGW-w64 GCC at `C:\msys64\ucrt64\bin\g++.exe`. Dear ImGui 1.91.9b and MinHook 1.3.4 are included in `vendor/` with their licenses.

```powershell
.\build.ps1
.\package.ps1 -SkipBuild
```

The outputs are `bin/ScarletAim.exe` and `bin/ScarletAim.dll`. For model glow, copy `glow.bundle` from the release ZIP or generate it from your own game installation with `scripts/prepare_glow.py` and UnityPy. See [THIRD_PARTY.md](THIRD_PARTY.md) for dependency credits.
