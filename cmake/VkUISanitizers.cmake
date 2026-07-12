# SPDX-License-Identifier: MIT

function(vkui_enable_sanitizers target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT WIN32)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    else()
        message(WARNING "VKUI_ENABLE_SANITIZERS is not supported by this toolchain")
    endif()
endfunction()

