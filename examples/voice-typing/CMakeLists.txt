set(TARGET voice-typing)

add_executable(${TARGET} voice-typing.cpp)

include(DefaultTargetOptions)

target_link_libraries(${TARGET} PRIVATE common common-sdl whisper)

# Add uinput text injection support (Linux only)
if(UNIX AND NOT APPLE)
    target_sources(${TARGET} PRIVATE uinput-text-input.cpp)
    message(STATUS "uinput support enabled - voice-typing will support text injection on Linux")
else()
    message(WARNING "uinput only available on Linux - voice-typing will not support text injection")
endif()

set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME "whisper-voice-typing")

