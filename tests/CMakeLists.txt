include(Catch)

add_executable(flashviewer_tests
    # Custom Catch2 main (creates the shared QApplication)
    test_main.cpp

    # Existing unit tests
    test_settings.cpp
    test_geo_transform.cpp
    test_colormap.cpp
    test_tile_cache.cpp

    # Phase 0 — test-infrastructure self-tests
    test_phase0_harness.cpp

    # Phase 1 — application shell (licenses dialog / FR-APP-8)
    test_app_licenses.cpp

    # Phase 2 — raster opening/formats + no-data/NaN transparency
    test_io_formats.cpp
    test_render_nodata.cpp

    # Phase 3 — rendering & navigation
    test_camera_nav.cpp
    test_render_pipeline.cpp
    test_lod.cpp

    # Phase 3A/3B — no-data edge-bleed fix + selectable display resampling
    test_render_resampling.cpp

    # Phase 4 — percentile conversions, auto-stretch stats, colorbar re-anchor
    test_percentile.cpp

    # Phase 5 — OSM basemap: default-off, span→zoom, basemap reprojection
    test_osm.cpp

    # Phase 6.0 — per-pane layer assignment foundation (FR-PNE-4)
    test_pane.cpp

    # Phase 7 — GIS overlays: scale-bar distance + metres-per-pixel helpers (FR-GIS-1/3)
    test_gis.cpp

    # Phase 8 — Raster Math expression engine (FR-MTH)
    test_math.cpp

    # Phase 9 — GDAL ops resampling policy + no-data warp (FR-OPS-2/5, FR-ACC-3)
    test_ops.cpp

    # Phase 10 — security (UrlGuard SSRF) + error routing (ErrorReporter)
    test_security.cpp
    test_error.cpp

    # Phase 11 — on-the-fly reprojection (warpedView / readWarpedRegion + CRS helpers)
    test_crs_reproject.cpp

    # Phase 12 — performance instrumentation (frame stats / stall / latency helpers) + FR-CAP-3
    test_perf.cpp

    # Shared test harnesses / fixtures
    fixtures/FixtureFactory.cpp
    harness/GdalOracle.cpp
    harness/MockNam.cpp
    harness/GlTestHarness.cpp

    # Sources under test (no shared lib yet)
    "${CMAKE_SOURCE_DIR}/src/app/Settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/AboutLicensesDialog.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/ColormapRegistry.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/TileCache.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/ThreadPool.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/RasterLayer.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/LayerManager.cpp"
    "${CMAKE_SOURCE_DIR}/src/render/SyncGroup.cpp"
    "${CMAKE_SOURCE_DIR}/src/math/ExpressionEngine.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/RasterDataset.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/DatasetFactory.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/CloudReader.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/UrlGuard.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/BinaryRasterParser.cpp"
    "${CMAKE_SOURCE_DIR}/src/render/GlslProgram.cpp"
    "${CMAKE_SOURCE_DIR}/src/render/Camera.cpp"
    "${CMAKE_SOURCE_DIR}/src/render/TileRenderer.cpp"
    "${CMAKE_SOURCE_DIR}/src/render/SurfaceFormat.cpp"
    "${CMAKE_SOURCE_DIR}/src/render/OsmTileRenderer.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/OsmTileProvider.cpp"
    "${CMAKE_SOURCE_DIR}/src/util/Logger.cpp"
    "${CMAKE_SOURCE_DIR}/src/util/ErrorReporter.cpp"
    "${CMAKE_SOURCE_DIR}/src/util/PerfMetrics.cpp"
)

# Phase 12 — mirror the app's instrumentation gate so the perf lane can build the
# tests with FV_PERF_INSTRUMENT=ON. The [perf][logic] cases run unconditionally.
if(FV_PERF_INSTRUMENT)
    target_compile_definitions(flashviewer_tests PRIVATE FV_PERF_INSTRUMENT)
endif()

# FR-APP-8 — embed the license manifest into the test binary so
# AboutLicensesDialog::licenseManifest() can load it from the qrc (TC-APP-08).
qt_add_resources(flashviewer_tests "test_licenses"
    PREFIX "/licenses"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/THIRD_PARTY_LICENSES.md"
)

target_include_directories(flashviewer_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}"        # tests/  → "fixtures/..", "harness/.."
)

target_link_libraries(flashviewer_tests PRIVATE
    Catch2::Catch2                       # NOT WithMain — test_main.cpp provides main()
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    spdlog::spdlog
    glm::glm
    nlohmann_json::nlohmann_json
    ${FV_GDAL_TARGET}
    ${FV_MUPARSER_TARGET}
)

set_target_properties(flashviewer_tests PROPERTIES
    AUTOMOC ON
)

catch_discover_tests(flashviewer_tests)
