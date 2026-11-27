# Single-device DHCP acceptance on an isolated, direct Windows-to-Pi cable.
[CmdletBinding(DefaultParameterSetName='Cable')]
param(
    [Parameter(Mandatory,ParameterSetName='Cable')][string]$InterfaceAlias,
    [Parameter(ParameterSetName='Cable')][ValidateSet('Ack','Rebind','Nak','Silence')][string]$Mode='Ack',
    [Parameter(ParameterSetName='Cable')][ValidateRange(30,300)][int]$TimeoutSeconds=120,
    [Parameter(ParameterSetName='Cable')][switch]$TemporaryFirewallRule,
    [Parameter(Mandatory,ParameterSetName='SelfTest')][switch]$SelfTest
)
$ErrorActionPreference='Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'rpi5-dhcp-fixture.cs')
if ($SelfTest) { [OurOS.DhcpFixture]::SelfTest(); return }
$rule=$null
try {
    $index=[OurOS.DhcpFixture]::WaitForCable($InterfaceAlias,15)
    if ($TemporaryFirewallRule) {
        $rule='OurOS-DHCP-'+[guid]::NewGuid().ToString('N')
        $options=@{
            Name=$rule
            DisplayName='OurOS direct DHCP test'
            Direction='Inbound'
            Action='Allow'
            Protocol='UDP'
            LocalPort=67
            RemotePort=68
            InterfaceAlias=$InterfaceAlias
        }
        New-NetFirewallRule @options | Out-Null
    }
    [OurOS.DhcpFixture]::Run($InterfaceAlias,$index,$Mode,$TimeoutSeconds)
} catch {
    Write-Error $_.Exception.GetBaseException().Message -ErrorAction Continue
    exit 1
} finally {
    if ($rule) { Remove-NetFirewallRule -Name $rule -ErrorAction SilentlyContinue }
}
