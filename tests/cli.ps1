$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$run=Join-Path $taskRoot ('.agentglide\tests\cli-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
$bin=Join-Path $taskRoot 'build\agentglide'
$base=Join-Path $run 'base 中文 space'
$overlay=Join-Path $run 'overlay 中文 space'
New-Item -ItemType Directory -Force $base,"$run\other" | Out-Null
[IO.File]::WriteAllText("$base\existing.txt",'ORIGINAL')
$before=Get-Item "$base\existing.txt" | Select-Object Length,LastWriteTimeUtc,@{n='hash';e={(Get-FileHash $_.FullName).Hash}} | ConvertTo-Json
$checks=[ordered]@{}
function Check($name,$ok) { $checks[$name]=[bool]$ok; if(!$ok){throw "FAIL $name"} }
function InvokeAgentGlide([string[]]$arguments) {
    $start=[Diagnostics.ProcessStartInfo]::new(); $start.FileName="$bin\agentglide.exe"; $start.UseShellExecute=$false; $start.CreateNoWindow=$true; $start.WorkingDirectory=(Get-Location).Path
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    foreach($a in $arguments){$start.ArgumentList.Add($a)}
    $p=[Diagnostics.Process]::Start($start); $o=$p.StandardOutput.ReadToEndAsync(); $e=$p.StandardError.ReadToEndAsync()
    if(!$p.WaitForExit(30000)){$p.Kill($true);throw 'CLI test timed out'}
    $result=@{exit=$p.ExitCode;out=$o.GetAwaiter().GetResult();error=$e.GetAwaiter().GetResult()};$p.Dispose()
    $result|ConvertTo-Json -Compress|Add-Content "$run\calls.jsonl"
    return $result
}
try {
    $r=InvokeAgentGlide @($base,$overlay,'--init'); Check 'first-create' ($r.exit -eq 0 -and (Test-Path "$overlay\vfs.toml"))
    $original=Get-Content "$overlay\vfs.toml" -Raw
    $r=InvokeAgentGlide @($base,$overlay,'--init'); Check 'config-never-overwritten' ((Get-Content "$overlay\vfs.toml" -Raw) -ceq $original)
    $r=InvokeAgentGlide @("$run\other",$overlay,'--init'); Check 'different-base-rejected' ($r.exit -eq 2)
    $r=InvokeAgentGlide @($base,"$base\nested",'--init'); Check 'overlap-child-rejected' ($r.exit -eq 2)
    $r=InvokeAgentGlide @($base,$run,'--init'); Check 'overlap-parent-rejected' ($r.exit -eq 2)
    $r=InvokeAgentGlide @($base,$base,'--init'); Check 'same-path-rejected' ($r.exit -eq 2)
    $r=InvokeAgentGlide @("$run\absent","$run\none",'--init'); Check 'missing-base-rejected' ($r.exit -eq 2)
    $r=InvokeAgentGlide @($base,"$run\bad.",'--init'); Check 'alias-path-rejected' ($r.exit -eq 2)
    New-Item -ItemType Directory "$run\unowned" | Out-Null; Set-Content "$run\unowned\keep.txt" KEEP
    $r=InvokeAgentGlide @($base,"$run\unowned",'--init'); Check 'nonempty-no-config-rejected' ($r.exit -eq 2 -and !(Test-Path "$run\unowned\vfs.toml"))
    New-Item -ItemType Junction -Path "$run\junction" -Target $base | Out-Null
    $r=InvokeAgentGlide @("$run\junction","$run\none",'--init'); Check 'junction-rejected' ($r.exit -eq 2)
    # Remove this known test junction only; never recurse through it.
    [IO.Directory]::Delete("$run\junction")
    foreach($case in @(@('syntax','version = ['),@('version',$original.Replace('version = 1','version = 9')),@('unknown',$original.Replace('model =','modle =')),@('duplicate',($original+"`n[agent]`n")),@('wrong-type',$original.Replace('version = 1','version = "1"')))) {
        [IO.File]::WriteAllText("$overlay\vfs.toml",$case[1]); $r=InvokeAgentGlide @($base,$overlay,'--init'); Check ("invalid-"+$case[0]) ($r.exit -eq 2)
    }
    $config=$original.Replace('[agent]',"[agent]`nexecutable = 'missing.exe'")
    [IO.File]::WriteAllText("$overlay\vfs.toml",$config); $r=InvokeAgentGlide @($base,$overlay,'--prompt','probe-write'); Check 'missing-client-rejected' ($r.exit -eq 2)
    $config=$original.Replace('[agent]',"[agent]`nexecutable = '$bin\cli-probe.exe'").Replace('My overlay','Changed 中文').Replace('instructions = "Work only in the Base working directory. Do not modify external paths."',"instructions = '''Line one`nLine two 中文'''" )
    [IO.File]::WriteAllText("$overlay\vfs.toml",$config)
    Push-Location $run
    try { $r=InvokeAgentGlide @('.\base 中文 space','.\overlay 中文 space','--prompt','probe-write') } finally { Pop-Location }
    Check 'relative-path-unicode-spaces-write' ($r.exit -eq 0 -and (Get-Content "$overlay\files\existing.txt" -Raw) -ceq 'PROBE')
    Check 'config-change-used' ($r.out.Contains('Changed 中文') -and (Get-Content "$overlay\.vfs\clients\codex\config.toml" -Raw).Contains('Line two 中文'))
    Check 'cwd-is-base' ($r.out.Contains("cwd=$base"))
    $r=InvokeAgentGlide @($base,$overlay,'--prompt','probe-read'); Check 'overlay-reuse' ($r.exit -eq 0)
    $relativeExe=[IO.Path]::GetRelativePath($overlay,"$bin\cli-probe.exe")
    [IO.File]::WriteAllText("$overlay\vfs.toml",$config.Replace("$bin\cli-probe.exe",$relativeExe))
    $r=InvokeAgentGlide @($base,$overlay,'--prompt','probe-read'); Check 'relative-executable' ($r.exit -eq 0)
    [IO.File]::WriteAllText("$overlay\vfs.toml",$config)
    Check 'edited-config-preserved' ((Get-Content "$overlay\vfs.toml" -Raw) -ceq $config)
    $copilotConfig=$config.Replace('type = "codex"','type = "copilot"')
    foreach($case in @(@('copilot-provider',$copilotConfig.Replace('provider_type = "openai"','provider_type = "invalid"')),@('copilot-wire',$copilotConfig.Replace('wire_api = "responses"','wire_api = "invalid"')),@('copilot-bool',$copilotConfig.Replace('allow_all_tools = false','allow_all_tools = "false"')))) {
        [IO.File]::WriteAllText("$overlay\vfs.toml",$case[1]);$r=InvokeAgentGlide @($base,$overlay,'--init');Check $case[0] ($r.exit -eq 2)
    }
    [IO.File]::WriteAllText("$overlay\vfs.toml",$copilotConfig)
    $r=InvokeAgentGlide @($base,$overlay,'--non-interactive','--prompt','probe-read');Check 'copilot-batch-explicit-tools' ($r.exit -eq 2 -and $r.error.Contains('allow_all_tools=true'))
    $r=InvokeAgentGlide @($base,$overlay,'--prompt','probe-read');Check 'copilot-interactive-adapter' ($r.exit -eq 0 -and $r.out.Contains("cwd=$base"))
    Check 'copilot-instructions-file' ((Get-Content "$overlay\.vfs\clients\copilot\instructions\AGENTS.md" -Raw).Contains('Line two 中文'))
    [IO.File]::WriteAllText("$overlay\vfs.toml",$copilotConfig.Replace('allow_all_tools = false','allow_all_tools = true'))
    $r=InvokeAgentGlide @($base,$overlay,'--non-interactive','--prompt','probe-read');Check 'copilot-batch-adapter' ($r.exit -eq 0)
    [IO.File]::WriteAllText("$overlay\vfs.toml",$config)
    $hold=[IO.File]::Open("$overlay\.vfs\lock",'Open','ReadWrite','None')
    try {$r=InvokeAgentGlide @($base,$overlay,'--init');Check 'concurrent-launch-rejected' ($r.exit -eq 2)} finally {$hold.Dispose()}
    $x86=$config.Replace("$bin\cli-probe.exe","$env:SystemRoot\SysWOW64\cmd.exe")
    [IO.File]::WriteAllText("$overlay\vfs.toml",$x86);$r=InvokeAgentGlide @($base,$overlay,'--prompt','probe-write'); Check 'injection-failure-closed' ($r.exit -eq 125 -and $r.error.Contains('terminated before resume'))
    [IO.File]::WriteAllText("$overlay\vfs.toml",$config)
    $savedState=[IO.File]::ReadAllText("$overlay\.vfs\state.json")
    Remove-Item -LiteralPath "$overlay\.vfs\state.json"
    $r=InvokeAgentGlide @($base,$overlay,'--init'); Check 'missing-state-not-rebound' ($r.exit -eq 2)
    [IO.File]::WriteAllText("$overlay\.vfs\state.json",$savedState)
    Remove-Item -LiteralPath "$overlay\vfs.toml"
    $r=InvokeAgentGlide @($base,$overlay,'--init'); Check 'missing-config-not-regenerated' ($r.exit -eq 2 -and !(Test-Path "$overlay\vfs.toml"))
    [IO.File]::WriteAllText("$overlay\vfs.toml",$config)
    New-Item -ItemType Directory "$overlay\deleted" | Out-Null
    $r=InvokeAgentGlide @($base,$overlay,'--init'); Check 'legacy-layout-not-adopted' ($r.exit -eq 2)
    [IO.Directory]::Delete("$overlay\deleted")
    Check 'new-layout-only' (!(Test-Path "$overlay\deleted") -and !(Get-ChildItem $overlay -Filter 'trace-*.log') -and (Test-Path "$overlay\.vfs\logs"))
    $after=Get-Item "$base\existing.txt" | Select-Object Length,LastWriteTimeUtc,@{n='hash';e={(Get-FileHash $_.FullName).Hash}} | ConvertTo-Json
    [IO.File]::WriteAllText("$run\base-after.json",$after)
    Check 'base-hash-mtime-unchanged' ($before -ceq $after)
} finally {
    [IO.File]::WriteAllText("$run\base-before.json",$before)
    $report=[ordered]@{run=$run;checks=$checks;passed=($checks.Count -ge 23 -and !($checks.Values -contains $false));dllSha256=(Get-FileHash "$bin\agentglide.dll").Hash}
    $report|ConvertTo-Json -Depth 6|Tee-Object "$run\result.json"
    Set-Content "$taskRoot\.agentglide\logs\latest-cli.txt" $run
}
