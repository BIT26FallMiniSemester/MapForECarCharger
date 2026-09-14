param(
    [string]$VmHost = "192.168.88.128",
    [string]$VmUser = "zjs",
    [string]$IdentityFile = "$HOME/.ssh/id_ed25519_hadoop_codex",
    [string]$RemoteRoot = "/home/zjs/map-for-ecar"
)

$ErrorActionPreference = "Stop"
$WarehouseRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Archive = Join-Path $env:TEMP "map-for-ecar-spark-warehouse.tar.gz"

if (-not (Test-Path -LiteralPath $IdentityFile)) {
    throw "SSH identity file not found: $IdentityFile"
}

try {
    tar -czf $Archive --exclude="runtime" --exclude="__pycache__" -C $WarehouseRoot .
    if ($LASTEXITCODE -ne 0) { throw "Failed to create deployment archive" }
    ssh -i $IdentityFile "$VmUser@$VmHost" "mkdir -p '$RemoteRoot/spark-warehouse'"
    if ($LASTEXITCODE -ne 0) { throw "Failed to create remote directory" }
    scp -i $IdentityFile $Archive "$VmUser@${VmHost}:$RemoteRoot/spark-warehouse.tar.gz"
    if ($LASTEXITCODE -ne 0) { throw "Failed to upload deployment archive" }
    ssh -i $IdentityFile "$VmUser@$VmHost" "tar -xzf '$RemoteRoot/spark-warehouse.tar.gz' -C '$RemoteRoot/spark-warehouse' && rm '$RemoteRoot/spark-warehouse.tar.gz' && chmod +x '$RemoteRoot/spark-warehouse/scripts/'*.sh"
    if ($LASTEXITCODE -ne 0) { throw "Failed to extract deployment archive" }
    Write-Host "Synced to $VmUser@$VmHost`:$RemoteRoot/spark-warehouse"
}
finally {
    Remove-Item -LiteralPath $Archive -Force -ErrorAction SilentlyContinue
}
