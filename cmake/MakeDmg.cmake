# Lumen.dmg aus dem Bundle erzeugen. hdiutil scheitert auf macOS gelegentlich mit
# "Resource busy" (Hintergrund-Scan hält das Image offen) – daher mehrere Versuche.
# Aufruf: cmake -DAPP=<Lumen.app> -DDMG=<Lumen.dmg> -P MakeDmg.cmake
foreach(_try RANGE 1 6)
    execute_process(COMMAND hdiutil create -volname Lumen -srcfolder "${APP}" -ov -format UDZO "${DMG}"
                    RESULT_VARIABLE _rc)
    if(_rc EQUAL 0)
        return()
    endif()
    message(STATUS "hdiutil fehlgeschlagen (Versuch ${_try}), neuer Versuch in 10 s")
    execute_process(COMMAND sleep 10)
endforeach()
message(FATAL_ERROR "hdiutil create ist 6-mal fehlgeschlagen")
