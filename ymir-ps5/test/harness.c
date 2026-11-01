/* Minimal headless libretro frontend: loads a core, runs frames, checks A/V
 * callbacks and save states, and reports emulation speed. */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libretro.h"

static const char *g_sysdir;
static unsigned g_frames_video, g_last_w, g_last_h;
static size_t g_audio_frames;
static enum retro_pixel_format g_fmt = RETRO_PIXEL_FORMAT_0RGB1555;

static void log_cb(enum retro_log_level level, const char *fmt, ...) {
    if (level < RETRO_LOG_INFO) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

static bool env_cb(unsigned cmd, void *data) {
    switch (cmd & 0xFFFF) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = g_sysdir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        g_fmt = *(enum retro_pixel_format *)data; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 2; return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: return false;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
    default: return false;
    }
}

static void video_cb(const void *d, unsigned w, unsigned h, size_t pitch) {
    (void)pitch; if (d) { g_frames_video++; g_last_w = w; g_last_h = h; }
}
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; g_audio_frames++; }
static size_t audio_batch_cb(const int16_t *d, size_t n) { (void)d; g_audio_frames += n; return n; }
static void poll_cb(void) {}
static int16_t input_cb(unsigned p, unsigned d, unsigned i, unsigned id) { return 0; }

#define SYM(name) typeof(&name) p_##name = (typeof(&name))dlsym(h, #name); \
    if (!p_##name) { fprintf(stderr, "missing %s\n", #name); return 1; }

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: harness core.so sysdir frames [content]\n"); return 2; }
    g_sysdir = argv[2];
    int frames = atoi(argv[3]);
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    SYM(retro_set_environment) SYM(retro_set_video_refresh) SYM(retro_set_audio_sample)
    SYM(retro_set_audio_sample_batch) SYM(retro_set_input_poll) SYM(retro_set_input_state)
    SYM(retro_init) SYM(retro_get_system_info) SYM(retro_get_system_av_info) SYM(retro_load_game)
    SYM(retro_run) SYM(retro_serialize_size) SYM(retro_serialize) SYM(retro_unserialize)
    SYM(retro_unload_game) SYM(retro_deinit)

    p_retro_set_environment(env_cb);
    p_retro_set_video_refresh(video_cb);
    p_retro_set_audio_sample(audio_cb);
    p_retro_set_audio_sample_batch(audio_batch_cb);
    p_retro_set_input_poll(poll_cb);
    p_retro_set_input_state(input_cb);
    p_retro_init();

    struct retro_system_info si; p_retro_get_system_info(&si);
    printf("core: %s %s exts=%s\n", si.library_name, si.library_version, si.valid_extensions);

    struct retro_game_info gi = {0}; gi.path = argc > 4 ? argv[4] : NULL;
    if (!p_retro_load_game(gi.path ? &gi : NULL)) { fprintf(stderr, "load_game failed\n"); return 1; }
    struct retro_system_av_info av; p_retro_get_system_av_info(&av);
    printf("av: %ux%u fps=%.3f rate=%.0f\n", av.geometry.base_width, av.geometry.base_height,
           av.timing.fps, av.timing.sample_rate);

    double t0 = now();
    for (int i = 0; i < frames; i++) p_retro_run();
    double dt = now() - t0;
    printf("ran %d frames in %.2fs (%.1f fps, %.0f%% of %.2f), video=%u last=%ux%u fmt=%d audio_frames=%zu\n",
           frames, dt, frames / dt, 100.0 * frames / dt / av.timing.fps, av.timing.fps,
           g_frames_video, g_last_w, g_last_h, g_fmt, g_audio_frames);

    size_t sz = p_retro_serialize_size();
    void *buf = malloc(sz);
    bool ok = p_retro_serialize(buf, sz);
    bool ok2 = ok && p_retro_unserialize(buf, sz);
    for (int i = 0; i < 10; i++) p_retro_run();
    printf("savestate: size=%zu save=%d load=%d\n", sz, ok, ok2);
    free(buf);

    p_retro_unload_game();
    p_retro_deinit();
    dlclose(h);
    printf("OK\n");
    return 0;
}
