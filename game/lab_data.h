/*
 * The level's data, generated at start-up by gen/gen_lab.c (on PS1: baked by tools/gen_dump into
 * gen/baked_lab.c). The LAB_* / lab_* names keep level.c reading it like a table.
 */
#ifndef LAB_DATA_H
#define LAB_DATA_H

#include "level.h"
#include "gen_lab.h"

typedef struct {
    LabVert *verts; LabFace *faces; LabChunk *chunks; LabBox *boxes;
    int nverts, nfaces, nchunks, nboxes;
    LabSprite sprites[16]; int nsprites;
    LabFan fans[4]; int nfans;
    LabRing rings[2]; int nrings;
    LabPod pods[8]; int npods;
    LabBox ceil[32]; int nceil;
    LabArea areas[32]; int nareas;
    LabView views[16]; int nviews;
    s32 route[48][2]; int nroute;
    LabSec *secs; LabWall *walls; int nsecs, nwalls;      /* sector maps only (else 0) */
    s32 spawn_x, spawn_z; int spawn_yaw, has_spawn;
} LabData;
extern LabData lab;

#define LAB_NVERTS   lab.nverts
#define LAB_NFACES   lab.nfaces
#define LAB_NCHUNKS  lab.nchunks
#define LAB_NBOXES   lab.nboxes
#define LAB_NSPRITES lab.nsprites
#define LAB_NFANS    lab.nfans
#define LAB_NRINGS   lab.nrings
#define LAB_NPODS    lab.npods
#define LAB_NCEIL    lab.nceil
#define LAB_NAREAS   lab.nareas
#define LAB_NVIEWS   lab.nviews
#define LAB_NROUTE   lab.nroute
#define LAB_NSECS    lab.nsecs
#define lab_secs     lab.secs
#define lab_walls    lab.walls
#define lab_verts    lab.verts
#define lab_faces    lab.faces
#define lab_chunks   lab.chunks
#define lab_boxes    lab.boxes
#define lab_sprites  lab.sprites
#define lab_fans     lab.fans
#define lab_rings    lab.rings
#define lab_pods     lab.pods
#define lab_ceil     lab.ceil
#define lab_areas    lab.areas
#define lab_views    lab.views
#define lab_route    lab.route

#define LAB_SPAWN_X   -19000
#define LAB_SPAWN_Z   -26500
#define LAB_SPAWN_YAW 0
#define LAB_TANK_X    -2000
#define LAB_TANK_Y    -2300
#define LAB_TANK_Z    3000
#define LAB_REACTOR_X 21500
#define LAB_REACTOR_Y -2600
#define LAB_REACTOR_Z 8500

#endif
