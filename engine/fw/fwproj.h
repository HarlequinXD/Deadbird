/* Game projects as text (fw/project.c). */
#ifndef FWPROJ_H
#define FWPROJ_H

#include "fw.h"

typedef struct {
    char title[32];
    char level[32];                         /* the level to play ("" = the world's default) */
    u8   proto_fsm[FW_PROTOS][1];           /* (loader scratch: which types name a machine) */
    char proto_fsm_name[FW_PROTOS][16];
} FwProject;

/* parse a project: resets the framework (fw_init) and fills types, state machines, the player
   and the weapon. 0 on an error: fw_project_error() says which line and why. */
int  fw_project_load(const char *text, int len, FwProject *out);
const char *fw_project_error(void);

#endif
