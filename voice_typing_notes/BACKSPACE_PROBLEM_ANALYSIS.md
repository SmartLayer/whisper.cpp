# Voice Typing with whisper-stream: The Sliding Window Problem

## Goal

Implement voice typing that replaces keyboard input:
- User speaks naturally into microphone
- Speech is transcribed in real-time
- Transcribed text is typed into the active window
- Works like keyboard input (appends to existing text)

## Challenge: Progressive Transcriptions with Corrections

When using whisper-stream for real-time voice typing, transcriptions are progressive and can include corrections:

**Example from real observation:**
```
User speaks: "This is a test" [pause 1s] "and I paused one second"

Transcription 0 (at 2.5s - partial/uncertain):
[00:00:00.000 --> 00:00:02.500]   This is a atilo

Transcription 1 (at 6s - corrected with more context):  
[00:00:00.000 --> 00:00:03.000]   This is a test
[00:00:03.000 --> 00:00:06.000]   and I paused one second
```

**Observations:**
1. **Transcription 0 is incorrect:** "atilo" (misheard with limited context)
2. **Transcription 1 corrects it:** "test" (correct with full context)
3. **Transcription 1 also adds new content:** "and I paused one second"

**If each transcription is typed as-is:**
- Type Transcription 0: "This is a atilo"
- Type Transcription 1: "This is a test and I paused one second"
- **Result:** "This is a atilo This is a test and I paused one second" (wrong text + duplication)

**Key insight:** Later transcriptions don't just add new text—they can **correct earlier transcriptions** as more audio context becomes available to the model.

## Proposed Solution: Backspace Method

To avoid duplication and handle corrections, one approach is to use backspace before typing updates:

1. Type Transcription 0: "This is a atilo"
2. When Transcription 1 arrives:
   - Recognize that it covers the same time range (timestamps overlap)
   - Calculate characters to delete: "This is a atilo" = 16 characters
   - Send 16 backspace key presses
   - Type the full Transcription 1: "This is a test and I paused one second"
3. **Result:** Correct text without duplication or errors

**Advantages:**
- Handles both corrections ("atilo" → "test") and additions
- Removes incorrect text before typing corrected version
- Maintains proper text flow

## Why Backspace Method Fails: The Sliding Window

### The 30-Second Buffer Constraint

**The 30-second limit originates from the OpenAI Whisper model:**
- Whisper was trained on 30-second audio segments
- This is a fundamental model architecture constraint
- whisper.cpp's `--length` parameter controls buffer size but:
  - Values > 30s may not work optimally (outside training distribution)
  - Values < 30s work fine (used for lower latency)
  - The sliding window is whisper.cpp's mechanism for handling continuous audio

**Analysis uses 30 seconds as the reference, but the problem applies to any fixed buffer length.**

### Sliding Window Behavior

From whisper.cpp source code (stream.cpp:279):
```cpp
// take up to params.length_ms audio from previous iteration
const int n_samples_take = std::min((int) pcmf32_old.size(), 
                                    std::max(0, n_samples_keep + n_samples_len - n_samples_new));
```

**This implements a sliding window:**
- Buffer keeps only the last N seconds of audio (e.g., 30s)
- Old audio beyond buffer length is **dropped**
- Timestamps are relative to the current window start

**Time-based behavior:**

```
Time 0-30s:
Buffer: [0-30s] - All audio kept
t0 = 0ms

Time 40s:
Buffer: [10-40s] - First 10s dropped
t0 = 10000ms (advanced)

Time 180s (3 minutes):
Buffer: [150-180s] - Only last 30s
t0 = 150000ms
```

### The Failure Case: Window Slides Past Typed Content

**Scenario:** Continuous speech for 45 seconds

**Time 0-3s (initial, uncertain):**
```
Audio Buffer: [0-3s]
Transcription 0: "Hello world is atilo"  (incorrect - limited context)
Typed to screen: "Hello world is atilo"
```

**Time 0-10s (corrected with more context):**
```
Audio Buffer: [0-10s]  
Transcription 1: "Hello world this is a test"  (corrected: "atilo" → "test")
Screen content: "Hello world is atilo"

Backspace method:
1. Recognize overlap via timestamps (both start at t0=0)
2. Delete previous transcription: 20 backspace key presses
3. Type corrected transcription: "Hello world this is a test"
Result: ✓ Success - incorrect text removed, correct text typed
```

