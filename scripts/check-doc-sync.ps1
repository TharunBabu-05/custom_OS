$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'build-apps.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Cannot parse build-apps.ps1' }
$loops = @($ast.FindAll({ param($node)
    $node -is [System.Management.Automation.Language.ForEachStatementAst] -and
        $node.Variable.VariablePath.UserPath -ceq 'app'
}, $true))
if ($loops.Count -ne 1) { throw 'Expected one app build loop' }
$apps = @($loops[0].Condition.FindAll({ param($node)
    $node -is [System.Management.Automation.Language.StringConstantExpressionAst]
}, $true) | ForEach-Object Value)
$sources = @(Get-ChildItem -LiteralPath (Join-Path $root 'user/applications') -File -Filter '*.c' | ForEach-Object BaseName)
if (!$apps.Count -or $apps.Count -ne $sources.Count -or (Compare-Object ($apps | Sort-Object) ($sources | Sort-Object))) {
    throw 'ELF build targets differ from user/applications sources'
}

$abi = Get-Content -LiteralPath (Join-Path $root 'include/user_abi.h') -Raw
$enum = [regex]::Match($abi, '(?s)enum uos_syscall\s*\{([^}]*)\}').Groups[1].Value
$ids = @([regex]::Matches($enum, '\bUOS_SYS_\w+\s*=\s*(\d+)\b') | ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object)
if (!$ids.Count -or ($ids -join ',') -cne ((0..($ids.Count - 1)) -join ',')) {
    throw 'The syscall enum must contain one explicit ID per slot starting at zero'
}
$process = Get-Content -LiteralPath (Join-Path $root 'include/process.h') -Raw
$foreground = [regex]::Match($process, '\bPROCESS_LIMIT\s*=\s*(\d+)').Groups[1].Value
$services = [regex]::Match($process, '\bSERVICE_LIMIT\s*=\s*(\d+)').Groups[1].Value
if (!$foreground -or !$services) { throw 'Cannot read the process or service slot limit' }

$dash = [char]0x2013
$expected = @("$($apps.Count) userland ELF programs", "$($ids.Count) syscall IDs (0$dash$($ids[-1]))",
    "$foreground foreground process slots and $services supervised service slots")
foreach ($path in @('README.md', 'docs/os-status.md')) {
    $doc = (Get-Content -LiteralPath (Join-Path $root $path) -Raw -Encoding UTF8) -replace '\s+', ' '
    foreach ($phrase in $expected) {
        if ($doc -cnotmatch "(?<!\d)$([regex]::Escape($phrase))") { throw "$path must contain '$phrase'" }
    }
}
Write-Output "PASS: docs match $($apps.Count) applications, $($ids.Count) syscall IDs, $foreground foreground slots, and $services service slots."
