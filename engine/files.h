/* User files (saves, levels): plain files in one folder, supplied by each platform layer.
   The folder and the extension plat_file_list shows come from the game's App (files_dir,
   files_ext): Windows keeps it next to the exe, Android in the app's files folder (reachable
   over USB), headless in ./<files_dir> or $ENGINE_FILES. The PS1 has none. */
#ifndef FILES_H
#define FILES_H

int  plat_file_save(const char *name, const char *data, int len);    /* 1 = saved */
int  plat_file_load(const char *name, char *buf, int cap);           /* bytes read, -1 = none */
int  plat_file_list(char (*names)[32], int max);                     /* names with the extension, sorted */
int  plat_file_delete(const char *name);
const char *plat_file_where(void);                                   /* the folder, to show the player */

/* helpers for the platform layers */
const char *eng_files_dir(void);                                     /* game_app.files_dir or "files" */
const char *eng_files_ext(void);                                     /* game_app.files_ext or ".dat" */

#endif
