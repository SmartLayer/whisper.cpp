#!/bin/bash

# Voice Typing with Silero-VAD - Single Shot (for GNOME shortcut)
# This script records and monitors in real-time using Silero-VAD
# It automatically stops when you pause speaking (1.5s silence detected)
#
# Dependencies: arecord, ffmpeg, awk, ydotool or xdotool
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

OPTIONS:
    -h, --help      Show this help message and exit

FEATURES:
    • Real-time speech detection using Silero-VAD
    • Automatic stop after silence (configurable)
    • Types directly into active window (uses ydotool or xdotool)
    • Maximum recording time: 30 seconds

DEPENDENCIES:
    • arecord (ALSA sound recorder)
    • ffmpeg (audio processing)
    • ydotool or xdotool (for typing into windows)
      - For Wayland/GNOME: ydotool 1.0.4-2 from Debian (not Ubuntu's 0.1.8)
      - For X11: xdotool
    • whisper.cpp with whisper-cli and vad-speech-segments

MODELS REQUIRED:
    • Whisper model: models/ggml-medium.en.bin
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

MODEL_PATH="models/ggml-medium.en.bin"
VAD_MODEL_PATH="models/ggml-silero-v5.1.2.bin"
WHISPER_CLI="./build/bin/whisper-cli"
VAD_TOOL="./build/bin/vad-speech-segments"
TEMP_AUDIO="/tmp/voice_typing_recording.wav"
TEMP_CHUNK="/tmp/voice_typing_chunk.wav"

# VAD settings - tuned for pause detection
VAD_THRESHOLD=0.4           # Higher = more confident speech required
SILENCE_DURATION_MS=3000    # Milliseconds of silence to end recording
CHUNK_DURATION_MS=500       # Check every 500ms
MAX_RECORDING_TIME_MS=30000 # Maximum recording time in milliseconds

# Check if typing tools are available
if ! command -v ydotool &> /dev/null && ! command -v xdotool &> /dev/null; then
    echo "❌ Error: No typing tool found"
    echo "   Install either:"
    echo "   - ydotool (for Wayland/GNOME): Download Debian's ydotool_1.0.4-2_amd64.deb"
    echo "     wget http://deb.debian.org/debian/pool/main/y/ydotool/ydotool_1.0.4-2_amd64.deb"
    echo "     sudo dpkg -i ydotool_1.0.4-2_amd64.deb"
    echo "     sudo usermod -aG input \$USER  # then logout/login"
    echo "   - xdotool (for X11): sudo apt install xdotool"
    exit 1
fi

# Check if ydotoold is running (required for ydotool)
if command -v ydotool &> /dev/null; then
    if ! systemctl --user is-active --quiet ydotool.service; then
        echo "⚠️  Warning: ydotoold service is not running"
        echo "   Starting it now..."
        systemctl --user start ydotool.service
        sleep 0.5
    fi
fi

# Check if required files exist
if [ ! -f "$WHISPER_CLI" ]; then
    echo "❌ Error: whisper-cli not found at: $WHISPER_CLI"
    echo "   Please build whisper.cpp first with: cd build && cmake .. && make"
    exit 1
fi

if [ ! -f "$VAD_TOOL" ]; then
    echo "❌ Error: vad-speech-segments not found at: $VAD_TOOL"
    echo "   Please build whisper.cpp first with: cd build && cmake .. && make"
    exit 1
fi

if [ ! -f "$MODEL_PATH" ]; then
    echo "❌ Error: Model file not found at: $MODEL_PATH"
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
    
    echo "⌨️  Typing: $text"
    
    # Small delay to ensure the window is ready to receive input
    sleep 0.1
    
    # Try ydotool first (works on Wayland with GNOME and other compositors)
    if command -v ydotool &> /dev/null; then
        ydotool type "$text"
        if [ $? -eq 0 ]; then
            return 0
        fi
    fi
    
    # Fallback to xdotool (works on X11 and XWayland apps)
    if command -v xdotool &> /dev/null; then
        # --clearmodifiers ensures no stuck modifier keys interfere
        # --delay 0 types as fast as possible
        printf "%s" "$text" | xdotool type --clearmodifiers --delay 0 --file -
        if [ $? -eq 0 ]; then
            return 0
        fi
    fi
    
    # If we get here, neither tool worked
    echo "⚠️  Warning: Could not type text. Install 'ydotool' (Wayland/GNOME) or 'xdotool' (X11)"
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
    # Stop any background recording
    if [ -n "$RECORDING_PID" ]; then
        kill $RECORDING_PID 2>/dev/null
        wait $RECORDING_PID 2>/dev/null
    fi
    rm -f "$TEMP_AUDIO" "$TEMP_CHUNK"
    exit 0
}

# Set up cleanup on exit
trap cleanup EXIT INT TERM

# Main execution
echo "🎯 Voice Typing - Ready!"
echo "🗣️  Speak now - will auto-detect when you finish ($(echo "scale=1; $SILENCE_DURATION_MS/1000" | bc)s pause)"
echo "🎤 Recording..."

# Start recording in background
arecord -f cd -t wav "$TEMP_AUDIO" 2>/dev/null &
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
    
    # Check if recording is still running
    if ! kill -0 $RECORDING_PID 2>/dev/null; then
        echo "⚠️  Recording stopped unexpectedly"
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

# Transcribe if we recorded something with speech
if [ -s "$TEMP_AUDIO" ] && [ "$speech_detected" = true ]; then
    echo "🔄 Transcribing..."
    
    # Use whisper-cli to transcribe the full recording
    # Performance optimizations:
    # -t $(nproc): Use all available CPU cores
    # -nf: No temperature fallback (faster, suitable for clear voice typing)
    # -nt -np: Suppress timestamps and diagnostic output
    transcribed_text=$(timeout 30s $WHISPER_CLI \
        -m "$MODEL_PATH" \
        -t $(nproc) \
        -nf \
        -f "$TEMP_AUDIO" \
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

