if(MSVC)
    target_compile_options(FlashViewer PRIVATE /W4 /WX-)
    target_compile_definitions(FlashViewer PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(FlashViewer PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Release>:-O3>
    )
    # Soft ABI guard: conda-forge binaries (Qt/GDAL) are built against GCC 12's
    # libstdc++ ABI. Building with an older system GCC against them causes
    # GLIBCXX/link errors — warn early (see docs/INSTALL.md §2.6).
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
        message(WARNING
            "Detected GCC ${CMAKE_CXX_COMPILER_VERSION} (< 12). conda-forge Qt/GDAL "
            "are built against GCC 12; mixing them with this compiler may cause "
            "GLIBCXX/libstdc++ ABI errors. Use the bundled conda toolchain or GCC >= 12.")
    endif()
endif()

if(FLASHVIEWER_ENABLE_ASAN)
    target_compile_options(FlashViewer PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(FlashViewer PRIVATE -fsanitize=address,undefined)
endif()
