param([string]$CodexPath='', [int]$TimeoutSeconds=300, [string]$BaseUrl='http://127.0.0.1:1010/v1', [string]$Model='gpt-6-luna')
& "$PSScriptRoot\agents.ps1" -Agent codex -ClientPath $CodexPath -TimeoutSeconds $TimeoutSeconds -BaseUrl $BaseUrl -Model $Model
