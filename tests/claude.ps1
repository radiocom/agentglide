param([string]$ClaudePath='', [int]$TimeoutSeconds=300, [string]$BaseUrl='http://127.0.0.1:1010', [string]$Model='gpt-6-luna')
& "$PSScriptRoot\agents.ps1" -Agent claude -ClientPath $ClaudePath -TimeoutSeconds $TimeoutSeconds -BaseUrl $BaseUrl -Model $Model
