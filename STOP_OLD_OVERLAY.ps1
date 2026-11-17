param([int[]]$ProcessId = @())

$ErrorActionPreference = 'Stop'
$projectDirectory = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
function Test-ProjectProcess($Candidate) {
    try {
        return [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Candidate.Path)).TrimEnd('\') -ieq $projectDirectory
    } catch { return $false }
}

# Only this application's known executable names; never stop CS2 or a compiler.
$overlayNames = @('LJOverlay', 'LJOverlay_new', 'LJOverlay_update',
    'LJOverlay_before_menu', 'LJOverlay_previous', 'LJOverlay_separate_menu')

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class LJOverlayShutdown {
    private delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassNameW(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] private static extern bool PostMessageW(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    public static void RequestClose(uint target) {
        EnumWindows((window, parameter) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == target) {
                var name = new StringBuilder(256);
                GetClassNameW(window, name, name.Capacity);
                if (name.ToString() == "LJOverlayWnd")
                    PostMessageW(window, 0x0010, IntPtr.Zero, IntPtr.Zero);
            }
            return true;
        }, IntPtr.Zero);
    }
}
'@

function Get-OldOverlays {
    @(Get-Process -Name $overlayNames -ErrorAction SilentlyContinue |
        Where-Object { ($ProcessId.Count -eq 0 -or $_.Id -in $ProcessId) -and (Test-ProjectProcess $_) })
}

$targets = Get-OldOverlays
if ($targets.Count -eq 0) {
    Write-Output '[STOP] No old LJOverlay processes.'
    exit 0
}

foreach ($target in $targets) {
    Write-Output "[STOP] Closing $($target.ProcessName), PID $($target.Id)..."
    [LJOverlayShutdown]::RequestClose($target.Id)
    try { [void]$target.CloseMainWindow() } catch { }
}

# First allow WM_CLOSE to save the session CSV and release the instance mutex.
$deadline = [DateTime]::UtcNow.AddSeconds(3)
do {
    $remaining = Get-OldOverlays
    if ($remaining.Count -eq 0) { break }
    Start-Sleep -Milliseconds 100
} while ([DateTime]::UtcNow -lt $deadline)

foreach ($target in $remaining) {
    try {
        # Check the identity again rather than terminating a potentially reused PID.
        $current = Get-Process -Id $target.Id -ErrorAction SilentlyContinue
        if (-not $current) { continue }
        if ($current.ProcessName -notin $overlayNames -or $current.StartTime -ne $target.StartTime -or -not (Test-ProjectProcess $current)) {
            throw "PID $($target.Id) changed identity; refusing termination."
        }
        Write-Output "[STOP] Graceful close did not finish; stopping PID $($target.Id)."
        Stop-Process -Id $target.Id -Force -ErrorAction Stop
        [void]$target.WaitForExit(2000)
    } catch {
        Write-Output "[STOP] ERROR PID $($target.Id): $($_.Exception.Message)"
    }
}

$remaining = Get-OldOverlays
if ($remaining.Count -ne 0) {
    Write-Output "[STOP] ERROR: LJOverlay still running, PID(s): $(($remaining.Id) -join ', ')."
    Write-Output '[STOP] Windows did not allow this session to close the process.'
    Write-Output '[STOP] Run BUILD_EXE.bat with Run as administrator; it will close the old overlay automatically.'
    Write-Output '[STOP] Build stopped before compilation. The existing EXE has not been replaced.'
    exit 7
}

Write-Output '[STOP] All selected old LJOverlay processes have exited.'
exit 0
