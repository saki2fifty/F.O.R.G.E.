# Isolated native regression, never a release acceptance gate.
param([Parameter(Mandatory = $true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$build = (Resolve-Path $BuildDirectory).Path
$output = Join-Path $build 'grid-test-images/compiler-options'
if (Test-Path $output) { throw 'Native regression output already exists' }
New-Item -ItemType Directory $output | Out-Null
$destination = Join-Path $output 'production-default'
New-Item -ItemType Directory $destination | Out-Null
$names = @('editor_render_dynamic-descriptors', 'editor_render_mesh-bindings',
    'editor_render_particle-gpu-plane-numeric', 'editor_render_particle-gpu-order-numeric',
    'editor_render_particle-gpu-material-numeric', 'editor_render_particle-gpu-material-scene',
    'particle_material_gallery_game', 'particle_material_graph_gallery_game')
$selection = '^(' + ($names -join '|') + ')$'
ctest --test-dir $build -R $selection --output-on-failure --no-tests=error --output-junit (Join-Path $destination 'results.xml') 2>&1 |
    Tee-Object -FilePath (Join-Path $destination 'ctest.log')
$code = $LASTEXITCODE
$results = @(@{ mode = 'production-default'; exit_code = $code; selection = $selection; sampler_heap = 'runtime defaults' })
$results | ConvertTo-Json -AsArray -Depth 4 | Set-Content (Join-Path $output 'results.json')
foreach ($name in $names) {
    if (!$name.StartsWith('editor_render_')) { continue }
    $directory = $name.Substring('editor_render_'.Length)
    $images = Join-Path $build "grid-test-images/$directory"
    if (Test-Path $images) { Move-Item -LiteralPath $images -Destination $destination }
}
if ($code -ne 0) { throw 'Native rendering regression failed; retained all per-case evidence' }
