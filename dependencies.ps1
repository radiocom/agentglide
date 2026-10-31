# Restore only pinned build inputs into this project's ignored deps directory.
$ErrorActionPreference = 'Stop'
$lock = Get-Content -LiteralPath "$PSScriptRoot\dependencies.lock.json" -Raw | ConvertFrom-Json
$cache = Join-Path $PSScriptRoot '.agentglide\cache\downloads'
foreach ($dependency in $lock) {
    foreach ($file in $dependency.files) {
        $target = Join-Path "$PSScriptRoot\deps\$($dependency.name)" $file.path
        if (Test-Path -LiteralPath $target) {
            if ((Get-FileHash -LiteralPath $target).Hash -ine $file.sha256) {
                throw "Dependency hash mismatch; existing file was not overwritten: $target"
            }
            continue
        }
        [IO.Directory]::CreateDirectory($cache) | Out-Null
        $download = Join-Path $cache ([guid]::NewGuid().ToString('N') + '.download')
        $upstream = $dependency.repository.Replace('https://github.com/', 'https://raw.githubusercontent.com/')
        $url = "$upstream/$($dependency.commit)/$($file.path)"
        Invoke-WebRequest -Uri $url -OutFile $download
        if ((Get-FileHash -LiteralPath $download).Hash -ine $file.sha256) {
            throw "Downloaded dependency hash mismatch: $url"
        }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
        [IO.File]::Copy($download, $target, $false)
    }
}
Write-Output 'AgentGlide dependency hashes verified.'
