param([string]$Root=$PSScriptRoot)
$ErrorActionPreference='Stop'
if(@(Get-Process -Name LJOverlay,LJOverlay_new,LJOverlay_update -ErrorAction SilentlyContinue).Count){throw 'Close other overlays before runtime testing; no existing overlay will be stopped.'}
$Work=Join-Path $Root ('build\runtime_'+[DateTime]::Now.ToString('yyyyMMdd_HHmmss_fff'))
New-Item -ItemType Directory -Path $Work -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $Root 'LJOverlay_new.exe') -Destination $Work
Copy-Item -LiteralPath (Join-Path $Root 'sounds') -Destination $Work -Recurse
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ReleaseRuntime {
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left,top,right,bottom; }
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,IntPtr title);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder t,int n);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string s,string k,string d,StringBuilder b,uint n,string p);
}
'@
$results=[Collections.Generic.List[string]]::new()
$nl=[Environment]::NewLine
$log=Join-Path $Work 'console.log'
$ini=Join-Path $Work 'lj_overlay.ini'
$data=@('11/17 01:00:01 anonimMizantr0p jumped 240.54033 units with a Long Jump','VNL | 2 Strafes | 80% AvgSync | 250 Pre','11/17 01:00:02 BlackMiddle jumped 291.73 units with a Multi Bunnyhop [CKZ]','CKZ | 2 Strafes | 80% AvgSync | 250 Pre','') -join $nl
[IO.File]::WriteAllText($log,$data,[Text.UTF8Encoding]::new($false))
$settings=@('[overlay]',"log=$log",'style=modern','scale=0.8','opacity=250','[sound]','enabled=1','master=30','menu=40','awards=50','') -join $nl
[IO.File]::WriteAllText($ini,$settings,[Text.Encoding]::Unicode)
function IniValue([string]$section,[string]$key) {
 $b=[Text.StringBuilder]::new(1024);[void][ReleaseRuntime]::GetPrivateProfileString($section,$key,'',$b,1024,$ini);return $b.ToString()
}
function Title($i){$b=[Text.StringBuilder]::new(256);[void][ReleaseRuntime]::GetWindowText($i.Window,$b,256);return $b.ToString()}
function StartOverlay {
 $start=[Diagnostics.ProcessStartInfo]::new()
 $start.FileName=Join-Path $Work 'LJOverlay_new.exe';$start.WorkingDirectory=$Work
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
 $start.EnvironmentVariables['PATH']=$env:SystemRoot+'\System32;'+$env:SystemRoot
 $start.EnvironmentVariables.Remove('MSYSTEM');$start.EnvironmentVariables.Remove('MINGW_PREFIX')
 $proc=[Diagnostics.Process]::Start($start)
 for($n=0;$n -lt 100;$n++){
  Start-Sleep -Milliseconds 100
  if($proc.HasExited){throw ('EXE exited during startup: '+$proc.ExitCode)}
  $h=[ReleaseRuntime]::FindWindow('LJOverlayWnd',[IntPtr]::Zero)
  if($h -ne [IntPtr]::Zero){$ownerId=[uint32]0;[void][ReleaseRuntime]::GetWindowThreadProcessId($h,[ref]$ownerId);if($ownerId -eq $proc.Id){return @{Process=$proc;Window=$h}}}
 }
 throw 'No HUD created'
}
function StopOverlay($i) {
 [void][ReleaseRuntime]::PostMessage($i.Window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 if(!$i.Process.WaitForExit(5000) -or $i.Process.ExitCode -ne 0){throw 'Unclean EXE exit'}
}
function Rows {
 $csv=Get-ChildItem -LiteralPath (Join-Path $Work 'lj_jumps') -Filter '*.csv' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
 if(!$csv){return @()};return @(Import-Csv -LiteralPath $csv.FullName -Delimiter ';' -Encoding UTF8)
}
function WaitRows([int]$count) {
 for($n=0;$n -lt 100;$n++){if((Rows).Count -eq $count){return};Start-Sleep -Milliseconds 100};throw ('Expected '+$count+' CSV rows')
}
function Snapshot($i,[string]$name) {
 $rect=[ReleaseRuntime+RECT]::new();[void][ReleaseRuntime]::GetClientRect($i.Window,[ref]$rect)
 if($rect.right -le 0 -or $rect.bottom -le 0){throw 'Invalid HUD dimensions'}
 $bmp=[Drawing.Bitmap]::new($rect.right,$rect.bottom);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc()
 try {if(![ReleaseRuntime]::PrintWindow($i.Window,$dc,1)){throw 'PrintWindow failed'}}finally{$g.ReleaseHdc($dc);$g.Dispose()}
 try {$bmp.Save((Join-Path $Work $name),[Drawing.Imaging.ImageFormat]::Png)}finally{$bmp.Dispose()}
}
$i=$null
try {
 $i=StartOverlay;WaitRows 2
 if((Title $i) -notmatch 'Modern'){throw 'Modern theme not active'}
 $values=@((Rows)[0].PSObject.Properties | Select-Object -ExpandProperty Value)
 $other=@((Rows)[1].PSObject.Properties | Select-Object -ExpandProperty Value)
 if($values[2] -ne 'anonimMizantr0p' -or $other[2] -ne 'BlackMiddle' -or $other[3] -ne 'MBH' -or $other[5] -ne 'CKZ'){throw 'Wrong player/type/mode in actual EXE CSV'}
 $modules=@($i.Process.Modules)
 $ambient=[Collections.Generic.List[string]]::new()
 foreach($module in $modules){
  if($module.ModuleName -ieq 'LJOverlay_new.exe'){continue}
  if($module.ModuleName -match '^(libgcc|libstdc|libwinpthread|msys-)'){throw ('Compiler runtime loaded: '+$module.ModuleName)}
  if(!$module.FileName.StartsWith($env:SystemRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$ambient.Add($module.ModuleName)}
 }
 if($ambient.Count){$results.Add('Host-injected ambient modules (checked separately from PE imports): '+($ambient -join ', '))}
 if(@($modules | Where-Object {$_.ModuleName -ieq 'winmm.dll'}).Count -ne 1){throw 'winmm.dll not loaded'}
 $results.Add('PASS native Windows x64 EXE starts with PATH restricted to Windows/System32; no MSYS2/compiler runtime DLL loaded. Required PE imports are validated separately; ambient host instrumentation is listed above.')
 $results.Add('PASS initial console.log import: exact LJ/MBH names, modes and CSV.')
 Snapshot $i 'runtime_modern.png'
 [void][ReleaseRuntime]::PostMessage($i.Window,0x800B,[IntPtr]::Zero,[IntPtr]::Zero);Start-Sleep -Milliseconds 150
 [void][ReleaseRuntime]::PostMessage($i.Window,0x100,[IntPtr]49,[IntPtr]::Zero)
 for($n=0;$n -lt 30;$n++){if((IniValue 'overlay' 'style') -eq 'cs16'){break};Start-Sleep -Milliseconds 100}
 if((IniValue 'overlay' 'style') -ne 'cs16' -or (Title $i) -notmatch 'CS 1.6'){throw 'Theme switching/persistence failed'}
 [void][ReleaseRuntime]::PostMessage($i.Window,0x100,[IntPtr]51,[IntPtr]::Zero);Start-Sleep -Milliseconds 150
 if((IniValue 'overlay' 'opacity') -ne '255'){throw 'INI setting save failed'}
 [void][ReleaseRuntime]::PostMessage($i.Window,0x800B,[IntPtr]::Zero,[IntPtr]::Zero);Start-Sleep -Milliseconds 150
 Snapshot $i 'runtime_cs16.png'
 $results.Add('PASS actual Modern and CS 1.6 HWND rendering, inline theme switching and INI opacity/style persistence.')
 [IO.File]::AppendAllText($log,(@('11/17 01:00:03 PlayerLive jumped 249.9 units with a Long Jump [VNL]','VNL | 2 Strafes | 85% AvgSync','') -join $nl),[Text.UTF8Encoding]::new($false));WaitRows 3
 StopOverlay $i;$i=$null
 $rows=Rows;$idIndex=@($rows[0].PSObject.Properties.Name).IndexOf('attempt_id')
 if($idIndex -lt 0){throw 'CSV attempt_id missing'}
 $ids=@($rows | ForEach-Object { @($_.PSObject.Properties.Value)[$idIndex] }) -join ','
 $i=StartOverlay;Start-Sleep -Milliseconds 500
 if((Title $i) -notmatch 'CS 1.6' -or (IniValue 'overlay' 'opacity') -ne '255'){throw 'Settings not restored on restart'}
 if((IniValue 'sound' 'enabled') -ne '1' -or (IniValue 'sound' 'master') -ne '30'){throw 'Sound settings not restored'}
 StopOverlay $i;$i=$null;$rows=Rows
 if($rows.Count -ne 3 -or (@($rows | ForEach-Object {@($_.PSObject.Properties.Value)[$idIndex]}) -join ',') -ne $ids){throw 'Restart duplicated attempts or changed IDs'}
 $results.Add('PASS live append, persistence, restart with same IDs/no duplicates, INI and sound settings reload.')
 $bare=Join-Path $Work 'sounds';$park=Join-Path $Work 'sounds_parked'
 if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($bare)) -ne [IO.Path]::GetFullPath($Work) -or [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($park)) -ne [IO.Path]::GetFullPath($Work)){throw 'Resource move escaped test directory'}
 Move-Item -LiteralPath $bare -Destination $park
 try {$i=StartOverlay;Start-Sleep -Milliseconds 300;StopOverlay $i;$i=$null}finally{Move-Item -LiteralPath $park -Destination $bare}
 $results.Add('PASS standalone EXE without sounds folder starts/exits cleanly; full audio requires the ZIP sounds/ resources.')
 $results.Add('PASS bundled sounds directory present; audio acceptance and PCM/settings checked by sound_test. Physical audibility is not claimed.')
 $results.Add('Host: '+[Environment]::OSVersion.VersionString+'; 64-bit OS='+[Environment]::Is64BitOperatingSystem)
} finally {
 if($i -and !$i.Process.HasExited){StopOverlay $i}
 $results | Set-Content -LiteralPath (Join-Path $Root 'reports\RUNTIME_RELEASE_TEST.txt') -Encoding UTF8
}
$results | Write-Output
