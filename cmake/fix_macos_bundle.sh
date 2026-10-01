#!/bin/bash
# Nacharbeit nach macdeployqt (Homebrew-Qt): macdeployqt löst manche @rpath-Abhängigkeiten
# nicht auf (Qt ist bei Homebrew auf viele Pakete verteilt). Dieses Skript
#  - kopiert fehlende Bibliotheken aus Homebrew ins Bundle und biegt absolute
#    Homebrew-Pfade auf @rpath um,
#  - entfernt Qt-Plugins, deren Qt-Module nicht im Bundle sind (z. B. VirtualKeyboard,
#    Qt 3D, Qt PDF – Lumen nutzt sie nicht).
#  - behandelt das Präfix aus tools/build_deps.sh (FFmpeg-mvc, libmpv, x264) wie Homebrew.
# Aufruf: fix_macos_bundle.sh <Lumen.app> [<deps-präfix>]
set -euo pipefail
APP="$1"
DEPS="${2:-/nonexistent-lumen-deps}"
FW="$APP/Contents/Frameworks"
BREW=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
# benötigte Qt-Module, die macdeployqt gelegentlich nicht findet: werden mitkopiert
COPY_FRAMEWORKS="QtSvg"

find_brew_lib() { # <dateiname> -> Pfad in Homebrew
    for d in "$DEPS/lib" "$BREW/lib" "$BREW"/opt/*/lib; do
        [ -e "$d/$1" ] && { echo "$d/$1"; return 0; }
    done
    return 1
}

machos() { # alle Mach-O-Dateien (Framework-Binärdateien sind oft nicht ausführbar markiert)
    find "$APP/Contents" -type f -print0 | xargs -0 file | grep 'Mach-O' | cut -d: -f1 | sort -u
}

# Install-Namen (LC_ID_DYLIB), die noch auf Homebrew zeigen, auf @rpath setzen
while IFS= read -r f; do
    id=$(otool -D "$f" | tail -n +2)
    case "$id" in
    "$BREW"/*|"$DEPS"/*|/usr/local/opt/*|/usr/local/Cellar/*)
        if [[ "$id" == *.framework/* ]]; then new="@rpath/${id#*/lib/}"; else new="@rpath/$(basename "$id")"; fi
        chmod u+w "$f"
        install_name_tool -id "$new" "$f" 2>/dev/null
        ;;
    esac
done < <(machos)

for pass in 1 2 3 4 5 6 7 8; do
    changed=0
    while IFS= read -r f; do
        [ -f "$f" ] || continue
        deps=$(otool -L "$f" | tail -n +2 | awk '{print $1}')
        for dep in $deps; do
            case "$dep" in
            "$BREW"/*|"$DEPS"/*|/usr/local/opt/*|/usr/local/Cellar/*)
                if [[ "$dep" == *.framework/* ]]; then
                    rel="${dep#*/lib/}"
                    chmod u+w "$f"
                    install_name_tool -change "$dep" "@rpath/$rel" "$f" 2>/dev/null
                    changed=1
                else
                    name=$(basename "$dep")
                    if [ ! -f "$FW/$name" ]; then
                        cp -L "$dep" "$FW/$name"
                        chmod u+w "$FW/$name"
                        install_name_tool -id "@rpath/$name" "$FW/$name" 2>/dev/null
                        echo "kopiert: $name"
                    fi
                    chmod u+w "$f"
                    install_name_tool -change "$dep" "@rpath/$name" "$f" 2>/dev/null
                    changed=1
                fi
                ;;
            @rpath/*.framework/*)
                rel="${dep#@rpath/}"
                fwname="${rel%%.framework/*}"
                [ -e "$FW/$fwname.framework" ] && continue
                if [[ " $COPY_FRAMEWORKS " == *" $fwname "* ]] && src=$(find_brew_lib "$fwname.framework"); then
                    cp -R "$src" "$FW/"
                    chmod -R u+w "$FW/$fwname.framework"
                    echo "kopiert: $fwname.framework"
                else
                    if [[ "$f" == "$FW"/*.framework/* ]]; then
                        victim="${f%%.framework/*}.framework"   # ganzes Framework entfernen
                    else
                        victim="$f"
                    fi
                    echo "entfernt (braucht $fwname): ${victim#$APP/}"
                    rm -rf "$victim"
                    changed=1
                    break
                fi
                changed=1
                ;;
            @rpath/*)
                name="${dep#@rpath/}"
                [[ "$name" == */* ]] && continue
                [ -f "$FW/$name" ] && continue
                if src=$(find_brew_lib "$name"); then
                    cp -L "$src" "$FW/$name"
                    chmod u+w "$FW/$name"
                    install_name_tool -id "@rpath/$name" "$FW/$name" 2>/dev/null
                    echo "kopiert: $name"
                    changed=1
                else
                    echo "WARNUNG: $name nicht gefunden (gebraucht von ${f#$APP/})"
                fi
                ;;
            esac
        done
    done < <(machos)
    [ "$changed" = 0 ] && break
done

# jede kopierte Bibliothek muss @rpath über Contents/Frameworks auflösen können
for f in "$FW"/*.dylib; do
    otool -l "$f" | grep -q "@loader_path" || { chmod u+w "$f"; install_name_tool -add_rpath "@loader_path" "$f" 2>/dev/null || true; }
done
echo "Bundle nachbearbeitet"
