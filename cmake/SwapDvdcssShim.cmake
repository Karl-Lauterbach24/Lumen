# Ersetzt ein von macdeployqt mitkopiertes libdvdcss im Bundle durch den Shim
# (aufgerufen mit -DSRC=<shim> -DDST=<Bundle>/Contents/Frameworks/libdvdcss.2.dylib)
if(EXISTS "${DST}")
    file(COPY_FILE "${SRC}" "${DST}")
    message(STATUS "libdvdcss im Bundle durch den Shim ersetzt")
endif()
