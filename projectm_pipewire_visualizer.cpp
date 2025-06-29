#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <cstring> // For memset

// SDL2 and OpenGL headers
#include <SDL2/SDL.h>
#include <GL/glew.h>

// libprojectM header - C API
#include <projectM-4/projectM.h>
// Explicitly include headers that define the C API functions we'll use
#include <projectM-4/core.h>
#include <projectM-4/audio.h>
#include <projectM-4/parameters.h>
#include <projectM-4/render_opengl.h>
#include <projectM-4/playlist_core.h>
#include <projectM-4/playlist_items.h>
#include <projectM-4/playlist_playback.h>
#include <projectM-4/types.h>

// PipeWire headers
#include <pipewire/pipewire.h>
#include <pipewire/main-loop.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>

// --- Configuration ---
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const int AUDIO_RATE = 44100;
const int AUDIO_CHANNELS = 2;
const int AUDIO_SAMPLES_PER_CALLBACK = 512;

const std::string MILK_FILE_PATH_STR = "/usr/share/projectM/presets/Aderrasi - Aimless (Gravity Directive Mix).milk";
const std::string PRESET_PATH_DIR_STR = "/usr/share/projectM/presets/";
const std::string FONT_DIR_PATH_STR = "/usr/share/projectM/fonts/";

// --- Globals ---
SDL_Window* g_window = nullptr;
SDL_GLContext g_gl_context;
projectm_handle g_projectm_instance = nullptr;
projectm_playlist_handle g_projectm_playlist = nullptr;
int winW, winH;

// PipeWire Globals
struct pw_main_loop *g_pw_loop = nullptr;
struct pw_context *g_pw_context = nullptr;
struct pw_core *g_pw_core = nullptr;
struct pw_stream *g_pw_stream = nullptr;

std::vector<short> g_audio_buffer(AUDIO_SAMPLES_PER_CALLBACK * AUDIO_CHANNELS, 0);
std::mutex g_audio_buffer_mutex;
std::atomic<bool> g_new_audio_data_available(false);

static void on_pipewire_process(void *userdata) {
    struct pw_buffer *b;
    struct spa_buffer *buf;
    if (!g_pw_stream) return;
    if ((b = pw_stream_dequeue_buffer(g_pw_stream)) == NULL) return;
    buf = b->buffer;
    if (!buf || !buf->datas[0].data) {
        if (b) pw_stream_queue_buffer(g_pw_stream, b);
        return;
    }
    short *input_data = static_cast<short*>(buf->datas[0].data);
    uint32_t n_bytes = buf->datas[0].chunk->size;
    uint32_t n_frames = n_bytes / (sizeof(short) * AUDIO_CHANNELS);
    if (n_frames > 0) {
        std::lock_guard<std::mutex> lock(g_audio_buffer_mutex);
        size_t samples_to_copy = std::min((size_t)n_frames, (size_t)AUDIO_SAMPLES_PER_CALLBACK);
        if (samples_to_copy > 0) {
             memcpy(g_audio_buffer.data(), input_data, samples_to_copy * AUDIO_CHANNELS * sizeof(short));
             if (samples_to_copy < AUDIO_SAMPLES_PER_CALLBACK) {
                 std::fill(g_audio_buffer.begin() + samples_to_copy * AUDIO_CHANNELS, g_audio_buffer.end(), 0);
             }
             g_new_audio_data_available = true;
        }
    }
    pw_stream_queue_buffer(g_pw_stream, b);
}

static void on_pipewire_state_changed(void *data, enum pw_stream_state old_state, enum pw_stream_state state, const char *error_message) {
    fprintf(stderr, "PipeWire stream state changed from %s to %s\n", pw_stream_state_as_string(old_state), pw_stream_state_as_string(state));
    if (error_message && strlen(error_message) > 0) fprintf(stderr, "PipeWire stream error: %s\n", error_message);
    if (state == PW_STREAM_STATE_STREAMING) fprintf(stderr, "PipeWire stream is now active and streaming.\n");
}

static const struct pw_stream_events g_pw_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_pipewire_state_changed,
    .process = on_pipewire_process,
};

