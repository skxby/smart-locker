param(
    [Parameter(Mandatory=$true)][string]$Command,
    [Parameter(Mandatory=$true)][string]$Log,
    [int]$TimeoutSeconds=20
)
# Direct serial API, not Windows terminal UI automation. COM7 must be released
# by SecureCRT. Never enters credentials or changes the saved SecureCRT session.
$ErrorActionPreference='Stop'
if (Test-Path -LiteralPath $Log) { throw 'Select a new evidence log' }
$s203Port=[System.IO.Ports.SerialPort]::new('COM7',1500000,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$s203Port.ReadTimeout=1000
$s203Port.ReadBufferSize=1048576
$s203Port.WriteTimeout=5000
$s203Transcript=''
$s203Marker='S203_DONE_'+[Guid]::NewGuid().ToString('N')
try {
    $s203Port.Open()
    $s203Port.Write("`r")
    Start-Sleep -Milliseconds 250
    $s203Prompt=$s203Port.ReadExisting()
    $s203Transcript+=$s203Prompt
    if ($s203Prompt -notmatch 'root@RK356X:[^\r\n]*#') {
        throw 'Expected already authenticated RK shell prompt; no command sent'
    }
    # Keep lines below the Linux canonical input limit. Caller supplies a single
    # reviewed shell command; no text is read from GUI or peer output as code.
    if ($Command.Length -gt 1500 -or $Command.Contains("`n") -or $Command.Contains("`r")) {
        throw 'One reviewed command of at most 1500 characters is required'
    }
    $s203Port.Write($Command+"; s203_rc=`$?; printf '\n"+$s203Marker+"=%s\n' `"`$s203_rc`"`r")
    $s203Timer=[Diagnostics.Stopwatch]::StartNew()
    $s203Pattern='(?m)^'+[Regex]::Escape($s203Marker)+'=(\d+)\r?$'
    while ($s203Timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        Start-Sleep -Milliseconds 100
        $s203Transcript+=$s203Port.ReadExisting()
        if ($s203Transcript -match $s203Pattern) {
            if ([int]$Matches[1] -ne 0) { throw ('RK command exit='+$Matches[1]) }
            Write-Output $s203Transcript
            return
        }
    }
    throw 'RK console command timed out; do not blindly replay mutations'
} finally {
    [IO.File]::WriteAllText($Log,$s203Transcript,[Text.UTF8Encoding]::new($false))
    $s203Port.Dispose()
}
