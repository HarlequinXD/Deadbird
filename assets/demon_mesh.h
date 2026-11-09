/* PS1 demon, rigid hierarchical parts: 281 verts, 314 prims (488 tris), 16 bones.
   Fixed point: 1 model unit = 1024 (4096 = 1.0), Y down, faces +Z.
   Vertices are LOCAL to their bone pivot; bone offsets are relative to the parent pivot.
   Render each bone with  world = parent_world * T(offset) * R(bone)  and draw its prim range. */
#ifndef DMODEL_TYPES_DEFINED
#define DMODEL_TYPES_DEFINED
typedef struct { short x, y, z, pad; } DVERT;
typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;
typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;
#endif
#define DEMON_NVERTS 281
#define DEMON_NPRIMS 314
#define DEMON_NBONES 16
#define DEMON_BONE_PELVIS 0
#define DEMON_BONE_TORSO 1
#define DEMON_BONE_NECK 2
#define DEMON_BONE_HEAD 3
#define DEMON_BONE_UPPER_ARM_L 4
#define DEMON_BONE_FOREARM_L 5
#define DEMON_BONE_FIST_L 6
#define DEMON_BONE_UPPER_ARM_R 7
#define DEMON_BONE_FOREARM_R 8
#define DEMON_BONE_FIST_R 9
#define DEMON_BONE_THIGH_L 10
#define DEMON_BONE_CALF_L 11
#define DEMON_BONE_HOOF_L 12
#define DEMON_BONE_THIGH_R 13
#define DEMON_BONE_CALF_R 14
#define DEMON_BONE_HOOF_R 15
/* right-hand weapon mount: local to bone fist_R, plus forward axis (4096 = 1.0) */
#define DEMON_MOUNT_BONE DEMON_BONE_FIST_R
#define DEMON_MOUNT_X -33
#define DEMON_MOUNT_Y 0
#define DEMON_MOUNT_Z 1
#define DEMON_MOUNT_DX -4094
#define DEMON_MOUNT_DY 0
#define DEMON_MOUNT_DZ 141
#ifndef DEMON_MESH_NO_DATA

static const DBONE demon_bones[] = {
  {"pelvis", -1, 0, -993, 0, 0, 25, 0, 30},
  {"torso", 0, 0, -154, 12, 25, 46, 30, 56},
  {"neck", 1, 0, -389, -17, 71, 10, 86, 12},
  {"head", 2, 0, -123, 10, 81, 40, 98, 36},
  {"upper_arm_L", 1, 174, -369, -12, 121, 14, 134, 16},
  {"forearm_L", 4, 282, 0, -10, 135, 14, 150, 16},
  {"fist_L", 5, 297, 0, 10, 149, 9, 166, 9},
  {"upper_arm_R", 1, -174, -369, -12, 158, 14, 175, 16},
  {"forearm_R", 7, -282, 0, -10, 172, 14, 191, 16},
  {"fist_R", 8, -297, 0, 10, 186, 9, 207, 9},
  {"thigh_L", 0, 87, 0, 0, 195, 17, 216, 20},
  {"calf_L", 10, 51, 440, 36, 212, 17, 236, 20},
  {"hoof_L", 11, 15, 389, -15, 229, 9, 256, 9},
  {"thigh_R", 0, -87, 0, 0, 238, 17, 265, 20},
  {"calf_R", 13, -51, 440, 36, 255, 17, 285, 20},
  {"hoof_R", 14, -15, 389, -15, 272, 9, 305, 9},
};
#endif /* DEMON_MESH_NO_DATA */
