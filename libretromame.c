/** **************************************************************************
 * libretromame.c
 * 
 * Copyright 2012 Bryan Ischo <bryan@ischo.com>
 *
 ************************************************************************** **/

#include <libmame/libmame.h>
#include <pthread.h>
#include "libretro.h"

#define LIBRARY_VERSION "1.0.0"

/* ************************************************************************ */
/* Forward declaration of libmame run game callbacks
/* ************************************************************************ */
static void StatusTextCb(const char *format, va_list args, void *callback_data);
static void StartingUpCb(LibMame_StartupPhase phase, int pct_complete,
                         LibMame_RunningGame *running_game,
                         void *callback_data);
static void PollAllControlsStateCb(LibMame_AllControlsState *all_states,
                                   void *callback_data);
static void UpdateVideoCb(const LibMame_RenderPrimitive *render_primitive_list,
                          void *callback_data);
static void UpdateAudioCb(int sample_rate, int samples_this_frame, 
                          const int16_t *buffer, void *callback_data);
static void SetMasterVolumeCb(int attenuation, void *callback_data);
static void MakeRunningGameCallsCb(void *callback_data);
static void PausedCb(void *callback_data);


/* ************************************************************************ */
/* Global State
/* ************************************************************************ */

static retro_environment_t retroEnvironmentG;
static retro_video_refresh_t retroVideoRefreshG;
static retro_audio_sample_t retroAudioSampleG;
static retro_audio_sample_batch_t retroAudioSampleBatchG;
static retro_input_poll_t retroInputPollG;
static retro_input_state_t retroInputStateG;

/* Global mutex and condition variables controlling runner thread */
static pthread_mutex_t mutexG;
static pthread_cond_t toRunnerCondG, fromRunnerCondG;

static LibMame_RunningGame *runningGameG;
static bool runningGameStopG;
static int runningGameNumberG = -1;
/* Values of the most recently received video frame */
static uint32_t runningGameWidthG, runningGameHeightG;
/* Values of the most recently received audio frame */
static int runningGameSampleRateG;

/* Next video frame */
static uint16_t videoFrameG[1000 * 1000];

/* Options to use when running a game */
static LibMame_RunGameOptions runGameOptionsG;
/* libmame callbacks */
LibMame_RunGameCallbacks runGameCallbacksG =
{
     StatusTextCb,
     StartingUpCb,
     PollAllControlsStateCb,
     UpdateVideoCb,
     UpdateAudioCb,
     SetMasterVolumeCb,
     MakeRunningGameCallsCb,
     PausedCb
 };


/* ************************************************************************ */
/* ************************************************************************ */


void retro_set_environment(retro_environment_t r)
{
    retroEnvironmentG = r;
}


void retro_set_video_refresh(retro_video_refresh_t r)
{
    retroVideoRefreshG = r;
}


void retro_set_audio_sample(retro_audio_sample_t r)
{
    retroAudioSampleG = r;
}


void retro_set_audio_sample_batch(retro_audio_sample_batch_t r)
{
    retroAudioSampleBatchG = r;
}


void retro_set_input_poll(retro_input_poll_t r)
{
    retroInputPollG = r;
}


void retro_set_input_state(retro_input_state_t r)
{
    retroInputStateG = r;
}


void retro_init()
{
    /* retro_init() assumes success, so so will we */
    (void) LibMame_Initialize();

    /* it is nonsensical for these to fail */
    (void) pthread_mutex_init(&mutexG, 0);
    (void) pthread_cond_init(&toRunnerCondG, 0);
    (void) pthread_cond_init(&fromRunnerCondG, 0);

    /* Set up the libmame options */
    LibMame_Get_Default_RunGameOptions(&runGameOptionsG);
    runGameOptionsG.auto_frame_skip = 0;
    runGameOptionsG.throttle = 0;
    runGameOptionsG.sleep = 0;
    runGameOptionsG.sound = 1;
    runGameOptionsG.skip_gameinfo_screens = 1;
    runGameOptionsG.quiet_startup = 1;
    runGameOptionsG.use_backdrops = 0;
    runGameOptionsG.use_overlays = 0;
    runGameOptionsG.use_bezels = 0;
}


