
cmake_minimum_required(VERSION 3.20)

project(GPUKeepAlive
    VERSION 1.0.2
    LANGUAGES CXX RC
)

if(NOT WIN32)
    message(FATAL_ERROR
        "GpuKeepAlive is Windows-only."
    )
endif()

add_executable(GPUKeepAlive WIN32
    main.cpp
    app.rc
)

set_target_properties(GPUKeepAlive PROPERTIES
    OUTPUT_NAME "GPUKeepAlive"
)

target_compile_features(GPUKeepAlive PRIVATE cxx_std_17)

target_compile_definitions(GPUKeepAlive PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(GPUKeepAlive PRIVATE
    d3d11
    dxgi
    d3dcompiler
    shell32
    comctl32
)

if(MSVC)
    target_compile_options(GPUKeepAlive PRIVATE
        /W4
        /permissive-
        /O2
        /MT
    )

    target_link_options(GPUKeepAlive PRIVATE
        /SUBSYSTEM:WINDOWS
    )
elseif(MINGW)
    target_link_options(GPUKeepAlive PRIVATE
        -municode
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()
