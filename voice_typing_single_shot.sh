#!/bin/bash

# Voice Typing with Silero-VAD - Single Shot (for GNOME shortcut)
# This script records and monitors in real-time using Silero-VAD
# It automatically stops when you pause speaking (1.5s silence detected)
#
# Dependencies: parecord, ffmpeg, awk, ydotool or xdotool
# Requires: whisper.cpp built with vad-speech-segments and whisper-cli
# Note: Uses Debian's ydotool 1.0.4-2 (not Ubuntu's 0.1.8) for Wayland/GNOME support

# Function to show help message
show_help() {
    cat << EOF
Voice Typing - Single Shot Mode

USAGE:
    $0 [-h]

DESCRIPTION:
    Records audio from your microphone and transcribes it using Whisper AI.
    The script automatically detects when you stop speaking (after 3 seconds 
    of silence) and types the transcribed text into your currently active window.
    
    This is designed to be triggered via keyboard shortcut or desktop launcher.
    Simply activate it, speak your text, pause for 3 seconds, and the script
    will automatically transcribe and type into whatever window has focus.
    
    Press the shortcut again while recording to immediately stop and transcribe
    (no need to wait for the silence pause).

OPTIONS:
    -h, --help      Show this help message and exit

FEATURES:
    • Real-time speech detection using Silero-VAD
    • Automatic stop after silence (configurable)
    • Types directly into active window (uses ydotool or xdotool)
    • Maximum recording time: 30 seconds

DEPENDENCIES:
    • parecord (PulseAudio/PipeWire sound recorder)
    • ffmpeg (audio processing)
    • netcat (nc) and ss (socket tools for inter-process signaling)
    • ydotool or xdotool (for typing into windows)
      - For Wayland sessions: ydotool 1.0.4-2 from Debian (not Ubuntu's 0.1.8)
        Must be properly configured with user in input group and service running
      - For X11 sessions: xdotool
      - Script automatically detects session type and selects appropriate tool
    • whisper.cpp with whisper-cli and vad-speech-segments

MODELS REQUIRED:
    • Whisper model: models/ggml-large-v3-turbo-q8_0.bin (preferred)
      Fallback: models/ggml-medium.en.bin
    • VAD model: models/ggml-silero-v5.1.2.bin

EXAMPLE:
    # Run directly
    ./voice_typing_single_shot.sh
    
    # Set as GNOME keyboard shortcut
    # 1. Go to Settings → Keyboard → Custom Shortcuts
    # 2. Add new shortcut pointing to this script
    # 3. Press your chosen key combo (e.g. Super+V)
    # 4. Use: Focus any text field, press shortcut, speak, wait 3s

EOF
    exit 0
}

# Parse command line arguments
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    show_help
fi

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Model list with fallback order (first available will be used)
MODELS=(
    "models/ggml-medium.en.bin"
    "models/ggml-large-v3-turbo-q8_0.bin"
)
VAD_MODEL_PATH="models/ggml-silero-v5.1.2.bin"
# Use system-wide if available, else Vulkan build, else regular build
WHISPER_CLI=$(command -v whisper-cli 2>/dev/null || echo "./build-vulkan/bin/whisper-cli")
[ -f "$WHISPER_CLI" ] || WHISPER_CLI="./build/bin/whisper-cli"
VAD_TOOL=$(command -v vad-speech-segments 2>/dev/null || echo "./build-vulkan/bin/vad-speech-segments")
[ -f "$VAD_TOOL" ] || VAD_TOOL="./build/bin/vad-speech-segments"
RECORDING_TIMESTAMP=$(date +%s)
TEMP_AUDIO="/tmp/voice_typing_recording_${RECORDING_TIMESTAMP}.wav"
TEMP_CHUNK="/tmp/voice_typing_chunk_${RECORDING_TIMESTAMP}.wav"
LISTEN_PORT=49152  # Port to indicate recording in progress

# Initial prompt to improve accuracy for domain-specific terms
# Condensed from capture-correction-index.md to fit Whisper's ~224 token limit
PROMPT_TEXT="This is a business transcription for Historic Rivermill. Key people: Liansu Yu, Weiwu Zhang, Iunus, Teegan, Rodrigo Peschiera, Priyanka Das, Diogo, Bhoomika Gondaliya. Locations: Historic Rivermill, Mount Nathan, Beaudesert-Nerang Road, Gold Coast Hinterland. Systems: Rezdy, Xero, Deputy, Square POS, AWS Connect. Terms: FOH, BOH, RSA, TSS visa, KPI, SOP, Maître d', à la carte, Peruvian Paso horses. Organizations: DETSI, TEQ, OLGR, ATO."

# VAD settings - tuned for pause detection
VAD_THRESHOLD=0.4           # Higher = more confident speech required
SILENCE_DURATION_MS=3000    # Milliseconds of silence to end recording
CHUNK_DURATION_MS=500       # Check every 500ms
MAX_RECORDING_TIME_MS=30000 # Maximum recording time in milliseconds

# Check if required tools are available
if ! command -v parecord &> /dev/null; then
    echo "❌ Error: parecord not found"
    echo "   Install with: sudo apt install pulseaudio-utils"
    exit 1
fi

if ! command -v ffmpeg &> /dev/null; then
    echo "❌ Error: ffmpeg not found"
    echo "   Install with: sudo apt install ffmpeg"
    exit 1
fi

if ! command -v nc &> /dev/null; then
    echo "❌ Error: netcat (nc) not found"
    echo "   Install with: sudo apt install netcat-openbsd"
    exit 1
fi

if ! command -v ss &> /dev/null; then
    echo "❌ Error: ss (socket statistics) not found"
    echo "   Install with: sudo apt install iproute2"
    exit 1
fi

# Detect session type
SESSION_TYPE=""
if [ -n "$WAYLAND_DISPLAY" ]; then
    SESSION_TYPE="wayland"
elif [ -n "$DISPLAY" ]; then
    SESSION_TYPE="x11"
else
    # Fallback detection methods
    if [ -n "$XDG_SESSION_TYPE" ]; then
        SESSION_TYPE="$XDG_SESSION_TYPE"
    elif [ -n "$XDG_CURRENT_DESKTOP" ]; then
        # Some desktop environments are typically Wayland
        case "$XDG_CURRENT_DESKTOP" in
            *GNOME*|*KDE*|*Sway*|*Hyprland*)
                SESSION_TYPE="wayland"
                ;;
            *)
                SESSION_TYPE="x11"
                ;;
        esac
    else
        SESSION_TYPE="unknown"
    fi
