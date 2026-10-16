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
    source/virtualmidi.h
    source/virtualmidi.cpp
    source/probeprocessor.h
    source/probeprocessor.cpp
    source/probecontroller.h
    source/probecontroller.cpp
    source/entry.cpp
)
target_include_directories(TransportProbe PRIVATE source)
target_link_libraries(TransportProbe PRIVATE sdk)
if(APPLE)
    target_link_libraries(TransportProbe PRIVATE "-framework CoreMIDI")
endif()
smtg_target_configure_version_file(TransportProbe)

# Offline simulation of process(); not part of the default build.
add_executable(ProbeSim EXCLUDE_FROM_ALL
    tests/sim.cpp source/probeprocessor.cpp source/virtualmidi.cpp)
target_include_directories(ProbeSim PRIVATE source)
target_link_libraries(ProbeSim PRIVATE sdk sdk_hosting "-framework CoreFoundation")
if(APPLE)
    target_link_libraries(ProbeSim PRIVATE "-framework CoreMIDI")
endif()

# Ninja/Makefile generators get CMake's default Info.plist (CFBundlePackageType=APPL, empty
# identifier, no PkgInfo), which makes Finder show the .vst3 as a plain folder. Use a BNDL plist
# and ship PkgInfo inside the bundle (before the SDK's ad-hoc codesign step seals it).
if(SMTG_MAC)
    set_target_properties(TransportProbe PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.dphelmer.vstsequencers.TransportProbe"
        MACOSX_BUNDLE_BUNDLE_NAME "TransportProbe"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    target_sources(TransportProbe PRIVATE mac/PkgInfo)
    set_source_files_properties(mac/PkgInfo PROPERTIES MACOSX_PACKAGE_LOCATION ".")
    # The SDK's own ad-hoc sign leaves Info.plist unbound ("Sealed Resources=none"); re-sign last.
    get_target_property(PROBE_PKG TransportProbe SMTG_PLUGIN_PACKAGE_PATH)
    add_custom_command(TARGET TransportProbe POST_BUILD
        COMMAND codesign --force --sign - "${PROBE_PKG}"
        COMMENT "Re-sign TransportProbe bundle (ad-hoc)")
endif()
