// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// SDL2 audio implementation for DOOM
// Replaces Linux OSS audio backend with cross-platform SDL2
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#include <SDL.h>

#include "z_zone.h"
#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "doomdef.h"

// Audio configuration
#define SAMPLECOUNT     512
#define NUM_CHANNELS    8
#define BUFMUL          4
#define MIXBUFFERSIZE   (SAMPLECOUNT*BUFMUL)
#define SAMPLERATE      11025
#define SAMPLESIZE      2

// The actual lengths of all sound effects
int lengths[NUMSFX];

// SDL audio device
static SDL_AudioDeviceID audio_device = 0;

// The global mixing buffer
// Samples from all active internal channels are mixed and stored here
signed short mixbuffer[MIXBUFFERSIZE];

// Channel step amount and 0.16 bit remainder of last step
unsigned int channelstep[NUM_CHANNELS];
unsigned int channelstepremainder[NUM_CHANNELS];

// Channel data pointers, start and end
unsigned char* channels[NUM_CHANNELS];
unsigned char* channelsend[NUM_CHANNELS];

// Time/gametic that the channel started playing
// Used to determine oldest, which automatically has lowest priority
int channelstart[NUM_CHANNELS];

// Channel handles (determined on registration)
int channelhandles[NUM_CHANNELS];

// SFX id of the playing sound effect
// Used to catch duplicates (like chainsaw)
int channelids[NUM_CHANNELS];

// Pitch to stepping lookup
int steptable[256];

// Volume lookups
int vol_lookup[128*256];

// Hardware left and right channel volume lookup
int* channelleftvol_lookup[NUM_CHANNELS];
int* channelrightvol_lookup[NUM_CHANNELS];


//
// This function loads the sound data from the WAD lump for single sound
//
static void*
getsfx(char* sfxname, int* len)
{
    unsigned char* sfx;
    unsigned char* paddedsfx;
    int i;
    int size;
    int paddedsize;
    char name[20];
    int sfxlump;

    // Get the sound data from the WAD
    sprintf(name, "ds%s", sfxname);

    // Try to load the sound
    if (W_CheckNumForName(name) == -1)
        sfxlump = W_GetNumForName("dspistol");
    else
        sfxlump = W_GetNumForName(name);

    size = W_LumpLength(sfxlump);

    // Debug info
    // fprintf(stderr, "." );

    sfx = (unsigned char*)W_CacheLumpNum(sfxlump, PU_STATIC);

    // Pad the sound data (skip DMX header)
    paddedsize = ((size-8 + (SAMPLECOUNT-1)) / SAMPLECOUNT) * SAMPLECOUNT;

    paddedsfx = (unsigned char*)Z_Malloc(paddedsize+8, PU_STATIC, 0);

    // Copy sound data (skip 8-byte DMX header)
    memcpy(paddedsfx, sfx, size);

    // Pad with silence (128 = center value for unsigned 8-bit)
    for (i = size; i < paddedsize+8; i++)
        paddedsfx[i] = 128;

    // Remove the cached lump
    Z_Free(sfx);

    // Preserve padded length
    *len = paddedsize;

    // Return allocated padded data (skip DMX header)
    return (void*)(paddedsfx + 8);
}


//
// addsfx - Adds a sound to the list of currently active sounds
//
static int
addsfx(int sfxid, int volume, int step, int seperation)
{
    static unsigned short handlenums = 0;
    int i;
    int rc = -1;
    int oldest = gametic;
    int oldestnum = 0;
    int slot;
    int rightvol;
    int leftvol;

    // Chainsaw troubles - play these sound effects only one at a time
    if (sfxid == sfx_sawup || sfxid == sfx_sawidl ||
        sfxid == sfx_sawful || sfxid == sfx_sawhit ||
        sfxid == sfx_stnmov || sfxid == sfx_pistol)
    {
        // Loop all channels, check
        for (i = 0; i < NUM_CHANNELS; i++)
        {
            if (channels[i] && (channelids[i] == sfxid))
            {
                channels[i] = 0;
                break;
            }
        }
    }

    // Loop all channels to find oldest SFX
    for (i = 0; (i < NUM_CHANNELS) && (channels[i]); i++)
    {
        if (channelstart[i] < oldest)
        {
            oldestnum = i;
            oldest = channelstart[i];
        }
    }

    // If we found a channel, fine. If not, overwrite the first one
    if (i == NUM_CHANNELS)
        slot = oldestnum;
    else
        slot = i;

    // Set pointer to raw data and end of raw data
    channels[slot] = (unsigned char*)S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];

    // Reset current handle number, limited to 0..100
    if (!handlenums)
        handlenums = 100;

    // Assign current handle number
    channelhandles[slot] = rc = handlenums++;

    // Set stepping
    channelstep[slot] = step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;

    // Separation (stereo positioning) - range is: 1 - 256
    seperation += 1;

    // Per left/right channel - x^2 separation, adjust volume properly
    leftvol = volume - ((volume*seperation*seperation) >> 16);
    seperation = seperation - 257;
    rightvol = volume - ((volume*seperation*seperation) >> 16);

    // Sanity check, clamp volume
    if (rightvol < 0 || rightvol > 127)
        I_Error("rightvol out of bounds");
    if (leftvol < 0 || leftvol > 127)
        I_Error("leftvol out of bounds");

    // Get the proper lookup table piece for this volume level
    channelleftvol_lookup[slot] = &vol_lookup[leftvol*256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol*256];

    // Preserve sound SFX id (e.g. for avoiding duplicates of chainsaw)
    channelids[slot] = sfxid;

    return rc;
}


