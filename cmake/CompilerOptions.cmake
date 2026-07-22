if(MSVC)
    target_compile_options(FlashViewer PRIVATE /W4 /WX-)
    target_compile_definitions(FlashViewer PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(FlashViewer PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Release>:-O3>
    )
endif()

if(FLASHVIEWER_ENABLE_ASAN)
    target_compile_options(FlashViewer PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(FlashViewer PRIVATE -fsanitize=address,undefined)
endif()
