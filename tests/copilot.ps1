param([int]$TimeoutSeconds=300)
# Real Copilot CLI model test through the public two-directory entry point.
& "$PSScriptRoot\agents.ps1" -Agent copilot -TimeoutSeconds $TimeoutSeconds