//
// I_UpdateSound
//
// This function loops all active (internal) sound channels,
// retrieves samples from the raw sound data, modifies it according
// to the current (internal) channel parameters, mixes the per channel
// samples into the global mixbuffer, clamping it to the allowed range.
//
// This is the CRITICAL mixing logic from i_sound.c lines 540-655.
// It must be preserved EXACTLY to maintain correct audio behavior.
//
void I_UpdateSound(void)
{
    // Mix current sound data
    register unsigned int sample;
    register int dl;
    register int dr;

    // Pointers in global mixbuffer, left, right, end
    signed short* leftout;
    signed short* rightout;
    signed short* leftend;

    // Step in mixbuffer (2 for stereo)
    int step;

    // Mixing channel index
    int chan;

    // Left and right channel are in global mixbuffer, alternating
    leftout = mixbuffer;
    rightout = mixbuffer + 1;
    step = 2;

    // Determine end, for left channel only (right channel is implicit)
    leftend = mixbuffer + SAMPLECOUNT*step;

    // Mix sounds into the mixing buffer
    // Loop over step*SAMPLECOUNT (512 values for two channels)
    while (leftout != leftend)
    {
        // Reset left/right value
        dl = 0;
        dr = 0;

        // Love thy L2 cache - made this a loop
        // Loop through all channels
        for (chan = 0; chan < NUM_CHANNELS; chan++)
        {
            // Check channel, if active
            if (channels[chan])
            {
                // Get the raw data from the channel
                sample = *channels[chan];

                // Add left and right part for this channel (sound)
                // to the current data, adjusting volume accordingly
                dl += channelleftvol_lookup[chan][sample];
                dr += channelrightvol_lookup[chan][sample];

                // Increment index with fractional stepping
                channelstepremainder[chan] += channelstep[chan];

                // MSB is next sample
                channels[chan] += channelstepremainder[chan] >> 16;

                // Limit to LSB (keep fractional part)
                channelstepremainder[chan] &= 65536-1;

                // Check whether we are done
                if (channels[chan] >= channelsend[chan])
                    channels[chan] = 0;
            }
        }

        // Clamp to range. Left hardware channel.
        if (dl > 0x7fff)
            *leftout = 0x7fff;
        else if (dl < -0x8000)
            *leftout = -0x8000;
        else
            *leftout = dl;

        // Same for right hardware channel
        if (dr > 0x7fff)
            *rightout = 0x7fff;
        else if (dr < -0x8000)
            *rightout = -0x8000;
        else
            *rightout = dr;

        // Increment current pointers in mixbuffer
        leftout += step;
        rightout += step;
    }
}


//
// I_SubmitSound
//
// Queue mixed audio to SDL2 (non-blocking)
// Note: I_UpdateSound() is called separately by the game loop
//
void I_SubmitSound(void)
{
    // Queue the mixed buffer to SDL2 (non-blocking)
    // SDL2 will play it when ready without blocking the game
    if (audio_device)
    {
        SDL_QueueAudio(audio_device, mixbuffer, MIXBUFFERSIZE * sizeof(short));
    }
}


//
// I_UpdateSoundParams
//
void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    // Currently unused
    handle = vol = sep = pitch = 0;
}


