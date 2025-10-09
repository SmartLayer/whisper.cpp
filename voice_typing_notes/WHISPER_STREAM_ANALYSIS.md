# Whisper-Stream Behavior Analysis

## Observed Behavior from Tests

### Test Case: "This is a test... [pause 1s] ...and I paused exactly 1 second"

**Transcription 0:**
```
t0 = 0 ms | t1 = 3104 ms
[00:00:00.000 --> 00:00:03.960]   Hi, this is a test.
```

**Transcription 1:**
```
t0 = 0 ms | t1 = 9423 ms
[00:00:00.000 --> 00:00:06.060]   Hi, this is a test.
[00:00:06.060 --> 00:00:08.560]   And I paused one second exactly.
```

### Key Observations

1. **Both transcriptions start at t0 = 0ms** - They're not separate chunks
2. **Transcription 1 contains ALL of Transcription 0** - It's not just new text
3. **Timestamps show complete overlap** - First line of Trans 1 covers the same time as all of Trans 0
4. **Progressive/Incremental model** - Each new transcription is a RE-TRANSCRIPTION of the entire audio buffer so far

## How whisper-stream Works (CORRECTED after code review)

### Sliding Window with VAD and Fixed Buffer Size

```
--length 30000 (30 seconds)
--step 0 (VAD mode)

Time 0-30s:
Audio Buffer: [============================] (fills up to 30s)
               ▲                           ▲
               t0=0                        current

Time 60s:
Audio Buffer: [============================] (last 30s only)
               ▲                           ▲
               t0=30000ms                  t1=60000ms
               (old audio dropped)

Time 180s (3 minutes):
Audio Buffer: [============================] (last 30s only)
               ▲                           ▲
               t0=150000ms                 t1=180000ms
```

### Key Behavior from stream.cpp (line 279):

```cpp
// take up to params.length_ms audio from previous iteration
const int n_samples_take = std::min((int) pcmf32_old.size(), 
                                    std::max(0, n_samples_keep + n_samples_len - n_samples_new));
```

**This means:**
- `--length 30000` = keeps ONLY the last 30 seconds
- Old audio beyond 30 seconds is DROPPED
- Timestamps are RELATIVE to the sliding window start
- For long recordings, t0 does NOT stay at 0ms!

### Implications

**Short recordings (< 30s):**
- t0 = 0ms
- Each new transcription includes all previous audio
- Transcription 1 will contain Transcription 0

**Long recordings (> 30s):**
- t0 advances as window slides
- Old transcriptions are NOT included in new ones (audio dropped)
- Each transcription is only for the last 30 seconds of audio

## Problem for Voice Typing

If we type each transcription as-is:
```
User types: "This is a test and I paused one second"
What gets typed:
1. "This is a test"
2. "This is a test and I paused one second"
Result: "This is a test This is a test and I paused one second"
```

## Proposed Solutions

### Option 1: Type Only Last Transcription (Current Broken Approach)
❌ Doesn't work - we lose intermediate transcriptions if user wants multiple separate utterances

### Option 2: Detect and Type Only New/Changed Portions
Strategy:
1. Type Transcription 0: "This is a test"
2. When Transcription 1 arrives, compare to Transcription 0
3. Find the difference (new text)
4. Type only: " and I paused one second"

**Implementation:**
```bash
if new_transcription starts with old_transcription:
    new_part = new_transcription[len(old_transcription):]
    type(new_part)
```

**Pros:** Handles progressive transcriptions correctly
**Cons:** Assumes new always includes old (seems to be true)

### Option 3: Backspace and Retype (User's Suggestion)
Strategy:
1. Type Transcription 0: "This is a test"
2. When Transcription 1 arrives, detect overlap
3. Send backspace keys to delete "This is a test"
4. Type full Transcription 1: "This is a test and I paused one second"

**Pros:** Handles corrections if whisper changes its mind about earlier text
**Cons:** More complex, requires counting characters accurately, visible deletion

### Option 4: Different Tool - Use whisper-cli with VAD Detection
Strategy:
- Don't use whisper-stream at all
- Use arecord/parecord with silence detection
- When silence detected, stop recording
- Run whisper-cli once on the recorded file
- Type the result

**Pros:** Single-shot, no progressive updates
**Cons:** Loses real-time aspect, back to the original problem

## Questions to Verify

1. **Does whisper-stream ALWAYS include previous text in new transcriptions?**
   - From tests: YES, timestamps always start at t0=0
   
2. **Can whisper change earlier text in later transcriptions?**
   - Example: Trans 0: "Hi this is a..." → Trans 1: "Hi this is a test"
   - Need to verify if "Hi this is a..." could become "Hi this was a test" (correction)
   
3. **What happens with very long audio (>30 seconds buffer)?**
   - Does it start dropping old audio?
   - Do timestamps reset?

4. **What happens if user has multiple separate utterances?**
   - Speak → silence → Transcription 0
   - Long pause
   - Speak again → silence → Transcription 1
   - Does Trans 1 include Trans 0 or is it independent?

## Recommended Approach (UPDATED)

**Hybrid Strategy: Timestamp-aware + Incremental Typing**

```bash
LAST_TEXT=""
LAST_T0=0

on_new_transcription(t0, t1, new_text):
    # Check if this is within the same sliding window (t0 hasn't advanced much)
    if t0 == LAST_T0 or t0 < LAST_T0 + 5000:  # Within same/overlapping window
        # Progressive transcription - check for overlap
        if new_text starts with LAST_TEXT:
            # Extract only new portion
            new_portion = remove_prefix(new_text, LAST_TEXT)
            type(new_portion)
        else:
            # Correction or different text - type full
            type(new_text)
        LAST_TEXT = new_text
        LAST_T0 = t0
    else:
        # Window has advanced significantly - independent transcription
        type(new_text)
        LAST_TEXT = new_text
        LAST_T0 = t0
```

**This handles:**
1. **Short bursts (< 30s)**: Uses incremental typing to avoid duplicates
2. **Long continuous (> 30s)**: Detects window sliding via t0 change, types each transcription independently
3. **Corrections**: If text changes unexpectedly, types the corrected version

**Requires:**
- Parsing t0 timestamp from each transcription
- String prefix comparison
- Careful handling of edge cases

## Alternative: Push-to-Talk Mode

Given the complexity, maybe push-to-talk is simpler:
- User holds key → record
- User releases key → stop recording, transcribe once, type result
- No progressive transcriptions to deal with

Would require:
- Key event detection
- Start/stop whisper-stream on demand
- OR use whisper-cli with temporary file

