param([switch]$RuntimeOnly, [string]$LauncherName = 'ScarletAim.exe')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force bin | Out-Null
New-Item -ItemType Directory -Force bin/licenses | Out-Null
Copy-Item -LiteralPath vendor/imgui/LICENSE.txt -Destination bin/licenses/ImGui.txt
Copy-Item -LiteralPath vendor/minhook/LICENSE.txt -Destination bin/licenses/MinHook.txt
$compiler = 'C:\msys64\ucrt64\bin\g++.exe'
& $compiler -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -Wno-misleading-indentation -static -shared src/aim.cpp vendor/minhook/src/buffer.c vendor/minhook/src/hook.c vendor/minhook/src/trampoline.c vendor/minhook/src/hde/hde64.c -o bin/ScarletAim.dll -luser32
if ($LASTEXITCODE -ne 0) { throw 'DLL build failed' }
if ($RuntimeOnly) { return }
& $compiler -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -Wno-misleading-indentation -static -Ivendor/imgui src/launcher.cpp src/overlay.cpp vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_tables.cpp vendor/imgui/imgui_widgets.cpp vendor/imgui/backends/imgui_impl_win32.cpp vendor/imgui/backends/imgui_impl_dx11.cpp -o (Join-Path bin $LauncherName) -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed' }