fi

# Check if typing tools are available
TYPING_TOOL=""
TYPING_TOOL_NAME=""

# Select tool based on session type
if [ "$SESSION_TYPE" = "wayland" ]; then
    # Prefer ydotool for Wayland sessions
    if command -v ydotool &> /dev/null; then
        TYPING_TOOL="ydotool"
        TYPING_TOOL_NAME="ydotool (Wayland/GNOME)"
    elif command -v xdotool &> /dev/null; then
        TYPING_TOOL="xdotool"
        TYPING_TOOL_NAME="xdotool (XWayland fallback)"
    fi
elif [ "$SESSION_TYPE" = "x11" ]; then
    # Prefer xdotool for X11 sessions
    if command -v xdotool &> /dev/null; then
        TYPING_TOOL="xdotool"
        TYPING_TOOL_NAME="xdotool (X11)"
    elif command -v ydotool &> /dev/null; then
        TYPING_TOOL="ydotool"
        TYPING_TOOL_NAME="ydotool (Wayland fallback)"
    fi
else
    # Unknown session type - try both tools
    if command -v ydotool &> /dev/null; then
        TYPING_TOOL="ydotool"
        TYPING_TOOL_NAME="ydotool (auto-detected)"
    elif command -v xdotool &> /dev/null; then
        TYPING_TOOL="xdotool"
        TYPING_TOOL_NAME="xdotool (auto-detected)"
    fi
fi

# Show session detection and tool selection
echo "🔍 Detected session type: $SESSION_TYPE"
echo "⌨️  Selected typing tool: $TYPING_TOOL_NAME"

# Error if no typing tool is available
if [ -z "$TYPING_TOOL" ]; then
    echo "❌ Error: No typing tool found for $SESSION_TYPE session"
    echo "Install the appropriate tool:"
    if [ "$SESSION_TYPE" = "wayland" ]; then
        echo "  - ydotool (Wayland/GNOME): sudo apt install ydotool"
        echo "    Note: Use Debian's version 1.0.4+ (not Ubuntu's 0.1.8)"
    elif [ "$SESSION_TYPE" = "x11" ]; then
        echo "  - xdotool (X11): sudo apt install xdotool"
    else
        echo "  - ydotool (Wayland): sudo apt install ydotool"
        echo "  - xdotool (X11): sudo apt install xdotool"
    fi
    exit 1
fi

# Check if required tools exist
if ! command -v "$WHISPER_CLI" &> /dev/null && [ ! -f "$WHISPER_CLI" ]; then
    echo "❌ Error: whisper-cli not found. Build whisper.cpp or install to /usr/local/bin"
    exit 1
fi
if ! command -v "$VAD_TOOL" &> /dev/null && [ ! -f "$VAD_TOOL" ]; then
    echo "❌ Error: vad-speech-segments not found. Build whisper.cpp or install to /usr/local/bin"
    exit 1
fi

