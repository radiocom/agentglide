$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
& "$taskRoot\dependencies.ps1"
$env:TEMP = Join-Path $taskRoot '.agentglide\cache'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Force -Path "$taskRoot\build\agentglide", "$taskRoot\.agentglide\logs", $env:TEMP | Out-Null
$vs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC x64 C build tools required' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$lines = @(
    '@echo off',
    "call `"$vcvars`" >nul",
    "cd /d `"$taskRoot\build\agentglide`"",
    'cl /nologo /TC /std:c11 /W4 /O2 /MT /D_CRT_SECURE_NO_WARNINGS /I..\..\deps\minhook\include /LD ..\..\src\agentglide.c ..\..\src\inject.c ..\..\deps\minhook\src\buffer.c ..\..\deps\minhook\src\hook.c ..\..\deps\minhook\src\trampoline.c ..\..\deps\minhook\src\hde\hde64.c /link /OUT:agentglide.dll',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /TC /std:c11 /W4 /O2 /MT /D_CRT_SECURE_NO_WARNINGS ..\..\src\launcher.c ..\..\src\inject.c /Fe:agentglide-run.exe',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /TC /std:c11 /W4 /O2 /MT /D_CRT_SECURE_NO_WARNINGS ..\..\src\cli.c /Fe:agentglide.exe',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /TC /std:c11 /W4 /O2 /MT /D_CRT_SECURE_NO_WARNINGS ..\..\src\config.c ..\..\deps\tomlc99\toml.c /Fe:agentglide-config.exe',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /TC /std:c11 /W4 /O2 /MT /D_CRT_SECURE_NO_WARNINGS ..\..\tests\fixture.c /Fe:fixture.exe',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /TC /std:c11 /W4 /O2 /MT /D_CRT_SECURE_NO_WARNINGS ..\..\tests\cli-probe.c /Fe:cli-probe.exe',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /TC /std:c11 /W4 /O2 /MT /D_CRT_SECURE_NO_WARNINGS ..\..\tests\path-fixture.c /Fe:path-fixture.exe',
    'exit /b %errorlevel%'
)
Set-Content -LiteralPath "$taskRoot\build\agentglide\compile.cmd" -Value $lines -Encoding ascii
& cmd.exe /d /c "$taskRoot\build\agentglide\compile.cmd" 2>&1 | Tee-Object -FilePath "$taskRoot\.agentglide\logs\build.log"
if ($LASTEXITCODE) { throw "Build failed: $LASTEXITCODE" }

Copy-Item -LiteralPath (Join-Path $taskRoot 'src\agentglide.ps1'), (Join-Path $taskRoot 'src\default-agentglide.toml') -Destination (Join-Path $taskRoot 'build\agentglide') -Force
