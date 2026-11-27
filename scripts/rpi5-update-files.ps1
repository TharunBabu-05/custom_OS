Add-Type -AssemblyName System.IO.Compression.FileSystem

function Test-Rpi5UpdateName([string]$Name) {
    $Name -cmatch '\A(OUROS\.IMG|CMDLINE\.TXT|OUROSLIC\.TXT|[A-Z][A-Z0-9_]{0,7}\.(ELF|OSH|OPR))\z' -and
        $Name -cnotmatch '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])\.'
}

function Get-Rpi5UpdateHash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash
}

function Get-Rpi5ConfigHash([string]$Path) {
    $config=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path)).Replace("`r`n","`n")
    $hasher=[Security.Cryptography.SHA256]::Create()
    try { [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($config))).Replace('-','') }
    finally { $hasher.Dispose() }
}

function Assert-Rpi5UpdateManifest($Manifest, [string]$Format) {
    if ($Manifest.format -cne $Format -or $Manifest.board -cne 'rpi5-header' -or
        $Manifest.config_sha256 -cnotmatch '\A[0-9A-F]{64}\z' -or $Manifest.dtb_sha256 -cnotmatch '\A[0-9A-F]{64}\z') {
        throw 'Invalid Pi 5 header boot-update manifest'
    }
    $seen=@{}
    $records=@($Manifest.files)
    if ($records.Count -lt 1 -or $records.Count -gt 126) { throw 'Invalid boot-update file count' }
    foreach ($file in $records) {
        if (!(Test-Rpi5UpdateName $file.name) -or $seen.ContainsKey($file.name)) { throw "Invalid boot-update name: $($file.name)" }
        if ([string]::IsNullOrEmpty($file.sha256)) {
            if ($Format -cne 'OurOS boot rollback v1' -or $file.name -ceq 'OUROS.IMG' -or
                'sha256' -cnotin $file.PSObject.Properties.Name) { throw "Missing hash for $($file.name)" }
        } elseif ($file.sha256 -cnotmatch '\A[0-9A-F]{64}\z') {
            throw "Invalid hash for $($file.name)"
        }
        $seen[$file.name]=$true
    }
    if ($Format -ceq 'OurOS boot package v1' -and
        (!$seen.ContainsKey('OUROS.IMG') -or !$seen.ContainsKey('SHELL.ELF') -or !$seen.ContainsKey('CMDLINE.TXT'))) {
        throw 'Boot package requires OUROS.IMG, SHELL.ELF and CMDLINE.TXT'
    }
}

function Expand-Rpi5UpdatePackage([string]$Package, [string]$Stage) {
    $zip=[IO.Compression.ZipFile]::OpenRead($Package)
    try {
        $seen=@{}
        if ($zip.Entries.Count -gt 127) { throw 'Too many boot package entries' }
        foreach ($entry in $zip.Entries) {
            $name=$entry.FullName
            $limit=switch -CaseSensitive ($name) {
                'OUROS.IMG' { 0x1f80000 }
                'CMDLINE.TXT' { 1024 }
                'OUROSLIC.TXT' { 4096 }
                'manifest.json' { 65536 }
                { $_.EndsWith('.OSH',[StringComparison]::Ordinal) } { 4096 }
                { $_.EndsWith('.OPR',[StringComparison]::Ordinal) } { 65536 }
                default { 32768 }
            }
            if (($name -cne 'manifest.json' -and !(Test-Rpi5UpdateName $name)) -or
                $seen.ContainsKey($name) -or $entry.Length -lt 1 -or $entry.Length -gt $limit) {
                throw "Invalid boot package entry: $name"
            }
            $seen[$name]=$true
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,(Join-Path $Stage $name),$false)
        }
    } finally { $zip.Dispose() }
    $manifest=Get-Content -Raw -LiteralPath (Join-Path $Stage 'manifest.json') | ConvertFrom-Json
    Assert-Rpi5UpdateManifest $manifest 'OurOS boot package v1'
    if ($seen.Count -ne $manifest.files.Count+1) { throw 'Package and manifest entries differ' }
    foreach ($file in $manifest.files) {
        if ((Get-Rpi5UpdateHash (Join-Path $Stage $file.name)) -cne $file.sha256) {
            throw "Package checksum mismatch: $($file.name)"
        }
    }
    return $manifest
}
