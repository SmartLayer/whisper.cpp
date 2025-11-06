#include "uinput-text-input.h"

#ifdef __linux__

#include <linux/uinput.h>
#include <linux/input-event-codes.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <map>

// Global uinput file descriptor
static int g_uinput_fd = -1;
static bool g_uinput_initialized = false;

// Mapping from ASCII characters to Linux key codes (unshifted)
static std::map<char, int> create_keycode_map() {
    std::map<char, int> map;
    
    // Letters (NOT alphabetical - they're in QWERTY keyboard position order!)
    // Top row
    map['q'] = KEY_Q;  // 16
    map['w'] = KEY_W;  // 17
    map['e'] = KEY_E;  // 18
    map['r'] = KEY_R;  // 19
    map['t'] = KEY_T;  // 20
    map['y'] = KEY_Y;  // 21
    map['u'] = KEY_U;  // 22
    map['i'] = KEY_I;  // 23
    map['o'] = KEY_O;  // 24
    map['p'] = KEY_P;  // 25
    
    // Middle row
    map['a'] = KEY_A;  // 30
    map['s'] = KEY_S;  // 31
    map['d'] = KEY_D;  // 32
    map['f'] = KEY_F;  // 33
    map['g'] = KEY_G;  // 34
    map['h'] = KEY_H;  // 35
    map['j'] = KEY_J;  // 36
    map['k'] = KEY_K;  // 37
    map['l'] = KEY_L;  // 38
    
    // Bottom row
    map['z'] = KEY_Z;  // 44
    map['x'] = KEY_X;  // 45
    map['c'] = KEY_C;  // 46
    map['v'] = KEY_V;  // 47
    map['b'] = KEY_B;  // 48
    map['n'] = KEY_N;  // 49
    map['m'] = KEY_M;  // 50
    
    // Numbers
    map['1'] = KEY_1;  // 2
    map['2'] = KEY_2;  // 3
    map['3'] = KEY_3;  // 4
    map['4'] = KEY_4;  // 5
    map['5'] = KEY_5;  // 6
    map['6'] = KEY_6;  // 7
    map['7'] = KEY_7;  // 8
    map['8'] = KEY_8;  // 9
    map['9'] = KEY_9;  // 10
    map['0'] = KEY_0;  // 11
    
    // Common punctuation (unshifted)
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
    
    // Uppercase letters (use same KEY codes as lowercase, but with shift)
    // Top row
    map['Q'] = KEY_Q;
    map['W'] = KEY_W;
    map['E'] = KEY_E;
    map['R'] = KEY_R;
    map['T'] = KEY_T;
    map['Y'] = KEY_Y;
    map['U'] = KEY_U;
    map['I'] = KEY_I;
    map['O'] = KEY_O;
    map['P'] = KEY_P;
    
    // Middle row
    map['A'] = KEY_A;
    map['S'] = KEY_S;
    map['D'] = KEY_D;
    map['F'] = KEY_F;
    map['G'] = KEY_G;
    map['H'] = KEY_H;
    map['J'] = KEY_J;
    map['K'] = KEY_K;
    map['L'] = KEY_L;
    
    // Bottom row
    map['Z'] = KEY_Z;
    map['X'] = KEY_X;
    map['C'] = KEY_C;
    map['V'] = KEY_V;
    map['B'] = KEY_B;
    map['N'] = KEY_N;
    map['M'] = KEY_M;
    
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

// Decode UTF-8 sequence and return Unicode code point + bytes consumed
static int decode_utf8(const std::string& text, size_t pos, uint32_t* codepoint) {
    unsigned char c = text[pos];
    
    if ((c & 0x80) == 0) {
        // 1-byte ASCII
        *codepoint = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0 && pos + 1 < text.size()) {
        // 2-byte sequence
        *codepoint = ((c & 0x1F) << 6) | (text[pos + 1] & 0x3F);
        return 2;
    } else if ((c & 0xF0) == 0xE0 && pos + 2 < text.size()) {
        // 3-byte sequence
        *codepoint = ((c & 0x0F) << 12) | ((text[pos + 1] & 0x3F) << 6) | (text[pos + 2] & 0x3F);
        return 3;
    } else if ((c & 0xF8) == 0xF0 && pos + 3 < text.size()) {
        // 4-byte sequence
        *codepoint = ((c & 0x07) << 18) | ((text[pos + 1] & 0x3F) << 12) | 
                     ((text[pos + 2] & 0x3F) << 6) | (text[pos + 3] & 0x3F);
        return 4;
    }
    
    *codepoint = 0;
    return 1; // Invalid, skip 1 byte
}

// Strip accents from Unicode code points
// Returns ASCII character or 0 if no mapping
static char strip_accent_unicode(uint32_t codepoint) {
    // Map common accented characters to ASCII equivalents
    static const std::map<uint32_t, char> accent_map = {
        // Lowercase letters
        {0x00E0, 'a'}, {0x00E1, 'a'}, {0x00E2, 'a'}, {0x00E3, 'a'}, {0x00E4, 'a'}, {0x00E5, 'a'}, // àáâãäå
        {0x00E8, 'e'}, {0x00E9, 'e'}, {0x00EA, 'e'}, {0x00EB, 'e'}, // èéêë
        {0x00EC, 'i'}, {0x00ED, 'i'}, {0x00EE, 'i'}, {0x00EF, 'i'}, // ìíîï
        {0x00F2, 'o'}, {0x00F3, 'o'}, {0x00F4, 'o'}, {0x00F5, 'o'}, {0x00F6, 'o'}, {0x00F8, 'o'}, // òóôõöø
        {0x00F9, 'u'}, {0x00FA, 'u'}, {0x00FB, 'u'}, {0x00FC, 'u'}, // ùúûü
        {0x00F1, 'n'}, // ñ
        {0x00E7, 'c'}, // ç
        {0x00FF, 'y'}, {0x00FD, 'y'}, // ÿý
        {0x00E6, 'a'}, // æ → a (could be "ae" but we'll simplify)
        {0x0153, 'o'}, // œ → o (could be "oe")
        // Uppercase letters
        {0x00C0, 'A'}, {0x00C1, 'A'}, {0x00C2, 'A'}, {0x00C3, 'A'}, {0x00C4, 'A'}, {0x00C5, 'A'},
        {0x00C8, 'E'}, {0x00C9, 'E'}, {0x00CA, 'E'}, {0x00CB, 'E'},
        {0x00CC, 'I'}, {0x00CD, 'I'}, {0x00CE, 'I'}, {0x00CF, 'I'},
        {0x00D2, 'O'}, {0x00D3, 'O'}, {0x00D4, 'O'}, {0x00D5, 'O'}, {0x00D6, 'O'}, {0x00D8, 'O'},
        {0x00D9, 'U'}, {0x00DA, 'U'}, {0x00DB, 'U'}, {0x00DC, 'U'},
        {0x00D1, 'N'},
        {0x00C7, 'C'},
        {0x00DD, 'Y'},
        {0x00C6, 'A'}, // Æ
        {0x0152, 'O'}, // Œ
    };
    
    auto it = accent_map.find(codepoint);
    if (it != accent_map.end()) {
        return it->second;
    }
    
    // If it's ASCII, return as-is
    if (codepoint < 128) {
        return (char)codepoint;
    }
    
    return 0; // Unknown/unsupported character
}

// Send a single input event
static bool send_event(int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    
    ev.type = type;
    ev.code = code;
    ev.value = value;
    
    if (write(g_uinput_fd, &ev, sizeof(ev)) != sizeof(ev)) {
        return false;
    }
    
    return true;
}

// Send a key press or release
static bool send_key_event(int keycode, bool press) {
    return send_event(EV_KEY, keycode, press ? 1 : 0);
}

// Send a sync event to indicate end of event group
static bool send_sync() {
    return send_event(EV_SYN, SYN_REPORT, 0);
}

// Initialize uinput device
static bool init_uinput() {
    if (g_uinput_initialized) {
        return g_uinput_fd >= 0;
    }
    
    g_uinput_initialized = true;
    
    // Try to open uinput device
    g_uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (g_uinput_fd < 0) {
        fprintf(stderr, "uinput: Failed to open /dev/uinput\n");
        fprintf(stderr, "        Make sure you're in the 'input' group: sudo usermod -aG input $USER\n");
        fprintf(stderr, "        Then log out and back in\n");
        return false;
    }
    
    // Enable key events
    if (ioctl(g_uinput_fd, UI_SET_EVBIT, EV_KEY) < 0) {
        fprintf(stderr, "uinput: Failed to set EV_KEY\n");
        close(g_uinput_fd);
        g_uinput_fd = -1;
        return false;
    }
    
    if (ioctl(g_uinput_fd, UI_SET_EVBIT, EV_SYN) < 0) {
        fprintf(stderr, "uinput: Failed to set EV_SYN\n");
        close(g_uinput_fd);
        g_uinput_fd = -1;
        return false;
    }
    
    // Enable all keys we might need
    // Letters - MUST enable each one explicitly (they're not consecutive!)
    // Top row
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_Q);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_W);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_E);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_R);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_T);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_Y);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_U);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_I);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_O);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_P);
    // Middle row
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_A);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_S);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_D);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_F);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_G);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_H);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_J);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_K);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_L);
    // Bottom row
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_Z);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_X);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_C);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_V);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_B);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_N);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_M);
    
    // Numbers
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_1);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_2);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_3);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_4);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_5);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_6);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_7);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_8);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_9);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_0);
    
    // Special keys
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_SPACE);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_ENTER);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_TAB);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_LEFTSHIFT);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_RIGHTSHIFT);
    
    // Punctuation
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_MINUS);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_EQUAL);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_LEFTBRACE);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_RIGHTBRACE);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_SEMICOLON);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_APOSTROPHE);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_GRAVE);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_BACKSLASH);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_COMMA);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_DOT);
    ioctl(g_uinput_fd, UI_SET_KEYBIT, KEY_SLASH);
    
    // Create the device
    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;  // Fake vendor ID
    setup.id.product = 0x5678; // Fake product ID
    strncpy(setup.name, "Whisper Voice Typing Keyboard", UINPUT_MAX_NAME_SIZE);
    
    if (ioctl(g_uinput_fd, UI_DEV_SETUP, &setup) < 0) {
        fprintf(stderr, "uinput: Failed to setup device\n");
        close(g_uinput_fd);
        g_uinput_fd = -1;
        return false;
    }
    
    if (ioctl(g_uinput_fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "uinput: Failed to create device\n");
        close(g_uinput_fd);
        g_uinput_fd = -1;
        return false;
    }
    
    // Give the system time to recognize the device
    usleep(100000); // 100ms
    
    fprintf(stderr, "uinput: Virtual keyboard created successfully\n");
    return true;
}

