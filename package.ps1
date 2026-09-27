param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not $SkipBuild) { & ./build.ps1 }
$stage = Join-Path $PSScriptRoot '.build/release/ScarletAim'
$archive = Join-Path $PSScriptRoot 'release/ScarletAim-1.0.0-win64.zip'
New-Item -ItemType Directory -Force $stage | Out-Null
New-Item -ItemType Directory -Force (Join-Path $stage 'bin') | Out-Null
New-Item -ItemType Directory -Force (Split-Path $archive) | Out-Null
Copy-Item -LiteralPath 'bin/ScarletAim.exe','bin/ScarletAim.dll','bin/glow.bundle' -Destination (Join-Path $stage 'bin') -Force
Copy-Item -LiteralPath 'Uruchom.cmd','README.md','THIRD_PARTY.md' -Destination $stage -Force
Copy-Item -LiteralPath 'bin/licenses' -Destination (Join-Path $stage 'bin') -Recurse -Force
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -Path $stage -DestinationPath $archive -CompressionLevel Optimal
Write-Output $archive
