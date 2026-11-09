/* sci-fi laser pistol (tools/build_pistol.py): vertices local to the trooper's right-hand mount
   (SOLDIER_MOUNT_*), same axes as the soldier model. One bone. */
#ifndef DMODEL_TYPES_DEFINED
#define DMODEL_TYPES_DEFINED
typedef struct { short x, y, z, pad; } DVERT;
typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;
typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;
#endif
#define PISTOL_NVERTS 56
#define PISTOL_NPRIMS 36
#define PISTOL_MUZZLE_X -211
#define PISTOL_MUZZLE_Y -56
#define PISTOL_MUZZLE_Z 0

static const DBONE pistol_bones[] = { {"pistol", -1, 0, 0, 0, 0, 56, 0, 36} };
