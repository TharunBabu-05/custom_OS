function Get-Rpi5DtbAddress([string[]]$Config) {
    $values = @($Config | Where-Object { $_ -match '^device_tree_address\s*=' })
    if ($values.Count -ne 1 -or $values[0] -notmatch '^device_tree_address\s*=\s*0x([0-9a-fA-F]+)$') {
        throw 'Missing or invalid device_tree_address'
    }
    $address = [Convert]::ToUInt64($Matches[1], 16)
    $limits = @($Config | Where-Object { $_ -match '^device_tree_end\s*=' })
    if ($limits.Count -ne 1 -or $limits[0] -notmatch '^device_tree_end\s*=\s*0x([0-9a-fA-F]+)$') {
        throw 'Missing or invalid device_tree_end'
    }
    $end = [Convert]::ToUInt64($Matches[1], 16)
    if (!(($address -eq 0x200000 -and $end -eq 0x300000) -or
          ($address -eq 0x02000000 -and $end -eq 0x02200000))) { throw 'Unsupported Pi 5 DTB window' }
    $address
}

function Assert-Rpi5ImageHeader([byte[]]$Kernel,[uint64]$DtbAddress) {
    if ($Kernel.Length -lt 64 -or [BitConverter]::ToUInt32($Kernel,56) -ne 0x644d5241 -or
        [BitConverter]::ToUInt64($Kernel,8) -ne 0x80000 -or
        [BitConverter]::ToUInt64($Kernel,16) -lt $Kernel.Length -or
        [BitConverter]::ToUInt64($Kernel,16) -gt ($DtbAddress-0x80000)) {
        throw 'Invalid AArch64 Image header or image overlaps the configured DTB'
    }
}

function Test-Rpi5KernelConsole([byte[]]$Kernel,[string]$Console,[switch]$RequireMarker) {
    $image = [Text.Encoding]::ASCII.GetString($Kernel)
    $header = $image.Contains("OUROS_PI5_CONSOLE=header`0")
    $debug = $image.Contains("OUROS_PI5_CONSOLE=debug`0")
    if ($header -or $debug -or $RequireMarker) {
        return ($header -ne $debug) -and ($header -eq ($Console -eq 'header'))
    }
    # Older images used the board name for identification.
    return $image.Contains("rpi5-header`0") -eq ($Console -eq 'header')
}

function Get-Rpi5Profile {
    param([ValidateSet('debug','header')][string]$Console = 'debug')
    if ($Console -eq 'header') {
        return @{
            Console='header'; Output='build/rpi5-header'
            Config='boot/rpi5/config-header.txt'; Readme='boot/rpi5/README-header.txt'
            Image='OurOS-Pi5-header-experimental-sd.img'
            Uart='RP1 UART0, GPIO14/15, physical header pins 8/10'
        }
    }
    return @{
        Console='debug'; Output='build/rpi5'
        Config='boot/rpi5/config.txt'; Readme='boot/rpi5/README.txt'
        Image='OurOS-Pi5-experimental-sd.img'
        Uart='BCM2712 UART10, dedicated 3-pin connector'
    }
}
