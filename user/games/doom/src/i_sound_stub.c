/**
 * @file src/i_sound_stub.c
 * @brief No-op sound backend for Alcor2OS.
 *
 * Alcor2 has no audio hardware driver.  All sound and music functions are
 * implemented as empty stubs so the game runs silently without SDL_mixer or
 * any other audio library.
 *
 * This file replaces doomgeneric/doomgeneric/i_sound.c in the build.
 * Global variables declared extern in i_sound.h are defined here with
 * sensible defaults so they satisfy the linker without side-effects.
 */

#include "i_sound.h"
#include <stddef.h>

/* Variables declared extern in i_sound.h — must be defined exactly once. */
int   snd_sfxdevice       = SNDDEVICE_NONE;
int   snd_musicdevice     = SNDDEVICE_NONE;
int   snd_samplerate      = 44100;
int   snd_cachesize       = 0;
int   snd_maxslicetime_ms = 0;
char *snd_musiccmd        = (char *)"";

/* Sound effects */

void I_InitSound(boolean use_sfx_prefix)
{
  (void)use_sfx_prefix;
}

void I_ShutdownSound(void) {}

/* cppcheck-suppress constParameterPointer */
int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
  return sfxinfo->lumpnum;
}

void I_UpdateSound(void) {}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
  (void)channel;
  (void)vol;
  (void)sep;
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
  (void)sfxinfo;
  (void)channel;
  (void)vol;
  (void)sep;
  return -1;
}

void I_StopSound(int channel)
{
  (void)channel;
}

boolean I_SoundIsPlaying(int channel)
{
  (void)channel;
  return 0;
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
  (void)sounds;
  (void)num_sounds;
}

/* Music */

void I_InitMusic(void) {}

void I_ShutdownMusic(void) {}

void I_SetMusicVolume(int volume)
{
  (void)volume;
}

void  I_PauseSong(void) {}

void  I_ResumeSong(void) {}

void *I_RegisterSong(void *data, int len)
{
  (void)data;
  (void)len;
  return NULL;
}

void I_UnRegisterSong(void *handle)
{
  (void)handle;
}

void I_PlaySong(void *handle, boolean looping)
{
  (void)handle;
  (void)looping;
}

void    I_StopSong(void) {}

boolean I_MusicIsPlaying(void)
{
  return 0;
}

void I_BindSoundVariables(void) {}
