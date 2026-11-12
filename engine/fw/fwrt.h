/* The game runtime (fw/runtime.c): an App that plays a project in a world. */
#ifndef FWRT_H
#define FWRT_H

#include "fw.h"

void fw_runtime_init(const FwWorld *w, const char *text, int len);  /* call from your App's init */
int  fw_runtime_load(const char *text, int len);                     /* another project (0 = error) */
const char *fw_runtime_title(void);

/* the App callbacks */
void fw_rt_tick(void);
void fw_rt_draw(FrameBuf *fb);
u32  fw_rt_touch_buttons(int x, int y);
void fw_rt_tap(int x, int y);
void fw_rt_look(int dx);
int  fw_rt_ram(void);
extern const KeyBind fw_runtime_keys[];

/* a complete App for a game: name, your init (which calls fw_runtime_init), user files folder */
#define FW_RUNTIME_APP(name, init, files_dir) { \
    name, init, fw_rt_tick, fw_rt_draw, \
    fw_rt_touch_buttons, fw_rt_tap, fw_rt_look, 45, \
    0, 0, 0, 0, 0, \
    fw_runtime_keys, 0, files_dir, ".game", fw_rt_ram, 0, 0, 0, 0 }

#endif