//
// I_ShutdownSound
//
void I_ShutdownSound(void)
{
    int i;

    fprintf(stderr, "I_ShutdownSound: closing audio device\n");

    // Wait for sounds to finish (simplified)
    for (i = 0; i < NUM_CHANNELS; i++)
        channels[i] = 0;

    // Close SDL audio device
    if (audio_device)
    {
        SDL_PauseAudioDevice(audio_device, 1);
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
}


//
// I_InitSound
//
void I_InitSound()
{
    int i;
    SDL_AudioSpec desired, obtained;

    fprintf(stderr, "I_InitSound: ");

    // Initialize SDL audio subsystem
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        fprintf(stderr, "Could not initialize SDL audio: %s\n", SDL_GetError());
        return;
    }

    // Set up desired audio format
    // Note: Using NULL callback to use queue-based audio instead
    // This prevents high-priority audio thread from blocking the system
    desired.freq = SAMPLERATE;
    desired.format = AUDIO_S16SYS;  // Signed 16-bit, system byte order
    desired.channels = 2;            // Stereo
    desired.samples = SAMPLECOUNT * 2;   // Larger buffer to reduce calls
    desired.callback = NULL;         // Use queue-based audio (non-blocking)
    desired.userdata = NULL;

    // Open audio device
    audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);

    if (!audio_device)
    {
        fprintf(stderr, "Could not open audio device: %s\n", SDL_GetError());
        return;
    }

    // Verify we got the format we wanted
    if (obtained.format != desired.format ||
        obtained.channels != desired.channels ||
        obtained.freq != desired.freq)
    {
        fprintf(stderr, "Warning: audio format mismatch\n");
        fprintf(stderr, "  Desired: %d Hz, %d channels, format %d\n",
                desired.freq, desired.channels, desired.format);
        fprintf(stderr, "  Obtained: %d Hz, %d channels, format %d\n",
                obtained.freq, obtained.channels, obtained.format);
    }

    fprintf(stderr, "configured audio device (%d Hz, %d channels)\n",
            obtained.freq, obtained.channels);

    // Initialize external data (all sounds) at start
    fprintf(stderr, "I_InitSound: ");

    for (i = 1; i < NUMSFX; i++)
    {
        // Alias? Example is the chaingun sound linked to pistol
        if (!S_sfx[i].link)
        {
            // Load data from WAD file
            S_sfx[i].data = getsfx(S_sfx[i].name, &lengths[i]);
        }
        else
        {
            // Previously loaded already?
            S_sfx[i].data = S_sfx[i].link->data;
            lengths[i] = lengths[(S_sfx[i].link - S_sfx)/sizeof(sfxinfo_t)];
        }
    }

    fprintf(stderr, "pre-cached all sound data\n");

    // Initialize mixbuffer with zero
    for (i = 0; i < MIXBUFFERSIZE; i++)
        mixbuffer[i] = 0;

    // Start audio playback
    SDL_PauseAudioDevice(audio_device, 0);

    fprintf(stderr, "I_InitSound: sound module ready\n");
}


//
// SFX API
//

void I_SetChannels()
{
    // Init internal lookups (raw data, mixing buffer, channels)
    int i;
    int j;
    int* steptablemid = steptable + 128;

    // This table provides step widths for pitch parameters
    for (i = -128; i < 128; i++)
        steptablemid[i] = (int)(pow(2.0, (i/64.0))*65536.0);

    // Generate volume lookup tables which also turn
    // unsigned samples into signed samples
    for (i = 0; i < 128; i++)
        for (j = 0; j < 256; j++)
            vol_lookup[i*256+j] = (i*(j-128)*256)/127;
}


void I_SetSfxVolume(int volume)
{
    snd_SfxVolume = volume;
}


int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}


int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    // Unused
    priority = 0;

    // Note: No locking needed here. The channel array modifications
    // are simple pointer assignments which are atomic on modern CPUs.
    // Locking would cause stuttering when multiple sounds play.
    // The worst case without locking is a brief audio glitch, which
    // is preferable to game freezing.

    // Returns a handle (not used)
    id = addsfx(id, vol, steptable[pitch], sep);

    return id;
}


void I_StopSound(int handle)
{
    // Unused
    handle = 0;
}


int I_SoundIsPlaying(int handle)
{
    return gametic < handle;
}


//
// MUSIC API - Dummies (music not implemented)
//

void I_InitMusic(void)        { }
void I_ShutdownMusic(void)    { }
void I_SetMusicVolume(int volume) { snd_MusicVolume = volume; }
void I_PauseSong(int handle)  { handle = 0; }
void I_ResumeSong(int handle) { handle = 0; }
void I_PlaySong(int handle, int looping) { handle = looping = 0; }
void I_StopSong(int handle)   { handle = 0; }
void I_UnRegisterSong(int handle) { handle = 0; }
int I_RegisterSong(void* data) { data = NULL; return 1; }
int I_QrySongPlaying(int handle) { handle = 0; return 0; }
