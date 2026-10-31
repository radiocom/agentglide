$ErrorActionPreference = 'Stop'
function Check($condition, $name) { if (!$condition) { throw "FAIL $name" }; Write-Output "PASS $name" }
Check ((Get-Content -Raw -LiteralPath 'fallback.txt').Trim() -ceq 'FALLBACK') 'powershell-read-fallback'
Set-Content -LiteralPath 'existing.txt' -Value 'POWERSHELL'
Set-Content -LiteralPath 'new.txt' -Value 'NEW'
Remove-Item -LiteralPath 'delete.txt'
Move-Item -LiteralPath 'rename.txt' -Destination 'renamed.txt'
$names = @(Get-ChildItem -Name)
Check ($names -contains 'fallback.txt' -and $names -contains 'new.txt' -and $names -contains 'renamed.txt' -and $names -notcontains 'delete.txt' -and $names -notcontains 'rename.txt') 'powershell-merged-enumeration'
Check ((Get-Content -Raw -LiteralPath 'existing.txt').Trim() -ceq 'POWERSHELL') 'powershell-cow'
& powershell.exe -NoProfile -NonInteractive -Command "[IO.File]::WriteAllText('child.txt','CHILD')"
Check ($LASTEXITCODE -eq 0 -and (Get-Content -Raw -LiteralPath 'child.txt') -ceq 'CHILD') 'powershell-nested-child'
$codex = (Get-Command codex.exe -CommandType Application -ErrorAction Stop).Source
$patch = "*** Begin Patch`n*** Update File: existing.txt`n@@`n-POWERSHELL`n+CODEX-LOCAL-PATCH`n*** Add File: codex-patch.txt`n+CODEX-PATCH`n*** End Patch"
& $codex --codex-run-as-apply-patch $patch
Check ($LASTEXITCODE -eq 0) 'real-codex-local-apply-patch-exit'
Check ((Get-Content -Raw -LiteralPath 'existing.txt').Trim() -ceq 'CODEX-LOCAL-PATCH' -and (Get-Content -Raw -LiteralPath 'codex-patch.txt').Trim() -ceq 'CODEX-PATCH') 'real-codex-local-apply-patch-content'
