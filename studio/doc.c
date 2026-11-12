/* The Studio's project document (see doc.h). */
#include "doc.h"
#include "fsm.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void doc_copy(char *d, const char *s, int n) { int i; for (i = 0; i < n - 1 && s && s[i]; i++) d[i] = s[i]; d[i] = 0; }
static int eqi(const char *a, const char *b)
{
    while (*a && *b) { char x = *a, y = *b; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return 0; a++; b++; }
    return *a == *b;
}

/* split a line into tokens (quoted strings kept with their quotes) */
#define MT 16
static char tk[MT][96];
static int ntk;
static void split(const char *s, int n)
{
    int i = 0;
    ntk = 0;
    while (i < n && ntk < MT) {
        int k = 0;
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) i++;
        if (i >= n || s[i] == '#') break;
        if (s[i] == '"') {
            tk[ntk][k++] = '"'; i++;
            while (i < n && s[i] != '"' && k < 93) tk[ntk][k++] = s[i++];
            tk[ntk][k++] = '"'; if (i < n) i++;
        } else while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '#' && k < 95) tk[ntk][k++] = s[i++];
        tk[ntk][k] = 0;
        ntk++;
    }
}
static void join_from(char *out, int n, int from)
{
    int i, o = 0;
    out[0] = 0;
    for (i = from; i < ntk; i++) {
        int k;
        if (i > from && o < n - 1) out[o++] = ' ';
        for (k = 0; tk[i][k] && o < n - 1; k++) out[o++] = tk[i][k];
    }
    out[o] = 0;
}

/* machines without saved positions: columns by distance from the start state, in link order */
static void auto_layout(DocFsm *m)
{
    int depth[DOC_STATES], q[DOC_STATES], qh = 0, qt = 0, i, k, rows[DOC_STATES], maxd = 0;
    for (i = 0; i < m->ns; i++) depth[i] = -1;
    if (!m->ns) return;
    depth[0] = 0; q[qt++] = 0;
    while (qh < qt) {
        int s = q[qh++];
        for (k = 0; k < m->s[s].nt; k++) {
            int t = doc_state_index(m, m->s[s].t[k].to);
            if (t >= 0 && depth[t] < 0) { depth[t] = depth[s] + 1; q[qt++] = t; if (depth[t] > maxd) maxd = depth[t]; }
        }
    }
    for (i = 0; i < m->ns; i++) if (depth[i] < 0) { depth[i] = ++maxd; q[qt++] = i; }
    memset(rows, 0, sizeof(rows));
    {   /* states that lead nowhere (dead, done) sit along the bottom */
        int sinks = 0, deepest = 0;
        for (k = 0; k < qt; k++) { i = q[k]; if (m->s[i].nt || i == 0) { m->s[i].x = 20 + depth[i] * 150.0f; m->s[i].y = 20 + rows[depth[i]]++ * 74.0f; } }
        for (k = 0; k < DOC_STATES; k++) if (rows[k] > deepest) deepest = rows[k];
        for (k = 0; k < qt; k++) { i = q[k]; if (!m->s[i].nt && i != 0) { m->s[i].x = 20 + sinks++ * 150.0f + 75; m->s[i].y = 20 + deepest * 74.0f + 20; } }
    }
}

