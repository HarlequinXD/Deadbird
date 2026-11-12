/*
 * Game projects as text: the whole game in one file the editor writes and the runtime reads.
 *
 *   game "HELL GATE"
 *   camera first                      # or third
 *   player walk 40 run 72 jump 70 gravity 5 eye 1450 hp 100 turn 70 height 1700 radius 300
 *   weapon hitscan 20000 damage 10 rate 12        # or: weapon shot <type> speed 160 damage 20 rate 15
 *
 *   type imp                          # an actor type
 *     color 200 40 20
 *     shape pillar                    # box orb pillar none
 *     size 600 radius 300 height 1500 hp 30 speed 30 damage 10 life 0
 *     flags solid gravity visible     # + pickup projectile
 *     tags enemy                      # any names: enemy item shot boss ...
 *     thing trooper                   # placed by the level wherever this map thing is
 *     fsm imp_ai
 *   end
 *
 *   fsm imp_ai
 *     state idle
 *       every IfSeePlayer 9000 SEEN   # "every": each tick; "do": once, in order
 *       on SEEN chase                 # event -> state
 *     state chase
 *       every ChasePlayer 34
 *       ...
 *   end
 *
 * Action arguments are numbers, or names for events, types, sounds (SHOOT HIT ...), buttons
 * (CROSS SQUARE ...), "quoted text", variables (v0..v7), globals (g0..g15), comparisons
 * (< <= == != >= >), targets (self player all enemies owner) and states.
 */
#include "fsm.h"
#include "fwsfx.h"
#include "fwproj.h"

static char err[96];
static int line_no;
const char *fw_project_error(void) { return err; }

static void fail(const char *what, const char *tok)
{
    int i = 0, k;
    if (err[0]) return;
    {   char n[12]; int l = line_no, d = 0; do { n[d++] = (char)('0' + l % 10); l /= 10; } while (l && d < 10);
        err[i++] = 'L'; err[i++] = 'I'; err[i++] = 'N'; err[i++] = 'E'; err[i++] = ' ';
        while (d) err[i++] = n[--d];
        err[i++] = ':'; err[i++] = ' '; }
    for (k = 0; what[k] && i < 70; k++) err[i++] = what[k];
    if (tok) { err[i++] = ' '; for (k = 0; tok[k] && i < 94; k++) err[i++] = tok[k]; }
    err[i] = 0;
}

/* ---- tokens: words, numbers, "quoted strings" */
#define MAXTOK 24
static char tok[MAXTOK][40];
static int ntok;
static void tokenize(const char *s, int n)
{
    int i = 0;
    ntok = 0;
    while (i < n && ntok < MAXTOK) {
        int k = 0;
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) i++;
        if (i >= n || s[i] == '#') break;
        if (s[i] == '"') {
            i++;
            while (i < n && s[i] != '"' && k < 39) tok[ntok][k++] = s[i++];
            if (i < n) i++;
        } else while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '#' && k < 39) tok[ntok][k++] = s[i++];
        tok[ntok][k] = 0;
        ntok++;
    }
}
static int eq(const char *a, const char *b)
{
    while (*a && *b) { char x = *a, y = *b; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return 0; a++; b++; }
    return *a == *b;
}
static int is_num(const char *s) { if (*s == '-' || *s == '+') s++; if (!*s) return 0; while (*s) { if (*s < '0' || *s > '9') return 0; s++; } return 1; }
static s32 num(const char *s) { s32 v = 0; int neg = *s == '-'; if (*s == '-' || *s == '+') s++; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return neg ? -v : v; }

/* ---- tag names */
static char tag_name[32][16];
static int ntags;
static u32 tag_bit(const char *s)
{
    int i;
    for (i = 0; i < ntags; i++) if (eq(tag_name[i], s)) return 1u << i;
    if (ntags >= 32) return 0;
    for (i = 0; i < 15 && s[i]; i++) tag_name[ntags][i] = s[i];
    tag_name[ntags][i] = 0;
    return 1u << ntags++;
}

