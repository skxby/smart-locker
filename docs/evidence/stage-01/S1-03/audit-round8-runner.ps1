# S1-03 round-8 审计 runner（必须保存为 UTF-8 BOM，否则中文路径按 ANSI 解码）
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File audit-round8-runner.ps1
# 不能用 $ErrorActionPreference='Stop'：codex 把版本横幅写到 stderr，PowerShell 会把它当
# NativeCommandError 抛出，整轮在启动瞬间中止。
$ErrorActionPreference = 'Continue'
$root = '%REPO%'
$ev   = Join-Path $root 'docs/evidence/stage-01/S1-03'
$in   = Join-Path $ev 'audit-round8-prompt.txt'
$out  = Join-Path $ev 'audit-round8-codex-output.txt'
$log  = Join-Path $ev 'audit-round8-run.log'
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
