#!/bin/bash

# Continuous Voice Typing using whisper-stream
# Design: Runs forever, types every transcription immediately
# User controls: Use microphone mute button to control when to speak

MODEL_PATH="models/ggml-medium.en.bin"
WHISPER_STREAM="./build/bin/whisper-stream"
TEMP_OUTPUT="/tmp/voice_typing_continuous.txt"
DEBUG_LOG="/tmp/voice_typing_continuous_debug.log"

# whisper-stream VAD settings
VAD_THRESHOLD=0.9          # Voice Activity Detection threshold (higher = tolerates longer pauses)
AUDIO_LENGTH=30000         # Audio buffer length in ms
STEP_SIZE=0                # Sliding window mode with VAD

# Check if required files exist
if [ ! -f "$WHISPER_STREAM" ]; then
    echo "❌ Error: whisper-stream not found at: $WHISPER_STREAM"
    echo "   Please build with SDL2 support: cmake -B build -DWHISPER_SDL2=ON && cmake --build build"
    exit 1
fi

if [ ! -f "$MODEL_PATH" ]; then
    echo "❌ Error: Model file not found at: $MODEL_PATH"
    exit 1
fi

# Function to type text into active window
type_text() {
    local text="$1"
    if [ -n "$text" ] && [ "$text" != " " ] && [ "$text" != "." ]; then
        # Type with leading space
        printf " %s" "$text" | wtype - 2>/dev/null || printf " %s" "$text" | xdotool type --clearmodifiers --delay 0 --file - 2>/dev/null
        echo "[$(date +%H:%M:%S)] Typed: $text" >> "$DEBUG_LOG"
    fi
}

# Cleanup function
cleanup() {
    echo "Shutting down..." >> "$DEBUG_LOG"
    rm -f "$TEMP_OUTPUT"
    if [ -n "$STREAM_PID" ]; then
        kill $STREAM_PID 2>/dev/null
        sleep 0.2
        kill -9 $STREAM_PID 2>/dev/null
    fi
    pkill -9 whisper-stream 2>/dev/null
    exit 0
}

trap cleanup EXIT INT TERM

# Start debug log
rm -f "$DEBUG_LOG"
echo "=== Continuous Voice Typing Started at $(date) ===" > "$DEBUG_LOG"
echo "Use microphone mute button to control speech" >> "$DEBUG_LOG"
echo "Press Ctrl+C to stop" >> "$DEBUG_LOG"

# Start whisper-stream (runs forever until killed)
rm -f "$TEMP_OUTPUT"
$WHISPER_STREAM \
    -m "$MODEL_PATH" \
    -t 8 \
    --step $STEP_SIZE \
    --length $AUDIO_LENGTH \
    -vth $VAD_THRESHOLD \
    -f "$TEMP_OUTPUT" \
    2>/dev/null &

STREAM_PID=$!
echo "whisper-stream started (PID: $STREAM_PID)" >> "$DEBUG_LOG"

# Give whisper-stream time to start
sleep 2

# Monitor for new transcriptions and type them immediately
LAST_TRANSCRIPTION_NUM=-1
echo "Monitoring for transcriptions..." >> "$DEBUG_LOG"

while kill -0 $STREAM_PID 2>/dev/null; do
    if [ -f "$TEMP_OUTPUT" ]; then
        # Count how many transcription blocks exist in the file
        CURRENT_TRANSCRIPTION_NUM=$(grep -c "### Transcription .* END" "$TEMP_OUTPUT" 2>/dev/null)
        
        # If there's a new completed transcription
        if [ "$CURRENT_TRANSCRIPTION_NUM" -gt "$LAST_TRANSCRIPTION_NUM" ]; then
            # Get the LAST completed transcription block
            # Extract all timestamp lines after the last START before the last END
            LAST_TRANSCRIPTION=$(awk '/### Transcription .* START/{buf=""} /^\[/{line=$0; gsub(/\[.*\]  */,"",line); buf=buf line " "} /### Transcription .* END/{last=buf} END{print last}' "$TEMP_OUTPUT" 2>/dev/null | xargs)
            
            if [ -n "$LAST_TRANSCRIPTION" ]; then
                echo "[$(date +%H:%M:%S)] Transcription #$CURRENT_TRANSCRIPTION_NUM: [$LAST_TRANSCRIPTION]" >> "$DEBUG_LOG"
                type_text "$LAST_TRANSCRIPTION"
                LAST_TRANSCRIPTION_NUM=$CURRENT_TRANSCRIPTION_NUM
            fi
        fi
    fi
    
    sleep 0.3
done

echo "whisper-stream stopped unexpectedly" >> "$DEBUG_LOG"