# Select the first available model from the list
MODEL_PATH=""
for model in "${MODELS[@]}"; do
    if [ -f "$model" ]; then
        MODEL_PATH="$model"
        echo "✅ Using model: $model"
        break
    fi
done

if [ -z "$MODEL_PATH" ]; then
    echo "❌ Error: No model files found from the following list:"
    for model in "${MODELS[@]}"; do
        echo "   - $model"
    done
    echo "   Available models:"
    ls -1 models/ggml-*.bin 2>/dev/null | grep -v "for-tests" | sed 's/^/   - /'
    exit 1
fi

if [ ! -f "$VAD_MODEL_PATH" ]; then
    echo "❌ Error: VAD model file not found at: $VAD_MODEL_PATH"
    echo "   Download it with: bash models/download-vad-model.sh"
    exit 1
fi

# Function to type text into active window
type_text() {
    local text="$1"
    
    # Skip empty or meaningless text
    if [ -z "$text" ] || [ "$text" = " " ] || [ "$text" = "." ]; then
        return
    fi
    
    echo "⌨️  Typing with $TYPING_TOOL_NAME: $text"
    
    # Small delay to ensure the window is ready to receive input
    sleep 0.1
    
    # Use the selected typing tool
    case "$TYPING_TOOL" in
        "ydotool")
            local ydotool_output
            ydotool_output=$(ydotool type --key-delay 0 "$text" 2>&1)
            local ydotool_exit_code=$?
            
            if [ $ydotool_exit_code -eq 0 ]; then
                return 0
            else
                echo "⚠️  ydotool failed to type text"
                
                # Check for specific error patterns and provide helpful guidance
                if echo "$ydotool_output" | grep -q "failed to open uinput device"; then
                    echo "   ❌ ydotool failed: Install Debian's version 1.0.4+ (Ubuntu's 0.1.8 has permission issues)"
                elif echo "$ydotool_output" | grep -q "ydotoold backend unavailable"; then
                    echo "   ❌ ydotool failed: Install Debian's version 1.0.4+ (Ubuntu's 0.1.8 lacks systemd service)"
                else
                    echo "   ❌ ydotool failed: Install Debian's version 1.0.4+ instead of Ubuntu's 0.1.8"
                fi
                
                return 1
            fi
            ;;
        "xdotool")
            # --clearmodifiers ensures no stuck modifier keys interfere
            # --delay 0 types as fast as possible
            printf "%s" "$text" | xdotool type --clearmodifiers --delay 0 --file -
            if [ $? -eq 0 ]; then
                return 0
            else
                echo "⚠️  Warning: xdotool failed to type text"
                return 1
            fi
            ;;
        *)
            echo "⚠️  Warning: No typing tool selected"
            return 1
            ;;
    esac
    
    # Show transcribed text if typing failed
    echo "    Transcribed text: $text"
    return 1
}

# Function to check if audio contains speech using Silero-VAD
has_speech() {
    local audio_file="$1"
    if [ ! -f "$audio_file" ] || [ ! -s "$audio_file" ]; then
        return 1
    fi
    
    # Use vad-speech-segments to detect speech
    # If it finds any segments, it has speech
    local segments=$($VAD_TOOL \
        -f "$audio_file" \
        -vm "$VAD_MODEL_PATH" \
        -vt $VAD_THRESHOLD \
        -np 2>/dev/null | grep "Speech segment" | wc -l)
    
    [ "$segments" -gt 0 ]
}

# Function to clean up
cleanup() {
    # Stop the port listener if it's running
    if [ -n "$LISTENER_PID" ]; then
        kill $LISTENER_PID 2>/dev/null
        wait $LISTENER_PID 2>/dev/null
    fi
    
    # Stop any background recording
    if [ -n "$RECORDING_PID" ]; then
        kill $RECORDING_PID 2>/dev/null
        wait $RECORDING_PID 2>/dev/null
    fi
    
    rm -f "$TEMP_AUDIO" "$TEMP_CHUNK"
    exit 0
}

# Set up signal handlers
trap cleanup EXIT INT TERM

# Main execution

# Check if another instance is already recording (port in use)
if ss -ln | grep -q ":$LISTEN_PORT "; then
    echo "🔔 Another recording in progress - stopping it to transcribe now..."
    
    # Find the PID listening on that port (that's the nc listener)
    NC_PID=$(ss -lntp 2>/dev/null | grep ":$LISTEN_PORT " | grep -oP 'pid=\K[0-9]+' | head -1)
    
    if [ -n "$NC_PID" ]; then
        # Find the parent bash script PID
        BASH_PID=$(ps -o ppid= -p $NC_PID | tr -d ' ')
        
        if [ -n "$BASH_PID" ]; then
            # Find and kill the parecord child process of that bash script
            PARECORD_PID=$(pgrep -P $BASH_PID parecord 2>/dev/null)
            
            if [ -n "$PARECORD_PID" ]; then
                kill $PARECORD_PID 2>/dev/null
                echo "✅ Stopped recording - transcription will start automatically"
            else
                echo "⚠️  Could not find parecord process"
            fi
        fi
    fi
    
    exit 0
