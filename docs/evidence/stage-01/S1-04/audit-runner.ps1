# S1-04 round-5 审计 runner（必须保存为 UTF-8 BOM，否则中文路径按 ANSI 解码）
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File audit-runner.ps1
#
# 两条踩过的坑：
# 1) 不能用 $ErrorActionPreference='Stop'：codex 把版本横幅写到 stderr，PowerShell 会把它当
#    NativeCommandError 抛出，整轮在启动瞬间中止。
# 2) 不能用 `Get-Content -Raw | codex exec -` 传中文提示词：PowerShell 5.1 的 $OutputEncoding
#    默认是 ASCII，中文经管道传给原生程序时全部变成 '?'，codex 收到的就是一堆问号
#    （round-3 的 audit.md 全篇变 '?' 就是这个原因）。这里改成把提示词文件路径写进一条
#    纯 ASCII 的指令里，让 codex 自己用 UTF-8 读文件，彻底绕开管道编码。
$ErrorActionPreference = 'Continue'
$OutputEncoding = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)

$root = '%REPO%'
$ev   = Join-Path $root 'docs/evidence/stage-01/S1-04'
$out  = Join-Path $ev 'audit-codex-output.txt'
$log  = Join-Path $ev 'audit-run.log'
$md   = Join-Path $ev 'audit.md'
Set-Location $root
$start = Get-Date
$instruction = 'Read the UTF-8 file docs/evidence/stage-01/S1-04/audit-prompt.txt in full and carry out exactly the audit task it describes. Do not ask questions; produce the report file it names.'
codex exec --skip-git-repo-check -s danger-full-access $instruction *> $out
$code = $LASTEXITCODE
$end  = Get-Date
# 编码校验：round-3 曾整篇中文变 '?'，任何一项不达标都要恢复或重跑。
$mdBytes = 0
$nonAscii = 0
$qmarks = 0
if (Test-Path $md) {
    $mdText = Get-Content -Raw -Encoding UTF8 $md
    $mdBytes = [System.Text.Encoding]::UTF8.GetByteCount($mdText)
    $nonAscii = [regex]::Matches($mdText, '[^\x00-\x7F]').Count
    $qmarks = [regex]::Matches($mdText, '\?').Count
}
"EXIT=$code" | Out-File -FilePath $log -Encoding utf8
"START=$($start.ToString('s'))" | Out-File -FilePath $log -Encoding utf8 -Append
"END=$($end.ToString('s'))" | Out-File -FilePath $log -Encoding utf8 -Append
"MINUTES=$([math]::Round(($end-$start).TotalMinutes,1))" | Out-File -FilePath $log -Encoding utf8 -Append
"AUDIT_MD_BYTES=$mdBytes" | Out-File -FilePath $log -Encoding utf8 -Append
"AUDIT_MD_NON_ASCII=$nonAscii" | Out-File -FilePath $log -Encoding utf8 -Append
"AUDIT_MD_QUESTION_MARKS=$qmarks" | Out-File -FilePath $log -Encoding utf8 -Append
Get-Content -Encoding UTF8 $log
