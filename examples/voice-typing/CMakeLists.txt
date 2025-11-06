set(TARGET voice-typing)

add_executable(${TARGET} voice-typing.cpp)

include(DefaultTargetOptions)

target_link_libraries(${TARGET} PRIVATE common common-sdl whisper)

# Try to find libei using pkg-config
find_package(PkgConfig)

if(PkgConfig_FOUND)
    pkg_check_modules(LIBEI libei-1.0)
    
    if(LIBEI_FOUND)
        target_sources(${TARGET} PRIVATE libei-text-input.cpp)
        target_include_directories(${TARGET} PRIVATE ${LIBEI_INCLUDE_DIRS})
        target_link_libraries(${TARGET} PRIVATE ${LIBEI_LIBRARIES})
        target_compile_definitions(${TARGET} PRIVATE HAVE_LIBEI)
        message(STATUS "libei found - voice-typing will support Wayland text injection")
    else()
        message(WARNING "libei not found - voice-typing will not support text injection")
        message(WARNING "  Install libei: sudo apt install libei-dev (or equivalent)")
    endif()
else()
    message(WARNING "pkg-config not found - cannot search for libei")
endif()

set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME "whisper-voice-typing")

