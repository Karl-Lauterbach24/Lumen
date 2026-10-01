# Lumen-MSI aus dem fertigen Programmordner bauen (WiX Toolset 3.x: heat, candle, light).
#
#   pwsh tools/make_msi.ps1 -Stage dist\Lumen -Version 0.2.2 -Out Lumen-0.2.2-windows-x64.msi
#
# -Stage ist der Ordner, der auch als portables ZIP verteilt wird (lumen.exe + DLLs + Qt).
# Der Installer bekommt zusätzlich die Datei "install-type.txt" (Inhalt "msi"): daran erkennt
# Lumen, dass es sich per MSI aktualisieren soll.
param(
    [Parameter(Mandatory = $true)][string]$Stage,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

# WiX finden: Umgebungsvariable WIX (Installer von wixtoolset.org), sonst der übliche Ordner
$wixBin = $null
if ($env:WIX -and (Test-Path (Join-Path $env:WIX "bin\candle.exe"))) { $wixBin = Join-Path $env:WIX "bin" }
if (-not $wixBin) {
    $found = Get-ChildItem "${env:ProgramFiles(x86)}\WiX Toolset v3*\bin\candle.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $wixBin = $found.DirectoryName }
}
if (-not $wixBin) { throw "WiX Toolset 3.x nicht gefunden (https://wixtoolset.org) – WIX setzen oder installieren" }
Write-Host "WiX: $wixBin"

if (-not (Test-Path (Join-Path $Stage "lumen.exe"))) { throw "$Stage enthält kein lumen.exe" }
$Out = [System.IO.Path]::GetFullPath($Out)
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("lumen-msi-" + [System.Guid]::NewGuid().ToString("N"))
$files = Join-Path $work "files"
New-Item -ItemType Directory -Force $files | Out-Null
Copy-Item (Join-Path $Stage "*") $files -Recurse
Set-Content -Path (Join-Path $files "install-type.txt") -Value "msi" -Encoding ascii

# Lizenz als RTF für den Dialog
$rtf = Join-Path $work "license.rtf"
$text = Get-Content (Join-Path $repo "LICENSE") -Raw
$text = $text.Replace("\", "\\").Replace("{", "\{").Replace("}", "\}") -replace "\r?\n", "\par`r`n"
Set-Content -Path $rtf -Value ("{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}}\fs18 " + $text + "}") -Encoding ascii

function Invoke-Tool($exe, [string[]]$arguments) {
    & (Join-Path $wixBin $exe) @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe ist fehlgeschlagen ($LASTEXITCODE)" }
}

# Dateiliste (eine Komponente je Datei, feste GUIDs aus dem Zielpfad)
$filesWxs = Join-Path $work "files.wxs"
Invoke-Tool "heat.exe" @("dir", $files, "-nologo", "-cg", "LumenFiles", "-dr", "INSTALLFOLDER", "-ag", "-sfrag", "-srd", "-sreg", "-scom",
                         "-var", "var.Stage", "-out", $filesWxs)
$defines = @("-dVersion=$Version", "-dStage=$files", "-dIcon=$(Join-Path $repo 'resources\logo\lumen.ico')", "-dLicense=$rtf")
Invoke-Tool "candle.exe" (@("-nologo", "-arch", "x64", "-ext", "WixUIExtension", "-out", "$work\") + $defines +
                          @((Join-Path $repo "packaging\windows\lumen.wxs"), $filesWxs))
Invoke-Tool "light.exe" @("-nologo", "-sw1076", "-ext", "WixUIExtension", "-cultures:en-us", "-out", $Out,
                          (Join-Path $work "lumen.wixobj"), (Join-Path $work "files.wixobj"))

Remove-Item -LiteralPath $work -Recurse -Force
Get-Item $Out | Select-Object Name, Length
