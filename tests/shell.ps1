$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$run = Join-Path $taskRoot ('.agentglide\tests\shell-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$base = Join-Path $run 'base'
New-Item -ItemType Directory -Force -Path $base,"$run\home","$run\temp" | Out-Null
foreach ($pair in @(@('fallback.txt','FALLBACK'),@('existing.txt','ORIGINAL'),@('delete.txt','DELETE'),@('rename.txt','RENAME'))) {
    [IO.File]::WriteAllText((Join-Path $base $pair[0]), ($pair[1] + "`n"))
}
function Manifest($path) {
    @(Get-ChildItem -LiteralPath $path -File -Recurse | Sort-Object FullName | ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($path.Length+1);size=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;modified=$_.LastWriteTimeUtc.ToString('o')}
    }) | ConvertTo-Json -Depth 4
}
$before = Manifest $base
[IO.File]::WriteAllText("$run\base-before.json", $before)
$env:CODEX_HOME = "$run\home"
$env:TEMP = "$run\temp"
$env:TMP = "$run\temp"
& "$taskRoot\build\agentglide\agentglide-run.exe" $base "$run\overlay" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PSScriptRoot\shell-operations.ps1" 2>&1 | Tee-Object -FilePath "$run\shell.log"
$code = $LASTEXITCODE
$after = Manifest $base
[IO.File]::WriteAllText("$run\base-after.json", $after)
$report = [ordered]@{run=$run;dllSha256=(Get-FileHash "$taskRoot\build\agentglide\agentglide.dll").Hash;exit=$code;baseUnchanged=($before -ceq $after);passed=($code -eq 0 -and $before -ceq $after)}
$report | ConvertTo-Json | Tee-Object -FilePath "$run\result.json"
[IO.File]::WriteAllText("$taskRoot\.agentglide\logs\latest-shell.txt", $run)
if (!$report.passed) { throw 'Shell/Codex local patch smoke failed' }
