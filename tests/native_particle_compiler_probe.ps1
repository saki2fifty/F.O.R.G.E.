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
    foreach ($mode in @('fxc-default', 'fxc-disabled', 'fxc-0', 'fxc-2', 'fxc-3', 'dxc')) {
        $env:FORGE_NATIVE_FXC_PROBE = $mode
        $destination = Join-Path $output $mode
        New-Item -ItemType Directory $destination | Out-Null
        $names = @('editor_render_mesh-bindings', 'editor_render_particle-gpu-plane-numeric',
                   'editor_render_particle-gpu-order-numeric', 'editor_render_particle-gpu-sorted-scene',
                   'editor_render_particle-gpu-material-scene')
        if ($mode -eq 'dxc') {
            $names += @('particle_material_gallery_game', 'particle_material_graph_gallery_game')
        }
        $selection = '^(' + ($names -join '|') + ')$'
        ctest --test-dir $build -R $selection --output-on-failure --no-tests=error --output-junit (Join-Path $destination 'results.xml') 2>&1 |
            Tee-Object -FilePath (Join-Path $destination 'ctest.log')
        $code = $LASTEXITCODE
        $results += @{ mode = $mode; exit_code = $code; selection = $selection; sampler_heap = 'runtime defaults' }
        $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $output 'results.json')
        foreach ($name in @('mesh-bindings', 'particle-gpu-plane-numeric', 'particle-gpu-order-numeric',
                            'particle-gpu-sorted-scene', 'particle-gpu-material-scene')) {
            $images = Join-Path $build "grid-test-images/$name"
            if (Test-Path $images) { Move-Item -LiteralPath $images -Destination $destination }
        }
        if ($mode -eq 'dxc') {
            foreach ($name in @('particle-material-game', 'particle-material-graph-game')) {
                $images = Join-Path $build $name
                if (Test-Path $images) { Move-Item -LiteralPath $images -Destination $destination }
            }
        }
    }
} finally {
    [Environment]::SetEnvironmentVariable('FORGE_NATIVE_FXC_PROBE', $prior, 'Process')
}
if ($results | Where-Object { $_.exit_code -ne 0 }) {
    throw 'One or more isolated compiler modes failed; retained all per-mode evidence'
}
