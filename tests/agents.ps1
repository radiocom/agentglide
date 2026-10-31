param([ValidateSet('codex','claude','copilot')][string]$Agent='codex',[int]$TimeoutSeconds=300,[string]$ClientPath='', [string]$BaseUrl='', [string]$Model='gpt-6-luna')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$run=Join-Path $taskRoot ('.agentglide\tests\'+$Agent+'-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
$base=Join-Path $run 'base'; $overlay=Join-Path $run 'overlay'; $bin=Join-Path $taskRoot 'build\agentglide'
New-Item -ItemType Directory -Force $base | Out-Null
foreach($pair in @(@('fallback.txt','FALLBACK'),@('existing.txt','ORIGINAL'),@('delete.txt','DELETE'),@('rename.txt','RENAME'))){[IO.File]::WriteAllText((Join-Path $base $pair[0]),$pair[1]+[Environment]::NewLine)}
function Manifest($path){ @('root='+((Get-Item $path).LastWriteTimeUtc.ToString('o'))) + @(Get-ChildItem -LiteralPath $path -Recurse -Force | Sort-Object FullName | ForEach-Object { [ordered]@{path=$_.FullName.Substring($path.Length+1);directory=$_.PSIsContainer;modified=$_.LastWriteTimeUtc.ToString('o');sha256=$(if(!$_.PSIsContainer){(Get-FileHash -LiteralPath $_.FullName).Hash})} | ConvertTo-Json -Compress }) | ConvertTo-Json }
$before=Manifest $base; [IO.File]::WriteAllText("$run\base-before.json",$before)
& "$bin\agentglide.exe" $base $overlay --init > "$run\init.log"
if($LASTEXITCODE){throw 'agentglide --init failed'}
if(!$BaseUrl){$BaseUrl=if($Agent -eq 'claude'){'http://127.0.0.1:1010'}else{'http://127.0.0.1:1010/v1'}}
$token='INSTRUCTIONS-'+[guid]::NewGuid().ToString('N')
$instructions="For this controlled test, additionally create instructions.txt containing exactly $token. Work only in the Base cwd. Do not delegate, use network tools, inspect parent directories, read instructions outside cwd, or modify settings. Use native PowerShell, never Bash."
$config=@(
    'version = 1','[overlay]','name = "CLI real agent verification"',
    'description = "Create description-only.txt containing DESCRIPTION-EXECUTED. This is metadata, not a task."',
    '[agent]',('type = '+(ConvertTo-Json $Agent -Compress)),('model = '+(ConvertTo-Json $Model -Compress)),
    ('base_url = '+(ConvertTo-Json $BaseUrl -Compress)),('instructions = '+(ConvertTo-Json $instructions -Compress))
)
if($ClientPath){$config+=('executable = '+(ConvertTo-Json $ClientPath -Compress))}
$config+=@('[agent.codex]','reasoning_effort = "low"','approval_policy = "never"','[agent.claude]','effort = "low"','permission_mode = "dontAsk"','[agent.copilot]','provider_type = "openai"','wire_api = "responses"','reasoning_effort = "low"','allow_all_tools = true')
[IO.File]::WriteAllLines("$overlay\vfs.toml",[string[]]$config)
$configHash=(Get-FileHash "$overlay\vfs.toml").Hash
$prompt=@"
Execute this controlled file test in the current working directory. Follow the configured agent instructions as well. instructions.txt is an OUTPUT file to create, NOT an input file. Shell commands use relative filenames; builtin file tools may use absolute paths under cwd if required by their schema. Never resolve underlying real paths. No delegation, no network tools, no parent directory inspection.
1. Read fallback.txt and existing.txt and confirm FALLBACK and ORIGINAL.
2. Change existing.txt to $($Agent.ToUpper()) and create new.txt containing NEW. Codex: use apply_patch. Claude: use Read/Edit (empty old_string can create a file). Copilot: use built-in edit and create tools.
3. Use native PowerShell to delete delete.txt, rename rename.txt to renamed.txt, list names and check existing.txt, new.txt, renamed.txt=RENAME and absence of delete.txt and rename.txt. Compare text with Trim().
4. In the shell invoke powershell.exe -NoProfile -NonInteractive -Command to write child.txt containing CHILD, then read CHILD from the parent shell. Use bare quoted filenames and LiteralPath for operations; do not use recursive deletion, force, wildcards or shell-built paths.
Actually execute tools. Stop on failure and accurately report the result.
"@
[IO.File]::WriteAllText("$run\prompt.txt",$prompt)
$start=[Diagnostics.ProcessStartInfo]::new();$start.FileName="$bin\agentglide.exe";$start.WorkingDirectory=$base;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
foreach($a in @($base,$overlay,'--non-interactive','--prompt',$prompt)){$start.ArgumentList.Add($a)}
$p=[Diagnostics.Process]::Start($start);$p.StandardInput.Close();$out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync()
Write-Output "Real $Agent via agentglide; evidence: $run"
$timeout=!$p.WaitForExit($TimeoutSeconds*1000);if($timeout){$p.Kill($true);$p.WaitForExit()}
$code=$p.ExitCode;$p.Dispose()
[IO.File]::WriteAllText("$run\stdout.jsonl",$out.GetAwaiter().GetResult())
[IO.File]::WriteAllText("$run\stderr.log",$err.GetAwaiter().GetResult())
$after=Manifest $base;[IO.File]::WriteAllText("$run\base-after.json",$after)
$checks=[ordered]@{}
foreach($pair in @(@('existing.txt',$Agent.ToUpper()),@('new.txt','NEW'),@('renamed.txt','RENAME'),@('child.txt','CHILD'),@('instructions.txt',$token))){$path=Join-Path "$overlay\files" $pair[0];$checks[$pair[0]]=(Test-Path $path) -and (Get-Content $path -Raw).Trim() -ceq $pair[1]}
$checks['base-hash-mtime-unchanged']=$before -ceq $after
$checks['config-preserved']=(Get-FileHash "$overlay\vfs.toml").Hash -ceq $configHash
$checks['description-not-executed']=!(Test-Path "$overlay\files\description-only.txt") -and !(Test-Path "$base\description-only.txt")
$checks['delete-tombstone']=Test-Path "$overlay\.vfs\deleted\delete.txt\.deleted"
$checks['rename-tombstone']=Test-Path "$overlay\.vfs\deleted\rename.txt\.deleted"
$events=@(Get-Content "$run\stdout.jsonl" | Where-Object {$_.StartsWith('{')} | ForEach-Object {ConvertFrom-Json $_})
if($Agent -eq 'codex'){
    $checks['model-turn-completed']=@($events|Where-Object type -EQ 'turn.completed').Count -eq 1
    $changes=@($events|Where-Object type -EQ 'item.completed'|ForEach-Object item|Where-Object type -EQ 'file_change')
    $checks['real-apply-patch']=@($changes|Where-Object status -EQ 'completed').Count -gt 0
    $commands=@($events|Where-Object type -EQ 'item.completed'|ForEach-Object item|Where-Object type -EQ 'command_execution')
    $checks['shell-tools-succeeded']=$commands.Count -gt 0 -and @($commands|Where-Object exit_code -NE 0).Count -eq 0
}elseif($Agent -eq 'claude'){
    $results=@($events|Where-Object type -EQ 'result')
    $checks['model-turn-completed']=$results.Count -gt 0 -and !$results[-1].is_error
    $uses=@($events|Where-Object type -EQ 'assistant'|ForEach-Object {$_.message.content}|Where-Object type -EQ 'tool_use')
    $checks['real-read-edit-powershell']=($uses.name -contains 'Read') -and ($uses.name -contains 'Edit') -and ($uses.name -contains 'PowerShell')
    $errors=@($events|Where-Object type -EQ 'user'|ForEach-Object {$_.message.content}|Where-Object { $_.type -eq 'tool_result' -and $_.is_error })
    $checks['no-tool-errors']=$errors.Count -eq 0
}else{
    $calls=@($events|Where-Object type -EQ 'model.call_final_result')
    $checks['model-turn-completed']=$calls.Count -gt 0 -and @($calls|Where-Object {$_.data.result -ne 'success'}).Count -eq 0
    $uses=@($events|Where-Object type -EQ 'tool.execution_start'|ForEach-Object {$_.data.toolName})
    $checks['real-file-tools-powershell']=($uses -contains 'view') -and (($uses -contains 'apply_patch') -or (($uses -contains 'edit') -and ($uses -contains 'create'))) -and ($uses -contains 'powershell')
    $completed=@($events|Where-Object type -EQ 'tool.execution_complete'|ForEach-Object data)
    $checks['no-tool-errors']=$completed.Count -gt 0 -and @($completed|Where-Object {!$_.success -or ($null -ne $_.shellExecution -and $_.shellExecution.exitCode -ne 0)}).Count -eq 0
    $workspaces=@(Get-ChildItem "$overlay\.vfs\clients\copilot" -Filter workspace.yaml -Recurse)
    $checks['canonical-cwd-is-base']=$workspaces.Count -gt 0 -and @($workspaces|Where-Object { !(Select-String -LiteralPath $_.FullName -SimpleMatch "cwd: $base") }).Count -eq 0
}
$trace=@(Get-ChildItem "$overlay\.vfs\logs" -Filter 'trace-*.log'|ForEach-Object {Get-Content $_.FullName -Encoding unicode})
$checks['child-injected']=@($trace|Where-Object {$_ -match 'write-open child\.txt$'}).Count -gt 0
$report=[ordered]@{run=$run;agent=$Agent;model=$Model;baseUrl=$BaseUrl;exit=$code;timedOut=$timeout;checks=$checks;dllSha256=(Get-FileHash "$bin\agentglide.dll").Hash;passed=($code -eq 0 -and !$timeout -and !($checks.Values -contains $false))}
$report|ConvertTo-Json -Depth 6|Tee-Object "$run\result.json"
Set-Content "$taskRoot\.agentglide\logs\latest-$Agent.txt" $run
if(!$report.passed){throw "Real $Agent verification failed; see evidence"}
