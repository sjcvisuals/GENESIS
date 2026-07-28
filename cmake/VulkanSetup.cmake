include(FetchContent)

# Must run before Dependencies.cmake so volk can find headers.

function(genesis_find_vulkan_sdk_root OUT_VAR)
    if(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Bin/glslc.exe")
        set(${OUT_VAR} "$ENV{VULKAN_SDK}" PARENT_SCOPE)
        return()
    endif()

    file(GLOB _sdk_candidates
        "$ENV{LOCALAPPDATA}/VulkanSDK/*"
        "C:/VulkanSDK/*"
    )
    list(SORT _sdk_candidates COMPARE NATURAL ORDER DESCENDING)
    foreach(_candidate IN LISTS _sdk_candidates)
        if(EXISTS "${_candidate}/Bin/glslc.exe")
            set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

genesis_find_vulkan_sdk_root(GENESIS_VULKAN_SDK_ROOT)

if(GENESIS_VULKAN_SDK_ROOT)
    message(STATUS "GENESIS: Vulkan tools root: ${GENESIS_VULKAN_SDK_ROOT}")
endif()

# Headers — prefer full SDK, then distro packages, otherwise fetch.
# System headers avoid FetchContent+Ninja "build.ninja still dirty" loops that
# break Cursor cloud install scripts on warm/stale build trees.
if(GENESIS_VULKAN_SDK_ROOT AND EXISTS "${GENESIS_VULKAN_SDK_ROOT}/Include/vulkan/vulkan.h")
    set(GENESIS_VULKAN_INCLUDE_DIR "${GENESIS_VULKAN_SDK_ROOT}/Include")
    message(STATUS "GENESIS: Vulkan headers from SDK")
elseif(EXISTS "/usr/include/vulkan/vulkan.h")
    set(GENESIS_VULKAN_INCLUDE_DIR "/usr/include")
    message(STATUS "GENESIS: Vulkan headers from system (/usr/include)")
else()
    # If a previous populate left a half-broken Ninja subbuild, wipe it before
    # FetchContent tries to rebuild (fixes cloud-agent install failures).
    set(_vh_subbuild "${CMAKE_BINARY_DIR}/_deps/vulkan-headers-subbuild")
    if(EXISTS "${_vh_subbuild}/build.ninja")
        file(REMOVE_RECURSE "${_vh_subbuild}")
        message(STATUS "GENESIS: cleared stale vulkan-headers-subbuild")
    endif()

    FetchContent_Declare(
        Vulkan-Headers
        URL https://codeload.github.com/KhronosGroup/Vulkan-Headers/zip/refs/tags/v1.4.350
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Vulkan-Headers)
    set(GENESIS_VULKAN_INCLUDE_DIR "${vulkan-headers_SOURCE_DIR}/include")
    set(VULKAN_HEADERS_INSTALL_DIR "${vulkan-headers_SOURCE_DIR}" CACHE PATH "" FORCE)
    message(STATUS "GENESIS: Vulkan headers from FetchContent")
endif()

find_program(GENESIS_GLSLC_EXECUTABLE glslc
    HINTS
        "${GENESIS_VULKAN_SDK_ROOT}/Bin"
        "$ENV{LOCALAPPDATA}/VulkanSDK/1.4.350.0/Bin"
)

if(NOT GENESIS_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Run scripts/build.ps1 or install the Vulkan SDK.")
endif()

message(STATUS "GENESIS: glslc: ${GENESIS_GLSLC_EXECUTABLE}")
