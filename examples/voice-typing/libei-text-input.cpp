#include "libei-text-input.h"

#ifdef HAVE_LIBEI

#include <libei.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <map>

// Global libei context
static struct ei * g_ei_context = nullptr;
static struct ei_device * g_keyboard_device = nullptr;
static bool g_libei_initialized = false;

// Mapping from ASCII characters to Linux key codes
// This is a simplified mapping for common characters
static std::map<char, int> create_keycode_map() {
    std::map<char, int> map;
    
    // Letters (lowercase, will handle shift separately)
    for (char c = 'a'; c <= 'z'; c++) {
        map[c] = KEY_A + (c - 'a');
    }
    
    // Numbers
    for (char c = '0'; c <= '9'; c++) {
        map[c] = KEY_0 + (c - '0');
    }
    
    // Common punctuation (unshifted versions)
    map[' '] = KEY_SPACE;
    map['\n'] = KEY_ENTER;
    map['\t'] = KEY_TAB;
    map['-'] = KEY_MINUS;
    map['='] = KEY_EQUAL;
    map['['] = KEY_LEFTBRACE;
    map[']'] = KEY_RIGHTBRACE;
    map['\\'] = KEY_BACKSLASH;
    map[';'] = KEY_SEMICOLON;
    map['\''] = KEY_APOSTROPHE;
    map['`'] = KEY_GRAVE;
    map[','] = KEY_COMMA;
    map['.'] = KEY_DOT;
    map['/'] = KEY_SLASH;
    
    return map;
}

// Mapping for shifted characters
static std::map<char, int> create_shifted_keycode_map() {
    std::map<char, int> map;
    
    // Uppercase letters
    for (char c = 'A'; c <= 'Z'; c++) {
        map[c] = KEY_A + (c - 'A');
    }
    
    // Shifted punctuation
    map['!'] = KEY_1;
    map['@'] = KEY_2;
    map['#'] = KEY_3;
    map['$'] = KEY_4;
    map['%'] = KEY_5;
    map['^'] = KEY_6;
    map['&'] = KEY_7;
    map['*'] = KEY_8;
    map['('] = KEY_9;
    map[')'] = KEY_0;
    map['_'] = KEY_MINUS;
    map['+'] = KEY_EQUAL;
    map['{'] = KEY_LEFTBRACE;
    map['}'] = KEY_RIGHTBRACE;
    map['|'] = KEY_BACKSLASH;
    map[':'] = KEY_SEMICOLON;
    map['"'] = KEY_APOSTROPHE;
    map['~'] = KEY_GRAVE;
    map['<'] = KEY_COMMA;
    map['>'] = KEY_DOT;
    map['?'] = KEY_SLASH;
    
    return map;
}

static const auto keycode_map = create_keycode_map();
static const auto shifted_keycode_map = create_shifted_keycode_map();

bool libei_available() {
    if (g_libei_initialized) {
        return g_ei_context != nullptr && g_keyboard_device != nullptr;
    }
    
    g_libei_initialized = true;
    
    // Try to connect to the EIS server via socket
    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir == nullptr) {
        fprintf(stderr, "libei: XDG_RUNTIME_DIR not set\n");
        return false;
    }
    
    // Try connecting to GNOME Remote Desktop EIS socket
    char socket_path[512];
    snprintf(socket_path, sizeof(socket_path), "%s/gnome-remote-desktop/eis-0", runtime_dir);
    
    // Check if socket exists
    if (access(socket_path, F_OK) != 0) {
        fprintf(stderr, "libei: EIS socket not found at %s\n", socket_path);
        fprintf(stderr, "       Make sure GNOME Remote Desktop is running\n");
        return false;
    }
    
    // Create libei context
    g_ei_context = ei_new_sender(nullptr);
    if (g_ei_context == nullptr) {
        fprintf(stderr, "libei: Failed to create EIS sender context\n");
        return false;
    }
    
    // Connect to the socket
    if (ei_setup_backend_socket(g_ei_context, socket_path) != 0) {
        fprintf(stderr, "libei: Failed to connect to EIS socket: %s\n", socket_path);
        ei_unref(g_ei_context);
        g_ei_context = nullptr;
        return false;
    }
    
    // Give libei time to establish connection
    // In a real implementation, we'd use proper event loop
    usleep(50000); // 50ms
    
    // For now, just check that we have a context
    // A full implementation would properly wait for devices to be available
    // We'll handle device creation lazily when actually typing
    
    fprintf(stderr, "libei: Connected to EIS server\n");
    return true;
}

static bool send_key(int keycode, bool shift_needed) {
    // Simplified version: just print what we would type
    // A full libei implementation requires proper event handling
    // which is complex for this initial version
    
    // For now, we'll mark this as "not fully implemented"
    return false;
}

bool libei_type_text(const std::string& text) {
    if (!libei_available()) {
        fprintf(stderr, "libei: Not available, cannot type text\n");
        fprintf(stderr, "       Transcribed text: %s\n", text.c_str());
        return false;
    }
    
    // Note: Full libei keyboard emulation requires:
    // 1. Waiting for seat and device setup events
    // 2. Creating a proper keyboard device with capabilities
    // 3. Sending key events through the device
    // 4. Handling the complex event loop
    //
    // This is a simplified version that connects but doesn't yet emit keys
    // A full implementation would need significant additional code
    
    fprintf(stderr, "libei: Text input not yet fully implemented\n");
    fprintf(stderr, "       Transcribed text: %s\n", text.c_str());
    
    // Clean up
    if (g_ei_context) {
        ei_unref(g_ei_context);
        g_ei_context = nullptr;
    }
    
    return false;
}

#endif // HAVE_LIBEI