// REPLACE THE ENTIRE initPipeWire() FUNCTION IN YOUR FILE WITH THIS:
bool initPipeWire() {
    std::cout << "Initializing PipeWire..." << std::endl;
    pw_init(nullptr, nullptr);
    g_pw_loop = pw_main_loop_new(nullptr);
    if (!g_pw_loop) { std::cerr << "Failed to create PipeWire main loop." << std::endl; return false; }
    g_pw_context = pw_context_new(pw_main_loop_get_loop(g_pw_loop), nullptr, 0);
    if (!g_pw_context) { std::cerr << "Failed to create PipeWire context." << std::endl; return false; }
    g_pw_core = pw_context_connect(g_pw_context, nullptr, 0);
    if (!g_pw_core) { std::cerr << "Failed to connect to PipeWire core." << std::endl; return false; }
    g_pw_stream = pw_stream_new_simple(
        pw_main_loop_get_loop(g_pw_loop), "projectm-visualizer-capture",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "Music", PW_KEY_STREAM_CAPTURE_SINK, "true", nullptr),
        &g_pw_stream_events, nullptr);
    if (!g_pw_stream) { std::cerr << "Failed to create PipeWire stream." << std::endl; return false; }

    uint8_t spa_buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(spa_buffer, sizeof(spa_buffer));
    const struct spa_pod *params[1];

    struct spa_audio_info_raw raw_info;
    memset(&raw_info, 0, sizeof(raw_info));
    raw_info.format = SPA_AUDIO_FORMAT_S16_LE;
    raw_info.rate = AUDIO_RATE;
    raw_info.channels = AUDIO_CHANNELS;
    if (AUDIO_CHANNELS >= 1) raw_info.position[0] = SPA_AUDIO_CHANNEL_FL;
    if (AUDIO_CHANNELS >= 2) raw_info.position[1] = SPA_AUDIO_CHANNEL_FR;

    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &raw_info);

    // Correctly initialize the flags variable with an explicit cast
    enum pw_stream_flags stream_flags = static_cast<pw_stream_flags>(
                                            PW_STREAM_FLAG_AUTOCONNECT |
                                            PW_STREAM_FLAG_MAP_BUFFERS |
                                            PW_STREAM_FLAG_RT_PROCESS  // Ensure this is correct!
                                        );

    if (pw_stream_connect(g_pw_stream,      // 1. stream
                          PW_DIRECTION_INPUT, // 2. direction
                          PW_ID_ANY,          // 3. target_id
                          stream_flags,       // 4. flags
                          params,             // 5. params array
                          1)                  // 6. n_params
                          != 0) {
        std::cerr << "Failed to connect PipeWire stream." << std::endl; return false;
    }
    std::cout << "PipeWired initialized and stream connection initiated." << std::endl; // Typo in "PipeWired" fixed
    return true;
}