void retro_deinit()
{
    /* Not going to bother to enforce that retro_init() has occurred
       successfully */
    Libmame_Deinitialize();

    (void) pthread_mutex_destroy(&mutexG);
    (void) pthread_cond_destroy(&toRunnerCondG);
    (void) pthread_cond_destroy(&fromRunnerCondG);
}


unsigned int retro_api_version()
{
    return RETRO_API_VERSION;
}


void retro_get_system_info(struct retro_system_info *info)
{
    info->library_name = "libretromame";
    info->library_version = LIBRARY_VERSION;
    info->valid_extensions = "zip|ZIP|chd|CHD";
    info->need_fullpath = true;
    info->block_extract = true;
}


void retro_get_system_av_info(struct retro_system_av_info *info)
{
    /* The width and height are only known after the first frame of video is
       received */
    info->geometry.base_width = runningGameWidthG;
    info->geometry.base_height = runningGameHeightG;
    info->geometry.max_width = runningGameWidthG;
    info->geometry.max_height = runningGameHeightG;
    /* The aspect ratio is defined by the base_width and base_height */
    info->geometry.aspect_ratio = 0.0;
    /* The timing is available from the running game number */
    if (runningGameNumberG >= 0) {
        info->timing.fps = 
            LibMame_GetGame_ScreenRefreshRateHz(runningGameNumberG);
    }
    else {
        info->timing.fps = 0.0;
    }
    /* The audio sample rate is only known after the first frame of audio is
       received */
    info->timing.sample_rate = runningGameSampleRateG;
}


void retro_set_controller_port_device(unsigned port, unsigned device)
{
    /* Not sure what this is even supposed to do */
    (void) port, (void) device;
}


static void *runner_main(void *)
{
    if (runningGameNumberG != -1) {
        LibMame_RunGameStatus status = LibMame_RunGame
            (runningGameNumberG, true, &runGameOptionsG,
             &runGameCallbacksG, NULL);
        switch (status) {
        case LibMame_RunGameStatus_Success:
            break;
        case LibMame_RunGameStatus_InvalidGameNum:
            printf("Invalid game\n");
            break;
        case LibMame_RunGameStatus_FailedValidityCheck:
            printf("Failed validity check\n");
            break;
        case LibMame_RunGameStatus_MissingFiles:
            printf("Missing files\n");
            break;
        case LibMame_RunGameStatus_NoSuchGame:
            printf("No such game\n");
            break;
        case LibMame_RunGameStatus_InvalidConfig:
            printf("Invalid config\n");
            break;
        case LibMame_RunGameStatus_GeneralError:
            printf("General error\n");
        }
    }

    runningGameNumberG = -1;

    pthread_cond_signal(&fromRunnerThreadG);
}


void retro_reset()
{
    resetG = true;
    pthread_cond_signal(&toRunnerCondG);
}


size_t retro_serialize_size()
{
    /* Not supported yet */
    return 0;
}


bool retro_serialize(void *data, size_t size)
{
    /* Not supported yet */
    (void) data, (void) size;
    return false;
}


bool retro_unserialize(const void *data, size_t size)
{
    /* Not supported yet */
    (void) data, (void) size;
    return false;
}


void retro_cheat_reset()
{
    /* Not supported yet */
}


void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    /* Not supported yet */
    (void) index, (void) enabled, (void) code;
}


bool retro_load_game_special(unsigned int game_type,
                             const struct retro_game_info *info,
                             size_t num_info)
{
    /* Not supported yet */
    (void) game_type, (void) info, (void) num_info;
    return false;
}


