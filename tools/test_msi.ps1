# Lumen-MSI prüfen: installieren, starten, auf eine neuere Version aktualisieren, deinstallieren.
#
#   pwsh tools/test_msi.ps1 -Msi Lumen-0.2.2-windows-x64.msi -Upgrade Lumen-99.0.0-windows-x64.msi
#
# -Upgrade ist ein zweites MSI mit höherer Version (gleicher Inhalt genügt); es wird so
# installiert, wie Lumens Updater es tut (msiexec /i … /passive /norestart).
# Braucht Administratorrechte (Installation nach "C:\Program Files\Lumen").
param(
    [Parameter(Mandatory = $true)][string]$Msi,
    [Parameter(Mandatory = $true)][string]$Upgrade
)
$ErrorActionPreference = "Stop"
$dir = Join-Path $env:ProgramFiles "Lumen"
$exe = Join-Path $dir "lumen.exe"
$failed = 0
function Check($ok, $what) {
    if ($ok) { Write-Host "OK   $what" } else { Write-Host "FAIL $what"; $script:failed++ }
}
function Msiexec([string[]]$arguments, $log) {
    $p = Start-Process msiexec.exe -ArgumentList ($arguments + @("/l*v", "`"$log`"")) -Wait -PassThru
    return $p.ExitCode
}
function Installed() {
    # Einträge unter "Apps & Features"
    Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*" -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -eq "Lumen" }
}
$Msi = (Resolve-Path $Msi).Path
$Upgrade = (Resolve-Path $Upgrade).Path

# 1. stille Installation
$rc = Msiexec @("/i", "`"$Msi`"", "/qn", "/norestart") "msi-install.log"
Check ($rc -eq 0) "Installation (msiexec $rc)"
if ($rc -ne 0) { Get-Content msi-install.log -Tail 60; exit 1 }
Check (Test-Path $exe) "lumen.exe in $dir"
Check ((Test-Path "$dir\install-type.txt") -and ((Get-Content "$dir\install-type.txt" -Raw).Trim() -eq "msi")) "install-type.txt = msi (Update per MSI)"
Check (Test-Path "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\Lumen.lnk") "Startmenü-Eintrag"
$first = @(Installed)
Check ($first.Count -eq 1) "ein Eintrag in Apps & Features ($($first.DisplayVersion))"

# 2. das installierte Programm startet (ohne weitere Suchpfade)
$savedPath = $env:PATH
$env:PATH = "C:\Windows\System32;C:\Windows"
$png = Join-Path $PWD "msi-snapshot.png"
Remove-Item $png -ErrorAction SilentlyContinue
$env:LUMEN_SNAPSHOT = $png
$p = Start-Process $exe -PassThru
for ($i = 0; $i -lt 60 -and -not (Test-Path $png); $i++) { Start-Sleep -Milliseconds 500 }
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
$env:LUMEN_SNAPSHOT = ""
$env:PATH = $savedPath
Check (Test-Path $png) "installiertes Lumen startet (Steuerfenster gerendert)"
Start-Sleep -Seconds 2

# 3. Update wie aus Lumen heraus: neuere Version ersetzt die alte
$rc = Msiexec @("/i", "`"$Upgrade`"", "/passive", "/norestart") "msi-upgrade.log"
Check ($rc -eq 0) "Update-Installation (msiexec $rc)"
if ($rc -ne 0) { Get-Content msi-upgrade.log -Tail 60 }
$now = @(Installed)
Check ($now.Count -eq 1 -and $now[0].DisplayVersion -ne $first[0].DisplayVersion) "nach dem Update genau ein Eintrag, Version $($now.DisplayVersion) (vorher $($first.DisplayVersion))"
Check (Test-Path $exe) "lumen.exe nach dem Update vorhanden"

# 4. Deinstallation räumt den Programmordner und das Startmenü auf
$rc = Msiexec @("/x", "`"$Upgrade`"", "/qn", "/norestart") "msi-uninstall.log"
Check ($rc -eq 0) "Deinstallation (msiexec $rc)"
Check (-not (Test-Path $exe)) "lumen.exe entfernt"
Check (-not (Test-Path "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\Lumen.lnk")) "Startmenü-Eintrag entfernt"
Check (@(Installed).Count -eq 0) "kein Eintrag mehr in Apps & Features"

if ($failed) { Write-Host "FEHLGESCHLAGEN ($failed Fehler)"; exit 1 }
Write-Host "BESTANDEN (0 Fehler)"
