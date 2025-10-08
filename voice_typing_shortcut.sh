#!/bin/bash

# Voice Typing with VAD - Single Shot (for GNOME shortcut)
# This script records with VAD and types when you finish speaking

MODEL_PATH="models/ggml-medium.en.bin"
VAD_MODEL_PATH="models/ggml-silero-v5.1.2.bin"
WHISPER_CLI="./build/bin/whisper-cli"
TEMP_AUDIO="/tmp/voice_typing_shortcut.wav"

# VAD settings - more sensitive for better detection
VAD_THRESHOLD=0.3
MIN_SPEECH_DURATION=0.2    # 200ms (0.0-1.0 range)
MIN_SILENCE_DURATION=0.5   # 500ms (0.0-1.0 range)
MAX_SPEECH_DURATION=10     # 10 seconds

# Check if required files exist
if [ ! -f "$WHISPER_CLI" ]; then
    echo "❌ Error: whisper-cli not found at: $WHISPER_CLI"
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
    if [ -n "$text" ] && [ "$text" != " " ] && [ "$text" != "." ]; then
        echo "⌨️  Typing: $text"
        # Add a space before typing to separate from previous text
        printf " %s" "$text" | wtype - 2>/dev/null || printf " %s" "$text" | xdotool type --clearmodifiers --delay 0 --file - 2>/dev/null
    fi
}

# Function to clean up
cleanup() {
    rm -f "$TEMP_AUDIO"
    exit 0
}

# Set up cleanup on exit
trap cleanup EXIT INT TERM

# Main execution
echo "🎯 Voice Typing - Ready!"
echo "🗣️  Speak now - it will detect when you finish and type automatically"

# Record audio with VAD - this will automatically stop when you finish speaking
echo "🎤 Recording with VAD..."
arecord -f cd -t wav -d 15 "$TEMP_AUDIO" 2>/dev/null

if [ -s "$TEMP_AUDIO" ]; then
    echo "🔄 Processing speech with VAD..."
    
    # Use whisper with VAD to process the audio
    # This will automatically detect speech segments
    transcribed_text=$(timeout 20s $WHISPER_CLI \
        -m "$MODEL_PATH" \
        -vm "$VAD_MODEL_PATH" \
        --vad \
        -vt $VAD_THRESHOLD \
        -vspd $MIN_SPEECH_DURATION \
        -vsd $MIN_SILENCE_DURATION \
        -vmsd $MAX_SPEECH_DURATION \
        -t 8 \
        -f "$TEMP_AUDIO" \
        -nt -np 2>/dev/null | tail -n +2 | tr -d '\n\r')
    
    # Type the transcribed text if it's not empty
    if [ -n "$transcribed_text" ] && [ "$transcribed_text" != " " ] && [ "$transcribed_text" != "." ]; then
        type_text "$transcribed_text"
        echo "✅ Done!"
    else
        echo "🔇 No clear speech detected"
    fi
else
    echo "🔇 No audio detected"
fi

