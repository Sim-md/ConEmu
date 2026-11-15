param([Parameter(Mandatory)][string]$Archive, [string]$WorkDir = $env:TEMP)

$ErrorActionPreference = 'Stop'
$Archive = (Resolve-Path -LiteralPath $Archive).Path
. (Join-Path $PSScriptRoot '../../Release/ConEmu/Scripts/Arm64HookUpdate.ps1')
$lab = Join-Path $WorkDir ('conemu-arm64-hook-test-' + [guid]::NewGuid().ToString('N'))
$root = Join-Path $lab 'installed'
$candidate = Join-Path $lab 'candidate'
Expand-Archive -LiteralPath $Archive -DestinationPath $root
Expand-Archive -LiteralPath $Archive -DestinationPath $candidate
$gui = Join-Path $root 'ConEmu64.exe'
$version = Get-HookBuild $gui
$validHash = (Get-FileHash -LiteralPath $Archive).Hash

# Give the fixture an older Windows file version by editing its VS_FIXEDFILEINFO.
$bytes = [IO.File]::ReadAllBytes($gui)
$fixedVersion = -1
$signature = [Convert]::ToUInt32('feef04bd', 16)
for ($i = 0; $i -lt $bytes.Length - 24; $i += 4) {
    if ([BitConverter]::ToUInt32($bytes, $i) -eq $signature -and
        [BitConverter]::ToUInt32($bytes, $i + 4) -eq 0x10000) {
        $fixedVersion = $i
        break
    }
}
if ($fixedVersion -lt 0) { throw 'Cannot find executable version resource' }
[BitConverter]::GetBytes([uint32]0x00170007).CopyTo($bytes, $fixedVersion + 8)
[BitConverter]::GetBytes([uint32]0x00180000).CopyTo($bytes, $fixedVersion + 12)
[IO.File]::WriteAllBytes($gui, $bytes)
if ((Get-HookBuild $gui) -ne '230724') { throw 'Cannot construct old executable fixture' }
$sentinels = @('Far.exe', 'ConEmu/ConEmu.xml', 'Plugins/ConEmu/Background/Background.xml')
foreach ($file in $sentinels) {
    [IO.File]::WriteAllText((Join-Path $root $file), "User fixture for $file")
}

function Get-State([string]$Directory, [string[]]$Files) {
    ($Files | ForEach-Object { (Get-FileHash -LiteralPath (Join-Path $Directory $_)).Hash }) -join ','
}

function Test-ExpectedFailure([scriptblock]$Operation, [string]$Pattern) {
    try { & $Operation }
    catch {
        if ($_ -notmatch $Pattern) { throw }
        return
    }
    throw "Expected failure: $Pattern"
}

$initial = Get-State $root ($updateFiles + $sentinels)
$settings = Get-State $root $sentinels
Test-ExpectedFailure { Install-HookUpdate $root $Archive ('0' * 64) $version } 'SHA-256'
if ((Get-State $root ($updateFiles + $sentinels)) -ne $initial) { throw 'Bad checksum changed files' }
'PASS invalid SHA-256 rejected before replacing files'

$lockedFile = [IO.File]::Open((Join-Path $root $updateFiles[2]), 'Open', 'Read', 'None')
try {
    Test-ExpectedFailure { Install-HookUpdate $root $Archive $validHash $version } 'cannot access|being used|sharing'
} finally { $lockedFile.Dispose() }
if ((Get-State $root ($updateFiles + $sentinels)) -ne $initial) { throw 'Locked installation changed files' }
'PASS locked installation rejected before replacing files'

$replace = (Get-Item Function:Set-HookFile).ScriptBlock
$script:completedReplacements = 0
function Set-HookFile([string]$Source, [string]$Target, [string]$Backup) {
    if ($script:completedReplacements -eq 2) {
        $lock = [IO.File]::Open($Target, 'Open', 'Read', 'None')
        try { & $replace $Source $Target $Backup }
        finally { $lock.Dispose() }
    } else {
        & $replace $Source $Target $Backup
        $script:completedReplacements++
    }
}
Test-ExpectedFailure { Install-HookUpdate $root $Archive $validHash $version } 'cannot access|being used|sharing'
Set-Item Function:Set-HookFile -Value $replace
if ($script:completedReplacements -ne 2 -or (Get-State $root ($updateFiles + $sentinels)) -ne $initial) {
    throw 'Rollback did not restore the installation'
}
'PASS two replacements rolled back after a mid-install sharing violation'

$work = Join-Path $root '.conemu-arm64-hook-update'
$previousGui = Join-Path $work "$($updateFiles.Count - 1).bak"
$copiedGui = Join-Path $work 'test-staged.exe'
[IO.File]::Copy((Join-Path $candidate 'ConEmu64.exe'), $copiedGui)
[IO.File]::WriteAllText((Join-Path $work 'active'), $version)
[IO.File]::Replace($copiedGui, $gui, $previousGui)
Install-HookUpdate -Root $root -RecoveryOnly
if ((Get-State $root ($updateFiles + $sentinels)) -ne $initial) { throw 'Interrupted update was not restored' }
'PASS interrupted transaction recovered on the next run'

Install-HookUpdate $root $Archive $validHash $version
if ((Get-State $root $updateFiles) -ne (Get-State $candidate $updateFiles)) { throw 'Incomplete update' }
if ((Get-State $root $sentinels) -ne $settings) { throw 'Far or settings changed' }
"PASS installed $version and preserved Far and both profiles"

Test-ExpectedFailure { Install-HookUpdate $root $Archive $validHash $version } 'not a newer'
'PASS same-version package rejected'
"Test fixture: $lab"