bool retro_load_game(const struct retro_game_info *game)
{
    /* Extract the rompath and game name from the path */
    char *c = game->path;
    
    char gamename[256];
    
    while (*c && (*c != '/') && (*c != '\\')) {
        c += 1;
    }
    if (*c) {
        c += 1;
        snprintf(runGameOptionsG.rom_path, sizeof(runGameOptionsG.rom_path),
                 "%.*s", c - game->path, game->path);
        char *g = c;
        while (*c && (*c != '.')) {
            c += 1;
        }
        snprintf(gamename, sizeof(gamename), "%.*s", c - g, g);
    }
    else {
        /* local file */
        rompath[0] = '.';
        rompath[1] = 0;
        snprintf(gamename, sizeof(gamename), "%s", game->path);
    }
    
    /* Eliminate file extension and tolower it */
    c = gamename;
    while (*c) {
        if (*c == '.') {
            *c = 0;
            break;
        }
        else {
            *c = tolower(*c);
            c += 1;
        }
    }
    
    /* Look up the game */
    runningGameNumberG = LibMame_Get_Game_Number(gamename);
    
    if (runningGameNumberG == -1) {
        return false;
    }

    /* Clear the stop indicator */
    runningGameStopG = false;

    /* Start the runner thread */
    pthread_t dontcare;
    return (pthread_create(&dontcare, 0, runner_main, 0) == 0);
}


void retro_run()
{
    pthread_mutex_lock(&mutexG);

    /* Signal the runner thread to continue for one frame */
    pthread_cond_signal(&toRunnerCondG);

    /* Wait until it signals that it is done and return */
    pthread_cond_wait(&fromRunnerCondG, &mutexG);
}


void retro_unload_game()
{
    pthread_mutex_lock(&mutexG);

    if (runningGameNumberG != -1) {
        /* Signal to the runner thread to exit */
        runningGameStopG = true;
        pthread_cond_signal(&toRunnerCondG, &mutexG);

        /* And wait for the thread to exit */
        while (runningGameNumberG != -1) {
            pthread_cond_wait(&fromRunnerCondG, &mutexG);
        }

        /* Reset game-related values */
        runningGameWidthG = runningGameHeightG = 0;
        runningGameSampleRateG = 0;
    }
}


unsigned int retro_get_region()
{
    /* Not supported yet */
    return 0;
}


void *retro_get_memory_data(unsigned id)
{
    /* Not supported yet */
    (void) id;
    return NULL;
}


size_t retro_get_memory_size(unsigned id)
{
    /* Not supported yet */
    return 0;
}


static void StatusTextCb(const char *format, va_list args, void *callback_data)
{
    (void) callback_data;

    (void) vprintf(format, args);
}


static void StartingUpCb(LibMame_StartupPhase phase, int pct_complete,
                         LibMame_RunningGame *running_game,
                         void *callback_data)
{
    (void) callback_data;

    runningGameG = running_game;
    
    const char *phase_name = "UNKNOWN";

    switch (phase) {
    case LibMame_StartupPhase_Preparing:
        phase_name = "Preparing";
        break;
    case LibMame_StartupPhase_LoadingRoms:
        phase_name = "Loading Roms";
        break;
    case LibMame_StartupPhase_InitializingMachine:
        phase_name = "Initializing Machine";
        break;
    }

    printf("Starting up: %s: %s - %d%%\n", 
           LibMame_GetGame_Full_Name(runningGameNumberG), phase_name,
           pct_complete);
}


static void PollAllControlsStateCb(LibMame_AllControlsState *all_states,
                                   void *callback_data)
{
    (void) callback_data;

    if (!retroInputPollG || !retroInputStateG) {
        return;
    }

    /* Ask libretro front end to latch all controller input */
    retroInputPollG();

    /* Could be sophisticated and only query for those controls that the
       running game needs, but for simplicity just query for everything */
    unsigned int playerCount = 
        LibMame_Get_Game_MaxSimultaneousPlayers(runningGameNumberG);

    /* And, use a fairly simple fixed input mapping */
    for (unsigned int i = 0; i < playerCount; i++) {
        /* OK, not sure how to map anything here.  libretro names a
           RETRO_DEVICE_KEYBOARD, which is the only one I am likely to
           be able to use during development, but doesn't say anything about
           what key ids there are */
    }
}


