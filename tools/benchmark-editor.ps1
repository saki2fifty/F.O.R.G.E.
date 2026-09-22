param([Parameter(Mandatory)][string]$Baseline, [Parameter(Mandatory)][string]$Current,
      [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'This benchmark requires an isolated GitHub Windows runner.' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ForgeWindowProbe {
 [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
}
'@
$Output = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force $Output | Out-Null
$preferences = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'FORGE/Editor'
$backup = Join-Path $Output 'original-preferences'
if (Test-Path $backup) { throw 'Benchmark output already contains a preferences backup.' }
$currentProvenance = Get-Content (Join-Path (Split-Path $Current) 'provenance.json') -Raw | ConvertFrom-Json
$baselineProvenance = Get-Content (Join-Path (Split-Path $Baseline) 'provenance.json') -Raw | ConvertFrom-Json
if ($currentProvenance.source -ne $env:FORGE_SOURCE_COMMIT -or $baselineProvenance.source -ne 'a98be9a672394d3d91c2b9067331d0252f9b4313') {
 throw 'Benchmark artifact source mismatch'
}
$samples = @()
if (Test-Path $preferences) { Move-Item $preferences $backup }
try {
 foreach ($workload in @('empty','cube')) {
  for ($repeat=0; $repeat -lt 3; ++$repeat) {
   # Alternate order to reduce systematic warm-cache / runner-load bias.
   $order = if ($repeat % 2 -eq 0) { @('baseline','current') } else { @('current','baseline') }
   foreach ($label in $order) {
    $exe = [IO.Path]::GetFullPath($(if ($label -eq 'baseline') { $Baseline } else { $Current }))
    $name = "$label-$workload-$repeat"
    $project = Join-Path $Output $name
    New-Item -ItemType Directory -Force "$project/Assets", $preferences | Out-Null
    if (Test-Path "$preferences/workspace.ini") { Remove-Item "$preferences/workspace.ini" }
    @{interface_scale=1.0;preview_lighting=$false;tooltips=$false} | ConvertTo-Json | Set-Content "$preferences/settings.json" -Encoding utf8
    $scene = @{version=3;asset_id=[guid]::NewGuid().ToString();entities=@()}
    if ($workload -eq 'cube') {
     $scene.entities = @(@{id=[guid]::NewGuid().ToString();name='Benchmark cube';components=@{
       'forge.local_translation'=@{x=0;y=1;z=0}; 'forge.local_rotation'=@{x=0;y=0;z=0;w=1};
       'forge.local_scale'=@{x=1;y=1;z=1};'forge.primitive'=@{kind=0};'forge.tint'=@{r=0.2;g=0.6;b=0.7}
     }})
    }
    $scene | ConvertTo-Json -Depth 16 | Set-Content "$project/main.scene.json" -Encoding utf8
    @{version=1;name="FORGE benchmark $name";startup_scene='main.scene.json'} | ConvertTo-Json | Set-Content "$project/forge.project.json" -Encoding utf8
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -ArgumentList ('"'+$project+'"') -WorkingDirectory (Split-Path $exe) -PassThru -RedirectStandardOutput "$project/stdout.log" -RedirectStandardError "$project/stderr.log"
    try {
     $responsive = $false
     while ($timer.Elapsed.TotalSeconds -lt 90) {
      Start-Sleep -Milliseconds 50
      $process.Refresh()
      if ($process.HasExited) { throw "Benchmark editor exited early: $name" }
      if ($process.MainWindowHandle -ne [IntPtr]::Zero -and $process.MainWindowTitle.Contains('FORGE benchmark')) {
       $result = [IntPtr]::Zero
       $responsive = [ForgeWindowProbe]::SendMessageTimeout($process.MainWindowHandle,0,[IntPtr]::Zero,[IntPtr]::Zero,2,100,[ref]$result) -ne [IntPtr]::Zero
       if ($responsive) { break }
      }
     }
     if (!$responsive) { throw "Editor did not become responsive: $name" }
     $startup = $timer.Elapsed.TotalMilliseconds
     if (![ForgeWindowProbe]::SetWindowPos($process.MainWindowHandle,[IntPtr]::Zero,20,20,1440,900,0x0040)) { throw 'Cannot set the matched benchmark window size' }
     Start-Sleep -Seconds 10
     $idle = @()
     for ($sample=0; $sample -lt 5; ++$sample) {
      $process.Refresh(); $before=$process.TotalProcessorTime.TotalMilliseconds
      $clock=[Diagnostics.Stopwatch]::StartNew(); Start-Sleep -Seconds 1
      $process.Refresh()
      if ($process.HasExited) { throw "Editor exited during idle sample: $name" }
      $idle += @{wall_ms=$clock.Elapsed.TotalMilliseconds;cpu_ms=$process.TotalProcessorTime.TotalMilliseconds-$before;working_set_bytes=$process.WorkingSet64;private_bytes=$process.PrivateMemorySize64}
     }
     $samples += @{label=$label;workload=$workload;repeat=$repeat;startup_responsive_ms=$startup;idle=$idle;version=(& $exe --version | Out-String).Trim();executable_sha256=(Get-FileHash $exe -Algorithm SHA256).Hash}
    } finally {
     if (!$process.HasExited) { [void]$process.CloseMainWindow(); if (!$process.WaitForExit(10000)) { $process.Kill(); $process.WaitForExit() } }
     $process.Dispose()
    }
   }
  }
 }
 @{scope='Process launch to responsive named editor window; NOT first GPU frame. Idle CPU/memory after 10s settling, five 1s samples. Same hosted runner; OS/driver caches uncontrolled.';
   baseline='Rebuilt accepted source a98be9a672394d3d91c2b9067331d0252f9b4313 with revision-local WARP device adapter; not shipped Build63 binary bytes';source=$env:FORGE_SOURCE_COMMIT;
   window_outer=@(1440,900);scale=1;vsync=$false;preview_lighting=$false;cpu_count=[Environment]::ProcessorCount;
   os=[Environment]::OSVersion.VersionString;image=$env:ImageVersion;samples=$samples} | ConvertTo-Json -Depth 12 | Set-Content "$Output/measurements.json"
} finally {
 if (Test-Path $preferences) { Remove-Item $preferences -Recurse -Force }
 if (Test-Path $backup) { Move-Item $backup $preferences }
}
