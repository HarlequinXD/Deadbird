/*
 * Sound: one small API on every platform.
 *   - Android / Windows / headless: a software mixer (snd_mix.c) plays 8-bit PCM; the platform
 *     pulls stereo frames with snd_mix().
 *   - PS1: engine/platform/psx/snd_psx.c uploads the sounds (as SPU ADPCM) and plays them on the
 *     SPU's 24 hardware voices.
 * The game hands over its sound bank with snd_init.
 * Volumes 0..4096, pan -4096 (left) .. 4096 (right), pitch 4096 = original speed.
 */
#ifndef SND_H
#define SND_H

#include "psx.h"

#define SND_RATE  22050          /* software mixer output rate */
#define SND_LOOPS 4              /* persistent looping slots (ambience, engine) */

#define SND_MAX   64             /* sounds in a bank */

/* one sound: 8-bit signed PCM for the software mixer, SPU ADPCM on the PS1 */
typedef struct { const void *data; int len, rate, loop; } SndSample;

void snd_init(const SndSample *bank, int count);   /* the game's sounds, indexed by id */
void snd_play(int id, int vol, int pan, int pitch);                 /* fire and forget */
void snd_loop(int slot, int id, int vol, int pan, int pitch);       /* keep a loop going; vol 0 = silent */
void snd_mix(s16 *stereo, int frames);                              /* software mixer only */

#endif
