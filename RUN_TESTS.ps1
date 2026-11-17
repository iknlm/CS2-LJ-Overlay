param([string]$Compiler='')
$ErrorActionPreference='Stop'
$projectRoot=$PSScriptRoot
if(!$Compiler) {
 foreach($candidate in @('C:\msys64\ucrt64\bin\g++.exe','D:\msys64\ucrt64\bin\g++.exe',"$env:ProgramFiles\MSYS2\ucrt64\bin\g++.exe")) {
  if(Test-Path -LiteralPath $candidate){$Compiler=$candidate;break}
 }
}
if(!$Compiler -or !(Test-Path -LiteralPath $Compiler)){throw 'MSYS2 UCRT64 g++ not found; pass -Compiler with its absolute path.'}
$env:PATH=(Split-Path -Parent $Compiler)+';'+$env:PATH
$work=Join-Path $projectRoot ('build\tests_'+[DateTime]::Now.ToString('yyyyMMdd_HHmmss_fff'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
Get-ChildItem -LiteralPath $projectRoot -File | Where-Object {$_.Extension -in '.cpp','.h' -or $_.Name -eq 'sample_jump_console.log'} | Copy-Item -Destination $work
Copy-Item -LiteralPath (Join-Path $projectRoot 'sounds') -Destination $work -Recurse
$reportDir=Join-Path $projectRoot 'reports'
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
$report=Join-Path $reportDir 'TEST_RESULTS.txt'
$results=[Collections.Generic.List[string]]::new()
$results.Add('CS2 LJ Overlay v1.2 current regression suite; source copy; no production application startup.')
$results.Add('Historical v1.1 verification_test.cpp is retained in tests/legacy and excluded from this suite.')
$targets=@('analysis_test','review_model_test','analysis_ui_test','console_log_test','console_gui_test','goals_test','players_test','review_gui_test','sound_settings_test','sound_test')
Push-Location $work
try {
 foreach($target in $targets) {
  Write-Output "Building $target..."
  & $Compiler -std=c++20 -O2 -static -static-libgcc -static-libstdc++ "$target.cpp" -o "$target.exe" -lgdi32 -luser32 -lshell32 -ladvapi32 -lwinmm
  if($LASTEXITCODE -ne 0){throw "Compile failed: $target ($LASTEXITCODE)"}
  $output=& (Join-Path $work "$target.exe") 2>&1
  if($LASTEXITCODE -ne 0){$results.Add(($output|Out-String));throw "Test failed: $target ($LASTEXITCODE)"}
  $results.Add("PASS $target");$results.Add(($output|Out-String).Trim());Write-Output "PASS $target"
  if($target -eq 'sound_settings_test') {
   $output=& (Join-Path $work "$target.exe") 'reload' 2>&1
   if($LASTEXITCODE -ne 0){throw 'Fresh-process sound settings reload failed'}
   $results.Add(($output|Out-String).Trim())
  }
 }
 Write-Output 'Building parser-only target...'
 & $Compiler -std=c++20 -O2 -static -static-libgcc -static-libstdc++ -DLJ_TEST lj_overlay.cpp -o lj_test.exe
 if($LASTEXITCODE -ne 0){throw 'Parser-only compilation failed'}
 $output=& (Join-Path $work 'lj_test.exe') 'sample_jump_console.log' 2>&1
 if($LASTEXITCODE -ne 0){throw 'Parser-only fixture failed'}
 $results.Add('PASS LJ_TEST sample_jump_console.log');$results.Add(($output|Out-String).Trim())
 $results.Add('PASS all 10 current test executables, sound settings fresh-process reload, and parser-only fixture.')
 $results.Add('No test of physical CS2 interaction or speaker audibility. Sound test exercises actual PlaySoundW calls.')
} catch {
 $results.Add('FAIL: '+$_.Exception.Message)
 throw
} finally {
 Pop-Location
 $results | Set-Content -LiteralPath $report -Encoding UTF8
}
Write-Output ('PASS current suite. Report: '+$report)
