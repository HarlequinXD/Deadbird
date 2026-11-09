/* PS1 arm chainsaw (tools/build_chainsaw.py): vertices local to the right-hand mount
   (DEMON_MOUNT_*), same axes as the demon model. Bone 0 = body, bones 1..4 = the chain
   with the 4 animation frames (same vertices, UVs one strip frame apart): draw one of them. */
#ifndef DMODEL_TYPES_DEFINED
#define DMODEL_TYPES_DEFINED
typedef struct { short x, y, z, pad; } DVERT;
typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;
typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;
#endif
#define SAW_NVERTS 262
#define SAW_NPRIMS 260
#define SAW_NBONES 5
#define SAW_CHAIN_FRAMES 4

static const DBONE saw_bones[] = {
  {"body", -1, 0, 0, 0, 0, 184, 0, 116},
  {"chain0", -1, 0, 0, 0, 184, 78, 116, 36},
  {"chain1", -1, 0, 0, 0, 184, 78, 152, 36},
  {"chain2", -1, 0, 0, 0, 184, 78, 188, 36},
  {"chain3", -1, 0, 0, 0, 184, 78, 224, 36},
};
