find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets Charts Network Svg)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(glm REQUIRED)
if(BUILD_TESTING)
    find_package(Catch2 3 REQUIRED)
endif()

# ---------------------------------------------------------------------------
# GDAL: cmake config (conda-forge / Homebrew / GDAL 3.8+) → pkg-config fallback
# ---------------------------------------------------------------------------
find_package(GDAL CONFIG QUIET)
if(TARGET GDAL::GDAL)
    set(FV_GDAL_TARGET GDAL::GDAL)
    message(STATUS "FlashViewer: GDAL via cmake config (GDAL::GDAL)")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GDAL_PC REQUIRED IMPORTED_TARGET gdal)
    set(FV_GDAL_TARGET PkgConfig::GDAL_PC)
    message(STATUS "FlashViewer: GDAL via pkg-config (PkgConfig::GDAL_PC)")
endif()

# ---------------------------------------------------------------------------
# muParser: cmake config (conda-forge / Homebrew) → pkg-config fallback
# ---------------------------------------------------------------------------
find_package(muparser CONFIG QUIET)
if(TARGET muparser::muparser)
    set(FV_MUPARSER_TARGET muparser::muparser)
    message(STATUS "FlashViewer: muParser via cmake config (muparser::muparser)")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(MUPARSER_PC QUIET IMPORTED_TARGET muparser)
    endif()
    if(TARGET PkgConfig::MUPARSER_PC)
        set(FV_MUPARSER_TARGET PkgConfig::MUPARSER_PC)
        message(STATUS "FlashViewer: muParser via pkg-config (PkgConfig::MUPARSER_PC)")
    else()
        message(FATAL_ERROR
            "muParser not found.\n"
            "  Linux:   sudo apt install libmuparser-dev\n"
            "  macOS:   brew install muparser\n"
            "  Windows: conda install -c conda-forge muparser (or add it to environment-windows.yml)\n"
            "  conda (Linux/macOS): provided by environment-linux.yml / environment-macos.yml")
    endif()
endif()

qt_standard_project_setup()