int doc_parse(Doc *d, const char *text, int len)
{
    int placed_all = 1;
    int pos = 0, type = -1, fsm = -1, auto_i = 0;
    DocState *st = 0;
    char file[32];
    memcpy(file, d->file, sizeof(file));
    memset(d, 0, sizeof(*d));
    memcpy(d->file, file, sizeof(file));
    while (pos < len) {
        int end = pos;
        while (end < len && text[end] != '\n') end++;
        split(text + pos, end - pos);
        pos = end + 1;
        if (!ntk) continue;
        if (fsm >= 0) {
            DocFsm *m = &d->fsms[fsm];
            if (eqi(tk[0], "end")) { if (!placed_all) auto_layout(m); fsm = -1; st = 0; continue; }
            if (eqi(tk[0], "state") && ntk > 1 && m->ns < DOC_STATES) {
                st = &m->s[m->ns++];
                doc_copy(st->name, tk[1], DOC_STR);
                if (ntk > 4 && eqi(tk[2], "at")) { st->x = (float)atof(tk[3]); st->y = (float)atof(tk[4]); }
                else { st->x = 40 + (auto_i % 3) * 180.0f; st->y = 30 + (auto_i / 3) * 110.0f; placed_all = 0; }
                auto_i++;
                continue;
            }
            if (!st) continue;
            if (eqi(tk[0], "on") && ntk > 2) doc_add_trans(st, tk[1], tk[2]);
            else if ((eqi(tk[0], "do") || eqi(tk[0], "every")) && ntk > 1) {
                int a = doc_add_act(st, tk[1], eqi(tk[0], "every")), k;
                if (a >= 0) for (k = 0; k < 4 && k + 2 < ntk; k++) doc_copy(st->a[a].arg[k], tk[k + 2], DOC_STR);
            }
            continue;
        }
        if (type >= 0) {
            DocType *t = &d->types[type];
            if (eqi(tk[0], "end")) { type = -1; continue; }
            if (t->np < DOC_PROPS) join_from(t->p[t->np++].line, 96, 0);
            continue;
        }
        if (eqi(tk[0], "type") && ntk > 1 && d->ntypes < DOC_TYPES) { type = d->ntypes++; doc_copy(d->types[type].name, tk[1], DOC_STR); continue; }
        if (eqi(tk[0], "fsm") && ntk > 1 && d->nfsms < DOC_FSMS) { fsm = d->nfsms++; doc_copy(d->fsms[fsm].name, tk[1], DOC_STR); auto_i = 0; placed_all = 1; continue; }
        if (eqi(tk[0], "game") && ntk > 1) { char t[96]; join_from(t, 96, 1); if (t[0] == '"') { doc_copy(d->title, t + 1, 48); d->title[strlen(d->title) ? strlen(d->title) - 1 : 0] = 0; } else doc_copy(d->title, t, 48); }
        if (d->nhead < DOC_HEAD) join_from(d->head[d->nhead++].line, 96, 0);
    }
    return d->nhead > 0 || d->nfsms > 0 || d->ntypes > 0;
}

static int put(char *o, int cap, int n, const char *s) { while (*s && n < cap - 1) o[n++] = *s++; o[n] = 0; return n; }

int doc_write(const Doc *d, char *o, int cap)
{
    int n = 0, i, j, k, a;
    char b[160];
    o[0] = 0;
    n = put(o, cap, n, "# made with Deadbird Studio\n");
    for (i = 0; i < d->nhead; i++) { n = put(o, cap, n, d->head[i].line); n = put(o, cap, n, "\n"); }
    for (i = 0; i < d->ntypes; i++) {
        const DocType *t = &d->types[i];
        snprintf(b, sizeof(b), "\ntype %s\n", t->name); n = put(o, cap, n, b);
        for (j = 0; j < t->np; j++) { n = put(o, cap, n, "  "); n = put(o, cap, n, t->p[j].line); n = put(o, cap, n, "\n"); }
        n = put(o, cap, n, "end\n");
    }
    for (i = 0; i < d->nfsms; i++) {
        const DocFsm *m = &d->fsms[i];
        snprintf(b, sizeof(b), "\nfsm %s\n", m->name); n = put(o, cap, n, b);
        for (j = 0; j < m->ns; j++) {
            const DocState *s = &m->s[j];
            snprintf(b, sizeof(b), "  state %s at %d %d\n", s->name, (int)s->x, (int)s->y); n = put(o, cap, n, b);
            for (k = 0; k < s->na; k++) {
                const DocAct *x = &s->a[k];
                snprintf(b, sizeof(b), "    %s %s", x->every ? "every" : "do", x->action); n = put(o, cap, n, b);
                for (a = 0; a < 4 && x->arg[a][0]; a++) { n = put(o, cap, n, " "); n = put(o, cap, n, x->arg[a]); }
                n = put(o, cap, n, "\n");
            }
            for (k = 0; k < s->nt; k++) { snprintf(b, sizeof(b), "    on %s %s\n", s->t[k].event, s->t[k].to); n = put(o, cap, n, b); }
        }
        n = put(o, cap, n, "end\n");
    }
    return n;
}

