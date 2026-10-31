$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$run=Join-Path $root ('.agentglide\tests\paths-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
$base=Join-Path $run 'base 中文 space';$overlay=Join-Path $run 'overlay 中文 space'
[IO.Directory]::CreateDirectory($base)|Out-Null
[IO.File]::WriteAllText("$base\fallback.txt",'FALLBACK')
function Manifest { @(Get-Item $base; Get-ChildItem $base -Recurse -Force)|ForEach-Object {[ordered]@{path=$_.FullName;mtime=$_.LastWriteTimeUtc.ToString('o');hash=$(if(!$_.PSIsContainer){(Get-FileHash $_.FullName).Hash})}}|ConvertTo-Json }
$before=Manifest;[IO.File]::WriteAllText("$run\base-before.json",$before)
$output=& "$root\build\agentglide\agentglide-run.exe" --layout-v1 $base $overlay -- "$root\build\agentglide\path-fixture.exe" $overlay 2>&1
$code=$LASTEXITCODE;$output|Set-Content "$run\native.log"
$node=Get-Command node.exe -CommandType Application -ErrorAction Stop
$probe=& "$root\build\agentglide\agentglide-run.exe" --layout-v1 $base $overlay -- $node.Source "$PSScriptRoot\canonical-path.js" 2> "$run\node-stderr.log"
$nodeCode=$LASTEXITCODE;$probe|Set-Content "$run\canonical-injected.json"
$after=Manifest;[IO.File]::WriteAllText("$run\base-after.json",$after)
$p=$probe|ConvertFrom-Json
$checks=[ordered]@{native=($code -eq 0);node=($nodeCode -eq 0 -and $p.canonical -eq $base -and $p.cwdRead.value -eq 'FALLBACK' -and $p.canonicalRead.value -eq 'FALLBACK');baseUnchanged=($before -ceq $after);overlayWrite=(Test-Path "$overlay\files\renamed.txt")}
[ordered]@{run=$run;checks=$checks;passed=!($checks.Values -contains $false);dllSha256=(Get-FileHash "$root\build\agentglide\agentglide.dll").Hash}|ConvertTo-Json -Depth 4|Tee-Object "$run\result.json"
Set-Content "$root\.agentglide\logs\latest-paths.txt" $run
if($checks.Values -contains $false){throw "Path regression failed: $run"}
