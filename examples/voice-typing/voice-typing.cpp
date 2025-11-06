// Voice typing with automatic speech detection and text injection
// This example captures audio from microphone, detects speech pauses,
// transcribes using whisper.cpp, and injects text using uinput (Linux)

#include "common-sdl.h"
#include "common.h"
#include "common-whisper.h"
#include "whisper.h"
#include "uinput-text-input.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// Parameters
struct voice_typing_params {
    int32_t n_threads  = std::min(4, (int32_t) std::thread::hardware_concurrency());
    int32_t capture_id = -1;
    int32_t max_tokens = 0;
    
    int32_t check_ms   = 100;   // Check interval for early stop signal
    int32_t max_len_ms = 30000; // Maximum recording length (30 seconds)
    
    bool print_special = false;
    bool no_fallback   = false;
    bool use_gpu       = true;
    bool flash_attn    = true;
    
    std::string language  = "en";
    std::string model     = "models/ggml-base.en.bin";
    std::string prompt    = "";
    std::string prompt_file = "";
};

// Global signal flag for early stop
volatile sig_atomic_t g_stop_early_flag = 0;

void signal_handler(int sig) {
    if (sig == SIGUSR1) {
        g_stop_early_flag = 1;
    }
}

std::string get_pid_file_path() {
    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir) {
        return std::string(runtime_dir) + "/whisper-voice-typing.pid";
    }
    return "/tmp/whisper-voice-typing.pid";
}

bool pid_file_exists() {
    std::string pid_file = get_pid_file_path();
    std::ifstream f(pid_file);
    return f.good();
}

pid_t read_pid_from_file() {
    std::string pid_file = get_pid_file_path();
    std::ifstream f(pid_file);
    if (!f.good()) {
        return -1;
    }
    
    pid_t pid;
    f >> pid;
    return pid;
}

bool write_pid_file() {
    std::string pid_file = get_pid_file_path();
    std::ofstream f(pid_file);
    if (!f.good()) {
        fprintf(stderr, "Warning: Could not write PID file: %s\n", pid_file.c_str());
        return false;
    }
    
    f << getpid();
    return true;
}

void remove_pid_file() {
    std::string pid_file = get_pid_file_path();
    unlink(pid_file.c_str());
}

bool is_process_alive(pid_t pid) {
    // Send signal 0 to check if process exists
    return kill(pid, 0) == 0;
}

void print_usage(int argc, char ** argv, const voice_typing_params & params) {
    fprintf(stderr, "\n");
    fprintf(stderr, "usage: %s [options]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -h,       --help           [default] show this help message and exit\n");
    fprintf(stderr, "  -t N,     --threads N      [%-7d] number of threads to use during computation\n", params.n_threads);
    fprintf(stderr, "  -c ID,    --capture ID     [%-7d] capture device ID\n", params.capture_id);
    fprintf(stderr, "  -max N,   --max-len N      [%-7d] maximum recording length in ms\n", params.max_len_ms);
    fprintf(stderr, "  -l LANG,  --language LANG  [%-7s] spoken language\n", params.language.c_str());
    fprintf(stderr, "  -m FNAME, --model FNAME    [%-7s] model path\n", params.model.c_str());
    fprintf(stderr, "            --prompt TEXT              initial prompt text\n");
    fprintf(stderr, "            --prompt-file FILE         file containing prompt text\n");
    fprintf(stderr, "  -nf,      --no-fallback    [%-7s] do not use temperature fallback\n", params.no_fallback ? "true" : "false");
    fprintf(stderr, "  -ps,      --print-special  [%-7s] print special tokens\n", params.print_special ? "true" : "false");
    fprintf(stderr, "  -ng,      --no-gpu         [%-7s] disable GPU\n", params.use_gpu ? "false" : "true");
    fprintf(stderr, "  -fa,      --flash-attn     [%-7s] enable flash attention\n", params.flash_attn ? "true" : "false");
    fprintf(stderr, "  -nfa,     --no-flash-attn  [%-7s] disable flash attention\n", params.flash_attn ? "false" : "true");
    fprintf(stderr, "\n");
    fprintf(stderr, "test mode:\n");
    fprintf(stderr, "  --test-type TEXT           type TEXT directly (bypass recording/transcription)\n");
    fprintf(stderr, "                             useful for rapid testing of text injection\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Press the shortcut again while recording to stop early and transcribe immediately.\n");
    fprintf(stderr, "\n");
}