fi

echo "🎯 Voice Typing - Ready!"
echo "🗣️  Speak now - will auto-detect when you finish ($(echo "scale=1; $SILENCE_DURATION_MS/1000" | bc)s pause)"
echo "   Press shortcut again to stop early and transcribe immediately"
echo "🎤 Recording..."

# Start a port listener in background to signal we're recording
# This allows new instances to detect us and send early-stop signal
nc -l $LISTEN_PORT >/dev/null 2>&1 &
LISTENER_PID=$!

# Small delay to ensure listener is up
sleep 0.1

# Start recording in background (using parecord for PipeWire/PulseAudio default source)
# Record at 16kHz mono - matches Whisper's preferred format and Jabra's native format
parecord --format=s16le --rate=16000 --channels=1 "$TEMP_AUDIO" 2>/dev/null &
RECORDING_PID=$!

# Wait a moment for recording to start and accumulate some audio
sleep 0.3

consecutive_silence_ms=0
total_time_ms=0
speech_detected=false

# Monitor the recording in real-time
while [ $total_time_ms -lt $MAX_RECORDING_TIME_MS ]; do
    sleep $(echo "scale=3; $CHUNK_DURATION_MS/1000" | bc)
    total_time_ms=$((total_time_ms + CHUNK_DURATION_MS))
    
    # Check if recording is still running (will be false if killed by second shortcut press)
    if ! kill -0 $RECORDING_PID 2>/dev/null; then
        echo "🛑 Recording stopped - proceeding to transcription"
        break
    fi
    
    # Extract last 1 second of audio for VAD check
    if [ -f "$TEMP_AUDIO" ] && [ -s "$TEMP_AUDIO" ]; then
        # Get the last 1 second of audio to check (using ffmpeg to seek from end)
        ffmpeg -sseof -1 -i "$TEMP_AUDIO" -t 1 -y "$TEMP_CHUNK" 2>/dev/null
        
        if has_speech "$TEMP_CHUNK"; then
            consecutive_silence_ms=0
            if [ "$speech_detected" = false ]; then
                speech_detected=true
                echo "🗣️  Speech detected!"
            fi
        else
            # Only count silence after we've detected speech
            if [ "$speech_detected" = true ]; then
                consecutive_silence_ms=$((consecutive_silence_ms + CHUNK_DURATION_MS))
                
                # Check if we've had enough silence to stop
                if [ $consecutive_silence_ms -ge $SILENCE_DURATION_MS ]; then
                    echo "🛑 Silence detected - stopping recording"
                    break
                fi
            fi
        fi
    fi
done

# Stop recording
if [ -n "$RECORDING_PID" ]; then
    kill $RECORDING_PID 2>/dev/null
    wait $RECORDING_PID 2>/dev/null
    RECORDING_PID=""
fi

# Stop the port listener now - we're done recording
# This allows a new instance to start if shortcut is pressed during transcription
if [ -n "$LISTENER_PID" ]; then
    kill $LISTENER_PID 2>/dev/null
    wait $LISTENER_PID 2>/dev/null
    LISTENER_PID=""
fi

# Transcribe if we recorded something with speech
# (speech_detected will be false if stopped early, but that's ok - we still transcribe)
if [ -s "$TEMP_AUDIO" ]; then
    echo "🔄 Transcribing..."
    
    # Use whisper-cli to transcribe the full recording
    # Performance optimizations:
    # -t $(nproc): Use all available CPU cores
    # -nf: No temperature fallback (faster, suitable for clear voice typing)
    # -nt -np: Suppress timestamps and diagnostic output
    # --prompt: Initial prompt to guide transcription with domain-specific terms
    transcribed_text=$(timeout 30s $WHISPER_CLI \
        -m "$MODEL_PATH" \
        -t $(nproc) \
        -nf \
        -f "$TEMP_AUDIO" \
        --prompt "$PROMPT_TEXT" \
        -nt -np 2>/dev/null | tail -n +2 | tr -d '\n\r')
    
    # Type the transcribed text if it's not empty
    if [ -n "$transcribed_text" ] && [ "$transcribed_text" != " " ] && [ "$transcribed_text" != "." ]; then
        type_text "$transcribed_text"
        echo "✅ Done!"
    else
        echo "🔇 No clear speech transcribed"
    fi
else
    echo "🔇 No speech detected"
fi

