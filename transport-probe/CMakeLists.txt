cmake_minimum_required(VERSION 3.25)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "")
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../vst3sdk vst3sdk)
smtg_enable_vst3_sdk()

project(TransportProbe VERSION 0.1.0)

smtg_add_vst3plugin(TransportProbe
    source/cids.h
    source/version.h
    source/probelog.h
    source/probeprocessor.h
    source/probeprocessor.cpp
    source/probecontroller.h
    source/probecontroller.cpp
    source/entry.cpp
)
target_include_directories(TransportProbe PRIVATE source)
target_link_libraries(TransportProbe PRIVATE sdk)
smtg_target_configure_version_file(TransportProbe)

# Offline simulation of process(); not part of the default build.
add_executable(ProbeSim EXCLUDE_FROM_ALL
    tests/sim.cpp source/probeprocessor.cpp)
target_include_directories(ProbeSim PRIVATE source)
target_link_libraries(ProbeSim PRIVATE sdk sdk_hosting "-framework CoreFoundation")
