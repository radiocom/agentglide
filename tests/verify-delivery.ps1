$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$bad=@();$count=0
$package=Get-Content "$root\.agentglide\logs\package-check.json" -Raw|ConvertFrom-Json
$docs=@(Get-ChildItem -LiteralPath $root -File -Filter '*.md') + @(Get-ChildItem "$root\docs" -File -Filter '*.md') + @(Get-ChildItem -LiteralPath $package.stage -Recurse -File -Filter '*.md')
foreach($doc in $docs){
    $content=Get-Content -LiteralPath $doc.FullName -Raw
    foreach($match in [regex]::Matches($content,'\[[^\]]+\]\(([^)]+)\)')){
        $url=$match.Groups[1].Value
        if($url -match '^(https?://|#)'){continue}
        $target=($url -split '#')[0]
        if(!$target){continue}
        $count++
        if(!(Test-Path -LiteralPath (Join-Path $doc.DirectoryName $target))){
            $bad += [pscustomobject]@{doc=$doc.FullName;target=$target}
        }
    }
}
$same=(Get-FileHash "$root\src\agentglide.ps1").Hash -eq (Get-FileHash "$root\build\agentglide\agentglide.ps1").Hash
$zipSame=(Get-FileHash "$root\dist\agentglide-win-x64.zip").Hash -eq $package.package
$pointers=@(Get-ChildItem "$root\.agentglide\logs" -Filter 'latest-*.txt'|ForEach-Object {
    $path=(Get-Content $_.FullName -Raw).Trim()
    [pscustomobject]@{pointer=$_.Name;exists=(Test-Path -LiteralPath "$path\result.json")}
})
$warnings=@(Get-Content "$root\.agentglide\logs\build.log"|Where-Object {$_ -match '(src|tests)\\.*warning' -and $_ -notmatch '\\deps\\'})
$check=[ordered]@{
    localLinksChecked=$count;brokenLinks=$bad;sourceDeliveryMatch=$same;zipMatchesValidatedCopy=$zipSame
    latestPointers=$pointers;sourceWarnings=$warnings
    passed=($bad.Count -eq 0 -and $same -and $zipSame -and !($pointers.exists -contains $false) -and $warnings.Count -eq 0)
}
$check|ConvertTo-Json -Depth 6|Tee-Object "$root\.agentglide\logs\final-check.json"
if(!$check.passed){throw 'Delivery verification failed'}
