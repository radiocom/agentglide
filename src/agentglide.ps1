# Invoked by agentglide.exe. No profile, no shared configuration, no un-injected fallback.
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
Set-StrictMode -Version Latest
$lock = $null
function Fail([string]$message) { throw $message }
function Keys($table, [string[]]$allowed, [string]$label) {
    if ($table -isnot [System.Collections.IDictionary]) { Fail "$label must be a table" }
    foreach ($key in $table.Keys) { if ($key -cnotin $allowed) { Fail "Unknown field in $label (see README schema)" } }
}
function Text($table, [string]$key, [bool]$required = $true) {
    if (!$table.Contains($key)) { if ($required) { Fail "Missing field: $key" }; return '' }
    if ($table[$key] -isnot [string] -or $table[$key].Contains([char]0)) { Fail "$key must be a string without NUL" }
    return $table[$key]
}
function Choice($table, $key, $default, $choices) {
    $value = if ($table.Contains($key)) { Text $table $key } else { $default }
    if ($value -cnotin $choices) { Fail "Invalid $key" }; return $value
}
function Boolean($table, $key, [bool]$default) {
    if (!$table.Contains($key)) { return $default }
    if ($table[$key] -isnot [bool]) { Fail "$key must be a boolean" }
    return $table[$key]
}
function PathCheck([string]$path, [bool]$mustExist) {
    if ([string]::IsNullOrWhiteSpace($path)) { Fail 'Empty path' }
    # Reject aliases before GetFullPath normalizes them away.
    if ($path -match '(^|[\\/])[^\\/]*[. ]([\\/]|$)' -and $path -notmatch '^\.{1,2}([\\/]|$)') { Fail 'Trailing dot/space aliases are unsupported' }
    $full = [IO.Path]::GetFullPath($path, (Get-Location).Path).TrimEnd('\')
    if ($full -notmatch '^[A-Za-z]:\\.+' -or $full.Length -gt 220 -or $full.Substring(2) -match '[:*?"<>|~]' -or $full -match '(^|\\)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|\\|$)') { Fail 'Use a local drive subdirectory without aliases (maximum 220 characters)' }
    foreach ($part in $full.Substring(3).Split('\')) { if ($part.EndsWith('.') -or $part.EndsWith(' ')) { Fail 'Trailing dot/space aliases are unsupported' } }
    $probe = $full
    while ($probe.Length -gt 3) {
        if (Test-Path -LiteralPath $probe) {
            $item = Get-Item -Force -LiteralPath $probe
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { Fail 'Reparse points in paths are unsupported' }
            if (!$item.PSIsContainer) { Fail 'Base and Overlay paths must be directories' }
        }
        $probe = [IO.Path]::GetDirectoryName($probe)
    }
    if ($mustExist -and !(Test-Path -LiteralPath $full -PathType Container)) { Fail 'Base directory does not exist' }
    return $full
}
function Inside($path, $rootPath) { return $path.Equals($rootPath, [StringComparison]::OrdinalIgnoreCase) -or $path.StartsWith($rootPath+'\', [StringComparison]::OrdinalIgnoreCase) }
function TomlString([string]$value) { return ConvertTo-Json -InputObject $value -Compress -EscapeHandling Default }
function SaveNew($path, $content) {
    $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $bytes = [Text.UTF8Encoding]::new($false).GetBytes($content); $stream.Write($bytes) } finally { $stream.Dispose() }
}
try {
    $cli = @($args)
    if (!$cli.Count -or $cli[0] -in @('--help','-h')) {
        Write-Output 'Usage: agentglide <BASE> <OVERLAY> [--init] [--prompt TEXT] [--non-interactive]'
        Write-Output 'Default: start the configured interactive agent, cwd=BASE. --init only creates/validates configuration.'
        exit 0
    }
    if ($cli.Count -lt 2) { Fail 'Required: agentglide <BASE> <OVERLAY>' }
    $init = $false; $batch = $false; $prompt = $null
    for ($i=2; $i -lt $cli.Count; $i++) {
        switch -CaseSensitive ($cli[$i]) {
            '--init' { $init = $true }
            '--non-interactive' { $batch = $true }
            '--prompt' { if (++$i -ge $cli.Count -or $null -ne $prompt) { Fail '--prompt requires one text argument' }; $prompt = $cli[$i] }
            default { Fail 'Unknown option; use agentglide --help' }
        }
    }
    if ($init -and ($batch -or $null -ne $prompt)) { Fail '--init cannot be combined with a task' }
    if ($batch -and [string]::IsNullOrWhiteSpace($prompt)) { Fail '--non-interactive requires --prompt TEXT' }
    $base = PathCheck $cli[0] $true
    $overlay = PathCheck $cli[1] $false
    if ((Inside $overlay $base) -or (Inside $base $overlay)) { Fail 'Base and Overlay must not overlap' }
    $configPath = Join-Path $overlay 'vfs.toml'
    if (!(Test-Path -LiteralPath $configPath)) {
        if ((Test-Path -LiteralPath $overlay) -and @(Get-ChildItem -LiteralPath $overlay -Force).Count) {
            Fail 'Nonempty Overlay has no vfs.toml. Legacy instances require agentglide-run.exe; no automatic adoption/migration.'
        }
        [IO.Directory]::CreateDirectory($overlay) | Out-Null
        SaveNew $configPath ([IO.File]::ReadAllText("$PSScriptRoot\default-agentglide.toml"))
        Write-Host "Created $configPath"
    }
    if (@(Get-ChildItem -LiteralPath $overlay -Force -Recurse | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { Fail 'Reparse points inside Overlay are unsupported' }
    foreach ($entry in Get-ChildItem -LiteralPath $overlay -Force) {
        if ($entry.Name -cnotin @('vfs.toml','files','.vfs')) { Fail 'Unrecognized/legacy Overlay contents; no automatic migration' }
    }
    $json = & "$PSScriptRoot\agentglide-config.exe" $configPath
    if ($LASTEXITCODE) { Fail 'Cannot load vfs.toml; fix TOML syntax/types before launching' }
    $config = $json | ConvertFrom-Json -AsHashtable
    Keys $config @('version','overlay','agent') 'root'
    if (!$config.Contains('version') -or $config.version -isnot [long] -or $config.version -ne 1) { Fail 'version must be integer 1' }
    Keys $config.overlay @('name','description') 'overlay'
    $name = Text $config.overlay name; $description = Text $config.overlay description
    Keys $config.agent @('type','model','base_url','instructions','executable','credential_env','codex','claude','copilot') 'agent'
    $agent = $config.agent
    $kind = Choice $agent type '' @('codex','claude','copilot')
    $model = Text $agent model; if (!$model.Trim()) { Fail 'model cannot be empty' }
    $url = Text $agent base_url
    $uri = $null
    if (![Uri]::TryCreate($url, [UriKind]::Absolute, [ref]$uri) -or $uri.Scheme -notin @('http','https') -or $uri.UserInfo -or $uri.Query -or $uri.Fragment) { Fail 'base_url must be an HTTP(S) URL without credentials, query, or fragment' }
    if ($kind -eq 'claude' -and $uri.AbsolutePath.TrimEnd('/') -match '/v1$') { Fail 'Claude base_url is the Anthropic endpoint root, without /v1' }
    $instructions = Text $agent instructions
    $codex = if ($agent.Contains('codex')) { $agent.codex } else { @{} }
    $claude = if ($agent.Contains('claude')) { $agent.claude } else { @{} }
    $copilot = if ($agent.Contains('copilot')) { $agent.copilot } else { @{} }
    Keys $codex @('reasoning_effort','approval_policy') 'agent.codex'
    Keys $claude @('effort','permission_mode') 'agent.claude'
    Keys $copilot @('provider_type','wire_api','reasoning_effort','allow_all_tools') 'agent.copilot'
    $copilotProvider = Choice $copilot provider_type 'openai' @('openai','anthropic','azure')
    $copilotWire = Choice $copilot wire_api 'responses' @('responses','completions')
    $copilotReasoning = Choice $copilot reasoning_effort 'low' @('none','minimal','low','medium','high','xhigh','max')
    $copilotAuto = Boolean $copilot allow_all_tools $false
    if ($kind -eq 'copilot' -and $copilotProvider -eq 'anthropic' -and $uri.AbsolutePath.TrimEnd('/') -match '/v1$') { Fail 'Anthropic base_url must omit /v1' }
    if ($kind -eq 'copilot' -and $batch -and !$copilotAuto) { Fail 'Copilot --non-interactive requires agent.copilot.allow_all_tools=true in vfs.toml' }
    $reasoning = Choice $codex reasoning_effort 'low' @('minimal','low','medium','high','xhigh')
    $approval = Choice $codex approval_policy 'on-request' @('on-request','never')
    $effort = Choice $claude effort 'low' @('low','medium','high','xhigh','max')
    $permissions = Choice $claude permission_mode 'default' @('default','acceptEdits','dontAsk','plan')
    $exeSetting = Text $agent executable $false
    $keyName = Text $agent credential_env $false
    if ($keyName -and $keyName -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { Fail 'credential_env must be an environment variable name, not a credential' }
    $internal = Join-Path $overlay '.vfs'
    [IO.Directory]::CreateDirectory($internal) | Out-Null
    try { $lock = [IO.File]::Open("$internal\lock", 'OpenOrCreate', 'ReadWrite', 'None') } catch { Fail 'Overlay is already in use (exclusive launch lock)' }
    $statePath = "$internal\state.json"
    if (Test-Path -LiteralPath $statePath) {
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json -AsHashtable
        if ($state.format -ne 1 -or $state.base -isnot [string] -or !$state.base.Equals($base, [StringComparison]::OrdinalIgnoreCase)) { Fail 'Overlay is bound to a different Base or unsupported state format' }
    } else {
        if ((Test-Path -LiteralPath "$overlay\files") -or @(Get-ChildItem -LiteralPath $internal -Force | Where-Object Name -NE 'lock').Count) { Fail 'Missing binding state in an existing Overlay; refusing to rebind' }
        SaveNew $statePath (ConvertTo-Json -InputObject ([ordered]@{format=1;base=$base}))
    }
    Write-Host "Overlay: $name"
    Write-Host "Description (metadata only): $description"
    Write-Host "Base: $base"
    Write-Host "Files: $overlay\files"
    if ($init) { Write-Host "Ready. Edit $configPath then run agentglide with the same two directories."; exit 0 }
    if ($exeSetting) {
        $exe = [IO.Path]::GetFullPath($exeSetting, $overlay)
        if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or [IO.Path]::GetExtension($exe) -ine '.exe') { Fail 'agent.executable must reference an existing native .exe (relative to Overlay)' }
    } else {
        $found = Get-Command "$kind.exe" -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if (!$found) { Fail "Missing $kind.exe on PATH; set agent.executable in vfs.toml" }
        $exe = $found.Source
    }
    foreach ($path in @('files','.vfs\deleted','.vfs\logs','.vfs\clients\codex','.vfs\clients\claude','.vfs\clients\copilot\instructions','.vfs\cache\temp','.vfs\cache\home','.vfs\cache\appdata','.vfs\cache\localappdata')) { [IO.Directory]::CreateDirectory((Join-Path $overlay $path)) | Out-Null }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = "$PSScriptRoot\agentglide-run.exe"
    $start.WorkingDirectory = $base
    $start.UseShellExecute = $false
    # No inherited client routing, credentials or background-server endpoints.
    foreach ($key in @($start.Environment.Keys)) {
        if ($key -match '^(CODEX_|CLAUDE_|ANTHROPIC_|OPENAI_)') { [void]$start.Environment.Remove($key) }
        if ($kind -eq 'copilot' -and $key -match '^(COPILOT_|GITHUB_|GH_|OTEL_)') { [void]$start.Environment.Remove($key) }
    }
    $credential = if ($keyName) { [Environment]::GetEnvironmentVariable($keyName) } else { $null }
    if ($keyName -and !$credential) { Fail 'The configured credential_env is not set; no credentials were copied' }
    foreach ($pair in @(@('TEMP','temp'),@('TMP','temp'),@('TMPDIR','temp'),@('HOME','home'),@('APPDATA','appdata'),@('LOCALAPPDATA','localappdata'))) { $start.Environment[$pair[0]] = "$internal\cache\$($pair[1])" }
    $start.Environment['POWERSHELL_TELEMETRY_OPTOUT'] = '1'
    $arguments = @('--layout-v1',$base,$overlay,'--',$exe)
    if ($kind -eq 'codex') {
        $clientHome = "$internal\clients\codex"
        $start.Environment['CODEX_HOME'] = $clientHome
        $cfg = @(
            ('model='+(TomlString $model)), 'model_provider="vfs"',
            ('model_reasoning_effort='+(TomlString $reasoning)),
            ('developer_instructions='+(TomlString $instructions)),
            'features.fast_mode=false', 'features.ultrafast_mode=false',
            'allow_login_shell=false',
            'model_providers.vfs.name="Overlay configured provider"',
            ('model_providers.vfs.base_url='+(TomlString $url)), 'model_providers.vfs.wire_api="responses"',
            'model_providers.vfs.requires_openai_auth=false', 'model_providers.vfs.supports_websockets=false',
            'model_providers.vfs.request_max_retries=1', 'model_providers.vfs.stream_max_retries=1'
        )
        if ($credential) { $start.Environment['VFS_CLIENT_API_KEY']=$credential; $cfg += 'model_providers.vfs.env_key="VFS_CLIENT_API_KEY"' }
        # CLI overrides take precedence over Base-local client config as well.
        foreach ($setting in $cfg) { $arguments += @('-c',$setting) }
        $cfg += @(('[projects.'+(TomlString $base)+']'),'trust_level="trusted"')
        [IO.File]::WriteAllLines("$clientHome\config.toml", [string[]]$cfg)
        $arguments += @('--no-daemon','-a',$approval)
        if ($batch) { $arguments += @('exec','--ignore-user-config','--ignore-rules','--ephemeral','--skip-git-repo-check','-s','danger-full-access','--json',$prompt) }
        else { $arguments += @('--no-alt-screen','-s','danger-full-access'); if ($null -ne $prompt) { $arguments += $prompt } }
    } elseif ($kind -eq 'claude') {
        $start.Environment['CLAUDE_CONFIG_DIR'] = "$internal\clients\claude"
        $start.Environment['ANTHROPIC_BASE_URL'] = $url.TrimEnd('/')
        if ($credential) { $start.Environment['ANTHROPIC_API_KEY'] = $credential }
        elseif ($uri.IsLoopback) { $start.Environment['ANTHROPIC_API_KEY'] = 'local-vfs-placeholder' }
        else { Fail 'Claude requires explicit credential_env for a non-loopback provider' }
        foreach ($key in @('ANTHROPIC_DEFAULT_HAIKU_MODEL','ANTHROPIC_DEFAULT_SONNET_MODEL','ANTHROPIC_DEFAULT_OPUS_MODEL','CLAUDE_CODE_SUBAGENT_MODEL')) { $start.Environment[$key] = $model }
        $start.Environment['CLAUDE_CODE_USE_POWERSHELL_TOOL']='1'
        $start.Environment['DISABLE_AUTOUPDATER']='1'
        $start.Environment['DISABLE_TELEMETRY']='1'
        $start.Environment['DISABLE_ERROR_REPORTING']='1'
        $arguments += @('--bare','--model',$model,'--effort',$effort,'--setting-sources','','--strict-mcp-config','--mcp-config','{"mcpServers":{}}','--disable-slash-commands','--tools','Read,Edit,Write,PowerShell')
        if ($permissions -ne 'default') { $arguments += @('--permission-mode',$permissions) }
        if ($permissions -eq 'dontAsk') { $arguments += @('--allowedTools','Read,Edit,Write,PowerShell') }
        if ($instructions) { $arguments += @('--append-system-prompt',$instructions) }
        if ($batch) { $arguments += @('--print','--verbose','--output-format','stream-json','--no-session-persistence') }
        if ($null -ne $prompt) { $arguments += $prompt }
    } else {
        $clientHome = "$internal\clients\copilot"
        $instructionsDir = "$clientHome\instructions"
        [IO.File]::WriteAllText("$instructionsDir\AGENTS.md",$instructions)
        [ordered]@{autoUpdate=$false;memory=$false;disableAllHooks=$true;ide=@{autoConnect=$false;openDiffOnEdit=$false};powershellFlags=@('-NoProfile','-NoLogo')} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$clientHome\settings.json"
        $start.Environment['USERPROFILE'] = "$internal\cache\home"
        $start.Environment['XDG_CONFIG_HOME'] = "$internal\cache\home"
        $start.Environment['XDG_CACHE_HOME'] = "$internal\cache\temp"
        $start.Environment['COPILOT_HOME'] = $clientHome
        $start.Environment['COPILOT_PROVIDER_BASE_URL'] = $url
        $start.Environment['COPILOT_PROVIDER_TYPE'] = $copilotProvider
        $start.Environment['COPILOT_PROVIDER_WIRE_API'] = $copilotWire
        $start.Environment['COPILOT_PROVIDER_TRANSPORT'] = 'http'
        $start.Environment['COPILOT_MODEL'] = $model
        if ($credential) { $start.Environment['COPILOT_PROVIDER_API_KEY'] = $credential }
        $start.Environment['COPILOT_OFFLINE'] = if ($uri.IsLoopback) { 'true' } else { 'false' }
        $start.Environment['COPILOT_AUTO_UPDATE'] = 'false'
        $start.Environment['COPILOT_CUSTOM_INSTRUCTIONS_DIRS'] = $instructionsDir
        $start.Environment['USE_TGREP'] = 'false'
        $arguments += @('--no-auto-update','--disable-client-telemetry','--disable-builtin-mcps','--sync-session=off','--add-dir',$instructionsDir,'--model',$model,'--reasoning-effort',$copilotReasoning,'--log-dir',"$internal\logs",'--log-level','info')
        if ($copilotAuto) { $arguments += '--allow-all-tools' }
        if ($batch) { $arguments += @('--trust-folder','--no-ask-user','--output-format','json','--prompt',$prompt) }
        elseif ($null -ne $prompt) { $arguments += @('--interactive',$prompt) }
    }
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    # Only non-secret launch metadata. Never log environment, task, or auth values.
    [ordered]@{time=[DateTime]::UtcNow.ToString('o');agent=$kind;executable=$exe;model=$model;base=$base;mode=$(if($batch){'non-interactive'}else{'interactive'});instructionsMechanism=$(switch($kind){codex{'developer_instructions'}claude{'--append-system-prompt'}copilot{'COPILOT_CUSTOM_INSTRUCTIONS_DIRS / AGENTS.md'}});noDaemon=($kind -eq 'codex')} | ConvertTo-Json | Set-Content -LiteralPath "$internal\logs\last-launch.json"
    $process = [Diagnostics.Process]::Start($start)
    $process.WaitForExit(); $code=$process.ExitCode; $process.Dispose()
    if ($code -eq 125) { [Console]::Error.WriteLine('agentglide: injection failed; target terminated before resume') }
    exit $code
} catch {
    [Console]::Error.WriteLine('agentglide: ' + $_.Exception.Message)
    exit 2
} finally { if ($null -ne $lock) { $lock.Dispose() } }
