# Optional ONNX Runtime for RVM background removal (no NVIDIA Broadcast SDK).
option(GENESIS_ENABLE_ONNXRUNTIME "Download/link ONNX Runtime for RVM matting" ON)

set(GENESIS_HAS_ONNXRUNTIME OFF)
set(GENESIS_HAS_ORT_DIRECTML OFF)

if(GENESIS_ENABLE_ONNXRUNTIME)
    set(_ort_ver "1.20.1")
    set(_dml_redist_ver "1.15.2")

    if(WIN32)
        # DirectML build of ORT (GPU via DirectX). Layout normalized under third_party/.
        set(_ort_name "onnxruntime-win-x64-directml-${_ort_ver}")
        set(_ort_dml_root "${CMAKE_SOURCE_DIR}/third_party/${_ort_name}")
        # Prefer DirectML tree even if an older CPU ORT path is cached.
        if(NOT DEFINED CACHE{GENESIS_ONNXRUNTIME_ROOT}
           OR NOT EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/include/dml_provider_factory.h")
            set(GENESIS_ONNXRUNTIME_ROOT "${_ort_dml_root}" CACHE PATH "ONNX Runtime root" FORCE)
        endif()
        set(GENESIS_ONNXRUNTIME_ROOT "${GENESIS_ONNXRUNTIME_ROOT}" CACHE PATH "ONNX Runtime root")

        set(_ort_ready FALSE)
        if(EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h"
           AND EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
           AND EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
            set(_ort_ready TRUE)
        endif()

        if(NOT _ort_ready)
            message(STATUS "GENESIS: downloading ONNX Runtime DirectML ${_ort_ver} + DirectML ${_dml_redist_ver}…")
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
            file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")

            set(_ort_nupkg "${CMAKE_BINARY_DIR}/_deps/Microsoft.ML.OnnxRuntime.DirectML.${_ort_ver}.nupkg")
            set(_dml_nupkg "${CMAKE_BINARY_DIR}/_deps/Microsoft.AI.DirectML.${_dml_redist_ver}.nupkg")
            set(_ort_url "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${_ort_ver}")
            set(_dml_url "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/${_dml_redist_ver}")

            file(DOWNLOAD "${_ort_url}" "${_ort_nupkg}" SHOW_PROGRESS STATUS _dl_ort)
            list(GET _dl_ort 0 _dl_ort_code)
            file(DOWNLOAD "${_dml_url}" "${_dml_nupkg}" SHOW_PROGRESS STATUS _dl_dml)
            list(GET _dl_dml 0 _dl_dml_code)

            if(NOT _dl_ort_code EQUAL 0 OR NOT _dl_dml_code EQUAL 0)
                message(WARNING "GENESIS: DirectML ORT download failed; trying CPU ORT zip fallback")
            else()
                set(_ort_stage "${CMAKE_BINARY_DIR}/_deps/ort-dml-stage")
                set(_dml_stage "${CMAKE_BINARY_DIR}/_deps/dml-redist-stage")
                file(REMOVE_RECURSE "${_ort_stage}" "${_dml_stage}")
                file(MAKE_DIRECTORY "${_ort_stage}" "${_dml_stage}")
                execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${_ort_nupkg}" WORKING_DIRECTORY "${_ort_stage}")
                execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${_dml_nupkg}" WORKING_DIRECTORY "${_dml_stage}")

                file(REMOVE_RECURSE "${GENESIS_ONNXRUNTIME_ROOT}")
                file(MAKE_DIRECTORY "${GENESIS_ONNXRUNTIME_ROOT}/include")
                file(MAKE_DIRECTORY "${GENESIS_ONNXRUNTIME_ROOT}/lib")
                file(COPY "${_ort_stage}/build/native/include/" DESTINATION "${GENESIS_ONNXRUNTIME_ROOT}/include/")
                file(COPY "${_ort_stage}/runtimes/win-x64/native/onnxruntime.dll"
                     DESTINATION "${GENESIS_ONNXRUNTIME_ROOT}/lib/")
                file(COPY "${_ort_stage}/runtimes/win-x64/native/onnxruntime.lib"
                     DESTINATION "${GENESIS_ONNXRUNTIME_ROOT}/lib/")
                file(COPY "${_dml_stage}/bin/x64-win/DirectML.dll"
                     DESTINATION "${GENESIS_ONNXRUNTIME_ROOT}/lib/")
            endif()
        endif()

        if(EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h"
           AND EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
            set(GENESIS_HAS_ONNXRUNTIME ON)
            if(EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/include/dml_provider_factory.h")
                set(GENESIS_HAS_ORT_DIRECTML ON)
            endif()
            message(STATUS "GENESIS: ONNX Runtime (DirectML) at ${GENESIS_ONNXRUNTIME_ROOT}")
        endif()

        # CPU zip fallback if DirectML staging failed.
        if(NOT GENESIS_HAS_ONNXRUNTIME)
            set(_cpu_name "onnxruntime-win-x64-${_ort_ver}")
            set(_cpu_root "${CMAKE_SOURCE_DIR}/third_party/${_cpu_name}")
            set(_cpu_url "https://github.com/microsoft/onnxruntime/releases/download/v${_ort_ver}/${_cpu_name}.zip")
            set(_cpu_archive "${CMAKE_BINARY_DIR}/_deps/${_cpu_name}.zip")
            if(NOT EXISTS "${_cpu_root}/include/onnxruntime_cxx_api.h")
                message(STATUS "GENESIS: downloading CPU ONNX Runtime ${_ort_ver}…")
                file(DOWNLOAD "${_cpu_url}" "${_cpu_archive}" SHOW_PROGRESS STATUS _dl_cpu)
                list(GET _dl_cpu 0 _dl_cpu_code)
                if(_dl_cpu_code EQUAL 0)
                    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${_cpu_archive}"
                        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
                endif()
            endif()
            if(EXISTS "${_cpu_root}/include/onnxruntime_cxx_api.h")
                set(GENESIS_ONNXRUNTIME_ROOT "${_cpu_root}" CACHE PATH "ONNX Runtime root" FORCE)
                set(GENESIS_HAS_ONNXRUNTIME ON)
                message(STATUS "GENESIS: ONNX Runtime (CPU) at ${GENESIS_ONNXRUNTIME_ROOT}")
            endif()
        endif()
    else()
        set(_ort_name "onnxruntime-linux-x64-${_ort_ver}")
        set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${_ort_ver}/${_ort_name}.tgz")
        set(_ort_archive "${CMAKE_BINARY_DIR}/_deps/${_ort_name}.tgz")
        set(GENESIS_ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/${_ort_name}" CACHE PATH "ONNX Runtime root")

        if(NOT EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
            message(STATUS "GENESIS: downloading ONNX Runtime ${_ort_ver}…")
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
            file(DOWNLOAD "${_ort_url}" "${_ort_archive}" SHOW_PROGRESS STATUS _dl_status)
            list(GET _dl_status 0 _dl_code)
            if(NOT _dl_code EQUAL 0)
                message(WARNING "GENESIS: ONNX Runtime download failed; RVM backend disabled")
            else()
                file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
                execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${_ort_archive}"
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
            endif()
        endif()

        if(EXISTS "${GENESIS_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
            set(GENESIS_HAS_ONNXRUNTIME ON)
            message(STATUS "GENESIS: ONNX Runtime at ${GENESIS_ONNXRUNTIME_ROOT}")
        endif()
    endif()
endif()
