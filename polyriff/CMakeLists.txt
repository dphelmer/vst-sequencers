cmake_minimum_required(VERSION 3.25)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "")
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../vst3sdk vst3sdk)
smtg_enable_vst3_sdk()

project(Polyriff VERSION 0.1.0)

smtg_add_vst3plugin(Polyriff
    source/cids.h
    source/version.h
    source/params.h
    source/polyriffgen.h
    source/virtualmidi.h
    source/virtualmidi.cpp
    source/polyriffprocessor.h
    source/polyriffprocessor.cpp
    source/polyriffcontroller.h
    source/polyriffcontroller.cpp
    source/entry.cpp
)
target_include_directories(Polyriff PRIVATE source)
target_link_libraries(Polyriff PRIVATE sdk)
if(APPLE)
    target_link_libraries(Polyriff PRIVATE "-framework CoreMIDI")
endif()
smtg_target_configure_version_file(Polyriff)

# Offline simulation of process(); not part of the default build.
add_executable(PolyriffSim EXCLUDE_FROM_ALL
    tests/sim.cpp source/polyriffprocessor.cpp source/virtualmidi.cpp)
target_include_directories(PolyriffSim PRIVATE source)
target_link_libraries(PolyriffSim PRIVATE sdk sdk_hosting "-framework CoreFoundation")
if(APPLE)
    target_link_libraries(PolyriffSim PRIVATE "-framework CoreMIDI")
endif()

# Ninja/Makefile generators get CMake's default Info.plist (CFBundlePackageType=APPL, empty
# identifier, no PkgInfo), which makes Finder show the .vst3 as a plain folder. Use a BNDL plist
# and ship PkgInfo inside the bundle (before the SDK's ad-hoc codesign step seals it).
if(SMTG_MAC)
    set_target_properties(Polyriff PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.dphelmer.vstsequencers.Polyriff"
        MACOSX_BUNDLE_BUNDLE_NAME "Polyriff"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    target_sources(Polyriff PRIVATE mac/PkgInfo)
    set_source_files_properties(mac/PkgInfo PROPERTIES MACOSX_PACKAGE_LOCATION ".")
    # The SDK's own ad-hoc sign leaves Info.plist unbound ("Sealed Resources=none"); re-sign last.
    get_target_property(POLYRIFF_PKG Polyriff SMTG_PLUGIN_PACKAGE_PATH)
    add_custom_command(TARGET Polyriff POST_BUILD
        COMMAND codesign --force --sign - "${POLYRIFF_PKG}"
        COMMENT "Re-sign Polyriff bundle (ad-hoc)")
endif()
