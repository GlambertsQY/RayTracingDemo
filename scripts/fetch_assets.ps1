param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$target = Join-Path $root 'assets\Sponza'

if (-not $Force -and (Test-Path (Join-Path $target 'Sponza.gltf'))) {
    $existing = (Get-ChildItem -Path $target -File | Measure-Object).Count
    if ($existing -ge 71) {
        Write-Host "Sponza already present ($existing files), skipping"
        exit 0
    }
}

New-Item -ItemType Directory -Force -Path $target | Out-Null

$api = 'https://api.github.com/repos/KhronosGroup/glTF-Sample-Assets/contents/Models/Sponza/glTF'
$headers = @{ 'User-Agent' = 'RayTracingDemo' }

$files = $null
try {
    $files = Invoke-RestMethod -Uri $api -Headers $headers -TimeoutSec 60
} catch {
    Write-Warning "GitHub API failed ($($_.Exception.Message)), falling back to sparse clone"
}

if ($null -ne $files) {
    foreach ($file in $files) {
        $destination = Join-Path $target $file.name
        if (-not $Force -and (Test-Path $destination) -and ((Get-Item $destination).Length -eq $file.size)) {
            continue
        }
        Write-Host ("Downloading {0} ({1:N0} KB)" -f $file.name, ($file.size / 1KB))
        Invoke-WebRequest -Uri $file.download_url -OutFile $destination -Headers $headers
    }
} else {
    $temp = Join-Path $env:TEMP ("gltf-sample-assets-" + [guid]::NewGuid().ToString('N'))
    git clone --depth 1 --filter=blob:none --sparse https://github.com/KhronosGroup/glTF-Sample-Assets.git $temp
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clone glTF-Sample-Assets"
    }
    git -C $temp sparse-checkout set Models/Sponza/glTF
    Copy-Item -Path (Join-Path $temp 'Models\Sponza\glTF\*') -Destination $target -Force
    Remove-Item -Recurse -Force $temp
}

$count = (Get-ChildItem -Path $target -File | Measure-Object).Count
Write-Host "Sponza assets ready: $count files in $target"
