$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LJSmokeV12 {
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, IntPtr title);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint id);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@
$smokeDir = Join-Path $PSScriptRoot 'v12_smoke'
New-Item -ItemType Directory -Force $smokeDir | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LJOverlay_new.exe') -Destination $smokeDir
$proc = Start-Process -FilePath (Join-Path $smokeDir 'LJOverlay_new.exe') -WorkingDirectory $smokeDir -WindowStyle Hidden -PassThru
$window = [IntPtr]::Zero
for ($n = 0; $n -lt 250; $n++) {
    Start-Sleep -Milliseconds 100
    $window = [LJSmokeV12]::FindWindow('LJOverlayWnd', [IntPtr]::Zero)
    if ($window -ne [IntPtr]::Zero) {
        $windowOwner = [uint32]0
        [void][LJSmokeV12]::GetWindowThreadProcessId($window, [ref]$windowOwner)
        if ($windowOwner -eq $proc.Id) { break }
        $window = [IntPtr]::Zero
    }
    if ($proc.HasExited) { throw 'EXE exited before creating HUD' }
}
if ($window -eq [IntPtr]::Zero) { throw 'No LJOverlayWnd' }
$ownerId = [uint32]0
[void][LJSmokeV12]::GetWindowThreadProcessId($window, [ref]$ownerId)
if ($ownerId -ne $proc.Id) { throw 'HUD belongs to another process' }
[void][LJSmokeV12]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
if (!$proc.WaitForExit(5000)) { throw 'WM_CLOSE did not finish' }
if ($proc.ExitCode -ne 0) { throw "Exit code $($proc.ExitCode)" }
"PASS final Windows x64 EXE launched, owned LJOverlayWnd, WM_CLOSE exit 0; isolated INI; no CS2 process used." | Tee-Object -FilePath (Join-Path $PSScriptRoot 'V12_EXE_SMOKE_LOG.txt')
