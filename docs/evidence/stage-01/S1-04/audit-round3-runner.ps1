# S1-04 round-2 / round-3 审计 runner（历史归档；必须保存为 UTF-8 BOM）
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File audit-runner.ps1
# 不能用 $ErrorActionPreference='Stop'：codex 把版本横幅写到 stderr，PowerShell 会把它当
# NativeCommandError 抛出，整轮在启动瞬间中止。
$ErrorActionPreference = 'Continue'
$root = '%REPO%'
$ev   = Join-Path $root 'docs/evidence/stage-01/S1-04'
$in   = Join-Path $ev 'audit-prompt.txt'
$out  = Join-Path $ev 'audit-codex-output.txt'
$log  = Join-Path $ev 'audit-run.log'
Set-Location $root
$start = Get-Date
Get-Content -Raw -Encoding UTF8 $in | codex exec --skip-git-repo-check -s danger-full-access - *> $out
$code = $LASTEXITCODE
$end  = Get-Date
"EXIT=$code" | Out-File -FilePath $log -Encoding utf8
"START=$($start.ToString('s'))" | Out-File -FilePath $log -Encoding utf8 -Append
"END=$($end.ToString('s'))" | Out-File -FilePath $log -Encoding utf8 -Append
"MINUTES=$([math]::Round(($end-$start).TotalMinutes,1))" | Out-File -FilePath $log -Encoding utf8 -Append
Get-Content -Encoding UTF8 $log