/* ---- action arguments */
static const struct { const char *n; u32 b; } BUTTONS[] = {
    { "UP", PAD_UP }, { "DOWN", PAD_DOWN }, { "LEFT", PAD_LEFT }, { "RIGHT", PAD_RIGHT }, { "CROSS", PAD_CROSS },
    { "CIRCLE", PAD_CIRCLE }, { "SQUARE", PAD_SQUARE }, { "TRIANGLE", PAD_TRIANGLE }, { "L1", PAD_L1 }, { "R1", PAD_R1 },
    { "L2", PAD_L2 }, { "R2", PAD_R2 }, { "START", PAD_START }, { "SELECT", PAD_SELECT },
};
static const char *const OPS[] = { "==", "!=", "<", "<=", ">", ">=" };
static const char *const OPN[] = { "EQ", "NE", "LT", "LE", "GT", "GE" };
static const char *const TARGETS[] = { "SELF", "PLAYER", "ALL", "ENEMIES", "OWNER" };

/* Goto names a state that may come later: patched when the machine ends */
static struct { int act; char name[16]; } gotos[64];
static int ngotos;

static int arg(int type, const char *s, s32 *out, int act_index)
{
    int i;
    if (is_num(s) && type != ARG_TEXT) { *out = num(s); return 1; }
    switch (type) {
    case ARG_EVENT: *out = fw_event_id(s); return *out >= 0;
    case ARG_PROTO: *out = fw_proto_add(s); return *out >= 0;
    case ARG_SOUND: for (i = 0; i < FWS_COUNT; i++) if (eq(fw_sound_names[i], s)) { *out = i; return 1; } return 0;
    case ARG_BUTTON: for (i = 0; i < (int)(sizeof(BUTTONS) / sizeof(BUTTONS[0])); i++) if (eq(BUTTONS[i].n, s)) { *out = (s32)BUTTONS[i].b; return 1; } return 0;
    case ARG_TEXT: *out = fw_text_add(s); return 1;
    case ARG_VAR: if ((s[0] == 'v' || s[0] == 'V') && is_num(s + 1)) { *out = num(s + 1); return *out < FW_VARS; } return 0;
    case ARG_GLOBAL: if ((s[0] == 'g' || s[0] == 'G') && is_num(s + 1)) { *out = num(s + 1); return *out < FW_GLOBALS; } return 0;
    case ARG_OP: for (i = 0; i < 6; i++) if (eq(OPS[i], s) || eq(OPN[i], s)) { *out = i; return 1; } return 0;
    case ARG_TARGET: for (i = 0; i < 5; i++) if (eq(TARGETS[i], s)) { *out = i; return 1; } return 0;
    case ARG_STATE:
        if (ngotos >= 64) return 0;
        gotos[ngotos].act = act_index;
        for (i = 0; i < 15 && s[i]; i++) gotos[ngotos].name[i] = s[i];
        gotos[ngotos].name[i] = 0;
        ngotos++;
        *out = -1;
        return 1;
    }
    return 0;
}

static int thing_type(const char *s)
{
    if (is_num(s)) return (int)num(s);
    return fw_world() && fw_world()->thing_type ? fw_world()->thing_type(s) : -1;   /* the world names its things */
}

