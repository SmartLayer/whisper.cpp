#pragma once

#include <string>

// Text injection using Linux uinput (works on both X11 and Wayland)
// This directly uses the kernel's uinput interface for fast, reliable text injection
// Requires access to /dev/uinput (user must be in 'input' group or run with appropriate permissions)

#ifdef __linux__

// Check if uinput is available (user has permission to /dev/uinput)
bool uinput_available();

// Type text using virtual keyboard via uinput
// Returns true on success, false on failure
bool uinput_type_text(const std::string& text);

#else

// Stub implementations for non-Linux platforms
inline bool uinput_available() { return false; }
inline bool uinput_type_text(const std::string& text) {
    fprintf(stderr, "Error: uinput text injection only available on Linux\n");
    fprintf(stderr, "       Transcribed text: %s\n", text.c_str());
    return false;
}

#endif // __linux__

