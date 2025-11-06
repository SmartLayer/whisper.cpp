# whisper.cpp/examples/voice-typing

Real-time voice typing with automatic speech detection and fast text injection.

This example captures audio from your microphone, detects when you finish speaking (via silence detection), transcribes using whisper.cpp, and types the text directly into your active window.

## Features

- **Automatic silence detection**: Stops recording after 3 seconds of silence (configurable)
- **Early stop**: Press the keyboard shortcut again while recording to transcribe immediately
- **Fast text injection**: Uses Linux uinput for efficient text input on X11 and Wayland
- **Model stays loaded**: Unlike bash scripts, the model remains in memory for potential repeated use
- **Minimal dependencies**: Only SDL2 (audio capture) - text injection uses kernel uinput

## System Requirements

### Required
- Linux (tested on Ubuntu 25.04 with GNOME 48 on Wayland)
- SDL2 library
- whisper.cpp models
- User in `input` group (for uinput access)

**Note**: Text injection uses Linux uinput kernel interface. Works on both X11 and Wayland.

## Building

### Install Dependencies

On Debian/Ubuntu:
```bash
# SDL2 for audio capture
sudo apt install libsdl2-dev

# Add user to input group for uinput access
sudo usermod -aG input $USER
# Log out and back in for group change to take effect
```

On Fedora:
```bash
# SDL2 for audio capture
sudo dnf install SDL2-devel

# Add user to input group for uinput access
sudo usermod -aG input $USER
# Log out and back in for group change to take effect
```

### Build with CMake

From the whisper.cpp root directory:

```bash
# Configure with SDL2 support
cmake -B build -DWHISPER_SDL2=ON

# Build
cmake --build build --config Release

# The binary will be at: build/bin/whisper-voice-typing
```

Text injection support is built-in on Linux (uses uinput kernel interface).

### Verify uinput Access

Check if you have access:
```bash
# Should show you're in the input group
groups | grep input

# Check uinput device access
ls -l /dev/uinput
# Should show: crw-rw---- ... root input ...
```

## Usage

### Basic Usage

```bash
./build/bin/whisper-voice-typing -m models/ggml-base.en.bin
```

The tool will:
1. Start recording from your default microphone
2. Display "🗣️  Speech detected!" when you start speaking
3. Automatically stop after 3 seconds of silence
4. Transcribe the audio
5. Type the transcribed text into your active window

### Early Stop

Press your keyboard shortcut again while recording to stop immediately and transcribe right away (no need to wait for silence).

### As GNOME Keyboard Shortcut

1. Open **Settings** → **Keyboard** → **Keyboard Shortcuts**
2. Scroll to **Custom Shortcuts** section
3. Click **Add Shortcut**
4. Set:
   - **Name**: Voice Typing
   - **Command**: `/home/yourusername/code/whisper.cpp/build/bin/whisper-voice-typing -m /home/yourusername/code/whisper.cpp/models/ggml-base.en.bin`
   - **Shortcut**: Press your desired key combination (e.g., `Super+V`)
5. Click **Add**

Now you can:
- Press `Super+V` → Speak → Wait 3 seconds → Text appears
- Press `Super+V` → Speak → Press `Super+V` again → Text appears immediately

## Command-line Options

```
  -h, --help              Show help message
  -t N, --threads N       Number of threads (default: 4)
  -c ID, --capture ID     Audio capture device ID (default: -1 = default device)
  -vth N, --vad-thold N   VAD threshold 0.0-1.0 (default: 0.6, higher = stricter)
  -fth N, --freq-thold N  High-pass frequency cutoff (default: 100.0 Hz)
  -sil N, --silence N     Silence duration in ms to auto-stop (default: 3000)
  -max N, --max-len N     Maximum recording length in ms (default: 30000)
  -l LANG, --language     Spoken language (default: "en")
  -m FILE, --model        Path to whisper model file (required)
  --prompt TEXT           Initial prompt text for better accuracy
  --prompt-file FILE      File containing prompt text
  -nf, --no-fallback      Disable temperature fallback (faster, less accurate)
  -ps, --print-special    Print special tokens
  -ng, --no-gpu           Disable GPU acceleration
  -fa, --flash-attn       Enable flash attention (default)
  -nfa, --no-flash-attn   Disable flash attention
```