static void UpdateVideoCb(const LibMame_RenderPrimitive *render_primitive_list,
                          void *callback_data)
{
    if (!retroVideoRefreshG) {
        return;
    }

    const LibMame_RenderPrimitive *prim = render_primitive_list;

    /* Just render pixmaps; don't worry about vector games as to support them
       properly, libretro needs vector graphics support in its API.  And just
       render the first pixmap, as multi-quad games as they are pretty rare,
       usually gambling machines, and doing the compositing in software would
       be dumb */
    while (prim && ((prim->type != LibMame_RenderPrimitiveType_Quad) ||
                    !LIBMAME_RENDERFLAGS_SCREEN_TEXTURE(prim->flags))) {
        prim = prim->next;
    }
    
    if (!prim) {
        return;
    }
    
    /* This should never happen; but if the texture is too big, ignore it */
    if ((prim->texture.width * prim->texture.height) > (1000 * 1000)) {
        return;
    }

    runningGameWidthG = prim->texture.width;
    runningGameHeightG = prim->texture.height;

    switch (LIBMAME_RENDERFLAGS_TEXTURE_FORMAT(prim.flags)) {
    case LibMame_TextureFormat_Palette16:
    case LibMame_TextureFormat_PaletteA16: {
        /* Convert */
        uint16_t *dest = videoFrameG;
        uint16_t *src = (uint16_t *) prim->texture.base;
        int rowdiff = prim->texture.rowpixels - prim->texture.width;
        for (uint32_t y = 0; y < prim->texture.height; y++) {
            for (uint32_t x = 0; x < prim>texture.width; x++) {
                *dest++ = prim.texture.palette[*src++];
            }
            src += rowdiff;
        }
        break;
    }
        
    case LibMame_TextureFormat_RGB32:
    case LibMame_TextureFormat_ARGB32:{
        /* Convert */
        uint16_t *dest = videoFrameG;
        uint32_t *src = (uint32_t *) prim->texture.base;
        int rowdiff = prim->texture.rowpixels - prim->texture.width;
        for (uint32_t y = 0; y < prim->texture.height; y++) {
            for (uint32_t x = 0; x < prim>texture.width; x++) {
                uint32_t xrgb = prim.texture.palette[*src++];
                *dest++ = ((((xrgb >> 16) & 0x3f) << 10) |
                           (((xrgb >>  8) & 0x3f) <<  5) |
                           (((xrgb >>  0) & 0x3f) <<  0));
            }
            src += rowdiff;
        }
        break;
    }
    case LibMame_TextureFormat_YUY16:
        /* Unimplemented */
        return;
    case LibMame_TextureFormat_Undefined:
        /* Should never happen */
        return;
    default:
        /* Should never happen */
        return;
    }

    (retroVideoRefreshG)(videoFrameG, runningGameWidthG, runningGameHeightG,
                         2 * runningGameWidthG);
}


static void UpdateAudioCb(int sample_rate, int frame_count, 
                          const int16_t *buffer, void *callback_data)
{
    runningGameSampleRateG = sample_rate;

    if (retroAudioSampleBatchG) {
        (retroAudioSampleBatchG)(buffer, samples_this_frame);
    }
    else if (retroAudioSampleG) {
        for (int i = 0; i < frame_count; i++) {
            (retroAudioSampleG)(buffer, &(buffer[1]));
            buffer = &(buffer[2]);
        }
    }
}


static void SetMasterVolumeCb(int attenuation, void *callback_data)
{
    (void) attenuation, (void) callback_data;
}


static void MakeRunningGameCallsCb(void *callback_data)
{
    (void) callback_data;

    pthread_mutex_lock(&mutexG);

    /* Signal done with this frame */
    pthread_cond_signal(&fromRunnerCondG);

    /* Now wait to be told to go again */
    pthread_cond_wait(&toRunnerCondG, &mutexG);

    /* If it's time to exit this game, do so */
    if (runningGameStopG) {
        /* Time to exit */
        LibMame_RunningGame_Schedule_Exit(runningGameG);
    }
    /* Else if a reset has been requested, do so */
    else if (resetG) {
        LibMame_RunningGame_Schedule_Soft_Reset(runningGameG);
        resetG = false;
    }
}


static void PausedCb(void *callback_data)
{
    (void) callback_data;
}