**Time 0-40s (window has slid - first 10s dropped):**
```
Audio Buffer: [10-40s] (audio from 0-10s no longer in buffer)
t0 = 10000ms (advanced)
Transcription 2: "this is a test and now more speech continues here"
                  ^ Buffer starts at "this" - "Hello world" is gone
Screen content: "Hello world this is a test"

Backspace method fails:
1. Screen contains: "Hello world this is a test" (30 characters)
2. New transcription: "this is a test and now more speech continues here"
3. Cannot determine overlap:
   - "Hello world" exists on screen but not in new transcription
   - New transcription starts mid-sentence from buffer's perspective
   - No way to determine: delete everything? delete partial? delete nothing?
   - Unknown how many characters to backspace
Result: ✗ Failure - Cannot determine correct action
```

### Root Cause Analysis

The sliding window creates a **temporal mismatch**:
- **Screen content**: Includes text from audio no longer in buffer
- **Buffer content**: Only contains recent audio (last N seconds)
- **No bijective mapping**: Cannot reliably map screen text to buffer content

**Key insight:** Once the window slides past previously typed content, the reference point is lost. There is no way to determine how many characters to backspace without the original audio context.

## Solutions from Online Research

### 1. Don't Use Streaming Mode (Most Common Solution)

**Approach:** Use whisper-cli in single-shot mode
- Record audio until user signals "done" (via silence detection, button press, etc.)
- Transcribe once
- Type the result
- No overlapping/incremental updates to handle

**Pros:**
- Simple, no overlap issues
- Accurate (full context)

**Cons:**
- Requires user to signal when done
- No real-time feedback

### 2. Accept Duplication (Some Tools Do This)

**Approach:** Type each new transcription fully, ignore duplicates
```
Transcription 0: "Hello world"
→ Type: "Hello world"

Transcription 1: "Hello world this is a test"  
→ Type: "Hello world this is a test"

Result on screen: "Hello world Hello world this is a test"
```

**Pros:**
- Simple implementation

**Cons:**
- Duplicate text (unacceptable for your use case)

### 3. Clear Screen / Replace Mode (Found in Some UI Apps)

**Approach:** Don't append text, replace it
- Use a text buffer/widget
- Each new transcription REPLACES the entire content
- Works for UI apps (text boxes, editors)

**Pros:**
- No duplication
- Handles corrections

**Cons:**
- **Doesn't work for voice typing to arbitrary windows!**
- Can't "clear" text in random apps

### 4. Timestamp-Based State Tracking (Proposed Solution)

**Approach:** Track what we've typed and match it with buffer state

```bash
# State
TYPED_TEXT=""           # What we've actually sent to screen
LAST_T0=0              # Last seen t0 timestamp
LAST_BUFFER_TEXT=""    # Last transcription from buffer

on_new_transcription(t0, t1, new_text):
    # Case 1: Window hasn't slid (t0 same or very close)
    if abs(t0 - LAST_T0) < 5000:
        # Same window - can do incremental
        if new_text starts with LAST_BUFFER_TEXT:
            # Append new portion only
            new_portion = new_text[len(LAST_BUFFER_TEXT):]
            type(new_portion)
            TYPED_TEXT += new_portion
            LAST_BUFFER_TEXT = new_text
        else:
            # Can't determine overlap - type full and accept possible duplicate
            type(new_text)
            TYPED_TEXT += new_text
            LAST_BUFFER_TEXT = new_text
    
    # Case 2: Window slid (t0 jumped significantly)
    else:
        # New window - buffer has moved
        # Just type the new text (it's independent)
        type(new_text)
        TYPED_TEXT = new_text  # Reset - old text no longer in buffer
        LAST_T0 = t0
        LAST_BUFFER_TEXT = new_text
```

**Pros:**
- Handles both short bursts and long recordings
- Uses timestamps to detect window sliding

**Cons:**
- Complex logic
- Still may have edge cases

### 5. Split by Silence (Alternative Approach)