### Using Custom Prompts

You can provide domain-specific terminology to improve transcription accuracy:

```bash
# Via command line
./build/bin/whisper-voice-typing -m models/ggml-base.en.bin \
    --prompt "Technical terms: Kubernetes, Docker, PostgreSQL, Redis, webhook"

# Via file
echo "Key people: Alice Johnson, Bob Smith. Systems: Salesforce, SAP." > prompt.txt
./build/bin/whisper-voice-typing -m models/ggml-base.en.bin --prompt-file prompt.txt
```

## How It Works

### Architecture

1. **Audio Capture**: SDL2 captures microphone input at 16kHz mono
2. **Speech Detection**: Simple VAD checks audio every 500ms for speech activity
3. **Silence Detection**: Tracks consecutive silence; stops after threshold reached
4. **Transcription**: Whisper model transcribes the full audio clip
5. **Text Injection**: uinput sends keyboard events to inject text into active window

### Signal-based IPC

The tool uses a PID file (`$XDG_RUNTIME_DIR/whisper-voice-typing.pid` or `/tmp/whisper-voice-typing.pid`) to enable the early-stop feature:

- First invocation: Writes PID to file, sets up SIGUSR1 handler, starts recording
- Second invocation: Reads PID from file, sends SIGUSR1 signal to that process, exits
- Recording process: Receives SIGUSR1, stops recording early, transcribes immediately

This allows the same keyboard shortcut to both start recording and stop it early.

## Troubleshooting

### "Failed to open /dev/uinput"

You need to be in the input group:
```bash
# Add yourself to input group
sudo usermod -aG input $USER

# Log out and back in (IMPORTANT!)
# Or use: newgrp input

# Verify
groups | grep input
ls -l /dev/uinput  # Should show: crw-rw---- ... input ...
```

### "Failed to connect to EIS socket"

This error should not appear anymore - the implementation uses uinput, not EIS/libei.

### "No speech detected"

Try adjusting the VAD threshold:
```bash
# Lower threshold = more sensitive (may pick up background noise)
./build/bin/whisper-voice-typing -m models/ggml-base.en.bin -vth 0.4

# Higher threshold = less sensitive (may miss quiet speech)
./build/bin/whisper-voice-typing -m models/ggml-base.en.bin -vth 0.8
```

### SDL Audio Device Issues

List available audio devices:
```bash
# The tool will print available devices on startup
./build/bin/whisper-voice-typing -m models/ggml-base.en.bin

# Select specific device by ID
./build/bin/whisper-voice-typing -m models/ggml-base.en.bin -c 1
```

## Recommended Models

For voice typing, we recommend:

- **Fast**: `ggml-base.en.bin` (good accuracy, ~150ms transcription time)
- **Balanced**: `ggml-medium.en.bin` (better accuracy, ~400ms transcription time)  
- **Best**: `ggml-large-v3-turbo-q8_0.bin` (best accuracy, ~600ms transcription time)

Download models:
```bash
cd models
./download-ggml-model.sh base.en
./download-ggml-model.sh medium.en
./download-ggml-model.sh large-v3-turbo
```

## Platform Support

### Current Support
- **Linux (X11 and Wayland)**: Full support with uinput text injection
- **macOS/Windows**: Not yet supported (platform-specific APIs needed)

### Future Platform Support
- **macOS**: Would require CoreGraphics/CGEventCreateKeyboardEvent API
- **Windows**: Would require SendInput API

Pull requests welcome for additional platform support!

## Performance Notes

- First run loads the model (1-3 seconds depending on model size)
- Subsequent runs would be instant if the tool kept running (not implemented yet - exits after each transcription)
- Text injection via uinput is very fast (direct kernel interface)
- VAD checking adds ~500ms overhead but prevents false activations

## Limitations

- Currently exits after each transcription (doesn't stay resident)
- Simple VAD may have false positives/negatives in noisy environments
- English language models work best (multilingual models also supported)
- Requires Linux with uinput support
- Maximum recording time is 30 seconds (configurable)

## License

This example follows the whisper.cpp project license (MIT).

