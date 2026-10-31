$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$stage=Join-Path $taskRoot ('.agentglide\packages\'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
New-Item -ItemType Directory -Force $stage,"$stage\licenses","$taskRoot\dist" | Out-Null
foreach($name in @('agentglide.exe','agentglide.ps1','agentglide-config.exe','default-agentglide.toml','agentglide-run.exe','agentglide.dll')) {
    Copy-Item -LiteralPath "$taskRoot\build\agentglide\$name" -Destination $stage
}
Copy-Item -LiteralPath "$taskRoot\README.md","$taskRoot\THIRD-PARTY-NOTICES.md","$taskRoot\dependencies.lock.json" -Destination $stage
# Include documentation and unmodified legal notices, never caches or test data.
Copy-Item -LiteralPath "$taskRoot\docs" -Destination $stage -Recurse
Copy-Item -LiteralPath "$taskRoot\licenses\minhook","$taskRoot\licenses\tomlc99" -Destination "$stage\licenses" -Recurse
Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object { [ordered]@{file=[IO.Path]::GetRelativePath($stage,$_.FullName);sha256=(Get-FileHash $_.FullName).Hash} } | ConvertTo-Json | Set-Content "$stage\manifest.json"
$zip=Join-Path $taskRoot 'dist\agentglide-win-x64.zip'
Compress-Archive -LiteralPath @(Get-ChildItem -LiteralPath $stage | Select-Object -ExpandProperty FullName) -DestinationPath $zip -Force
Write-Output $zip
