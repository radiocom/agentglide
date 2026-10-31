$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$run=Join-Path $root ('.agentglide\package-check-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
$stage=Join-Path $run 'runtime'
Expand-Archive -LiteralPath "$root\dist\agentglide-win-x64.zip" -DestinationPath $stage
$base=Join-Path $run 'base';$overlay=Join-Path $run 'overlay'
[IO.Directory]::CreateDirectory($base)|Out-Null
[IO.File]::WriteAllText("$base\existing.txt",'ORIGINAL')
$before=Get-Item "$base\existing.txt"|Select-Object LastWriteTimeUtc,Length,@{n='hash';e={(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json
& "$stage\agentglide.exe" --help > "$run\help.txt"
if($LASTEXITCODE){throw 'Packaged help failed'}
& "$stage\agentglide.exe" $base $overlay --init > "$run\init.txt"
if($LASTEXITCODE){throw 'Packaged init failed'}
$config=Get-Content "$overlay\vfs.toml" -Raw
$config=$config.Replace('type = "codex"','type = "copilot"').Replace('[agent]',"[agent]`nexecutable = '$root\build\agentglide\cli-probe.exe'")
[IO.File]::WriteAllText("$overlay\vfs.toml",$config)
& "$stage\agentglide.exe" $base $overlay --prompt probe-write > "$run\write.txt"
$code=$LASTEXITCODE
$after=Get-Item "$base\existing.txt"|Select-Object LastWriteTimeUtc,Length,@{n='hash';e={(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json
[IO.File]::WriteAllText("$run\base-before.json",$before);[IO.File]::WriteAllText("$run\base-after.json",$after)
$same=$true
foreach($name in @('agentglide.exe','agentglide.ps1','agentglide-config.exe','default-agentglide.toml','agentglide-run.exe','agentglide.dll')){if((Get-FileHash "$stage\$name").Hash -ne (Get-FileHash "$root\build\agentglide\$name").Hash){$same=$false}}
$checks=[ordered]@{deliveryMatchesBuild=$same;injectedWrite=($code -eq 0 -and (Get-Content "$overlay\files\existing.txt" -Raw) -ceq 'PROBE');baseUnchanged=($before -ceq $after)}
$report=[ordered]@{run=$run;stage=$stage;package=(Get-FileHash "$root\dist\agentglide-win-x64.zip").Hash;dllSha256=(Get-FileHash "$stage\agentglide.dll").Hash;checks=$checks;passed=!($checks.Values -contains $false)}
$report|ConvertTo-Json -Depth 5|Tee-Object "$run\result.json"|Set-Content "$root\.agentglide\logs\package-check.json"
$report|ConvertTo-Json -Depth 5
if(!$report.passed){throw 'Packaged runtime validation failed'}
