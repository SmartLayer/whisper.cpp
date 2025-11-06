# whisper.cpp/examples/voice-typing

Real-time voice typing with automatic speech detection and fast text injection.

This example captures audio from your microphone, detects when you finish speaking (via silence detection), transcribes using whisper.cpp, and types the text directly into your active window.

## Features

- **Automatic silence detection**: Stops recording after 3 seconds of silence (configurable)
- **Early stop**: Press the keyboard shortcut again while recording to transcribe immediately
- **Fast text injection**: Uses libei for efficient text input on Wayland/GNOME
- **Model stays loaded**: Unlike bash scripts, the model remains in memory for potential repeated use
- **Minimal dependencies**: Only SDL2 (audio capture) and optionally libei (text injection)

## System Requirements

### Required
- Linux (tested on Ubuntu 25.04 with GNOME 48 on Wayland)
- SDL2 library
- whisper.cpp models

### Optional (for text injection)
- libei-dev (version 1.3+)
- GNOME Remote Desktop with EIS support (GNOME 48+)
- Wayland session

**Note**: Without libei, the program will still transcribe and display the text, but won't inject it into applications.

## Building

### Install Dependencies

On Debian/Ubuntu:
```bash
# Required
sudo apt install libsdl2-dev

# Optional (for text injection)
sudo apt install libei-dev
```

On Fedora:
```bash
# Required
sudo dnf install SDL2-devel

# Optional (for text injection)
sudo dnf install libei-devel
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

The build system will automatically detect libei. If found, text injection support will be compiled in. If not found, you'll see a warning but the build will continue without text injection support.

### Verify libei Support

Check if your binary has libei support:
```bash
# If this prints the transcribed text, libei is NOT available
# If it prints an error about libei, libei IS available
./build/bin/whisper-voice-typing -m models/ggml-base.en.bin
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
5. **Text Injection**: libei sends keyboard events to inject text into active window

### Signal-based IPC

The tool uses a PID file (`$XDG_RUNTIME_DIR/whisper-voice-typing.pid` or `/tmp/whisper-voice-typing.pid`) to enable the early-stop feature:

- First invocation: Writes PID to file, sets up SIGUSR1 handler, starts recording
- Second invocation: Reads PID from file, sends SIGUSR1 signal to that process, exits
- Recording process: Receives SIGUSR1, stops recording early, transcribes immediately

This allows the same keyboard shortcut to both start recording and stop it early.

## Troubleshooting

### "libei support not compiled"

You need to install libei-dev and rebuild:
```bash
sudo apt install libei-dev
cmake -B build -DWHISPER_SDL2=ON
cmake --build build --config Release
```

### "Failed to connect to EIS socket"

Make sure GNOME Remote Desktop is installed and running:
```bash
# Install if missing
sudo apt install gnome-remote-desktop

# Check if running
systemctl --user status gnome-remote-desktop

# Start if needed
systemctl --user start gnome-remote-desktop
```

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
- **Linux/Wayland/GNOME**: Full support with libei text injection
- **Linux/X11**: Transcription works; text injection not yet implemented (consider using xdotool externally)

### Future Platform Support
- **macOS**: Would require CoreGraphics/CGEventCreateKeyboardEvent API
- **Windows**: Would require SendInput API
- **Other Wayland compositors**: Should work if they support the Remote Desktop portal

Pull requests welcome for additional platform support!

## Performance Notes

- First run loads the model (1-3 seconds depending on model size)
- Subsequent runs would be instant if the tool kept running (not implemented yet - exits after each transcription)
- Text injection via libei is significantly faster than ydotool character-by-character mode
- VAD checking adds ~500ms overhead but prevents false activations

## Limitations

- Currently exits after each transcription (doesn't stay resident)
- Simple VAD may have false positives/negatives in noisy environments
- English language models work best (multilingual models also supported)
- Requires Wayland with EIS support for text injection
- Maximum recording time is 30 seconds (configurable)

## License

This example follows the whisper.cpp project license (MIT).