bool parse_params(int argc, char ** argv, voice_typing_params & params) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argc, argv, params);
            exit(0);
        }
        else if (arg == "-t"    || arg == "--threads")      { params.n_threads      = std::stoi(argv[++i]); }
        else if (arg == "-c"    || arg == "--capture")      { params.capture_id     = std::stoi(argv[++i]); }
        else if (arg == "-max"  || arg == "--max-len")      { params.max_len_ms     = std::stoi(argv[++i]); }
        else if (arg == "-l"    || arg == "--language")     { params.language       = argv[++i]; }
        else if (arg == "-m"    || arg == "--model")        { params.model          = argv[++i]; }
        else if (arg ==            "--prompt")              { params.prompt         = argv[++i]; }
        else if (arg ==            "--prompt-file")         { params.prompt_file    = argv[++i]; }
        else if (arg == "-nf"   || arg == "--no-fallback")  { params.no_fallback    = true; }
        else if (arg == "-ps"   || arg == "--print-special"){ params.print_special  = true; }
        else if (arg == "-ng"   || arg == "--no-gpu")       { params.use_gpu        = false; }
        else if (arg == "-fa"   || arg == "--flash-attn")   { params.flash_attn     = true; }
        else if (arg == "-nfa"  || arg == "--no-flash-attn"){ params.flash_attn     = false; }
        else {
            fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            print_usage(argc, argv, params);
            return false;
        }
    }
    
    return true;
}

