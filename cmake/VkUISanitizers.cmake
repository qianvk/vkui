# SPDX-License-Identifier: MIT

function(vkui_enable_sanitizers target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT WIN32)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        # Static libraries do not perform a final link. Propagate the runtime
        # flags so every executable or shared library consuming an instrumented
        # target links the matching sanitizer runtime.
        target_link_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    else()
        message(WARNING "VKUI_ENABLE_SANITIZERS is not supported by this toolchain")
    endif()
endfunction()
