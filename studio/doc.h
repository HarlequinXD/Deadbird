/*
 * The Studio's editable copy of a game project (.game text, see engine/fw/project.c).
 * State machines are parsed into states, actions and transitions the editor changes; actor types
 * and the game settings are kept as property lines. doc_write turns it back into the text the
 * runtime plays. State positions in the graph are saved as "state NAME at X Y".
 */
#ifndef DOC_H
#define DOC_H

#define DOC_TYPES   32
#define DOC_FSMS    16
#define DOC_STATES  24
#define DOC_ACTS    16
#define DOC_TRANS   12
#define DOC_PROPS   16
#define DOC_HEAD    24
#define DOC_STR     24

typedef struct { char action[DOC_STR]; int every; char arg[4][DOC_STR]; } DocAct;
typedef struct { char event[DOC_STR]; char to[DOC_STR]; } DocTrans;
typedef struct { char name[DOC_STR]; float x, y; int na, nt; DocAct a[DOC_ACTS]; DocTrans t[DOC_TRANS]; } DocState;
typedef struct { char name[DOC_STR]; int ns; DocState s[DOC_STATES]; } DocFsm;
typedef struct { char line[96]; } DocLine;
typedef struct { char name[DOC_STR]; int np; DocLine p[DOC_PROPS]; } DocType;     /* its lines, verbatim */
typedef struct {
    char file[32];                 /* where it's saved */
    char title[48];
    int  nhead; DocLine head[DOC_HEAD];   /* top-level lines except types / fsms (game, camera, player, weapon, level) */
    int  ntypes; DocType types[DOC_TYPES];
    int  nfsms; DocFsm fsms[DOC_FSMS];
    int  dirty;
} Doc;

int  doc_parse(Doc *d, const char *text, int len);        /* 0 if it isn't a project */
int  doc_write(const Doc *d, char *out, int cap);          /* -> length */
/* edits */
int  doc_add_state(DocFsm *m, const char *base, float x, float y);   /* -> index or -1 */
void doc_del_state(DocFsm *m, int i);
void doc_rename_state(DocFsm *m, int i, const char *name);
int  doc_add_trans(DocState *s, const char *event, const char *to);
void doc_del_trans(DocState *s, int i);
int  doc_add_act(DocState *s, const char *action, int every);
void doc_del_act(DocState *s, int i);
void doc_move_act(DocState *s, int i, int dir);
int  doc_state_index(const DocFsm *m, const char *name);
/* the events a project uses (built-ins first) */
int  doc_events(const Doc *d, char (*out)[DOC_STR], int max);
/* type properties: "hp 30" -> get / set the number after a key, anywhere in its lines */
int  doc_type_num(const DocType *t, const char *key, int *val);
void doc_type_set_num(DocType *t, const char *key, int val);
const char *doc_type_word(const DocType *t, const char *key);   /* first word after key ("" none) */
void doc_type_set_line(DocType *t, const char *key, const char *rest);
/* game settings in the header: "camera first" ... */
const char *doc_head_word(const Doc *d, const char *key, int nth);
void doc_head_set(Doc *d, const char *key, const char *rest);

void doc_copy(char *d, const char *s, int n);

#endif
