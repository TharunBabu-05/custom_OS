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
