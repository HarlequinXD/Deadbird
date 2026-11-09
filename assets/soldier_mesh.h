/* PS1 soldier, rigid hierarchical parts: 507 verts, 347 prims (636 tris), 16 bones.
   Fixed point: 1 model unit = 1024 (4096 = 1.0), Y down, faces +Z.
   Vertices are LOCAL to their bone pivot; bone offsets are relative to the parent pivot.
   Render each bone with  world = parent_world * T(offset) * R(bone)  and draw its prim range. */
#ifndef DMODEL_TYPES_DEFINED
#define DMODEL_TYPES_DEFINED
typedef struct { short x, y, z, pad; } DVERT;
typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;
typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;
#endif
#define SOLDIER_NVERTS 507
#define SOLDIER_NPRIMS 347
#define SOLDIER_NBONES 16
#define SOLDIER_BONE_PELVIS 0
#define SOLDIER_BONE_TORSO 1
#define SOLDIER_BONE_NECK 2
#define SOLDIER_BONE_HEAD 3
#define SOLDIER_BONE_UPPER_ARM_L 4
#define SOLDIER_BONE_FOREARM_L 5
#define SOLDIER_BONE_FIST_L 6
#define SOLDIER_BONE_UPPER_ARM_R 7
#define SOLDIER_BONE_FOREARM_R 8
#define SOLDIER_BONE_FIST_R 9
#define SOLDIER_BONE_THIGH_L 10
#define SOLDIER_BONE_CALF_L 11
#define SOLDIER_BONE_HOOF_L 12
#define SOLDIER_BONE_THIGH_R 13
#define SOLDIER_BONE_CALF_R 14
#define SOLDIER_BONE_HOOF_R 15
/* right-hand weapon mount: local to bone fist_R, plus forward axis (4096 = 1.0) */
#define SOLDIER_MOUNT_BONE SOLDIER_BONE_FIST_R
#define SOLDIER_MOUNT_X -38
#define SOLDIER_MOUNT_Y 0
#define SOLDIER_MOUNT_Z 0
#define SOLDIER_MOUNT_DX -4096
#define SOLDIER_MOUNT_DY 0
#define SOLDIER_MOUNT_DZ 0
#ifndef SOLDIER_MESH_NO_DATA

static const DBONE soldier_bones[] = {
  {"pelvis", -1, 0, -993, 0, 0, 32, 0, 22},
  {"torso", 0, 0, -154, 12, 32, 90, 22, 60},
  {"neck", 1, 0, -389, -17, 122, 12, 82, 6},
  {"head", 2, 0, -123, 10, 134, 97, 88, 79},
  {"upper_arm_L", 1, 210, -364, -12, 231, 31, 167, 24},
  {"forearm_L", 4, 282, 0, -10, 262, 24, 191, 12},
  {"fist_L", 5, 276, 0, 10, 286, 9, 203, 8},
  {"upper_arm_R", 1, -210, -364, -12, 295, 31, 211, 24},
  {"forearm_R", 7, -282, 0, -10, 326, 24, 235, 12},
  {"fist_R", 8, -276, 0, 10, 350, 9, 247, 8},
  {"thigh_L", 0, 87, 0, 0, 359, 24, 255, 12},
  {"calf_L", 10, 51, 440, 36, 383, 32, 267, 18},
  {"hoof_L", 11, 15, 389, -15, 415, 18, 285, 16},
  {"thigh_R", 0, -87, 0, 0, 433, 24, 301, 12},
  {"calf_R", 13, -51, 440, 36, 457, 32, 313, 18},
  {"hoof_R", 14, -15, 389, -15, 489, 18, 331, 16},
};
#endif /* SOLDIER_MESH_NO_DATA */