bool uinput_available() {
    return init_uinput();
}

// Type a single key with optional shift
static bool type_key(int keycode, bool shift_needed) {
    if (shift_needed) {
        if (!send_key_event(KEY_LEFTSHIFT, true)) return false;
        if (!send_sync()) return false;
    }
    
    // Press key
    if (!send_key_event(keycode, true)) return false;
    if (!send_sync()) return false;
    
    // Release key  
    if (!send_key_event(keycode, false)) return false;
    if (!send_sync()) return false;
    
    if (shift_needed) {
        if (!send_key_event(KEY_LEFTSHIFT, false)) return false;
        if (!send_sync()) return false;
    }
    
    return true;
}

bool uinput_type_text(const std::string& text) {
    if (!uinput_available()) {
        fprintf(stderr, "uinput: Not available, cannot type text\n");
        fprintf(stderr, "        Transcribed text: %s\n", text.c_str());
        return false;
    }
    
    fprintf(stderr, "uinput: Typing text: %s\n", text.c_str());
    
    // Type each character (decode UTF-8 properly)
    for (size_t i = 0; i < text.size(); ) {
        uint32_t codepoint;
        int bytes = decode_utf8(text, i, &codepoint);
        
        // Try to find ASCII equivalent (handles accents and plain ASCII)
        char ascii_char = strip_accent_unicode(codepoint);
        bool sent = false;
        
        if (ascii_char != 0) {
            // Check if it's a regular (unshifted) character
            auto it = keycode_map.find(ascii_char);
            if (it != keycode_map.end()) {
                sent = type_key(it->second, false);
            } else {
                // Check if it's a shifted character
                auto it_shifted = shifted_keycode_map.find(ascii_char);
                if (it_shifted != shifted_keycode_map.end()) {
                    sent = type_key(it_shifted->second, true);
                }
            }
        }
        
        if (!sent && codepoint != 0) {
            // Extract the full UTF-8 character for error message
            std::string utf8_char = text.substr(i, bytes);
            fprintf(stderr, "uinput: Warning: Cannot type '%s' (U+%04X)\n", 
                    utf8_char.c_str(), codepoint);
        }
        
        i += bytes;
    }
    
    fprintf(stderr, "uinput: Text typing complete\n");
    return true;
}

// Cleanup function (optional, called on program exit)
void __attribute__((destructor)) cleanup_uinput() {
    if (g_uinput_fd >= 0) {
        ioctl(g_uinput_fd, UI_DEV_DESTROY);
        close(g_uinput_fd);
        g_uinput_fd = -1;
    }
}

#endif // __linux__