int main(int argc, char ** argv) {
    voice_typing_params params;
    
    // Check for test mode first
    std::string test_text;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--test-type") {
            if (i + 1 < argc) {
                test_text = argv[i + 1];
                // If test mode, just type the text and exit
                fprintf(stderr, "🧪 TEST MODE: Typing text in 2 seconds...\n");
                fprintf(stderr, "   Text: %s\n", test_text.c_str());
                sleep(2);
                
                // Add newline to the end
                test_text += "\n";
                
                if (uinput_type_text(test_text)) {
                    fprintf(stderr, "✅ Test complete!\n");
                    return 0;
                } else {
                    fprintf(stderr, "❌ Test failed!\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: --test-type requires TEXT argument\n");
                return 1;
            }
        }
    }
    
    if (!parse_params(argc, argv, params)) {
        return 1;
    }
    
    // Check if another instance is already recording
    if (pid_file_exists()) {
        pid_t existing_pid = read_pid_from_file();
        if (existing_pid > 0 && is_process_alive(existing_pid)) {
            fprintf(stderr, "🔔 Another recording in progress - stopping it to transcribe now...\n");
            
            // Send SIGUSR1 to stop the recording early
            if (kill(existing_pid, SIGUSR1) == 0) {
                fprintf(stderr, "✅ Signal sent - transcription will start automatically\n");
                return 0;
            } else {
                fprintf(stderr, "⚠️  Failed to send signal to process %d\n", existing_pid);
                return 1;
            }
        } else {
            // Stale PID file, remove it
            remove_pid_file();
        }
    }
    
    // Write our PID file
    write_pid_file();
    
    // Set up signal handler for early stop
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, nullptr);
    
    // Load prompt from file if specified
    if (!params.prompt_file.empty()) {
        std::ifstream prompt_file(params.prompt_file);
        if (prompt_file.good()) {
            std::stringstream buffer;
            buffer << prompt_file.rdbuf();
            params.prompt = buffer.str();
            fprintf(stderr, "Loaded prompt from file: %s\n", params.prompt_file.c_str());
        } else {
            fprintf(stderr, "Warning: Could not read prompt file: %s\n", params.prompt_file.c_str());
        }
    }
    
    // Initialize SDL and audio
    ggml_backend_load_all();
    
    audio_async audio(params.max_len_ms);
    if (!audio.init(params.capture_id, WHISPER_SAMPLE_RATE)) {
        fprintf(stderr, "%s: audio.init() failed!\n", __func__);
        remove_pid_file();
        return 1;
    }
    
    audio.resume();
    
    // Initialize whisper
    if (params.language != "auto" && whisper_lang_id(params.language.c_str()) == -1) {
        fprintf(stderr, "error: unknown language '%s'\n", params.language.c_str());
        remove_pid_file();
        return 1;
    }
    
    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu    = params.use_gpu;
    cparams.flash_attn = params.flash_attn;
    
    struct whisper_context * ctx = whisper_init_from_file_with_params(params.model.c_str(), cparams);
    if (ctx == nullptr) {
        fprintf(stderr, "error: failed to initialize whisper context\n");
        remove_pid_file();
        return 2;
    }
    
    fprintf(stderr, "🎯 Voice Typing - Ready!\n");
    fprintf(stderr, "🗣️  Speak now - will record for up to %.1fs\n", params.max_len_ms / 1000.0f);
    fprintf(stderr, "   Press shortcut again to stop and transcribe immediately\n");
    fprintf(stderr, "🎤 Recording...\n");
    
    // Recording loop
    std::vector<float> pcmf32;
    std::vector<float> pcmf32_new;
    
    int total_time_ms = 0;
    
    // Wait a bit for audio to start accumulating
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    while (total_time_ms < params.max_len_ms) {
        // Check for early stop signal
        if (g_stop_early_flag) {
            fprintf(stderr, "🛑 Recording stopped - proceeding to transcription\n");
            break;
        }
        
        // Sleep for check interval
        std::this_thread::sleep_for(std::chrono::milliseconds(params.check_ms));
        total_time_ms += params.check_ms;
        
        // Get new audio data since last check
        audio.get(params.check_ms, pcmf32_new);
        
        // Accumulate all audio data
        pcmf32.insert(pcmf32.end(), pcmf32_new.begin(), pcmf32_new.end());
    }
    
    audio.pause();
    
    // Check if we have any audio data
    if (pcmf32.empty()) {
        fprintf(stderr, "🔇 No audio recorded\n");
        whisper_free(ctx);
        remove_pid_file();
        return 0;
    }
    
    fprintf(stderr, "📊 Recorded %.1f seconds of audio\n", pcmf32.size() / (float)WHISPER_SAMPLE_RATE);
    fprintf(stderr, "🔄 Transcribing...\n");
    
    // Transcribe
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    
    wparams.print_progress   = false;
    wparams.print_special    = params.print_special;
    wparams.print_realtime   = false;
    wparams.print_timestamps = false;
    wparams.translate        = false;
    wparams.no_context       = true;
    wparams.single_segment   = false;
    wparams.max_tokens       = params.max_tokens;
    wparams.language         = params.language.c_str();
    wparams.n_threads        = params.n_threads;
    wparams.temperature_inc  = params.no_fallback ? 0.0f : wparams.temperature_inc;
    
    // Set prompt if provided
    std::vector<whisper_token> prompt_tokens;
    if (!params.prompt.empty()) {
        prompt_tokens.resize(1024);
        const int n_tokens = whisper_tokenize(ctx, params.prompt.c_str(), prompt_tokens.data(), prompt_tokens.size());
        if (n_tokens >= 0) {
            prompt_tokens.resize(n_tokens);
            wparams.prompt_tokens   = prompt_tokens.data();
            wparams.prompt_n_tokens = prompt_tokens.size();
        } else {
            fprintf(stderr, "Warning: Failed to tokenize prompt, ignoring it\n");
        }
    }
    
    if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
        fprintf(stderr, "error: whisper_full() failed\n");
        whisper_free(ctx);
        remove_pid_file();
        return 3;
    }
    
    // Extract transcribed text
    std::string transcribed_text;
    const int n_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < n_segments; ++i) {
        const char * text = whisper_full_get_segment_text(ctx, i);
        transcribed_text += text;
    }
    
    // Remove leading/trailing whitespace
    transcribed_text = trim(transcribed_text);
    
    if (transcribed_text.empty() || transcribed_text == " " || transcribed_text == ".") {
        fprintf(stderr, "🔇 No clear speech transcribed\n");
        whisper_free(ctx);
        remove_pid_file();
        return 0;
    }
    
    fprintf(stderr, "⌨️  Typing: %s\n", transcribed_text.c_str());
    
    // Type the text using uinput (direct kernel access - much faster than ydotool)
    if (uinput_type_text(transcribed_text)) {
        fprintf(stderr, "✅ Done!\n");
    } else {
        fprintf(stderr, "⚠️  Failed to type text\n");
        fprintf(stderr, "    Transcribed text: %s\n", transcribed_text.c_str());
    }
    
    // Cleanup
    whisper_free(ctx);
    remove_pid_file();
    
    return 0;
}

