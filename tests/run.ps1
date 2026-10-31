$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$run = Join-Path $taskRoot ('.agentglide\tests\fixture-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$base = Join-Path $run 'base'
New-Item -ItemType Directory -Force -Path $base | Out-Null
New-Item -ItemType Directory -Force -Path "$run\temp" | Out-Null
$env:TEMP="$run\temp"
$env:TMP=$env:TEMP
$env:TMPDIR=$env:TEMP
$seed = @{
    'fallback.txt'='FALLBACK'; 'existing.txt'='ORIGINAL'; 'delete.txt'='DELETE';
    'rename.txt'='RENAME'; 'replace-source.txt'='SOURCE'; 'replace-target.txt'='TARGET'; 'doc.txt'='DOC'; 'stat.txt'='ORIGINAL-STAT'
}
foreach ($name in $seed.Keys) { [IO.File]::WriteAllText((Join-Path $base $name), $seed[$name]) }
function Manifest($path) {
    @(Get-ChildItem -LiteralPath $path -File -Recurse | Sort-Object FullName | ForEach-Object {
        [ordered]@{ path=$_.FullName.Substring($path.Length + 1); size=$_.Length;
            sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash;
            modified=$_.LastWriteTimeUtc.ToString('o'); attributes=[int]$_.Attributes }
    }) | ConvertTo-Json -Depth 4
}
$before = Manifest $base
[IO.File]::WriteAllText("$run\base-before.json", $before)
$results = @()
foreach ($case in @(@{name='main';instance='one';mode='main'}, @{name='isolation';instance='two';mode='isolate'}, @{name='persistence';instance='one';mode='persist'})) {
    $output = & "$taskRoot\build\agentglide\agentglide-run.exe" $base "$run\$($case.instance)" -- "$taskRoot\build\agentglide\fixture.exe" $case.mode 2>&1
    $code = $LASTEXITCODE
    $output | Tee-Object -FilePath "$run\$($case.name).log"
    $results += [ordered]@{test=$case.name;exit=$code}
}
$after = Manifest $base
foreach ($tag in @('raceone','racetwo')) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = "$taskRoot\build\agentglide\agentglide-run.exe"
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @($base,"$run\$tag",'--',"$taskRoot\build\agentglide\fixture.exe",$tag)) { $start.ArgumentList.Add($argument) }
    $proc = [Diagnostics.Process]::Start($start)
    if ($tag -eq 'raceone') { $first = $proc } else { $second = $proc }
}
foreach ($item in @(@{name='raceone';process=$first},@{name='racetwo';process=$second})) {
    $item.process.WaitForExit()
    $item.process.StandardOutput.ReadToEnd() | Tee-Object -FilePath "$run\$($item.name).log"
    $results += [ordered]@{test=$item.name;exit=$item.process.ExitCode}
    $item.process.Dispose()
}
$after = Manifest $base
[IO.File]::WriteAllText("$run\base-after.json", $after)
$wow = Join-Path $env:SystemRoot 'SysWOW64\cmd.exe'
if (Test-Path -LiteralPath $wow) {
    & "$taskRoot\build\agentglide\agentglide-run.exe" $base "$run\unsupported-x86" -- $wow /d /c 'echo BAD>must-not-run.txt' 2>&1 | Tee-Object -FilePath "$run\reject-x86.log"
    $rejected = $LASTEXITCODE -eq 125 -and !(Test-Path -LiteralPath "$base\must-not-run.txt")
    $results += [ordered]@{test='reject-x86-before-resume';exit=$(if ($rejected) {0} else {1})}
}
$after = Manifest $base
[IO.File]::WriteAllText("$run\base-after.json", $after)
$unchanged = $before -ceq $after
$report = [ordered]@{run=$run;dllSha256=(Get-FileHash "$taskRoot\build\agentglide\agentglide.dll").Hash;results=$results;baseUnchanged=$unchanged;passed=($unchanged -and !($results | Where-Object exit -NE 0))}
$report | ConvertTo-Json -Depth 5 | Tee-Object -FilePath "$run\result.json"
[IO.File]::WriteAllText("$taskRoot\.agentglide\logs\latest-fixture.txt", $run)
if (!$report.passed) { throw 'Fixture failed; inspect run/result.json' }
