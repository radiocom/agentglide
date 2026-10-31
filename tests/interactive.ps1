param([ValidateSet('codex','claude','copilot')][string]$Agent='codex',[string]$Prompt='')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$run=Join-Path $taskRoot ('.agentglide\tests\interactive-'+$Agent+'-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
$base=Join-Path $run 'base';$overlay=Join-Path $run 'overlay'
New-Item -ItemType Directory -Force $base | Out-Null
[IO.File]::WriteAllText("$base\existing.txt","ORIGINAL"+[Environment]::NewLine)
function Manifest { Get-ChildItem -LiteralPath $base -File -Recurse | Select-Object Name,Length,LastWriteTimeUtc,@{n='sha256';e={(Get-FileHash $_.FullName).Hash}} | ConvertTo-Json }
$before=Manifest;[IO.File]::WriteAllText("$run\base-before.json",$before)
& "$taskRoot\build\agentglide\agentglide.exe" $base $overlay --init
if($LASTEXITCODE){throw 'init failed'}
$config=Get-Content "$overlay\vfs.toml" -Raw
$config=$config.Replace('type = "codex"',"type = "+(ConvertTo-Json $Agent -Compress))
if($Agent -eq 'claude'){$config=$config.Replace('http://127.0.0.1:1010/v1','http://127.0.0.1:1010')}
$config=$config.Replace('approval_policy = "on-request"','approval_policy = "never"').Replace('permission_mode = "default"','permission_mode = "dontAsk"')
$config=$config.Replace('allow_all_tools = false','allow_all_tools = true')
$config=$config.Replace('instructions = "Work only in the Base working directory. Do not modify external paths."','instructions = "Work only in cwd; do not delegate, inspect parent paths or use network tools. Additionally create instructions.txt containing INTERACTIVE-INSTRUCTIONS. Use only native PowerShell, Read/Edit or apply_patch."')
[IO.File]::WriteAllText("$overlay\vfs.toml",$config)
Write-Host "Evidence: $run"
Write-Host 'Ask the agent to change existing.txt to INTERACTIVE-OK; exit after its task completes.'
$cli=@($base,$overlay);if($Prompt){$cli+=@('--prompt',$Prompt)}
& "$taskRoot\build\agentglide\agentglide.exe" @cli
$code=$LASTEXITCODE
$after=Manifest;[IO.File]::WriteAllText("$run\base-after.json",$after)
$checks=[ordered]@{
    baseUnchanged=($before -ceq $after)
    edited=((Test-Path "$overlay\files\existing.txt") -and (Get-Content "$overlay\files\existing.txt" -Raw).Trim() -ceq 'INTERACTIVE-OK')
    instructions=((Test-Path "$overlay\files\instructions.txt") -and (Get-Content "$overlay\files\instructions.txt" -Raw).Trim() -ceq 'INTERACTIVE-INSTRUCTIONS')
}
[ordered]@{run=$run;agent=$Agent;exit=$code;checks=$checks;passed=($code -eq 0 -and !($checks.Values -contains $false));dllSha256=(Get-FileHash "$taskRoot\build\agentglide\agentglide.dll").Hash}|ConvertTo-Json -Depth 5|Tee-Object "$run\result.json"
Set-Content "$taskRoot\.agentglide\logs\latest-interactive-$Agent.txt" $run
