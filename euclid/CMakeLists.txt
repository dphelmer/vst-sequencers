cmake_minimum_required(VERSION 3.25)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "")
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../vst3sdk vst3sdk)
smtg_enable_vst3_sdk()

# VSTGUI enables its PCH from a .cpp TU; under Ninja the .mm files then fail
# ("Objective-C was disabled in precompiled file"). Disable PCH on the vendored targets.
foreach(_vstgui_tgt vstgui vstgui_uidescription)
    if(TARGET ${_vstgui_tgt})
        set_target_properties(${_vstgui_tgt} PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
    endif()
endforeach()

project(Euclid VERSION 0.1.0)

smtg_add_vst3plugin(Euclid
    source/cids.h
    source/version.h
    source/params.h
    source/euclidean.h
    source/euclidprocessor.h
    source/euclidprocessor.cpp
    source/euclidcontroller.h
    source/euclidcontroller.cpp
    source/entry.cpp
)
target_include_directories(Euclid PRIVATE source)
target_link_libraries(Euclid PRIVATE sdk vstgui_support)
smtg_target_configure_version_file(Euclid)
smtg_target_add_plugin_resources(Euclid
    RESOURCES
        resource/euclid.uidesc
)

# Offline simulation of process(); not part of the default build.
add_executable(EuclidSim EXCLUDE_FROM_ALL
    tests/sim.cpp source/euclidprocessor.cpp)
target_include_directories(EuclidSim PRIVATE source)
target_link_libraries(EuclidSim PRIVATE sdk sdk_hosting "-framework CoreFoundation")

# Ninja/Makefile generators get CMake's default Info.plist (CFBundlePackageType=APPL, empty
# identifier, no PkgInfo), which makes Finder show the .vst3 as a plain folder. Use a BNDL plist
# and ship PkgInfo inside the bundle (before the SDK's ad-hoc codesign step seals it).
if(SMTG_MAC)
    set_target_properties(Euclid PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.dphelmer.vstsequencers.Euclid"
        MACOSX_BUNDLE_BUNDLE_NAME "Euclid"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    target_sources(Euclid PRIVATE mac/PkgInfo)
    set_source_files_properties(mac/PkgInfo PROPERTIES MACOSX_PACKAGE_LOCATION ".")
    # The SDK's own ad-hoc sign leaves Info.plist unbound ("Sealed Resources=none"); re-sign last.
    get_target_property(EUCLID_PKG Euclid SMTG_PLUGIN_PACKAGE_PATH)
    add_custom_command(TARGET Euclid POST_BUILD
        COMMAND codesign --force --sign - "${EUCLID_PKG}"
        COMMENT "Re-sign Euclid bundle (ad-hoc)")
endif()