int doc_state_index(const DocFsm *m, const char *name) { int i; for (i = 0; i < m->ns; i++) if (!strcmp(m->s[i].name, name)) return i; return -1; }

int doc_add_state(DocFsm *m, const char *base, float x, float y)
{
    char nm[DOC_STR];
    int k = 1;
    DocState *s;
    if (m->ns >= DOC_STATES) return -1;
    doc_copy(nm, base, DOC_STR);
    while (doc_state_index(m, nm) >= 0) snprintf(nm, sizeof(nm), "%s%d", base, ++k);
    s = &m->s[m->ns];
    memset(s, 0, sizeof(*s));
    doc_copy(s->name, nm, DOC_STR);
    s->x = x; s->y = y;
    return m->ns++;
}

void doc_del_state(DocFsm *m, int i)
{
    int j, k;
    char nm[DOC_STR];
    if (i < 0 || i >= m->ns) return;
    doc_copy(nm, m->s[i].name, DOC_STR);
    for (j = i; j + 1 < m->ns; j++) m->s[j] = m->s[j + 1];
    m->ns--;
    for (j = 0; j < m->ns; j++)                          /* links into it go too */
        for (k = m->s[j].nt - 1; k >= 0; k--) if (!strcmp(m->s[j].t[k].to, nm)) doc_del_trans(&m->s[j], k);
}

void doc_rename_state(DocFsm *m, int i, const char *name)
{
    int j, k;
    char old[DOC_STR];
    doc_copy(old, m->s[i].name, DOC_STR);
    doc_copy(m->s[i].name, name, DOC_STR);
    for (j = 0; j < m->ns; j++) {
        for (k = 0; k < m->s[j].nt; k++) if (!strcmp(m->s[j].t[k].to, old)) doc_copy(m->s[j].t[k].to, name, DOC_STR);
        for (k = 0; k < m->s[j].na; k++) if (!strcmp(m->s[j].a[k].action, "Goto") && !strcmp(m->s[j].a[k].arg[0], old)) doc_copy(m->s[j].a[k].arg[0], name, DOC_STR);
    }
}

int doc_add_trans(DocState *s, const char *ev, const char *to)
{
    if (s->nt >= DOC_TRANS) return -1;
    doc_copy(s->t[s->nt].event, ev, DOC_STR);
    doc_copy(s->t[s->nt].to, to, DOC_STR);
    return s->nt++;
}
void doc_del_trans(DocState *s, int i) { int j; if (i < 0 || i >= s->nt) return; for (j = i; j + 1 < s->nt; j++) s->t[j] = s->t[j + 1]; s->nt--; }

int doc_add_act(DocState *s, const char *action, int every)
{
    DocAct *a;
    if (s->na >= DOC_ACTS) return -1;
    a = &s->a[s->na];
    memset(a, 0, sizeof(*a));
    doc_copy(a->action, action, DOC_STR);
    a->every = every;
    return s->na++;
}
void doc_del_act(DocState *s, int i) { int j; if (i < 0 || i >= s->na) return; for (j = i; j + 1 < s->na; j++) s->a[j] = s->a[j + 1]; s->na--; }
void doc_move_act(DocState *s, int i, int dir)
{
    int j = i + dir;
    DocAct t;
    if (i < 0 || i >= s->na || j < 0 || j >= s->na) return;
    t = s->a[i]; s->a[i] = s->a[j]; s->a[j] = t;
}

