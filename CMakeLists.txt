cmake_minimum_required(VERSION 3.21)
project(Lumen VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS/Homebrew: OpenSSL, libxml2 und libarchive sind "keg-only" (nicht im Standardpfad) –
# ihre Präfixe für find_package/pkg-config ergänzen
if(APPLE)
    find_program(LUMEN_BREW brew)
    if(LUMEN_BREW)
        foreach(pkg openssl@3 libxml2 libarchive)
            execute_process(COMMAND "${LUMEN_BREW}" --prefix ${pkg}
                OUTPUT_VARIABLE _prefix OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
            if(_rc EQUAL 0 AND EXISTS "${_prefix}/lib")
                list(APPEND CMAKE_PREFIX_PATH "${_prefix}")
                set(ENV{PKG_CONFIG_PATH} "${_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
                if(pkg STREQUAL "openssl@3" AND NOT OPENSSL_ROOT_DIR)
                    set(OPENSSL_ROOT_DIR "${_prefix}")
                endif()
            endif()
        endforeach()
    endif()
endif()

# ---------------------------------------------------------------------------
# Medienbibliotheken aus tools/build_deps.sh: FFmpeg-mvc (Blu-ray 3D, H.264-Encoder
# für die Übertragung), libmpv und ggf. libdvdnav 7 – auf allen Plattformen dieselben
# Versionen. Liegt das Präfix vor, hat es Vorrang vor den Paketen des Systems.
# ---------------------------------------------------------------------------
set(LUMEN_DEPS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/prefix" CACHE PATH "Präfix von tools/build_deps.sh")
if(EXISTS "${LUMEN_DEPS_PREFIX}/lib/pkgconfig")
    list(PREPEND CMAKE_PREFIX_PATH "${LUMEN_DEPS_PREFIX}")
    message(STATUS "Lumen: Medienbibliotheken aus ${LUMEN_DEPS_PREFIX}")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network OpenGL Qml Quick QuickControls2 Xml)
qt_standard_project_setup(REQUIRES 6.5)

# ---------------------------------------------------------------------------
# libmpv  (Pflicht)
#   -DMPV_ROOT=<pfad>   SDK-Ordner mit include/mpv/client.h und libmpv.dll.a / mpv.lib
#   sonst pkg-config "mpv"
# ---------------------------------------------------------------------------
set(MPV_ROOT "" CACHE PATH "Pfad zu einem libmpv-SDK (include/ + lib)")
find_package(PkgConfig QUIET)

if(MPV_ROOT)
    find_path(MPV_INCLUDE_DIR mpv/client.h PATHS "${MPV_ROOT}" PATH_SUFFIXES include NO_DEFAULT_PATH REQUIRED)
    find_library(MPV_LIBRARY NAMES mpv libmpv mpv-2 PATHS "${MPV_ROOT}" PATH_SUFFIXES lib . NO_DEFAULT_PATH REQUIRED)
    add_library(lumen_mpv UNKNOWN IMPORTED)
    set_target_properties(lumen_mpv PROPERTIES
        IMPORTED_LOCATION "${MPV_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}")
    file(GLOB MPV_RUNTIME_DLLS "${MPV_ROOT}/*mpv*.dll" "${MPV_ROOT}/bin/*mpv*.dll")
elseif(PKG_CONFIG_FOUND)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    add_library(lumen_mpv INTERFACE)
    target_link_libraries(lumen_mpv INTERFACE PkgConfig::MPV)
else()
    message(FATAL_ERROR "libmpv nicht gefunden: -DMPV_ROOT=<sdk> setzen oder pkg-config installieren")
endif()

# ---------------------------------------------------------------------------
# FFmpeg  (Pflicht) – Lumen kodiert damit den Sendestrom für Empfänger im Netz
# (H.264 + AAC in MPEG-TS). Es muss dasselbe FFmpeg sein, das libmpv lädt.
# ---------------------------------------------------------------------------
if(NOT PKG_CONFIG_FOUND)
    message(FATAL_ERROR "pkg-config fehlt (für FFmpeg: libavcodec, libavformat, libavutil, libswscale)")
endif()
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
message(STATUS "Lumen: FFmpeg libavcodec ${FFMPEG_libavcodec_VERSION}")

# ---------------------------------------------------------------------------
# libbluray  (optional) – nur zum Auslesen von Disc-/Titel-Infos.
# Die Entschlüsselung erfolgt NICHT durch Lumen, sondern extern (Laufwerk mit
# LibreDrive + eine vom Nutzer bereitgestellte AACS-Bibliothek).
# ---------------------------------------------------------------------------
option(LUMEN_WITH_LIBBLURAY "Disc-Infos via libbluray anzeigen" ON)
set(BLURAY_ROOT "" CACHE PATH "Pfad zu einem libbluray-SDK (optional)")
if(LUMEN_WITH_LIBBLURAY)
    if(BLURAY_ROOT)
        find_path(BLURAY_INCLUDE_DIR libbluray/bluray.h PATHS "${BLURAY_ROOT}" PATH_SUFFIXES include NO_DEFAULT_PATH)
        find_library(BLURAY_LIBRARY NAMES bluray libbluray PATHS "${BLURAY_ROOT}" PATH_SUFFIXES lib NO_DEFAULT_PATH)
        if(BLURAY_INCLUDE_DIR AND BLURAY_LIBRARY)
            add_library(lumen_bluray UNKNOWN IMPORTED)
            set_target_properties(lumen_bluray PROPERTIES
                IMPORTED_LOCATION "${BLURAY_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${BLURAY_INCLUDE_DIR}")
            set(LUMEN_HAVE_BLURAY ON)
        endif()
    elseif(PKG_CONFIG_FOUND)
        pkg_check_modules(BLURAY QUIET IMPORTED_TARGET libbluray>=1.0)
        if(BLURAY_FOUND)
            add_library(lumen_bluray INTERFACE)
            target_link_libraries(lumen_bluray INTERFACE PkgConfig::BLURAY)
            set(LUMEN_HAVE_BLURAY ON)
        endif()
    endif()
endif()
message(STATUS "Lumen: libbluray Disc-Infos: ${LUMEN_HAVE_BLURAY}")

# ---------------------------------------------------------------------------
# Weitere Formate (alle optional)
#   OpenSSL    – verschlüsselte DCPs: KDM (RSA-OAEP), Essenz (AES-128), Zertifikate
#   libdvdnav  – DVD-Video mit Menüs (ohne: mpv "dvd://", nur Titel)
#   libcdio    – Video-CD/SVCD von Laufwerk und CUE/BIN/NRG-Abbildern
# ---------------------------------------------------------------------------
option(LUMEN_WITH_OPENSSL "Verschlüsselte DCPs (OpenSSL)" ON)
option(LUMEN_WITH_DVDNAV "DVD-Menüs (libdvdnav)" ON)
option(LUMEN_WITH_CDIO "Video-CD sektorgenau (libcdio)" ON)
if(LUMEN_WITH_OPENSSL)
    find_package(OpenSSL 1.1 COMPONENTS Crypto)
    set(LUMEN_HAVE_OPENSSL ${OPENSSL_FOUND})
endif()
if(PKG_CONFIG_FOUND)
    if(LUMEN_WITH_DVDNAV)
        pkg_check_modules(DVDNAV QUIET IMPORTED_TARGET dvdnav>=6.0)
        set(LUMEN_HAVE_DVDNAV ${DVDNAV_FOUND})
    endif()
    if(LUMEN_WITH_CDIO)
        pkg_check_modules(CDIO QUIET IMPORTED_TARGET libcdio libiso9660)
        set(LUMEN_HAVE_CDIO ${CDIO_FOUND})
    endif()
    # KDM-Signaturprüfung (XML-DSig): Kanonisierung (C14N) über libxml2
    if(LUMEN_HAVE_OPENSSL)
        pkg_check_modules(LIBXML2 QUIET IMPORTED_TARGET libxml-2.0)
        set(LUMEN_HAVE_LIBXML2 ${LIBXML2_FOUND})
    endif()
endif()
# Immersive Audio (Dolby Atmos / SMPTE IAB in DCPs): offener DTS-IAB-Renderer (BSD-3).
# IAB_SOURCE_DIR = lokale Quellen, sonst Download (festgelegter Commit) von GitHub
option(LUMEN_WITH_IAB "Atmos/IAB in DCPs rendern (DTS IAB Renderer)" ON)
set(IAB_SOURCE_DIR "" CACHE PATH "Quellen von github.com/DTSProAudio/iab-renderer (optional)")
if(LUMEN_WITH_IAB)
    if(NOT IAB_SOURCE_DIR)
        include(FetchContent)
        FetchContent_Declare(iabrenderer
            GIT_REPOSITORY https://github.com/DTSProAudio/iab-renderer.git
            GIT_TAG 1232c550aac4be4d4106605d30bd0eb798b3f4bf
            SOURCE_SUBDIR lumen-no-cmake) # nur Quellen holen, deren CMakeLists nicht einbinden
        FetchContent_MakeAvailable(iabrenderer)
        set(IAB_SOURCE_DIR "${iabrenderer_SOURCE_DIR}")
    endif()
    if(EXISTS "${IAB_SOURCE_DIR}/include/IABRendererAPI.h")
        file(GLOB_RECURSE IAB_LIB_SOURCES CONFIGURE_DEPENDS "${IAB_SOURCE_DIR}/src/lib/*.cpp")
        list(FILTER IAB_LIB_SOURCES EXCLUDE REGEX "IABRendererMT[.]cpp$")
        if(NOT APPLE)
            list(FILTER IAB_LIB_SOURCES EXCLUDE REGEX "VectDSPMacAccelerate[.]cpp$")
        endif()
        add_library(lumen_iab STATIC ${IAB_LIB_SOURCES})
        target_include_directories(lumen_iab PUBLIC "${IAB_SOURCE_DIR}/include" "${IAB_SOURCE_DIR}/src/lib")
        # nutzt std::ios_base::streampos (in C++17 entfernt)
        set_target_properties(lumen_iab PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 14 CXX_EXTENSIONS ON)
        target_compile_options(lumen_iab PRIVATE -w)
        if(APPLE)
            target_link_libraries(lumen_iab PUBLIC "-framework Accelerate")
        endif()
        set(LUMEN_HAVE_IAB ON)
    endif()
endif()

message(STATUS "Lumen: OpenSSL (DCP-Entschlüsselung): ${LUMEN_HAVE_OPENSSL} · libdvdnav: ${LUMEN_HAVE_DVDNAV} (${DVDNAV_VERSION}) · libcdio: ${LUMEN_HAVE_CDIO} · libxml2 (KDM-Signatur): ${LUMEN_HAVE_LIBXML2} · IAB/Atmos: ${LUMEN_HAVE_IAB}")

# DCP-Kern (ohne GUI, auch für das Testprogramm)
set(LUMEN_DCP_SOURCES
    src/DcpPackage.h        src/DcpPackage.cpp
    src/DcpCrypto.h         src/DcpCrypto.cpp
    src/DcpStream.h         src/DcpStream.cpp
    src/DcpSubtitles.h      src/DcpSubtitles.cpp
    src/DcpSignature.h      src/DcpSignature.cpp
    src/DcpIab.h            src/DcpIab.cpp
)

qt_add_executable(lumen WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/MpvController.h     src/MpvController.cpp
    src/DriveManager.h      src/DriveManager.cpp
    src/DisplayManager.h    src/DisplayManager.cpp
    src/ProfileManager.h    src/ProfileManager.cpp
    src/DiscScanner.h       src/DiscScanner.cpp
    src/BlurayNav.h         src/BlurayNav.cpp
    src/PlayerWindow.h      src/PlayerWindow.cpp
    src/PathUtil.h
    src/DvdNav.h            src/DvdNav.cpp
    src/OpticalMedia.h      src/OpticalMedia.cpp
    src/VcdNav.h            src/VcdNav.cpp
    src/DcpManager.h        src/DcpManager.cpp
    src/I18n.h              src/I18n.cpp
    src/PluginManager.h     src/PluginManager.cpp
    src/PluginStore.h       src/PluginStore.cpp
    src/MediaServers.h      src/MediaServers.cpp
    src/Updater.h           src/Updater.cpp
    src/Recent.h            src/Recent.cpp
    src/Stereo3D.h          src/Stereo3D.cpp
    src/AudioTap.h          src/AudioTap.cpp
    src/CastStream.h        src/CastStream.cpp
    src/CastAudio.h         src/CastAudio.cpp
    src/CastEncoder.h       src/CastEncoder.cpp
    src/CastRenderer.h      src/CastRenderer.cpp
    src/CastServer.h        src/CastServer.cpp
    src/CastDiscovery.h     src/CastDiscovery.cpp
    src/CastTargets.h       src/CastTargets.cpp
    src/CastManager.h       src/CastManager.cpp
    include/lumen/plugin.h
    src/Tr.h
    ${LUMEN_DCP_SOURCES}
)

set(LUMEN_QML
    qml/Main.qml
    qml/Theme.qml
    qml/IconButton.qml
    qml/SeekBar.qml
    qml/Chip.qml
    qml/Select.qml
    qml/SelectList.qml
    qml/ValueSlider.qml
    qml/Toggle.qml
    qml/SectionLabel.qml
    qml/ProfileEditor.qml
    qml/CinemaPane.qml
    qml/PluginsPane.qml
    qml/StreamingPane.qml
    qml/HelpDialog.qml
    qml/CastDialog.qml
)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# Alle QML-Dateien ins Modul-Wurzelverzeichnis legen (gemeinsames qmldir -> Singleton "Theme")
foreach(f ${LUMEN_QML})
    get_filename_component(name "${f}" NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(lumen
    URI Lumen
    VERSION 1.0
    QML_FILES ${LUMEN_QML}
    RESOURCES
        icons/play.svg icons/pause.svg icons/stop.svg icons/prev.svg icons/next.svg
        icons/rewind.svg icons/forward.svg icons/volume.svg icons/mute.svg
        icons/fullscreen.svg icons/eject.svg icons/disc.svg icons/folder.svg
        icons/tune.svg icons/loop.svg icons/camera.svg icons/info.svg
        icons/frame-back.svg icons/frame-fwd.svg icons/refresh.svg
        icons/menu.svg icons/up.svg icons/down.svg icons/left.svg icons/right.svg icons/help.svg icons/close.svg icons/cast.svg
        profiles/presets.json
        resources/logo/lumen-emblem-small.png resources/logo/lumen-icon-256.png resources/logo/lumen-logo.png
        i18n/en.json i18n/fr.json i18n/es.json i18n/it.json i18n/pt.json i18n/nl.json i18n/pl.json
        i18n/sv.json i18n/cs.json i18n/tr.json i18n/uk.json i18n/ru.json i18n/ja.json i18n/zh.json i18n/ko.json
)

target_compile_definitions(lumen PRIVATE LUMEN_VERSION="${PROJECT_VERSION}")
target_include_directories(lumen PRIVATE include)

target_link_libraries(lumen PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::OpenGL Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Xml
    lumen_mpv PkgConfig::FFMPEG)

# Empfänger-Seite für Browser und TV-Apps (liefert der eingebaute HTTP-Server aus)
qt_add_resources(lumen "lumen_receiver" PREFIX "/receiver" BASE "${CMAKE_CURRENT_SOURCE_DIR}/receiver"
    FILES receiver/index.html receiver/receiver.js receiver/receiver.css receiver/icon.png
          receiver/hls.light.min.js)

function(lumen_optional_libs target)
    if(LUMEN_HAVE_OPENSSL)
        target_link_libraries(${target} PRIVATE OpenSSL::Crypto)
        target_compile_definitions(${target} PRIVATE LUMEN_HAVE_OPENSSL=1)
    endif()
    if(LUMEN_HAVE_DVDNAV)
        target_link_libraries(${target} PRIVATE PkgConfig::DVDNAV)
        target_compile_definitions(${target} PRIVATE LUMEN_HAVE_DVDNAV=1)
        # libdvdnav 7: genaues Springen, Ton-/Untertitelwahl im Navigator, Regionscode der Disc.
        # Mit libdvdnav 6 (z. B. Debian 13) laufen Menüs und Wiedergabe ohne diese Extras.
        if(DVDNAV_VERSION VERSION_GREATER_EQUAL 7.0)
            target_compile_definitions(${target} PRIVATE LUMEN_DVDNAV7=1)
        endif()
    endif()
    if(LUMEN_HAVE_CDIO)
        target_link_libraries(${target} PRIVATE PkgConfig::CDIO)
        target_compile_definitions(${target} PRIVATE LUMEN_HAVE_CDIO=1)
    endif()
    if(LUMEN_HAVE_LIBXML2)
        target_link_libraries(${target} PRIVATE PkgConfig::LIBXML2)
        target_compile_definitions(${target} PRIVATE LUMEN_HAVE_LIBXML2=1)
    endif()
    if(LUMEN_HAVE_IAB)
        target_link_libraries(${target} PRIVATE lumen_iab)
        target_compile_definitions(${target} PRIVATE LUMEN_HAVE_IAB=1)
        # Lautsprecherlayouts (DTS-Konfigurationen, 5.1/7.1 von Lumen abgeleitet)
        qt_add_resources(${target} "${target}_iab" PREFIX "/iab" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/iab"
            FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/iab/2.0.cfg" "${CMAKE_CURRENT_SOURCE_DIR}/resources/iab/5.1.cfg"
                  "${CMAKE_CURRENT_SOURCE_DIR}/resources/iab/7.1.cfg" "${CMAKE_CURRENT_SOURCE_DIR}/resources/iab/5.1.4.cfg"
                  "${CMAKE_CURRENT_SOURCE_DIR}/resources/iab/7.1.4.cfg")
    endif()
endfunction()
lumen_optional_libs(lumen)

# libdvdcss-Ersatz ohne Kopierschutz-Code: MSYS2/Homebrew-libdvdread linkt
# libdvdcss fest – statt der echten Bibliothek liegt dieser Shim im Paket.
# Eine vom Nutzer per Plugin bereitgestellte libdvdcss wird durchgereicht.
set(LUMEN_DVDCSS_SHIM OFF)
if(LUMEN_HAVE_DVDNAV AND (WIN32 OR APPLE))
    enable_language(C)
    add_library(lumen_dvdcss_shim SHARED src/dvdcss_shim.c)
    set_target_properties(lumen_dvdcss_shim PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    if(WIN32)
        set_target_properties(lumen_dvdcss_shim PROPERTIES PREFIX "" OUTPUT_NAME "libdvdcss-2"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    else()
        set_target_properties(lumen_dvdcss_shim PROPERTIES OUTPUT_NAME dvdcss VERSION 2 SOVERSION 2
            MACHO_COMPATIBILITY_VERSION 99.0.0 MACHO_CURRENT_VERSION 99.0.0
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dvdcss-shim")
    endif()
    add_dependencies(lumen lumen_dvdcss_shim)
    set(LUMEN_DVDCSS_SHIM ON)
endif()

if(LUMEN_HAVE_BLURAY)
    target_link_libraries(lumen PRIVATE lumen_bluray)
    target_compile_definitions(lumen PRIVATE LUMEN_HAVE_BLURAY=1)
    # Blu-ray 3D: MVC-Ansicht zumischen (Decoder: FFmpeg-mvc zur Laufzeit)
    target_sources(lumen PRIVATE src/MvcMerger.h src/MvcMerger.cpp)
endif()

if(WIN32)
    # Programmsymbol (Explorer, Taskleiste)
    enable_language(RC)
    file(WRITE "${CMAKE_BINARY_DIR}/lumen.rc" "IDI_ICON1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/resources/logo/lumen.ico\"
")
    target_sources(lumen PRIVATE "${CMAKE_BINARY_DIR}/lumen.rc")
    target_compile_definitions(lumen PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(lumen PRIVATE user32)
    foreach(dll ${MPV_RUNTIME_DLLS})
        add_custom_command(TARGET lumen POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:lumen>")
    endforeach()
endif()

if(APPLE)
    set_target_properties(lumen PROPERTIES
        OUTPUT_NAME "Lumen"
        MACOSX_BUNDLE_BUNDLE_NAME "Lumen"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.lumen-player"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Lumen · AGPL-3.0-or-later"
        MACOSX_BUNDLE_ICON_FILE lumen.icns)
    target_sources(lumen PRIVATE resources/logo/lumen.icns)
    set_source_files_properties(resources/logo/lumen.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    # CoreGraphics (Bildraten-Umschaltung) steckt in ApplicationServices
    target_link_libraries(lumen PRIVATE "-framework ApplicationServices")
    # OpenGL (Render-API von mpv) ist seit macOS 10.14 als veraltet markiert
    target_compile_definitions(lumen PRIVATE GL_SILENCE_DEPRECATION)

    # "cmake --build . --target lumen_dmg": eigenständiges Lumen.app (Qt, libmpv,
    # FFmpeg, libbluray … im Bundle) plus Lumen.dmg
    find_program(MACDEPLOYQT macdeployqt HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
    # Homebrew verteilt Qt auf mehrere Pakete (qtbase, qtdeclarative, …): deren
    # Frameworks liegen verlinkt in <prefix>/lib und müssen dort gesucht werden
    set(_deploy_libpaths "")
    if(LUMEN_BREW)
        execute_process(COMMAND "${LUMEN_BREW}" --prefix OUTPUT_VARIABLE _brew_prefix OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(EXISTS "${_brew_prefix}/lib")
            list(APPEND _deploy_libpaths "-libpath=${_brew_prefix}/lib")
        endif()
        # z. B. QtVirtualKeyboard/QtPdf, die Plugins aus anderen Qt-Paketen nachziehen
        file(GLOB _qt_kegs LIST_DIRECTORIES true "${_brew_prefix}/opt/qt*")
        foreach(_keg ${_qt_kegs})
            if(EXISTS "${_keg}/lib")
                list(APPEND _deploy_libpaths "-libpath=${_keg}/lib")
            endif()
        endforeach()
    endif()
    if(EXISTS "${LUMEN_DEPS_PREFIX}/lib")
        list(APPEND _deploy_libpaths "-libpath=${LUMEN_DEPS_PREFIX}/lib")
    endif()
    if(MACDEPLOYQT)
        # 1. Bundle füllen, 2. libdvdcss durch den Shim ersetzen, 3. ad hoc signieren, 4. DMG
        set(_frameworks "$<TARGET_BUNDLE_DIR:lumen>/Contents/Frameworks")
        set(_shim_cmd "")
        if(LUMEN_DVDCSS_SHIM)
            set(_shim_cmd COMMAND "${CMAKE_COMMAND}" "-DSRC=$<TARGET_FILE:lumen_dvdcss_shim>"
                "-DDST=${_frameworks}/libdvdcss.2.dylib" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SwapDvdcssShim.cmake")
        endif()
        add_custom_target(lumen_dmg
            COMMAND "${MACDEPLOYQT}" "$<TARGET_BUNDLE_DIR:lumen>" -qmldir=${CMAKE_CURRENT_SOURCE_DIR}/qml ${_deploy_libpaths} -always-overwrite
            ${_shim_cmd}
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix_macos_bundle.sh" "$<TARGET_BUNDLE_DIR:lumen>" "${LUMEN_DEPS_PREFIX}"
            COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:lumen>"
            COMMAND "${CMAKE_COMMAND}" "-DAPP=$<TARGET_BUNDLE_DIR:lumen>" -DDMG=Lumen.dmg -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MakeDmg.cmake"
            DEPENDS lumen
            WORKING_DIRECTORY "$<TARGET_BUNDLE_DIR:lumen>/.."
            COMMENT "macdeployqt: Lumen.app + Lumen.dmg")
    endif()
endif()

# ---------------------------------------------------------------------------
# Windows-Laufzeit: Qt (windeployqt) + alle DLL-Abhängigkeiten rekursiv.
# LUMEN_DEPLOY_SEARCH: Suchordner in Reihenfolge – das Präfix aus build_deps.sh
# (FFmpeg-mvc, libmpv) zuerst, damit dessen DLLs Vorrang vor denen des Systems haben.
# ---------------------------------------------------------------------------
set(LUMEN_DEPLOY_SEARCH "" CACHE STRING "Ordner mit Laufzeit-DLLs (;-getrennt), z. B. <msys2>/ucrt64/bin")
if(WIN32 AND LUMEN_DEPLOY_SEARCH)
    find_package(Python3 COMPONENTS Interpreter)
    find_program(WINDEPLOYQT NAMES windeployqt6 windeployqt HINTS ${LUMEN_DEPLOY_SEARCH})
    set(_search "")
    if(EXISTS "${LUMEN_DEPS_PREFIX}/bin")
        list(APPEND _search "${LUMEN_DEPS_PREFIX}/bin")
    endif()
    list(APPEND _search ${LUMEN_DEPLOY_SEARCH})
    if(WINDEPLOYQT)
        add_custom_command(TARGET lumen POST_BUILD
            COMMAND "${WINDEPLOYQT}" --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" --no-translations
                    --no-compiler-runtime "$<TARGET_FILE:lumen>"
            COMMENT "windeployqt")
    endif()
    if(Python3_Interpreter_FOUND)
        add_custom_command(TARGET lumen POST_BUILD
            COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/deploy_windows.py"
                    "$<TARGET_FILE:lumen>" --search ${_search} --own libdvdcss-2.dll
            COMMENT "Laufzeit-DLLs (FFmpeg-mvc, libmpv, libbluray, …)")
    endif()
endif()

# Test: MVC-Zusammenführung gegen eine (synthetische) 3D-Disc, siehe README
option(LUMEN_BUILD_TESTS "Test-Programme bauen" OFF)
if(LUMEN_BUILD_TESTS AND LUMEN_HAVE_BLURAY)
    add_executable(mvcmerge_test tests/mvcmerge_test.cpp src/MvcMerger.cpp src/MvcMerger.h)
    target_link_libraries(mvcmerge_test PRIVATE Qt6::Core lumen_bluray)
endif()
# Test: DCP lesen, KDM auspacken, Spurdateien entschlüsseln, Untertitel wandeln
if(LUMEN_BUILD_TESTS)
    add_executable(dcp_test tests/dcp_test.cpp ${LUMEN_DCP_SOURCES})
    target_include_directories(dcp_test PRIVATE src)
    target_link_libraries(dcp_test PRIVATE Qt6::Core Qt6::Gui Qt6::Xml lumen_mpv)
    lumen_optional_libs(dcp_test)
    # Test: Video-CD-Wiedergabesteuerung (PBC) mit echter mpv-Wiedergabe
    add_executable(vcd_test tests/vcd_test.cpp src/VcdNav.cpp src/VcdNav.h src/OpticalMedia.cpp src/OpticalMedia.h)
    target_include_directories(vcd_test PRIVATE src)
    target_link_libraries(vcd_test PRIVATE Qt6::Core Qt6::Xml lumen_mpv)
    lumen_optional_libs(vcd_test)
    # IAB-Testmaterial (Atmos/IAB-DCP, tools/make_test_dcp.py --iab)
    if(LUMEN_HAVE_IAB)
        add_executable(iab_testgen tests/iab_testgen.cpp)
        target_link_libraries(iab_testgen PRIVATE lumen_iab)
    endif()
    # Test: DVD-Menüs mit echter mpv-Ausgabe (Test-DVD: tools/make_test_dvd.py)
    add_executable(dvd_test tests/dvd_test.cpp src/DvdNav.cpp src/DvdNav.h)
    target_include_directories(dvd_test PRIVATE src)
    target_link_libraries(dvd_test PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL lumen_mpv)
    lumen_optional_libs(dvd_test)
endif()

# Plugins: Beispiele (plugins/examples) nach <build>/plugins, Test des Plugin-Systems
option(LUMEN_BUILD_PLUGIN_EXAMPLES "Beispiel-Plugins bauen" ${LUMEN_BUILD_TESTS})
if(LUMEN_BUILD_PLUGIN_EXAMPLES)
    enable_language(C)
    add_library(demo_plugin MODULE plugins/examples/demo/demo_plugin.c)
    set_target_properties(demo_plugin PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_include_directories(demo_plugin PRIVATE include)
    set_target_properties(demo_plugin PROPERTIES PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/demo"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/demo")
    add_custom_command(TARGET demo_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/plugins/examples/demo/plugin.json"
                "${CMAKE_BINARY_DIR}/plugins/demo/plugin.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/plugins/examples/osd-clock"
                "${CMAKE_BINARY_DIR}/plugins/osd-clock"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/plugins/examples/disc-libraries"
                "${CMAKE_BINARY_DIR}/plugins/disc-libraries")
endif()
if(LUMEN_BUILD_TESTS)
    add_executable(plugin_test tests/plugin_test.cpp src/PluginManager.cpp src/PluginManager.h ${LUMEN_DCP_SOURCES})
    target_include_directories(plugin_test PRIVATE src include)
    target_link_libraries(plugin_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Xml lumen_mpv)
    # Disc-Erkennung: Disc-ID/CD-Text einer Audio-CD, Skript-Schnittstelle mit dem Plugin "disc-identify"
    add_executable(discinfo_test tests/discinfo_test.cpp src/OpticalMedia.cpp src/OpticalMedia.h
                   src/PluginManager.cpp src/PluginManager.h ${LUMEN_DCP_SOURCES})
    target_include_directories(discinfo_test PRIVATE src include)
    target_link_libraries(discinfo_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Xml lumen_mpv)
    lumen_optional_libs(discinfo_test)
    # Update-Prüfung/-Installation gegen einen lokalen Release-Server (Windows: ZIP)
    if(WIN32)
        add_executable(updater_test tests/updater_test.cpp src/Updater.cpp src/Updater.h)
        target_include_directories(updater_test PRIVATE src)
        target_link_libraries(updater_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
    endif()
    # Automatische Bildschirmzuordnung
    add_executable(display_test tests/display_test.cpp src/DisplayManager.cpp src/DisplayManager.h)
    target_include_directories(display_test PRIVATE src)
    target_link_libraries(display_test PRIVATE Qt6::Core Qt6::Gui)
    if(APPLE)
        target_link_libraries(display_test PRIVATE "-framework ApplicationServices")
    endif()
    # Medienserver (Jellyfin/Emby/Plex) gegen tools/mock_media_server.py
    add_executable(media_test tests/media_test.cpp src/MediaServers.cpp src/MediaServers.h)
    target_include_directories(media_test PRIVATE src)
    target_link_libraries(media_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network lumen_mpv)
    # 3D-Bildfolge: gibt die Filterkette aus (tools/test_seq3d.sh prüft sie mit FFmpeg)
    add_executable(stereo_test tests/stereo_test.cpp src/Stereo3D.cpp src/Stereo3D.h)
    target_include_directories(stereo_test PRIVATE src)
    target_link_libraries(stereo_test PRIVATE Qt6::Core Qt6::Gui)
    # Übertragung: Sendestrom (H.264/AAC, Ton-Bild-Abgleich), Geräteerkennung, DLNA/Chromecast/AirPlay/TV-App
    # gegen tools/mock_cast_devices.py
    qt_add_executable(cast_test tests/cast_test.cpp
        src/AudioTap.cpp src/AudioTap.h src/CastStream.cpp src/CastStream.h src/CastAudio.cpp src/CastAudio.h src/CastEncoder.cpp src/CastEncoder.h
        src/CastRenderer.cpp src/CastRenderer.h src/CastServer.cpp src/CastServer.h src/CastDiscovery.cpp src/CastDiscovery.h
        src/CastTargets.cpp src/CastTargets.h src/CastManager.cpp src/CastManager.h src/CastOutput.h)
    target_include_directories(cast_test PRIVATE src)
    target_compile_definitions(cast_test PRIVATE LUMEN_VERSION="${PROJECT_VERSION}")
    target_link_libraries(cast_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::OpenGL lumen_mpv PkgConfig::FFMPEG)
    qt_add_resources(cast_test "cast_test_receiver" PREFIX "/receiver" BASE "${CMAKE_CURRENT_SOURCE_DIR}/receiver"
        FILES receiver/index.html receiver/receiver.js receiver/receiver.css receiver/icon.png receiver/hls.light.min.js)
    if(APPLE)
        target_compile_definitions(cast_test PRIVATE GL_SILENCE_DEPRECATION)
    endif()
    # Plugin-Store gegen eine lokale Quelle (Ordner mit index.json)
    add_executable(store_test tests/store_test.cpp src/PluginStore.cpp src/PluginStore.h src/PluginManager.cpp src/PluginManager.h ${LUMEN_DCP_SOURCES})
    target_include_directories(store_test PRIVATE src include)
    target_link_libraries(store_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Xml lumen_mpv)
    lumen_optional_libs(store_test)
    lumen_optional_libs(plugin_test)
    if(LUMEN_BUILD_PLUGIN_EXAMPLES)
        add_dependencies(plugin_test demo_plugin)
    endif()
endif()

include(GNUInstallDirs)
if(UNIX AND NOT APPLE AND EXISTS "${LUMEN_DEPS_PREFIX}/lib")
    # Eigene Medienbibliotheken (FFmpeg-mvc, libmpv, x264, ggf. libdvdnav) liegen privat neben
    # den Plugins; build_deps.sh hat ihnen den Suchpfad $ORIGIN gegeben.
    set_target_properties(lumen PROPERTIES INSTALL_RPATH "$ORIGIN/../lib/lumen")
    install(DIRECTORY "${LUMEN_DEPS_PREFIX}/lib/" DESTINATION lib/lumen
            FILES_MATCHING PATTERN "*.so.*" PATTERN "pkgconfig" EXCLUDE PATTERN "cmake" EXCLUDE)
    set(LUMEN_BUNDLED_LIBS ON)
endif()
install(TARGETS lumen BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ---------------------------------------------------------------------------
# Linux: Desktop-Eintrag, Symbol, Plugin-Vorlagen und Pakete (DEB/RPM)
#   cmake --install build --prefix /usr
#   cd build && cpack -G DEB    (bzw. -G RPM)
# Abhängigkeiten auf Bibliotheken ermitteln dpkg-shlibdeps/rpm selbst; die
# QML-Module (zur Laufzeit geladen) stehen unten ausdrücklich.
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/lumen.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/logo/lumen-icon-256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME lumen.png)
    install(FILES LICENSE THIRD_PARTY.md README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/lumen)
    # Plugin-Vorlagen (nur Daten/Skripte; aktiviert der Nutzer selbst). Suchpfad: <bin>/../lib/lumen/plugins
    install(DIRECTORY plugins/examples/osd-clock plugins/examples/disc-libraries DESTINATION lib/lumen/plugins)

    set(CPACK_PACKAGE_NAME "lumen-player")
    set(CPACK_PACKAGE_VENDOR "Lumen")
    set(CPACK_PACKAGE_CONTACT "Karl-Lauterbach24 <Karl-Lauterbach24@users.noreply.github.com>")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Karl-Lauterbach24/Lumen")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Disc and digital cinema player (Blu-ray, DVD, Video CD, DCP, streams)")
    set(CPACK_PACKAGE_DESCRIPTION "Lumen plays Blu-ray (incl. disc menus), DVD-Video, HD DVD, Video CD, audio CD, Digital Cinema Packages (JPEG 2000, KDM) and streams, with a separate player and control window. It contains no copy-protection circumvention; such libraries can only be added by the user through plugins.")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_STRIP_FILES ON)

    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
    # Architekturnamen wie in den Distributionen: DEB amd64/arm64, RPM x86_64/aarch64
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(_deb_arch arm64)
        set(_rpm_arch aarch64)
    else()
        set(_deb_arch amd64)
        set(_rpm_arch x86_64)
    endif()
    set(CPACK_DEBIAN_FILE_NAME "Lumen-${PROJECT_VERSION}-linux-${_deb_arch}.deb")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    if(LUMEN_BUNDLED_LIBS)
        # die privaten Bibliotheken auflösen, ohne sie als Abhängigkeit zu verlangen
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${LUMEN_DEPS_PREFIX}/lib")
    endif()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-templates, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtquick-dialogs, qml6-module-qtcore, qml6-module-qtqml, qml6-module-qtqml-models, qml6-module-qtqml-workerscript, qml6-module-qt-labs-folderlistmodel, qt6-svg-plugins")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "yt-dlp")

    set(CPACK_RPM_FILE_NAME "Lumen-${PROJECT_VERSION}-linux-${_rpm_arch}.rpm")
    set(CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0-or-later")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
    set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, qt6-qtsvg")
    set(CPACK_RPM_PACKAGE_SUGGESTS "yt-dlp")
    # Ordner, die anderen Paketen gehören, nicht in die Dateiliste aufnehmen
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications /usr/share/doc /usr/share/icons /usr/share/icons/hicolor
        /usr/share/icons/hicolor/256x256 /usr/share/icons/hicolor/256x256/apps)
    set(CPACK_RPM_SPEC_MORE_DEFINE "%define _build_id_links none")
    if(LUMEN_BUNDLED_LIBS)
        # private Bibliotheken weder anbieten noch von außen verlangen
        string(APPEND CPACK_RPM_SPEC_MORE_DEFINE "
%global __provides_exclude_from ^/usr/lib/lumen/.*$
%global __requires_exclude ^lib(mpv|avcodec|avformat|avutil|avfilter|avdevice|swscale|swresample|x264|dvdnav|dvdread)[.]so.*$")
    endif()
    include(CPack)
endif()
