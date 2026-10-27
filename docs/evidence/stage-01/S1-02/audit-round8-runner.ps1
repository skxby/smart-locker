$ErrorActionPreference = 'Continue'
$in = 'C:/Users/Public/s102_round8_in.txt'
$out = 'C:/Users/Public/s102_round8_out.txt'
$log = 'C:/Users/Public/s102_round8_run.log'
"[start] $(Get-Date -Format o)" | Out-File -FilePath $log -Encoding utf8
Get-Content -Raw -Encoding UTF8 $in | codex exec --skip-git-repo-check -s workspace-write - *> $out
"[exit] $LASTEXITCODE" | Out-File -FilePath $log -Append -Encoding utf8
"[end] $(Get-Date -Format o)" | Out-File -FilePath $log -Append -Encoding utf8
