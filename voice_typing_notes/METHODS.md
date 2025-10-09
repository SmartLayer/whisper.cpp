# Voice Typing with Whisper‑stream: Solving the Sliding Window Problem (Ubuntu 25.04)

## Background: how the sliding window breaks whisper‑stream

OpenAI’s Whisper models are trained on **30‑second audio segments**. To perform “streaming” transcription, *whisper‑stream* repeatedly feeds a fixed‑length buffer of audio into the Whisper model, which slides forward as new audio arrives. This buffer is usually **30 seconds** (configurable but exceeding the training distribution causes instability) and shorter chunks are padded with zeros. Picovoice researchers note that this design makes Whisper unsuitable for real‑time streaming because it “processes audio in 30‑second segments” and pads shorter chunks, which causes words to be broken across segment boundaries and can lead to hallucinations[picovoice.ai](https://picovoice.ai/blog/whisper-speech-to-text-for-real-time-transcription/#:~:text=Whisper%20does%20not%20have%20streaming,transcription%20as%20they%20are%20received). When the buffer slides past previously spoken audio, the timestamps of subsequent transcriptions are relative to the new window; the earlier context is gone and there is no reliable way to map updated transcripts to previously typed text. Any approach based on *backspacing* or *diffing* the transcripts (deleting incorrect text and re‑typing the corrected version) fails once the window has slid, because the new transcript no longer contains the original words for comparison. This is why the backspace method works only for utterances shorter than the buffer and fails for continuous speech longer than ~30 seconds.

## Limitations of the backspace approach

When using whisper‑stream for continuous voice typing, each progressive transcription can overwrite or correct previous text. A simple backspace method (delete the old transcription and type the new one) works as long as all audio remains in the buffer. Once the **sliding window** drops earlier audio, the new transcript may begin mid‑sentence. The typing program cannot know how many characters to delete because the corrected transcript no longer contains the original beginning, so it cannot determine the overlap. As a result, duplication or deletion of correct text occurs. The root cause is Whisper’s fixed‑length buffer and sequence‑to‑sequence architecture[picovoice.ai](https://picovoice.ai/blog/whisper-speech-to-text-for-real-time-transcription/#:~:text=Whisper%20does%20not%20have%20streaming,transcription%20as%20they%20are%20received). To solve the problem, one must either avoid streaming with Whisper or use a speech‑to‑text system designed for continuous transcription.

## Solution approaches

### 1. Record discrete segments and transcribe with Whisper CLI

One way to avoid the sliding‑window problem is to **bypass streaming** and transcribe **complete utterances** instead of partial updates. You can still use Whisper’s high‑quality models offline by recording speech until the user pauses (for example, a silence longer than 1–2 seconds) or presses a key, then transcribing the recorded segment using `whisper-cli` (whisper.cpp) or OpenAI’s Whisper API. Since you transcribe the entire segment at once, there is no overlap between segments and no sliding window. The steps are:

1. **Continuous listening with voice activity detection (VAD).** Use a lightweight library (e.g., [silero VAD](https://github.com/snakers4/silero-vad) or WebRTC VAD) to monitor the microphone. When the energy level drops below a threshold for a specified duration (e.g., 1.5 s), treat this as the end of an utterance.

2. **Record audio to a file** until silence is detected or the user presses a hotkey (push‑to‑talk). Save the segment as `segment.wav` or similar.

3. **Transcribe using Whisper CLI.** Invoke `whisper` (or `whisper.cpp`) on the recorded file. Because the entire utterance is transcribed in one shot, Whisper uses full context rather than a sliding window, so there is no duplication or misalignment. Example command:
   
   `whisper.cpp/main -m models/ggml-base.en.bin -f segment.wav -otxt -o output`
   
   The `-otxt` option outputs plain text; you can parse the output file.

4. **Type the result into the active window.** Use a tool like `xdotool` or `pyautogui` to send the transcribed text as keyboard input to the currently focused window. This preserves punctuation and spacing.

5. **Repeat** for the next utterance.

This method provides high transcription quality and works offline. However, it requires a brief pause or user interaction between utterances, so it is not strictly “real‑time.” For applications like dictation or code comments where natural pauses occur, it offers a good compromise between latency and accuracy.

### 2. Use speech‑to‑text engines designed for real‑time streaming

If you need continuous voice typing without segmentation, consider engines specifically built for streaming. These tools maintain an internal state across the entire audio stream and provide incremental transcriptions that remain consistent; they do not rely on a fixed sliding window, so they can update past words without losing alignment. Below are several options with evidence from credible sources:

#### 2.1 Cheetah by Picovoice (on‑device)

Picovoice’s **Cheetah** is a lightweight streaming speech‑to‑text engine that runs **entirely on‑device**, avoiding cloud latency. The company emphasises that large models like Whisper or non‑streaming architectures cause significant delay; Cheetah instead processes audio locally and “delivers real‑time transcripts with guaranteed response time”[picovoice.ai](https://picovoice.ai/platform/cheetah/#:~:text=Why%20Cheetah%20Streaming%20Speech,time%20applications). It is designed for low‑latency applications and does not require network connectivity. Cheetah supports deployment across web browsers, mobile, on‑premises servers, cloud environments, and more[picovoice.ai](https://picovoice.ai/platform/cheetah/#:~:text=Deploy%20Cheetah%20Streaming%20Speech,any%20platform). The same article notes that Cheetah offers **custom vocabulary and keyword boosting**, supports English, French, German, Italian, Portuguese and Spanish[picovoice.ai](https://picovoice.ai/platform/cheetah/#:~:text=Design%20with%20privacy%20in%20mind), and runs with minimal resources. For voice typing, you can integrate Cheetah’s SDK (available for C, Python, JavaScript, C#, etc.) and feed microphone samples to the engine; it will emit transcribed words in real time. Because it maintains an internal state across the entire stream, it avoids the sliding‑window misalignment. However, Cheetah is proprietary; pricing and licensing apply, though on‑device processing may satisfy privacy requirements.

#### 2.2 Vosk (open source, offline)

**Vosk** is an open‑source speech recognition toolkit that supports over **20 languages** and works offline on devices ranging from Raspberry Pi to PCs[alphacephei.com](https://alphacephei.com/vosk/#:~:text=Vosk%20is%20a%20speech%20recognition,best%20things%20in%20Vosk%20are). It provides a **streaming API** that yields incremental transcriptions for real‑time applications[alphacephei.com](https://alphacephei.com/vosk/#:~:text=Vosk%20is%20a%20speech%20recognition,best%20things%20in%20Vosk%20are). The models are relatively small (around 50 MB for many languages), making it suitable for resource‑constrained hardware[alphacephei.com](https://alphacephei.com/vosk/#:~:text=Vosk%20is%20a%20speech%20recognition,best%20things%20in%20Vosk%20are). The VideoSDK guide notes that Vosk ensures **privacy and low latency** because it processes audio locally and offline[videosdk.live](https://www.videosdk.live/developer-hub/stt/vosk-speech-recognition#:~:text=What%20Makes%20Vosk%20Speech%20Recognition,Unique). It supports cross‑platform deployment on iOS, Raspberry Pi, Windows, Linux and macOS, and offers bindings for Python, Java, C#, Node.js, etc.[videosdk.live](https://www.videosdk.live/developer-hub/stt/vosk-speech-recognition#:~:text=Other%20Platforms%20,Windows%2C%20Linux%2C%20Mac). To implement voice typing, feed microphone data into the Vosk recogniser’s `AcceptWaveform` method and capture partial results through `result` or `partialResult`; then send the recognized text to the active window. Vosk is a strong candidate for an offline, open‑source real‑time solution.

#### 2.3 Coqui STT (open source)

**Coqui STT** is a neural speech‑to‑text engine derived from Mozilla’s DeepSpeech. The VideoSDK open‑source STT guide highlights that Coqui STT is **easy to use**, provides high accuracy through deep learning, supports multiple languages, and offers an efficient real‑time streaming API with a flexible Python interface[videosdk.live](https://www.videosdk.live/developer-hub/stt/open-source-speech-to-text#:~:text=Coqui%20STT). Coqui STT models are larger than Vosk but still run locally. Integration on Linux requires the `coqui-stt` library and the streaming API; however, community support is smaller than for Vosk.

#### 2.4 Cloud STT services (Google Cloud, Deepgram, Microsoft Azure)

If you do not mind using cloud services and have internet access, major providers offer high‑quality streaming speech‑to‑text with robust features:

- **Google Cloud Speech‑to‑Text**: VideoSDK’s overview explains that Google’s service uses **advanced AI models like Chirp** for high accuracy and supports **streaming mode** for real‑time interactive applications[videosdk.live](https://www.videosdk.live/developer-hub/stt/google-cloud-speech-to-text#:~:text=Google%20Cloud%20Speech%20to%20Text,scalability%20developers%20need%20in%202025). It supports more than **125 languages**, offers custom vocabulary and phrase hints, and provides enterprise‑grade security and compliance including GDPR and HIPAA[videosdk.live](https://www.videosdk.live/developer-hub/stt/google-cloud-speech-to-text#:~:text=Google%20Cloud%20Speech%20to%20Text,scalability%20developers%20need%20in%202025)[videosdk.live](https://www.videosdk.live/developer-hub/stt/google-cloud-speech-to-text#:~:text=Security%2C%20Compliance%2C%20and%20Data%20Residency). Developers can use the streaming API via gRPC; the service returns interim results with low latency.

- **Deepgram**: Deepgram’s `Flux` model is designed for real‑time conversational AI. It offers **sub‑300 ms end‑of‑turn latency** and can handle natural interruptions[deepgram.com](https://deepgram.com/product/speech-to-text#:~:text=Speech%20to%20Text%20API%20for,level%20apps). Deepgram supports **36 languages**, including domain‑specific models; transcripts are delivered in under **300 ms**, enabling near‑instantaneous typing[deepgram.com](https://deepgram.com/product/speech-to-text#:~:text=Deepgram%20models%20maintain%20high%20transcription,them%20ideal%20for%20real%20conversations). Additional features include filler‑word detection, smart formatting, and diarization[deepgram.com](https://deepgram.com/product/speech-to-text#:~:text=Speech%20to%20Text%20API%20for,level%20apps). Deepgram provides streaming via WebSocket or gRPC. Pricing is usage‑based.

- **Microsoft Azure Speech to Text**: Microsoft’s service offers **real‑time transcription with intermediate results**, **fast transcription** for lower latency and *batch transcription* for large jobs[learn.microsoft.com](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/speech-to-text#:~:text=Azure%20AI%20Speech%20service%20offers,converting%20audio%20streams%20into%20text). The Speech SDK returns partial and final results for continuous transcription and supports custom speech models and pronunciation assessment[learn.microsoft.com](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/speech-to-text#:~:text=Real,REST%20API%20for%20short%20audio). It integrates easily with other Azure cognitive services.

These cloud services provide high accuracy and robust streaming capabilities, but they require internet connectivity and may involve usage costs.

### 3. Accept duplication or short‑utterance push‑to‑talk (not recommended)

Some users accept duplicative transcriptions (append every new transcript even if it duplicates previous text) or use push‑to‑talk only for short utterances (< 30 s). However, duplication degrades the user experience, and enforcing short segments defeats the purpose of continuous voice typing. These workarounds are generally unsuitable for productivity.

## Implementation considerations on Ubuntu 25.04

To build a voice typing tool on Ubuntu 25.04, you need to integrate microphone capture, speech recognition, and simulated keyboard input. Consider the following components:

- **Microphone capture and VAD:** Use `pyaudio`/`sounddevice` for microphone access and a VAD library to detect silence. Alternatively, some STT engines (Deepgram, Google, Azure) provide built‑in VAD.

- **Speech recognition engine:** Choose one of the approaches above (Whisper CLI with segmentation, Cheetah, Vosk, Coqui STT, or a cloud API). Ensure the engine’s Python bindings or CLI are compatible with Ubuntu 25.04.

- **Simulating keyboard input:** Use `xdotool` (X11) or `ydotool`/`libinput` (Wayland) to send keystrokes to the active window. On Wayland, `wtype` or GNOME’s built‑in Remote Desktop API may be needed. For cross‑platform Python, `pyautogui` can send keystrokes but may require xdotool for reliability on Linux.

- **Hotkeys and user feedback:** Provide keyboard shortcuts to start/stop listening, clear buffer, or pause. Optionally display transcribed text in a floating widget before sending it to the active application.

## Conclusion and recommendations

Whisper‑stream’s sliding‑window architecture limits its use for continuous voice typing: once the buffer drops previously spoken words, there is no way to align new transcriptions with already typed text. The backspace method fails because the updated transcript no longer contains the text to be deleted. The fundamental issue is the model’s fixed 30‑second segment design and zero‑padding during streaming[picovoice.ai](https://picovoice.ai/blog/whisper-speech-to-text-for-real-time-transcription/#:~:text=Whisper%20does%20not%20have%20streaming,transcription%20as%20they%20are%20received).

For voice typing on Ubuntu 25.04, two viable strategies emerge:

1. **Segmented single‑shot transcription with Whisper CLI:** Use VAD or push‑to‑talk to record discrete utterances. Transcribe each segment using Whisper (offline or via OpenAI API) and type the result. This avoids duplication and maintains high quality but introduces short delays between segments.

2. **Adopt a streaming STT engine:** Use on‑device or cloud engines designed for continuous speech. Cheetah delivers real‑time on‑device transcription with guaranteed response time and cross‑platform support[picovoice.ai](https://picovoice.ai/platform/cheetah/#:~:text=Why%20Cheetah%20Streaming%20Speech,time%20applications)[picovoice.ai](https://picovoice.ai/platform/cheetah/#:~:text=Deploy%20Cheetah%20Streaming%20Speech,any%20platform). Vosk offers offline, open‑source streaming with small models and cross‑platform APIs[alphacephei.com](https://alphacephei.com/vosk/#:~:text=Vosk%20is%20a%20speech%20recognition,best%20things%20in%20Vosk%20are)[videosdk.live](https://www.videosdk.live/developer-hub/stt/vosk-speech-recognition#:~:text=Other%20Platforms%20,Windows%2C%20Linux%2C%20Mac). Coqui STT provides accurate real‑time transcription with a flexible API[videosdk.live](https://www.videosdk.live/developer-hub/stt/open-source-speech-to-text#:~:text=Coqui%20STT). Cloud services like Google Cloud STT, Deepgram and Microsoft Azure supply high‑accuracy streaming with features such as custom vocabularies, low latency and multilingual support[videosdk.live](https://www.videosdk.live/developer-hub/stt/google-cloud-speech-to-text#:~:text=Google%20Cloud%20Speech%20to%20Text,scalability%20developers%20need%20in%202025)[deepgram.com](https://deepgram.com/product/speech-to-text#:~:text=Deepgram%20models%20maintain%20high%20transcription,them%20ideal%20for%20real%20conversations)[learn.microsoft.com](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/speech-to-text#:~:text=Azure%20AI%20Speech%20service%20offers,converting%20audio%20streams%20into%20text).

Selecting the right solution depends on your priorities: privacy and offline capability favour Cheetah or Vosk; maximum accuracy and advanced features may justify cloud services; and open‑source advocates may prefer Vosk or Coqui STT. Implementing voice typing using one of these streaming engines will circumvent the sliding‑window issues of whisper‑stream and provide a practical, modern voice typing tool on Ubuntu 25.04.
