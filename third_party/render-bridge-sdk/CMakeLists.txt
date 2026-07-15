cmake_minimum_required(VERSION 3.14)
project(RXExt)

#set(OMIT_SAMPLE_PROJECTS true)

# optionally deploy and run extension in specific PIXERA/RX installation
# build the ZERO_CHECK project to update the solution
#set(PIXERA_DIR "C:/Program Files/AV Stumpfl/Pixera/build_25-1_R_4")
#set(RX_DIR "${PIXERA_DIR}/RX")
#set(DEPLOY_DIR "${PIXERA_DIR}/data/rxExt/ExtTemplate")
#set(RUN_PIXERA TRUE)

include(RenderBridge/CMakeLists.txt)
set(EXTENSIONS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

#--------------------------------------------------------------------------

rx_extension("ExtTemplate")
  target_include_directories(${PROJECT} PRIVATE
    ${PROJECT_DIR}/src
  )
  target_link_directories(${PROJECT} PRIVATE
    ${PROJECT_DIR}/libs
  )

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT})