**Approach:** Only transcribe discrete utterances separated by long silence
- Detect silence >2 seconds
- Process that segment independently
- No overlapping windows

**Pros:**
- Each transcription is independent
- No overlap issues

**Cons:**
- Loses real-time aspect
- User must pause >2s between utterances

## Why Common Approaches Fail

### Push-to-Talk
**Approach:** Hold key → record → release key → transcribe → type
**Limitation:** If speech duration exceeds buffer length (30s), the sliding window problem persists:
- whisper-stream continues with fixed-length buffer
- Progressive transcriptions still generated
- Overlap and duplication still occur
- Only works if speech artificially limited to < buffer length

### Mute Button Control
**Approach:** Unmute microphone → speak → mute → transcribe → type
**Limitation:** Same as push-to-talk - if speech exceeds buffer length, sliding window problem remains

### Timestamp-Based Tracking
**Approach:** Parse t0/t1 timestamps to detect window sliding
**Limitation:** Once window slides, cannot determine overlap point (as demonstrated above)

**Critical insight:** These approaches only function correctly when speech duration remains within buffer length, which defeats the goal of unrestricted voice input.

## Actual Solutions

### Solution 1: Record to File, Then Transcribe (Single-Shot)
```bash
# Record until user signals done (silence detection, button, etc.)
arecord -d 120 -f cd /tmp/speech.wav

# Transcribe once with whisper-cli (no streaming)
whisper-cli -m model.bin -f /tmp/speech.wav

# Type the result
type_result
```

**Pros:**
- No overlap issues
- Works for any length speech
- Full context for better accuracy

**Cons:**
- Not "real-time" - user must wait for transcription after speaking
- Requires silence detection or user signal to know when done

### Solution 2: Chunk and Process Independently
```bash
# Every time VAD detects silence:
1. Stop current recording
2. Transcribe that segment with whisper-cli (single-shot)
3. Type the result
4. Clear buffer
5. Start new recording for next segment
```

**Pros:**
- Each segment independent - no overlap
- Works for any total length

**Cons:**
- Pauses between segments (not truly continuous)
- Loses context between segments (may affect accuracy)

### Solution 3: Accept Duplication for Long Speech
```bash
# For speech < 30s: Use incremental typing (works fine)
# For speech > 30s: Type each transcription fully, ignore duplicates
```

**Pros:**
- Simpler implementation

**Cons:**
- **Duplicate text on screen** (unacceptable for keyboard replacement)

### Solution 4: Use Different Tool
```bash
# Don't use whisper-stream at all
# Use real-time STT designed for continuous transcription:
# - Google Cloud Speech-to-Text
# - Azure Speech
# - Vosk (offline)
# - etc.
```

**Pros:**
- Designed for continuous real-time transcription
- Handle streaming properly

**Cons:**
- External dependencies / API costs
- May not be offline
- Different quality than Whisper

## Conclusion

**The backspace method is fundamentally incompatible with fixed-length sliding windows.**

### Viable Approaches for Voice Typing (Keyboard Replacement)

Only two categories of solutions address the sliding window problem:

#### 1. Single-Shot Transcription (Solutions 1 & 2)
- Record discrete segments (separated by silence or user signal)
- Transcribe each segment independently using whisper-cli
- Type the result
- Clear buffer before next segment
- Each segment is independent → no overlap

**Recommended implementation:**
- Continuous listening with VAD
- Detect significant silence (≥2 seconds)
- Stop recording and transcribe completed segment
- Type result to active window
- Resume listening for next segment

#### 2. Different STT Engine (Solution 4)
- Use a speech-to-text system designed for continuous streaming
- Examples: Google Cloud Speech-to-Text, Azure Speech, Vosk
- These systems handle streaming with proper incremental updates
- May require external dependencies or API access

### Implementation Recommendation

For voice typing that replaces keyboard input, **chunk-based transcription with whisper-cli** (Solution 2) provides the best balance:
- Works with arbitrary application windows
- No overlap/duplication issues
- Offline operation (no external APIs)
- Leverages Whisper's transcription quality
- Natural segmentation at silence boundaries

This approach is similar to the original `voice_typing_shortcut.sh` concept, but with proper VAD-based silence detection instead of fixed recording timeouts.

