/** **************************************************************************
 * libretromame.c
 * 
 * Copyright 2012 Bryan Ischo <bryan@ischo.com>
 *
 ************************************************************************** **/

#include <libmame/libmame.h>
#include "libretro.h"

#define LIBRARY_VERSION "1.0.0"

/* ************************************************************************ */
/* Forward declaration of run game callbacks
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
/* Global State - unavoidable due to the libretro API */
/* ************************************************************************ */

static retro_environment_t retroEnvironmentG;
static retro_video_refresh_t retroVideoRefreshG;
static retro_audio_sample_t retroAudioSampleG;
static retro_audio_sample_batch_t retroAudioSampleBatchG;
static retro_input_poll_t retroInputPollG;
static retro_input_state_t retroInputStateG;

static LibMame_RunningGame *runningGameG;
static int runningGameNumberG = -1;
/* Values of the most recently received video frame */
static uint32_t runningGameWidthG, runningGameHeightG;
/* Values of the most recently received audio frame */
static int runningGameSampleRateG;

/* Options to use when running a game */
static LibMame_RunGameOptions runGameOptionsG:
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
}


void retro_deinit()
{
    /* Not going to bother to enforce that retro_init() have occurred
       successfully */
    Libmame_Deinitialize();
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
}


void retro_reset()
{
    resetG = true;
}


void retro_run()
{
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


bool retro_load_game(const struct retro_game_info *game)
{
    /* Extract the rompath and game name from the path */

    /* If a game is running, schedule an exit */
}


bool retro_load_game_special(unsigned int game_type,
                             const struct retro_game_info *info,
                             size_t num_info)
{
    /* Not supported yet */
    (void) game_type, (void) info, (void) num_info;
    return false;
}


void retro_unload_game()
{
    /* Not supported yet */
    unloadGameG = true;
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

    /* Ask libretro front end to latch all controller input */
    retroInputPollG();

    /* Could be sophisticated and only query for those controls that the
       running game needs, but for simplicity just query for everything */
    
}


static void UpdateVideoCb(const LibMame_RenderPrimitive *render_primitive_list,
                          void *callback_data)
{
    (void) callback_data;
    
}


static void UpdateAudioCb(int sample_rate, int samples_this_frame, 
                          const int16_t *buffer, void *callback_data)
{
    (void) callback_data;
}


static void SetMasterVolumeCb(int attenuation, void *callback_data)
{
    (void) attenuation, (void) callback_data;
}


static void MakeRunningGameCallsCb(void *callback_data)
{
    (void) callback_data;

    if (resetG) {
        LibMame_RunningGame_Schedule_Hard_Reset(runningGameG);
        resetG = false;
    }

    if (unloadGameG) {
        LibMame_RunningGame_Schedule_Exit(runningGameG);
        unloadGameG = false;
    }
}


static void PausedCb(void *callback_data)
{
    (void) callback_data;
}
