# Loads the MSVC x64 compiler environment into the current PowerShell session,
# so cmake/ninja can find cl.exe. Dot-source it once per terminal:
#
#     . .\scripts\dev-shell.ps1
#
# (Not needed inside a "Developer PowerShell for VS" or "x64 Native Tools" prompt.)

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: install Visual Studio Build Tools" }

$vs = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "No Visual Studio installation with the MSVC x64 toolset was found" }

$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
# vcvars64.bat itself calls vswhere by name, so its folder must be on PATH for that call.
$env:Path = "$(Split-Path $vswhere);$env:Path"
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

# vcvars64.bat points VCPKG_ROOT at Visual Studio's bundled vcpkg. The project pins its
# own vcpkg (vcpkg.json "builtin-baseline"), so the user-level setting must win.
$userVcpkg = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
if ($userVcpkg) { $env:VCPKG_ROOT = $userVcpkg }
if (-not $env:VCPKG_ROOT) { Write-Warning "VCPKG_ROOT is not set; the CMake presets need it" }

Write-Host "MSVC x64 environment loaded from $vs"
