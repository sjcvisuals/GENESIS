# WinSparkle — Adobe/Sparkle-like auto-updater for Windows (optional).
# Downloads the official prebuilt x64 package into the build tree on WIN32.
# Non-Windows builds get a no-op AutoUpdate stub.

option(GENESIS_ENABLE_WINSPARKLE "Fetch and link WinSparkle on Windows" ON)

set(GENESIS_WINSPARKLE_VERSION "0.8.3" CACHE STRING "WinSparkle release version")
set(GENESIS_UPDATE_APPCAST_URL
    "https://updates.example.com/genesis/appcast.xml"
    CACHE STRING "Default Sparkle/WinSparkle appcast URL (override at runtime via GENESIS_UPDATE_FEED)")

set(GENESIS_HAS_WINSPARKLE 0)

if(WIN32 AND GENESIS_ENABLE_WINSPARKLE)
    set(_ws_root "${CMAKE_BINARY_DIR}/_deps/winsparkle-${GENESIS_WINSPARKLE_VERSION}")
    set(_ws_zip "${CMAKE_BINARY_DIR}/_deps/WinSparkle-${GENESIS_WINSPARKLE_VERSION}.zip")
    set(_ws_url "https://github.com/vslavik/winsparkle/releases/download/v${GENESIS_WINSPARKLE_VERSION}/WinSparkle-${GENESIS_WINSPARKLE_VERSION}.zip")

    if(NOT EXISTS "${_ws_root}/include/winsparkle.h")
        message(STATUS "GENESIS: downloading WinSparkle ${GENESIS_WINSPARKLE_VERSION}...")
        file(DOWNLOAD "${_ws_url}" "${_ws_zip}" SHOW_PROGRESS STATUS _ws_status)
        list(GET _ws_status 0 _ws_code)
        if(NOT _ws_code EQUAL 0)
            message(WARNING "GENESIS: WinSparkle download failed; auto-update will be a stub")
        else()
            file(ARCHIVE_EXTRACT INPUT "${_ws_zip}" DESTINATION "${CMAKE_BINARY_DIR}/_deps")
            file(RENAME "${CMAKE_BINARY_DIR}/_deps/WinSparkle-${GENESIS_WINSPARKLE_VERSION}" "${_ws_root}")
        endif()
    endif()

    if(EXISTS "${_ws_root}/include/winsparkle.h")
        add_library(winsparkle SHARED IMPORTED GLOBAL)
        set_target_properties(winsparkle PROPERTIES
            IMPORTED_IMPLIB "${_ws_root}/x64/Release/WinSparkle.lib"
            IMPORTED_LOCATION "${_ws_root}/x64/Release/WinSparkle.dll"
            INTERFACE_INCLUDE_DIRECTORIES "${_ws_root}/include"
        )
        set(GENESIS_HAS_WINSPARKLE 1)
        set(GENESIS_WINSPARKLE_DLL "${_ws_root}/x64/Release/WinSparkle.dll" CACHE FILEPATH "" FORCE)
        message(STATUS "GENESIS: WinSparkle ${GENESIS_WINSPARKLE_VERSION} ready")
    endif()
endif()

# Always vendor headers for IDE browsing / docs (not linked on non-Windows).
if(NOT GENESIS_HAS_WINSPARKLE AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/winsparkle/include/winsparkle.h")
    # Headers only — AutoUpdate.cpp uses #if GENESIS_HAS_WINSPARKLE.
endif()
