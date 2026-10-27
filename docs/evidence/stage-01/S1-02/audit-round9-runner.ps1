$ErrorActionPreference = 'Continue'
$in = 'C:/Users/Public/s102_round9_in.txt'
$out = 'C:/Users/Public/s102_round9_out.txt'
$log = 'C:/Users/Public/s102_round9_run.log'
"START $(Get-Date -Format o)" | Out-File -Encoding utf8 $log
Get-Content -Raw -Encoding UTF8 $in | codex exec --skip-git-repo-check -s workspace-write - *> $out
"EXIT=$LASTEXITCODE" | Out-File -Encoding utf8 -Append $log
"END $(Get-Date -Format o)" | Out-File -Encoding utf8 -Append $log
