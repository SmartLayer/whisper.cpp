# Text Injection Implementation

This document explains the technical choices for text injection in the voice-typing example.

## Implementation: Linux uinput

The voice-typing example uses **Linux uinput** for keyboard emulation. uinput is a kernel-level interface that allows userspace programs to create virtual input devices.

### Why uinput

**Platform compatibility**: Works on both X11 and Wayland without compositor-specific protocols.

**Performance**: Direct kernel interface with minimal overhead. Events are written directly to `/dev/uinput` and processed by the Linux input subsystem.

**Simplicity**: No external dependencies beyond Linux kernel support. Single file descriptor, straightforward ioctl-based setup.

**Reliability**: Not dependent on display server protocols, compositor features, or background daemons.

## Alternative Approaches and Their Limitations

### libei (Emulated Input Library)

**Purpose**: Modern input emulation API for Wayland, designed for remote desktop scenarios.

**Limitation**: Requires an active EIS (Emulated Input Server) connection, which only exists during active remote desktop sessions (RDP/VNC). The EIS socket is created by GNOME Remote Desktop for remote clients, not local applications.

**Technical reason**: libei's design is session-based - it expects a remote desktop context where the EIS server mediates between remote clients and local input. Local applications cannot create this context.

**Conclusion**: Wrong abstraction layer for local keyboard emulation.

### wtype (Wayland Virtual Keyboard)

**Purpose**: Wayland-native text injection tool.

**Limitation**: Requires `zwp_virtual_keyboard_v1` Wayland protocol extension.

**Compositor support**: 
- ✅ KDE Plasma (kwin)
- ✅ Sway
- ❌ GNOME (mutter)

**Technical reason**: GNOME's mutter compositor doesn't implement the virtual keyboard protocol for security reasons. Each Wayland compositor decides which protocol extensions to support.

**Conclusion**: Not portable across Wayland compositors.

### xdotool (X11 Input Simulation)

**Purpose**: X11 keyboard/mouse automation tool.

**Limitation**: X11-only. Does not work on native Wayland sessions.

**XWayland compatibility**: May work for X11 applications running under XWayland, but with limitations and security restrictions.

**Conclusion**: Not suitable for modern Wayland-native environments.

### ydotool (Generic Input Emulation)

**Architecture**: Client-server model with `ydotoold` daemon and `ydotool` command-line client.

**Method**: Uses uinput (same as our implementation) but adds daemon layer for privilege management.

**Overhead**:
- Process spawning for each invocation
- Unix socket IPC between client and daemon
- Additional message serialization/deserialization

**Use case**: Command-line scripting where per-invocation overhead is acceptable.

**Why not used**: Unnecessary daemon layer when application can access uinput directly. The daemon exists to allow unprivileged clients to share a single uinput device, but our application creates its own device.

## Technical Architecture

### Device Creation

```c
// Open uinput device
fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

// Declare capabilities
ioctl(fd, UI_SET_EVBIT, EV_KEY);
ioctl(fd, UI_SET_EVBIT, EV_SYN);

// Enable specific keys
ioctl(fd, UI_SET_KEYBIT, KEY_A);
ioctl(fd, UI_SET_KEYBIT, KEY_B);
// ... etc

// Create virtual device
ioctl(fd, UI_DEV_CREATE);
```

The virtual keyboard device persists for the lifetime of the file descriptor.

### Event Submission

```c
struct input_event ev;
ev.type = EV_KEY;
ev.code = KEY_A;
ev.value = 1;  // Press
write(fd, &ev, sizeof(ev));

// Sync event marks end of event group
ev.type = EV_SYN;
ev.code = SYN_REPORT;
ev.value = 0;
write(fd, &ev, sizeof(ev));
```

Events are processed by the kernel's input subsystem and delivered to the focused application.

## Implementation Details

### Keycode Mapping

Linux input keycodes follow **QWERTY keyboard physical layout**, not alphabetical order:

```
KEY_Q = 16, KEY_W = 17, KEY_E = 18, ...  (top row)
KEY_A = 30, KEY_S = 31, KEY_D = 32, ...  (home row)
KEY_Z = 44, KEY_X = 45, KEY_C = 46, ...  (bottom row)
```

Character-to-keycode mapping must use explicit lookup tables, not arithmetic offsets.

### Key Capability Registration

Each key the device can emit must be explicitly registered with `UI_SET_KEYBIT`. The range `KEY_A` to `KEY_Z` is not contiguous - keys must be enabled individually.

### Modifier Keys

Uppercase letters and symbols require the SHIFT modifier:
1. Press SHIFT key
2. Send sync event
3. Press target key
4. Send sync event
5. Release target key
6. Send sync event
7. Release SHIFT key
8. Send sync event

### UTF-8 and Character Limitations

**Capability**: uinput operates at the physical key level (keycodes), not the symbol level (Unicode).

**ASCII coverage**: Full support for US QWERTY keyboard characters:
- Letters: a-z, A-Z
- Numbers: 0-9
- Punctuation: `` `~!@#$%^&*()-_=+[{]}\|;:'",<.>/? ``
- Whitespace: space, tab, newline

**Accented characters**: Implemented via transliteration mapping:
- é, è, ê, ë → e
- à, á, â, ã, ä, å → a
- ñ → n
- ç → c
- etc. (Latin-1 Supplement range)

Implementation uses UTF-8 decoding to Unicode code points, then maps to ASCII equivalents.

**Non-Latin scripts**: Not supported. Characters outside the mapping (Chinese, Arabic, Cyrillic, etc.) are skipped with warning.

**Rationale**: Whisper transcription of English audio produces primarily ASCII with occasional accented words (café, résumé). Transliteration provides graceful degradation for the common case. Complete Unicode support would require compose sequences or clipboard injection, adding significant complexity for marginal benefit.

## Setup Requirements

### Permissions

User must have write access to `/dev/uinput`. Standard approach:

```bash
# Add user to input group
sudo usermod -aG input $USER

# Log out and back in for group membership to take effect
```

The input group has default access to input devices including uinput.

### Alternative: udev Rule

For environments where input group membership is not desired:

```bash
# /etc/udev/rules.d/99-uinput.rules
KERNEL=="uinput", MODE="0660", GROUP="input", OPTIONS+="static_node=uinput"
```

## Platform Portability

### Linux
✅ **Fully supported** - uinput is part of the Linux kernel.

### macOS
❌ **Not supported** - no uinput equivalent.

**Alternative**: CGEventCreateKeyboardEvent from Core Graphics framework.

### Windows
❌ **Not supported** - no uinput equivalent.

**Alternative**: SendInput from Win32 API.

### Other Unix systems
❌ **Not supported** - uinput is Linux-specific.

**Note**: The implementation is conditionally compiled (`#ifdef __linux__`). On unsupported platforms, the text injection functions are no-ops that print the transcribed text to stderr.

## Security Considerations

Access to `/dev/uinput` allows creating virtual input devices that can send arbitrary keyboard and mouse events to any application. This is equivalent to having physical access to the keyboard.

**Scope**: The voice-typing application only creates keyboard devices and only sends events corresponding to transcribed text. No mouse events, no raw input reading, no keystroke logging.

**Permissions model**: Requires explicit user group membership or udev configuration, preventing accidental privilege escalation.

## References

- Linux kernel documentation: `Documentation/input/uinput.rst`
- Input event codes: `/usr/include/linux/input-event-codes.h`
- uinput header: `/usr/include/linux/uinput.h`


