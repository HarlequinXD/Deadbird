/* Level files for the headless build: plain files in ./levels (or $DEADBIRD_LEVELS). */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "editor.h"

static const char *dir(void) { const char *d = getenv("DEADBIRD_LEVELS"); return d ? d : "levels"; }
static void path(char *o, const char *name) { snprintf(o, 512, "%s/%s", dir(), name); }
const char *plat_file_where(void) { return dir(); }
int plat_file_save(const char *name, const char *data, int len)
{
    char p[512]; FILE *f;
    mkdir(dir(), 0755);
    path(p, name);
    f = fopen(p, "wb");
    if (!f) return 0;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return 1;
}
int plat_file_load(const char *name, char *buf, int cap)
{
    char p[512]; FILE *f; size_t n;
    path(p, name);
    f = fopen(p, "rb");
    if (!f) return -1;
    n = fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return (int)n;
}
int plat_file_list(char (*names)[32], int max)
{
    DIR *d = opendir(dir());
    struct dirent *e;
    int n = 0;
    if (!d) return 0;
    while ((e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l > 4 && l < 31 && !strcmp(e->d_name + l - 4, ".map")) { strcpy(names[n], e->d_name); n++; }
    }
    closedir(d);
    /* sorted by name */
    {
        int i, j; char t[32];
        for (i = 1; i < n; i++) for (j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) { strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t); }
    }
    return n;
}
int plat_file_delete(const char *name) { char p[512]; path(p, name); return remove(p) == 0; }
