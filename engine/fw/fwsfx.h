/* The framework's built-in sound ids (fw/sfx.c). */
#ifndef FWSFX_H
#define FWSFX_H
enum { FWS_SHOOT, FWS_HIT, FWS_DIE, FWS_JUMP, FWS_PICKUP, FWS_LAND, FWS_ALERT, FWS_EXPLODE, FWS_CLICK, FWS_HUM, FWS_COUNT };
extern const char *const fw_sound_names[FWS_COUNT];
void fw_sfx_init(void);              /* synthesise and hand them to snd_init */
int  fw_sfx_ram(void);
void fw_font_init(void);
#endif