int doc_events(const Doc *d, char (*out)[DOC_STR], int max)
{
    static const char *const B[] = { "FINISHED", "HIT", "DIED", "TOUCH", "LANDED", "WALL", "SPAWNED", "TIMER" };
    int n = 0, i, j, k, a;
    for (i = 0; i < 8 && n < max; i++) doc_copy(out[n++], B[i], DOC_STR);
    #define ADD(s) do { int _q; for (_q = 0; _q < n; _q++) if (!strcmp(out[_q], (s))) break; if (_q == n && n < max && (s)[0]) doc_copy(out[n++], (s), DOC_STR); } while (0)
    for (i = 0; i < d->nfsms; i++)
        for (j = 0; j < d->fsms[i].ns; j++) {
            const DocState *s = &d->fsms[i].s[j];
            for (k = 0; k < s->nt; k++) ADD(s->t[k].event);
            for (k = 0; k < s->na; k++) {
                int id = fw_action_find(s->a[k].action);
                if (id < 0) continue;
                for (a = 0; a < fw_actions[id].nargs && a < 4; a++)
                    if (fw_actions[id].type[a] == ARG_EVENT) ADD(s->a[k].arg[a]);
            }
        }
    return n;
}

/* ---- type properties */
static int find_key(const DocType *t, const char *key, int *line, int *tokn)
{
    int i, k;
    for (i = 0; i < t->np; i++) {
        split(t->p[i].line, (int)strlen(t->p[i].line));
        for (k = 0; k < ntk; k++) if (eqi(tk[k], key)) { *line = i; *tokn = k; return 1; }
    }
    return 0;
}
int doc_type_num(const DocType *t, const char *key, int *val)
{
    int l, k;
    if (!find_key(t, key, &l, &k) || k + 1 >= ntk) return 0;
    *val = atoi(tk[k + 1]);
    return 1;
}
const char *doc_type_word(const DocType *t, const char *key)
{
    static char w[96];
    int l, k;
    w[0] = 0;
    if (find_key(t, key, &l, &k) && k + 1 < ntk) doc_copy(w, tk[k + 1], 96);
    return w;
}
void doc_type_set_num(DocType *t, const char *key, int val)
{
    int l, k, i, o = 0;
    char out[96];
    if (!find_key(t, key, &l, &k)) {
        if (t->np < DOC_PROPS) snprintf(t->p[t->np++].line, 96, "%s %d", key, val);
        return;
    }
    snprintf(tk[k + 1 < ntk ? k + 1 : k], 96, "%d", val);
    if (k + 1 >= ntk) { snprintf(tk[ntk++], 96, "%d", val); }
    for (i = 0; i < ntk; i++) { int q; if (i) out[o++] = ' '; for (q = 0; tk[i][q] && o < 94; q++) out[o++] = tk[i][q]; }
    out[o] = 0;
    doc_copy(t->p[l].line, out, 96);
}
void doc_type_set_line(DocType *t, const char *key, const char *rest)
{
    int i;
    for (i = 0; i < t->np; i++) {
        split(t->p[i].line, (int)strlen(t->p[i].line));
        if (ntk && eqi(tk[0], key)) { snprintf(t->p[i].line, 96, "%s %s", key, rest); return; }
    }
    if (t->np < DOC_PROPS) snprintf(t->p[t->np++].line, 96, "%s %s", key, rest);
}

const char *doc_head_word(const Doc *d, const char *key, int nth)
{
    static char w[96];
    int i;
    w[0] = 0;
    for (i = 0; i < d->nhead; i++) {
        split(d->head[i].line, (int)strlen(d->head[i].line));
        if (ntk && eqi(tk[0], key)) { if (nth < ntk) doc_copy(w, tk[nth], 96); return w; }
    }
    return w;
}
void doc_head_set(Doc *d, const char *key, const char *rest)
{
    int i;
    for (i = 0; i < d->nhead; i++) {
        split(d->head[i].line, (int)strlen(d->head[i].line));
        if (ntk && eqi(tk[0], key)) { snprintf(d->head[i].line, 96, "%s %s", key, rest); return; }
    }
    if (d->nhead < DOC_HEAD) snprintf(d->head[d->nhead++].line, 96, "%s %s", key, rest);
}