bool initSDLOpenGL() {
    std::cout << "Initializing SDL and OpenGL..." << std::endl;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl; return false; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    g_window = SDL_CreateWindow("projectM PipeWire Visualizer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!g_window) { std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl; return false; }
    g_gl_context = SDL_GL_CreateContext(g_window);
    if (!g_gl_context) { std::cerr << "OpenGL context could not be created! SDL_Error: " << SDL_GetError() << std::endl; return false; }
    glewExperimental = GL_TRUE; GLenum glewError = glewInit();
    if (glewError != GLEW_OK) { std::cerr << "Error initializing GLEW! " << glewGetErrorString(glewError) << std::endl; return false; }
    if (SDL_GL_SetSwapInterval(1) < 0) std::cout << "Warning: Unable to set VSync! SDL Error: " << SDL_GetError() << std::endl;
    //glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SDL_GetWindowSize(g_window, &winW, &winH);
    std::cout << "Window size: " << winW << "x" << winH << std::endl;
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    std::cout << "SDL and OpenGL initialized." << std::endl;
    return true;
}

bool initProjectM() {
    std::cout << "Initializing projectM..." << std::endl;

    g_projectm_instance = projectm_create();

    if (!g_projectm_instance) {
        std::cerr << "Failed to create projectM instance." << std::endl;
        return false;
    }
    std::cout << "projectM instance created." << std::endl;

    //projectm_set_window_size(g_projectm_instance, SCREEN_WIDTH, SCREEN_HEIGHT);
    //projectm_set_mesh_size(g_projectm_instance, 32, 24);
    projectm_set_window_size(g_projectm_instance, winW, winH);
    projectm_set_mesh_size(g_projectm_instance, winW / 32, winH / 32);

    g_projectm_playlist = projectm_playlist_create(g_projectm_instance);
    if (!g_projectm_playlist) {
        std::cerr << "Failed to create projectM playlist." << std::endl;
        projectm_destroy(g_projectm_instance);
        g_projectm_instance = nullptr;
        return false;
    }
    std::cout << "projectM playlist created." << std::endl;

    uint32_t num_added = projectm_playlist_add_path(g_projectm_playlist,
                                                    PRESET_PATH_DIR_STR.c_str(),
                                                    true,  // recursive_scan
                                                    true); // select_first_added
    std::cout << "Added " << num_added << " presets from directory: " << PRESET_PATH_DIR_STR << std::endl;

    if (num_added == 0) {
         std::cerr << "Warning: No presets found or loaded from: " << PRESET_PATH_DIR_STR << std::endl;
    }

    if (!MILK_FILE_PATH_STR.empty()) {
        projectm_load_preset_file(g_projectm_instance, MILK_FILE_PATH_STR.c_str(), 0);
        std::cout << "Attempted to load specific .milk file: " << MILK_FILE_PATH_STR << std::endl;
    } else if (num_added > 0) {
        std::cout << "No specific milk file, relying on playlist to play first (if select_first_added=true worked)." << std::endl;
    }

    std::cout << "projectM initialized." << std::endl;
    return true;
}

void cleanup() {
    std::cout << "Cleaning up..." << std::endl;
    if (g_projectm_playlist) {
        projectm_playlist_destroy(g_projectm_playlist);
        g_projectm_playlist = nullptr;
    }
    if (g_projectm_instance) {
        projectm_destroy(g_projectm_instance);
        g_projectm_instance = nullptr;
    }
    if (g_pw_stream) { pw_stream_set_active(g_pw_stream, false); pw_stream_disconnect(g_pw_stream); pw_stream_destroy(g_pw_stream); g_pw_stream = nullptr; }
    if (g_pw_core) { pw_core_disconnect(g_pw_core); g_pw_core = nullptr; }
    if (g_pw_context) { pw_context_destroy(g_pw_context); g_pw_context = nullptr; }
    if (g_pw_loop) { pw_main_loop_destroy(g_pw_loop); g_pw_loop = nullptr; }
    if (g_gl_context) SDL_GL_DeleteContext(g_gl_context);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
    std::cout << "Cleanup complete." << std::endl;
}

void renderLoop() {
    bool quit = false; SDL_Event e; Uint64 last_audio_feed_time = SDL_GetTicks64();
    struct pw_loop *loop = g_pw_loop ? pw_main_loop_get_loop(g_pw_loop) : nullptr;

    while (!quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN && g_projectm_playlist) {
                if (e.key.keysym.sym == SDLK_RIGHT ) {
                    projectm_playlist_play_next(g_projectm_playlist, true);
                    std::cout << "Playlist: Next." << std::endl;
                } else if (e.key.keysym.sym == SDLK_LEFT) {
                    projectm_playlist_play_previous(g_projectm_playlist, true);
                    std::cout << "Playlist: Previous." << std::endl;
                }
            }
        }
        if (loop) {
             pw_loop_iterate(loop, 0);
        }

        if (g_projectm_instance) {
            bool fed_real_audio = false;
            if (g_new_audio_data_available.load()) {
                std::lock_guard<std::mutex> lock(g_audio_buffer_mutex);
                projectm_pcm_add_int16(g_projectm_instance, g_audio_buffer.data(), AUDIO_SAMPLES_PER_CALLBACK, PROJECTM_STEREO);
                g_new_audio_data_available = false; fed_real_audio = true; last_audio_feed_time = SDL_GetTicks64();
            }
            if (!fed_real_audio && (SDL_GetTicks64() - last_audio_feed_time > 30)) {
                static std::vector<short> silence_buffer(AUDIO_SAMPLES_PER_CALLBACK * AUDIO_CHANNELS, 0);
                projectm_pcm_add_int16(g_projectm_instance, silence_buffer.data(), AUDIO_SAMPLES_PER_CALLBACK, PROJECTM_STEREO);
            }
        }
        if (g_projectm_instance) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            projectm_opengl_render_frame(g_projectm_instance);
            SDL_GL_SwapWindow(g_window);
        }
        SDL_Delay(10);
    }
}

int main(int argc, char* args[]) {
    std::cout << "--- Starting PipeWire projectM Visualizer ---" << std::endl;
    std::cout << "\n--- Initializing PipeWire ---" << std::endl;
    if (!initPipeWire()) { std::cerr << "Critical: Failed to initialize PipeWire." << std::endl; cleanup(); return 1; }
    std::cout << "\n--- Initializing SDL & OpenGL ---" << std::endl;
    if (!initSDLOpenGL()) { std::cerr << "Critical: Failed to initialize SDL/OpenGL." << std::endl; cleanup(); return 1; }
    std::cout << "\n--- Initializing projectM ---" << std::endl;
    if (!initProjectM()) { std::cerr << "Critical: Failed to initialize projectM." << std::endl; cleanup(); return 1; }
    std::cout << "\n--- Initialization Successful ---" << std::endl;
    std::cout << "Starting render loop. Ensure audio is playing through PipeWire." << std::endl;
    std::cout << "Press LEFT/RIGHT arrow keys to change presets (if playlist loads)." << std::endl;
    renderLoop();
    std::cout << "\n--- Shutting Down ---" << std::endl;
    cleanup();
    return 0;
}
