# Generates original README artwork. Does not alter hardware proof screenshots.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'docs/assets'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -Path (Join-Path $root 'tools/readme/RenderBootReplay.cs') -ReferencedAssemblies System.Drawing
[RenderBootReplay]::Generate($output)
$node='C:/Program Files/nodejs/node.exe'
if (!(Test-Path -LiteralPath $node)) { $node=(Get-Command node -ErrorAction Stop).Source }
& $node (Join-Path $root 'tools/readme/generate-vectors.mjs')
if ($LASTEXITCODE -ne 0) { throw 'Vector generation failed' }
