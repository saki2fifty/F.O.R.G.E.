# Isolated compiler comparison, never a release acceptance gate.
param([Parameter(Mandatory = $true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$build = (Resolve-Path $BuildDirectory).Path
$output = Join-Path $build 'grid-test-images/compiler-options'
if (Test-Path $output) { throw 'Compiler comparison output already exists' }
New-Item -ItemType Directory $output | Out-Null
$prior = [Environment]::GetEnvironmentVariable('FORGE_NATIVE_FXC_PROBE', 'Process')
$results = @()
try {
    foreach ($mode in @('fxc-50')) {
        $env:FORGE_NATIVE_FXC_PROBE = $mode
        $destination = Join-Path $output $mode
        New-Item -ItemType Directory $destination | Out-Null
        $names = @('editor_render_particle-gpu-material-numeric')
        $selection = '^(' + ($names -join '|') + ')$'
        ctest --test-dir $build -R $selection --output-on-failure --no-tests=error --output-junit (Join-Path $destination 'results.xml') 2>&1 |
            Tee-Object -FilePath (Join-Path $destination 'ctest.log')
        $code = $LASTEXITCODE
        $results += @{ mode = $mode; exit_code = $code; selection = $selection; sampler_heap = 'runtime defaults' }
        $results | ConvertTo-Json -AsArray -Depth 4 | Set-Content (Join-Path $output 'results.json')
        foreach ($name in $names) {
            $directory = $name.Substring('editor_render_'.Length)
            $images = Join-Path $build "grid-test-images/$directory"
            if (Test-Path $images) { Move-Item -LiteralPath $images -Destination $destination }
        }
    }
} finally {
    [Environment]::SetEnvironmentVariable('FORGE_NATIVE_FXC_PROBE', $prior, 'Process')
}
if ($results | Where-Object { $_.exit_code -ne 0 }) {
    throw 'One or more isolated compiler modes failed; retained all per-mode evidence'
}
