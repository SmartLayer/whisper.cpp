#pragma once

#include <string>

// Text injection using libei (Emulated Input library)
// This provides fast text input on Wayland/GNOME via the org.gnome.Mutter.RemoteDesktop interface

#ifdef HAVE_LIBEI

// Check if libei is available and can connect to the EIS server
bool libei_available();

// Type text using libei virtual keyboard
// Returns true on success, false on failure
bool libei_type_text(const std::string& text);

#else

// Stub implementations when libei is not available
inline bool libei_available() { return false; }
inline bool libei_type_text(const std::string& text) {
    fprintf(stderr, "Error: libei support not compiled. Text cannot be injected.\n");
    fprintf(stderr, "       Transcribed text: %s\n", text.c_str());
    return false;
}

#endif // HAVE_LIBEI

