# Isolated exact shader capture; not a production compiler policy or release gate.
param([Parameter(Mandatory = $true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$build = (Resolve-Path $BuildDirectory).Path
$output = Join-Path $build 'grid-test-images/compiler-options'
if (Test-Path $output) { throw 'Native capture output already exists' }
New-Item -ItemType Directory $output | Out-Null
$prior = [Environment]::GetEnvironmentVariable('FORGE_NATIVE_MATERIAL_GRAPHICS', 'Process')
$priorCapture = [Environment]::GetEnvironmentVariable('FORGE_NATIVE_SHADER_CAPTURE', 'Process')
$results = @()
try {
    foreach ($mode in @('default', 'disabled')) {
        $destination = Join-Path $output "graphics-$mode"
        New-Item -ItemType Directory $destination | Out-Null
        $env:FORGE_NATIVE_MATERIAL_GRAPHICS = $mode
        $env:FORGE_NATIVE_SHADER_CAPTURE = Join-Path $destination 'shaders'
        $selection = '^editor_render_particle-gpu-material-numeric$'
        ctest --test-dir $build -R $selection --output-on-failure --no-tests=error --output-junit (Join-Path $destination 'results.xml') 2>&1 |
            Tee-Object -FilePath (Join-Path $destination 'ctest.log')
        $code = $LASTEXITCODE
        $shaders = @(Get-ChildItem -LiteralPath $env:FORGE_NATIVE_SHADER_CAPTURE -Filter '*.dxbc')
        if ($shaders.Count -lt 4) { throw 'Missing native vertex/pixel shader pairs' }
        foreach ($shader in $shaders) {
            & fxc.exe /nologo /dumpbin $shader.FullName 2>&1 |
                Out-File -FilePath ($shader.FullName + '.asm') -Encoding utf8
            if ($LASTEXITCODE) { throw 'Native shader disassembly failed' }
        }
        $results += @{ mode = $mode; exit_code = $code; selection = $selection; sampler_heap = 'runtime defaults'; compute = 'production default'; shaders = $shaders.Count }
        $results | ConvertTo-Json -AsArray -Depth 4 | Set-Content (Join-Path $output 'results.json')
        $images = Join-Path $build 'grid-test-images/particle-gpu-material-numeric'
        if (Test-Path $images) { Move-Item -LiteralPath $images -Destination $destination }
    }
} finally {
    [Environment]::SetEnvironmentVariable('FORGE_NATIVE_MATERIAL_GRAPHICS', $prior, 'Process')
    [Environment]::SetEnvironmentVariable('FORGE_NATIVE_SHADER_CAPTURE', $priorCapture, 'Process')
}
if ($results | Where-Object { $_.exit_code -ne 0 }) {
    throw 'Original image assertion failed in a graphics control; exact shader evidence retained'
}
