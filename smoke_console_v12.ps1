$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ConsoleSmoke {
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, IntPtr title);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint id);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
'@
$testDir = Join-Path $PSScriptRoot 'v12_console_smoke'
New-Item -ItemType Directory -Force $testDir | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LJOverlay_new.exe') -Destination $testDir
$testLog = Join-Path $testDir 'console.log'
$utf8 = [Text.UTF8Encoding]::new($false)
function JumpBlock([string]$time, [string]$distance) {
    return "11/17 $time JS jumped $distance units with a Long Jump`r`nVNL | 2 Strafes | 80% AvgSync | 250 Pre | 290 Max`r`n#. Sync Gain Loss`r`n1. 85 10 -0.1`r`n2. 75 8 -0.2`r`n"
}
$oldA = JumpBlock '01:00:01' '240.42000'
$oldB = JumpBlock '01:00:02' '239.96712'
[IO.File]::WriteAllText($testLog, $oldA + $oldB + $oldA + "LJMARK QUIT`r`nLJMARK BAD`r`n", $utf8)
$ini = "[overlay]`r`nlog=$testLog`r`nstyle=modern`r`nscale=0.8`r`n[sound]`r`nenabled=0`r`n"
[IO.File]::WriteAllText((Join-Path $testDir 'lj_overlay.ini'), $ini, [Text.Encoding]::Unicode)
function WaitRows([int]$count) {
    for ($n = 0; $n -lt 100; $n++) {
        $file = Get-ChildItem -LiteralPath (Join-Path $testDir 'lj_jumps') -Filter '*.csv' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($file) {
            try { $rows = [IO.File]::ReadAllLines($file.FullName); if ($rows.Count -eq $count + 1) { return } } catch {}
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Expected $count jump rows in actual EXE CSV"
}
function StartOverlay {
    $process = Start-Process -FilePath (Join-Path $testDir 'LJOverlay_new.exe') -WorkingDirectory $testDir -WindowStyle Hidden -PassThru
    for ($n = 0; $n -lt 100; $n++) {
        Start-Sleep -Milliseconds 100
        $window = [ConsoleSmoke]::FindWindow('LJOverlayWnd', [IntPtr]::Zero)
        if ($window -ne [IntPtr]::Zero) {
            $ownerId = [uint32]0; [void][ConsoleSmoke]::GetWindowThreadProcessId($window, [ref]$ownerId)
            if ($ownerId -eq $process.Id) { return @{ Process=$process; Window=$window } }
        }
        if ($process.HasExited) { throw 'Overlay exited unexpectedly' }
    }
    throw 'No HUD owned by test EXE'
}
function StopOverlay($instance) {
    [void][ConsoleSmoke]::PostMessage($instance.Window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (!$instance.Process.WaitForExit(5000) -or $instance.Process.ExitCode -ne 0) { throw 'Unclean EXE exit' }
}
$instance = StartOverlay
try {
    WaitRows 2
    [IO.File]::AppendAllText($testLog, '11/17 01:00:03 JS jumped 244.', $utf8)
    Start-Sleep -Milliseconds 300
    WaitRows 2
    [IO.File]::AppendAllText($testLog, "80000 units with a Long Jump`r`nVNL | 2 Strafes | 80% AvgSync | 250 Pre`r`n", $utf8)
    WaitRows 3
} finally { StopOverlay $instance }
$instance = StartOverlay
try { WaitRows 3 } finally { StopOverlay $instance }
'PASS actual final Windows x64 EXE: startup imports old history once; duplicate stamped block skipped; incomplete new line held; live append imported; restart has 3 rows, not duplicates; LJOverlayWnd and clean exit 0.' | Tee-Object -FilePath (Join-Path $PSScriptRoot 'CONSOLE_EXE_SMOKE_LOG.txt')