/* ------------------------------------------------------------------ the loader */
int fw_project_load(const char *text, int len, FwProject *out)
{
    int pos = 0, proto = -1, in_fsm = 0;
    err[0] = 0; line_no = 0; ntags = 0; ngotos = 0;
    tag_bit("enemy"); tag_bit("item"); tag_bit("shot");            /* bits 1, 2, 4 */
    fw_init();
    memset(out, 0, sizeof(*out));
    out->title[0] = 0;
    while (pos < len && !err[0]) {
        int end = pos, a;
        while (end < len && text[end] != '\n') end++;
        line_no++;
        tokenize(text + pos, end - pos);
        pos = end + 1;
        if (!ntok) continue;
        /* ---- inside a machine */
        if (in_fsm) {
            if (eq(tok[0], "end")) {
                int i;
                for (i = 0; i < ngotos; i++) {
                    int s = fsm_state_find(fsm_nm - 1, gotos[i].name);
                    if (s < 0) { fail("no state named", gotos[i].name); break; }
                    fsm_a[gotos[i].act].arg[0] = s;
                }
                ngotos = 0;
                if (!fsm_end()) fail("a transition names a missing state", 0);
                in_fsm = 0;
            } else if (eq(tok[0], "state") && ntok > 1) {
                if (fsm_state(tok[1]) < 0) fail("too many states", 0);
            } else if (eq(tok[0], "on") && ntok > 2) {
                int ev = fw_event_id(tok[1]);
                if (ev < 0 || fsm_add_trans(ev, tok[2]) < 0) fail("bad transition", tok[1]);
            } else if ((eq(tok[0], "do") || eq(tok[0], "every")) && ntok > 1) {
                int id = fw_action_find(tok[1]), k;
                s32 args[FSM_ARGS] = { 0, 0, 0, 0 };
                if (id < 0) { fail("unknown action", tok[1]); break; }
                for (k = 0; k < fw_actions[id].nargs && k + 2 < ntok; k++)
                    if (!arg(fw_actions[id].type[k], tok[k + 2], &args[k], fsm_na)) { fail("bad argument", tok[k + 2]); break; }
                if (k < fw_actions[id].nargs && !err[0]) fail("missing arguments for", tok[1]);
                if (fsm_add_action(id, eq(tok[0], "every"), args) < 0) fail("too many actions", 0);
            } else fail("expected state / do / every / on / end, got", tok[0]);
            continue;
        }
        /* ---- inside an actor type */
        if (proto >= 0) {
            FwProto *p = fw_proto(proto);
            if (eq(tok[0], "end")) { proto = -1; continue; }
            if (eq(tok[0], "color") && ntok > 3) { p->r = (u8)num(tok[1]); p->g = (u8)num(tok[2]); p->b = (u8)num(tok[3]); continue; }
            if (eq(tok[0], "shape") && ntok > 1) {
                p->shape = eq(tok[1], "orb") ? SHAPE_ORB : eq(tok[1], "pillar") ? SHAPE_PILLAR : eq(tok[1], "none") ? SHAPE_NONE : SHAPE_BOX;
                continue;
            }
            if (eq(tok[0], "flags")) {
                p->flags = 0;
                for (a = 1; a < ntok; a++)
                    p->flags |= (u16)(eq(tok[a], "solid") ? FA_SOLID : eq(tok[a], "gravity") ? FA_GRAVITY : eq(tok[a], "visible") ? FA_VISIBLE :
                                      eq(tok[a], "pickup") ? FA_PICKUP : eq(tok[a], "projectile") ? FA_PROJECTILE : 0);
                continue;
            }
            if (eq(tok[0], "tags")) { p->tags = 0; for (a = 1; a < ntok; a++) p->tags |= tag_bit(tok[a]); continue; }
            if (eq(tok[0], "thing") && ntok > 1) { p->thing = (s16)thing_type(tok[1]); if (p->thing < 0) fail("unknown thing", tok[1]); continue; }
            if (eq(tok[0], "fsm") && ntok > 1) { out->proto_fsm[proto][0] = 1; { int i; for (i = 0; i < 15 && tok[1][i]; i++) out->proto_fsm_name[proto][i] = tok[1][i]; out->proto_fsm_name[proto][i] = 0; } continue; }
            /* name value pairs */
            for (a = 0; a + 1 < ntok; a += 2) {
                s32 v = num(tok[a + 1]);
                if (eq(tok[a], "size")) p->size = (s16)v;
                else if (eq(tok[a], "radius")) p->radius = (s16)v;
                else if (eq(tok[a], "height")) p->height = (s16)v;
                else if (eq(tok[a], "hp")) p->hp = v;
                else if (eq(tok[a], "speed")) p->speed = (s16)v;
                else if (eq(tok[a], "damage")) p->damage = (s16)v;
                else if (eq(tok[a], "life")) p->life = (s16)v;
                else { fail("unknown property", tok[a]); break; }
            }
            continue;
        }
        /* ---- top level */
        if (eq(tok[0], "game") && ntok > 1) { int i; for (i = 0; i < 31 && tok[1][i]; i++) out->title[i] = tok[1][i]; out->title[i] = 0; }
        else if (eq(tok[0], "camera") && ntok > 1) fw_player_cfg.mode = eq(tok[1], "third") ? CAM_THIRD_PERSON : CAM_FIRST_PERSON;
        else if (eq(tok[0], "level") && ntok > 1) { int i; for (i = 0; i < 31 && tok[ntok - 1][i]; i++) out->level[i] = tok[ntok - 1][i]; out->level[i] = 0; }
        else if (eq(tok[0], "player")) {
            FwProto *p = fw_proto(fw_proto_add("player"));
            p->shape = SHAPE_PILLAR; p->height = 1700; p->hp = 100; p->flags = FA_SOLID | FA_GRAVITY;
            for (a = 1; a + 1 < ntok; a += 2) {
                s32 v = num(tok[a + 1]);
                FwPlayerCfg *c = &fw_player_cfg;
                if (eq(tok[a], "walk")) c->walk = (s16)v;
                else if (eq(tok[a], "run")) c->run = (s16)v;
                else if (eq(tok[a], "jump")) c->jump = (s16)v;
                else if (eq(tok[a], "gravity")) c->gravity = (s16)v;
                else if (eq(tok[a], "eye")) c->eye = (s16)v;
                else if (eq(tok[a], "turn")) c->turn = (s16)v;
                else if (eq(tok[a], "camdist")) c->cam_dist = (s16)v;
                else if (eq(tok[a], "camheight")) c->cam_height = (s16)v;
                else if (eq(tok[a], "hp")) p->hp = v;
                else if (eq(tok[a], "height")) p->height = (s16)v;
                else if (eq(tok[a], "radius")) p->radius = (s16)v;
                else { fail("unknown player property", tok[a]); break; }
            }
        }
        else if (eq(tok[0], "weapon") && ntok > 1) {
            FwPlayerCfg *c = &fw_player_cfg;
            a = 1;
            if (eq(tok[1], "hitscan") && ntok > 2) { c->range = num(tok[2]); c->shot_proto = -1; a = 3; }
            else if (eq(tok[1], "shot") && ntok > 2) { c->range = 0; c->shot_proto = (s16)fw_proto_add(tok[2]); a = 3; }
            for (; a + 1 < ntok; a += 2) {
                s32 v = num(tok[a + 1]);
                if (eq(tok[a], "damage")) c->damage = (s16)v;
                else if (eq(tok[a], "rate")) c->fire_rate = (s16)v;
                else if (eq(tok[a], "speed")) c->shot_speed = (s16)v;
                else { fail("unknown weapon property", tok[a]); break; }
            }
        }
        else if (eq(tok[0], "type") && ntok > 1) { proto = fw_proto_add(tok[1]); if (proto < 0) fail("too many types", 0); }
        else if (eq(tok[0], "fsm") && ntok > 1) { if (fsm_begin(tok[1]) < 0) fail("too many state machines", 0); in_fsm = 1; ngotos = 0; }
        else fail("unknown line", tok[0]);
    }
    if (!err[0] && (in_fsm || proto >= 0)) fail("missing end", 0);
    /* types name their machines: resolve now that all are read */
    if (!err[0]) {
        int i;
        for (i = 0; i < fw_proto_count(); i++) {
            fw_proto(i)->fsm = -1;
            if (out->proto_fsm[i][0]) {
                fw_proto(i)->fsm = (s16)fsm_find(out->proto_fsm_name[i]);
                if (fw_proto(i)->fsm < 0) { fail("no state machine named", out->proto_fsm_name[i]); break; }
            }
        }
    }
    return err[0] == 0;
}

