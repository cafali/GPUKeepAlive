
cmake_minimum_required(VERSION 3.20)

project(GpuKeepAlive
    VERSION 1.0.0
    LANGUAGES CXX RC
)

if(NOT WIN32)
    message(FATAL_ERROR
        "GpuKeepAlive is Windows-only."
    )
endif()

add_executable(GpuKeepAlive WIN32
    main.cpp
    app.rc
)

target_compile_features(GpuKeepAlive PRIVATE cxx_std_17)

target_compile_definitions(GpuKeepAlive PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(GpuKeepAlive PRIVATE
    d3d11
    dxgi
    d3dcompiler
    shell32
)

if(MSVC)
    target_compile_options(GpuKeepAlive PRIVATE
        /W4
        /permissive-
        /O2
        /MT
    )

    target_link_options(GpuKeepAlive PRIVATE
        /SUBSYSTEM:WINDOWS
    )
endif()
