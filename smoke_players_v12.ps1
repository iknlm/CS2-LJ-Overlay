$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PlayersSmoke {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,IntPtr title);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$testDir=Join-Path $PSScriptRoot ('players_exe_smoke_'+[DateTime]::Now.ToString('yyyyMMdd_HHmmss_fff'))
New-Item -ItemType Directory -Path $testDir | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LJOverlay_new.exe') -Destination $testDir
$testLog=Join-Path $testDir 'console.log'
$utf8=[Text.UTF8Encoding]::new($false)
$data="11/17 01:00:01 anonimMizantr0p jumped 240.54033 units with a Long Jump`r`nVNL | 2 Strafes | 80% AvgSync`r`n11/17 01:00:02 BlackMiddle jumped 291.73 units with a Multi Bunnyhop [CKZ]`r`nCKZ | 2 Strafes | 80% AvgSync`r`n11/17 01:00:03 Игрок_猫 ★ jumped 240.54033 units with a Long Jump [KZT]`r`nKZT | 2 Strafes`r`n11/17 01:00:04 JS | BH: 255.9 | VNL`r`n"
[IO.File]::WriteAllText($testLog,$data,$utf8)
[IO.File]::WriteAllText((Join-Path $testDir 'lj_overlay.ini'),"[overlay]`r`nlog=$testLog`r`nstyle=modern`r`nscale=0.8`r`n[sound]`r`nenabled=0`r`n",[Text.Encoding]::Unicode)
function StartOverlay {
 $p=Start-Process -FilePath (Join-Path $testDir 'LJOverlay_new.exe') -WorkingDirectory $testDir -WindowStyle Hidden -PassThru
 for($n=0;$n -lt 100;$n++) {
  Start-Sleep -Milliseconds 100
  $h=[PlayersSmoke]::FindWindow('LJOverlayWnd',[IntPtr]::Zero)
  if($h -ne [IntPtr]::Zero) { $ownerId=[uint32]0;[void][PlayersSmoke]::GetWindowThreadProcessId($h,[ref]$ownerId);if($ownerId -eq $p.Id){return @{Process=$p;Window=$h}} }
  if($p.HasExited){throw 'EXE unexpectedly exited'}
 }
 throw 'HUD not created'
}
function StopOverlay($i) {
 [void][PlayersSmoke]::PostMessage($i.Window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 if(!$i.Process.WaitForExit(5000) -or $i.Process.ExitCode -ne 0){throw 'Unclean exit'}
}
function Rows {
 $file=Get-ChildItem -LiteralPath (Join-Path $testDir 'lj_jumps') -Filter '*.csv' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
 return @(Import-Csv -LiteralPath $file.FullName -Delimiter ';' -Encoding UTF8)
}
function WaitRows([int]$count) {
 for($n=0;$n -lt 100;$n++) {try {if((Rows).Count -eq $count){return}}catch{};Start-Sleep -Milliseconds 100}
 throw "Expected $count rows"
}
$i=StartOverlay
try {WaitRows 4}finally{StopOverlay $i}
$first=Rows
if($first[0].ник -ne 'anonimMizantr0p' -or $first[1].ник -ne 'BlackMiddle' -or $first[1].тип -ne 'MBH' -or $first[1].Mode -ne 'CKZ' -or $first[2].ник -ne 'Игрок_猫 ★' -or $first[3].ник -ne 'Неизвестный игрок'){throw 'Wrong player mapping in actual EXE CSV'}
$ids=@($first | ForEach-Object {$_.attempt_id}) -join ','
$i=StartOverlay
try {Start-Sleep -Milliseconds 800}finally{StopOverlay $i}
$second=Rows
if($second.Count -ne 4 -or (@($second | ForEach-Object {$_.attempt_id}) -join ',') -ne $ids){throw 'IDs changed or attempts duplicated after restart'}
# Only the isolated test log is truncated; stored history must survive rotation.
[IO.File]::WriteAllText($testLog,"11/17 01:00:05 NewPlayer jumped 240.54033 units with a Long Jump [VNL]`r`nVNL | 2 Strafes`r`n",$utf8)
$i=StartOverlay
try {WaitRows 5}finally{StopOverlay $i}
$third=Rows
if($third.Count -ne 5 -or $third[4].ник -ne 'NewPlayer' -or (@($third[0..3] | ForEach-Object {$_.attempt_id}) -join ',') -ne $ids){throw 'Snapshot history lost or wrong new player after log rotation'}
'PASS final Windows x64 EXE: exact named LJ/MBH, suffix CKZ/KZT, equal distances/different players, Unicode, anonymous unknown, CSV names + IDs, two restarts with stable IDs/no duplicates, rotated log retains four old attempts and adds correct fifth player, LJOverlayWnd, clean exit 0.' | Tee-Object -FilePath (Join-Path $PSScriptRoot 'PLAYERS_EXE_SMOKE_LOG.txt')
"Artifacts: $testDir" | Add-Content -LiteralPath (Join-Path $PSScriptRoot 'PLAYERS_EXE_SMOKE_LOG.txt')
