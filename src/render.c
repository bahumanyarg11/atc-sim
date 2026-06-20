#include "render.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  layout                                                             */
/* ------------------------------------------------------------------ */

#define WIN_W      1360
#define WIN_H      900
#define SIDEBAR_W  360
#define CONSOLE_H  128
#define SCOPE_PAD  46
#define STAGE_H    (WIN_H - CONSOLE_H)   /* scope + sidebar height      */

/* ------------------------------------------------------------------ */
/*  palette — modern dark "ops" theme: deep navy + cyan accent         */
/* ------------------------------------------------------------------ */

static const Color BG_TOP     = { 13,  19,  33, 255 };
static const Color BG_BOT     = {  8,  12,  22, 255 };
static const Color SCOPE_BG   = { 12,  19,  34, 255 };
static const Color SCOPE_EDGE = { 34,  54,  86, 255 };
static const Color RING       = { 30,  52,  82, 255 };
static const Color RING_FAINT = { 21,  35,  58, 255 };
static const Color SWEEP      = { 56, 189, 248,  34 };

static const Color ACCENT     = { 56, 189, 248, 255 };  /* cyan        */
static const Color TEAL       = { 45, 212, 160, 255 };  /* departures  */
static const Color GROUND     = {167, 139, 250, 255 };  /* on ground   */
static const Color AMBER      = {245, 179,  66, 255 };  /* holding     */
static const Color ALERT      = {242,  84,  91, 255 };  /* emergency   */

static const Color INK        = {226, 233, 245, 255 };
static const Color INK_DIM    = {126, 144, 175, 255 };
static const Color PANEL      = { 16,  23,  39, 255 };
static const Color CONSOLE_BG = { 11,  17,  30, 255 };
static const Color CARD       = { 22,  31,  51, 255 };
static const Color CARD_LINE  = { 39,  54,  82, 255 };
static const Color WHITE_SEL  = {244, 248, 255, 255 };

/* ------------------------------------------------------------------ */
/*  small helpers                                                      */
/* ------------------------------------------------------------------ */

typedef struct { float cx, cy, radius; } Scope;

#define HIST_N 90
typedef struct { int buckets[HIST_N]; int n; double next_t; int last_dep; } Throughput;

static Color fade(Color c, unsigned char a) { c.a = a; return c; }

static void wall_clock(double sim_min, char *out, int n)
{
    int total = 360 + (int)sim_min;     /* simulated day starts 06:00 */
    int hh = (total / 60) % 24, mm = total % 60;
    snprintf(out, n, "%02d:%02d", hh, mm);
}

static Color wx_color(Weather w)
{
    return w == WX_STORM ? ALERT : w == WX_WINDY ? AMBER : TEAL;
}

static Scope scope_geometry(void)
{
    float avail_w = WIN_W - SIDEBAR_W;
    Scope s;
    s.cx = avail_w * 0.5f;
    s.cy = STAGE_H * 0.5f;
    s.radius = fminf(avail_w, (float)STAGE_H) * 0.5f - SCOPE_PAD;
    return s;
}

static Vector2 polar_to_screen(Scope sc, double bearing_deg, double range_nm)
{
    double rng = range_nm;
    if (rng > 10.0) rng = 10.0;
    if (rng < 0.0)  rng = 0.0;
    double rad = (bearing_deg - 90.0) * (PI / 180.0);
    float rr = (float)(rng / 10.0) * sc.radius;
    Vector2 v = { sc.cx + cosf(rad) * rr, sc.cy + sinf(rad) * rr };
    return v;
}

/* ------------------------------------------------------------------ */
/*  per-aircraft visual target driven by the sim state                 */
/* ------------------------------------------------------------------ */

static void target_for(const Aircraft *a, double *out_bear, double *out_rng)
{
    double joff = ((a->id * 37) % 24) - 12.0;
    double roff = ((a->id * 13) % 10) * 0.08;

    switch (a->state) {
        case AC_INBOUND:  *out_bear = a->bearing_deg; *out_rng = 7.5 + roff; break;
        case AC_HOLDING:  *out_bear = a->bearing_deg; *out_rng = 5.5 + roff; break;
        case AC_LANDING:  *out_bear = a->bearing_deg; *out_rng = 1.6;        break;
        case AC_TAXI_IN:
        case AC_AT_GATE:
        case AC_TAXI_OUT: {
            int g = a->assigned_gate >= 0 ? a->assigned_gate : (int)(a->id % 8);
            *out_bear = 90.0 + (360.0 / 12) * g;
            *out_rng  = (a->state == AC_AT_GATE) ? 1.3 : 2.4;
            break;
        }
        case AC_DEPARTING:    *out_bear = a->bearing_deg + 12.0; *out_rng = 1.8;        break;
        case AC_AIRBORNE_OUT: *out_bear = a->bearing_deg + 12.0; *out_rng = 9.5 + roff; break;
        default:              *out_bear = a->bearing_deg;        *out_rng = 11.0;       break;
    }
    if (a->state == AC_INBOUND || a->state == AC_HOLDING) *out_bear += joff;
}

static Color color_for(const Aircraft *a)
{
    if (a->emergency || a->state == AC_LOST) return ALERT;
    switch (a->state) {
        case AC_TAXI_IN: case AC_AT_GATE: case AC_TAXI_OUT: return GROUND;
        case AC_HOLDING:  return AMBER;
        case AC_DEPARTING: case AC_AIRBORNE_OUT: return TEAL;
        default: return ACCENT;
    }
}

/* ------------------------------------------------------------------ */
/*  scope drawing                                                      */
/* ------------------------------------------------------------------ */

static void draw_scope_grid(Scope sc, float sweep_angle, Weather wx)
{
    DrawCircleV((Vector2){sc.cx, sc.cy}, sc.radius + 10, fade(SCOPE_EDGE, 60));
    DrawCircleV((Vector2){sc.cx, sc.cy}, sc.radius + 6, SCOPE_BG);

    for (int nm = 2; nm <= 10; nm += 2) {
        float rr = (float)nm / 10.0f * sc.radius;
        DrawCircleLines((int)sc.cx, (int)sc.cy, rr, (nm % 4 == 0) ? RING : RING_FAINT);
        char lbl[8]; snprintf(lbl, sizeof(lbl), "%dNM", nm);
        DrawText(lbl, (int)(sc.cx + rr - 14), (int)sc.cy + 4, 11, fade(ACCENT, 90));
    }

    for (int deg = 0; deg < 360; deg += 30) {
        double rad = (deg - 90) * (PI/180.0);
        Vector2 e = { sc.cx + cosf(rad)*sc.radius, sc.cy + sinf(rad)*sc.radius };
        DrawLineV((Vector2){sc.cx, sc.cy}, e, RING_FAINT);
        Vector2 t0 = { sc.cx + cosf(rad)*(sc.radius+2), sc.cy + sinf(rad)*(sc.radius+2) };
        Vector2 t1 = { sc.cx + cosf(rad)*(sc.radius+9), sc.cy + sinf(rad)*(sc.radius+9) };
        DrawLineEx(t0, t1, 1.5f, fade(ACCENT, 120));
    }

    const char *card[4] = { "N", "E", "S", "W" };
    for (int i = 0; i < 4; i++) {
        double rad = (i*90 - 90) * (PI/180.0);
        Vector2 p = { sc.cx + cosf(rad)*(sc.radius+22) - 4,
                      sc.cy + sinf(rad)*(sc.radius+22) - 7 };
        DrawText(card[i], (int)p.x, (int)p.y, 14, fade(ACCENT, 160));
    }

    /* sweep wedge — tinted by weather */
    Color sweepc = SWEEP; Color wc = wx_color(wx);
    sweepc.r = wc.r; sweepc.g = wc.g; sweepc.b = wc.b;
    int segs = 44;
    for (int i = 0; i < segs; i++) {
        float a0 = sweep_angle - (float)i * 0.8f * DEG2RAD;
        float a1 = sweep_angle - (float)(i+1) * 0.8f * DEG2RAD;
        unsigned char alpha = (unsigned char)(SWEEP.a * (1.0f - (float)i/segs));
        Vector2 p0 = { sc.cx + cosf(a0)*sc.radius, sc.cy + sinf(a0)*sc.radius };
        Vector2 p1 = { sc.cx + cosf(a1)*sc.radius, sc.cy + sinf(a1)*sc.radius };
        DrawTriangle((Vector2){sc.cx,sc.cy}, p1, p0, fade(sweepc, alpha));
    }
    Vector2 tip = { sc.cx + cosf(sweep_angle)*sc.radius, sc.cy + sinf(sweep_angle)*sc.radius };
    DrawLineEx((Vector2){sc.cx,sc.cy}, tip, 1.5f, fade(ACCENT, 200));

    DrawCircleLines((int)sc.cx, (int)sc.cy, sc.radius, SCOPE_EDGE);
    DrawCircleV((Vector2){sc.cx, sc.cy}, 5, INK);
    DrawCircleLines((int)sc.cx, (int)sc.cy, 9, fade(INK, 120));
}

static void draw_blip(Vector2 p, Color c, const Aircraft *a, bool selected)
{
    if (a->emergency) {
        float pr = 12 + 5*sinf((float)GetTime()*8);
        DrawCircleV(p, pr, fade(c, 60));
    }
    DrawCircleV(p, 8, fade(c, 45));
    DrawCircleV(p, 4, c);
    DrawCircleLines((int)p.x, (int)p.y, 7, c);

    int bx = (int)p.x + 12, by = (int)p.y - 9;
    if (selected) {
        float rr = 14 + 2.5f*sinf((float)GetTime()*4);
        DrawCircleLines((int)p.x, (int)p.y, rr, WHITE_SEL);
        DrawCircleLines((int)p.x, (int)p.y, rr+1, fade(WHITE_SEL, 120));
        DrawLine((int)p.x+8, (int)p.y-8, bx-2, by+4, fade(WHITE_SEL, 150));
        DrawText(a->callsign, bx, by, 13, WHITE_SEL);
        char sub[32];
        snprintf(sub, sizeof(sub), "%s  %s", a->ac_type, state_name(a->state));
        DrawText(sub, bx, by + 13, 10, fade(INK, 220));
    } else {
        DrawText(a->callsign, bx, by, 12, c);
        DrawText(state_name(a->state), bx, by + 12, 9, INK_DIM);
    }
}

/* ------------------------------------------------------------------ */
/*  immediate-mode widgets                                             */
/* ------------------------------------------------------------------ */

static void text_center(const char *s, int cx, int cy, int sz, Color col)
{
    DrawText(s, cx - MeasureText(s, sz)/2, cy, sz, col);
}

static bool ui_button(Rectangle r, const char *label, bool enabled,
                      Color accent, Vector2 mouse, bool clicked)
{
    bool hot = enabled && CheckCollisionPointRec(mouse, r);
    Color fill = enabled ? (hot ? accent : fade(accent, 55)) : fade(CARD, 200);
    Color edge = enabled ? accent : CARD_LINE;
    Color txt  = enabled ? (hot ? BG_BOT : accent) : fade(INK_DIM, 140);
    DrawRectangleRounded(r, 0.30f, 6, fill);
    DrawRectangleRoundedLines(r, 0.30f, 6, edge);
    text_center(label, (int)(r.x + r.width/2), (int)(r.y + r.height/2 - 5), 12, txt);
    return enabled && hot && clicked;
}

static void draw_card(int x, int y, int w, int h, const char *label,
                      const char *value, Color vcol)
{
    Rectangle r = { (float)x, (float)y, (float)w, (float)h };
    DrawRectangleRounded(r, 0.18f, 6, CARD);
    DrawRectangleRoundedLines(r, 0.18f, 6, CARD_LINE);
    DrawText(label, x + 12, y + 9, 10, INK_DIM);
    DrawText(value, x + 12, y + 23, 22, vcol);
}

static void draw_bar(int x, int y, int w, float frac, Color c)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    DrawRectangleRounded((Rectangle){(float)x,(float)y,(float)w,9}, 1.0f, 4, fade(CARD_LINE,180));
    if (frac > 0.001f)
        DrawRectangleRounded((Rectangle){(float)x,(float)y,w*frac,9}, 1.0f, 4, c);
}

/* ------------------------------------------------------------------ */
/*  sidebar                                                            */
/* ------------------------------------------------------------------ */

static int count_in_sector(const Simulation *s)
{
    int n = 0;
    for (int i = 0; i < s->flight_count; i++) {
        AircraftState st = s->flights[i].state;
        if (st != AC_DONE && st != AC_LOST && st != AC_DIVERTED) n++;
    }
    return n;
}

static void draw_stats(const Simulation *s, int px, int right)
{
    int gap = 10, cw = (right - px - gap) / 2, ch = 44, y = 110;
    char buf[64];
    double sc = sim_score(s);

    struct { const char *l; const char *fmt; double v; int iv; bool usei; Color c; } rows[8];
    int n = 0;
    #define ROW(L, IV, C)  do{ rows[n].l=L; rows[n].fmt="%d";  rows[n].iv=(IV); rows[n].usei=true;  rows[n].c=C; n++; }while(0)
    #define ROWF(L,F,V,C)  do{ rows[n].l=L; rows[n].fmt=F;     rows[n].v=(V);   rows[n].usei=false; rows[n].c=C; n++; }while(0)

    ROWF("SCORE",          "%.0f", sc, sc >= 0 ? TEAL : ALERT);
    ROW ("IN SECTOR",      count_in_sector(s),          ACCENT);
    ROW ("DEPARTED",       s->stats.flights_departed,   TEAL);
    ROWF("MEAN WAIT (m)",  "%.1f", sim_mean_wait(s),    INK);
    ROW ("EMERGENCIES",    s->stats.emergencies,        s->stats.emergencies ? AMBER : INK);
    ROW ("LOST",           s->stats.flights_lost,       s->stats.flights_lost ? ALERT : INK);
    ROW ("DIVERTED",       s->stats.flights_diverted,   s->stats.flights_diverted ? GROUND : INK);
    ROW ("GO-AROUNDS",     s->stats.go_arounds,         s->stats.go_arounds ? AMBER : INK);

    for (int i = 0; i < n; i++) {
        int x = px + (i % 2) * (cw + gap);
        int yy = y + (i / 2) * (ch + 6);
        if (rows[i].usei) snprintf(buf, sizeof(buf), rows[i].fmt, rows[i].iv);
        else              snprintf(buf, sizeof(buf), rows[i].fmt, rows[i].v);
        draw_card(x, yy, cw, ch, rows[i].l, buf, rows[i].c);
    }
    #undef ROW
    #undef ROWF
}

static void draw_resources(const Simulation *s, int px, int right, int *yio)
{
    int y = *yio;
    char buf[64];

    DrawText("RUNWAY LOAD", px, y, 11, INK_DIM);
    snprintf(buf, sizeof(buf), "%.0f%% rwy   %.0f%% gate",
             sim_runway_utilisation(s)*100, sim_gate_utilisation(s)*100);
    DrawText(buf, right - MeasureText(buf,11), y, 11, INK_DIM); y += 16;
    draw_bar(px, y, right - px, (float)sim_runway_utilisation(s), ACCENT); y += 20;

    /* runway chips, width scaled to the count */
    int rn = s->num_runways, rgap = 8;
    int rw_w = (right - px - (rn - 1)*rgap) / rn;
    for (int i = 0; i < rn; i++) {
        Color c = s->runways[i].state == RW_IDLE ? INK_DIM :
                  s->runways[i].state == RW_TAKEOFF ? TEAL : ACCENT;
        Rectangle r = { (float)(px + i*(rw_w+rgap)), (float)y, (float)rw_w, 24 };
        DrawRectangleRounded(r, 0.3f, 6, fade(c, 40));
        DrawRectangleRoundedLines(r, 0.3f, 6, c);
        const char *lab = s->runways[i].state == RW_IDLE ? "IDLE" :
                          s->runways[i].state == RW_LANDING ? "LAND" :
                          s->runways[i].state == RW_TAKEOFF ? "DEP" : "LOCK";
        char l2[12]; snprintf(l2, sizeof(l2), "%d:%s", i+1, lab);
        text_center(l2, (int)(r.x + rw_w/2), y + 6, 11, c);
    }
    y += 30;

    DrawText("GATES", px, y, 11, INK_DIM); y += 16;
    for (int i = 0; i < s->num_gates; i++) {
        int gx = px + (i % 6) * 50, gy = y + (i / 6) * 24;
        Color c = s->gates[i].state == GATE_VACANT ? fade(CARD_LINE, 200) :
                  s->gates[i].state == GATE_RESERVED ? AMBER : GROUND;
        DrawRectangleRounded((Rectangle){(float)gx,(float)gy,42,19}, 0.3f, 5, c);
        char gl[4]; snprintf(gl, sizeof(gl), "%d", i+1);
        text_center(gl, gx + 21, gy + 4, 12,
                    s->gates[i].state == GATE_VACANT ? INK_DIM : BG_BOT);
    }
    y += ((s->num_gates + 5) / 6) * 24 + 6;
    *yio = y;
}

static void draw_flight_strip(Simulation *s, int selected, int px, int right,
                              int top, Vector2 mouse, bool clicked)
{
    Rectangle panel = { (float)(px - 8), (float)top, (float)(right - px + 16), 214 };
    DrawRectangleRounded(panel, 0.06f, 6, fade(CARD, 160));
    DrawRectangleRoundedLines(panel, 0.06f, 6, CARD_LINE);

    int x = px + 6, y = top + 12;
    DrawText("FLIGHT STRIP", x, y, 11, INK_DIM); y += 20;

    if (selected < 0 || selected >= s->flight_count) {
        DrawText("No target selected.", x, y + 6, 14, INK_DIM); y += 26;
        DrawText("Click an aircraft on the scope", x, y, 12, fade(INK_DIM,200));
        return;
    }

    Aircraft *a = &s->flights[selected];
    Color c = color_for(a);
    char line[72];

    DrawText(a->callsign, x, y, 26, c);
    DrawText(a->ac_type, x + MeasureText(a->callsign,26) + 12, y + 9, 16, INK);
    char wk[2] = { a->wake, 0 };
    DrawText(wk, right - 22, y + 4, 18, fade(INK_DIM, 220));
    y += 32;

    DrawText(a->airline, x, y, 13, INK); y += 18;
    snprintf(line, sizeof(line), "%s (%s)  >  TOWER", a->origin_city, a->origin);
    DrawText(line, x, y, 12, INK_DIM); y += 22;

    DrawText(state_name(a->state), x, y, 13, c);
    snprintf(line, sizeof(line), "WAIT %.1fm  GA %d", a->total_wait, a->go_arounds);
    DrawText(line, right - MeasureText(line,12), y, 12, INK_DIM);
    y += 18;

    Color fc = a->fuel > 50 ? TEAL : a->fuel > FUEL_EMERGENCY ? AMBER : ALERT;
    draw_bar(x, y, right - x - 56, (float)(a->fuel/FUEL_INITIAL), fc);
    snprintf(line, sizeof(line), "%2.0f%% FUEL", a->fuel);
    DrawText(line, right - 50, y - 3, 11, fc);
    y += 20;

    /* 2x2 command grid */
    int bw = (right - x - 8) / 2, bh = 28, bg = 8;
    Rectangle b00 = { (float)x,         (float)y,         (float)bw, (float)bh };
    Rectangle b01 = { (float)(x+bw+bg), (float)y,         (float)bw, (float)bh };
    Rectangle b10 = { (float)x,         (float)(y+bh+bg), (float)bw, (float)bh };
    Rectangle b11 = { (float)(x+bw+bg), (float)(y+bh+bg), (float)bw, (float)bh };
    if (ui_button(b00, "EXPEDITE", sim_can_prioritize(s, selected), ACCENT, mouse, clicked))
        sim_cmd_prioritize(s, selected);
    if (ui_button(b01, "HOLD", sim_can_hold(s, selected), AMBER, mouse, clicked))
        sim_cmd_hold(s, selected);
    if (ui_button(b10, "GO-AROUND", sim_can_go_around(s, selected), GROUND, mouse, clicked))
        sim_cmd_go_around(s, selected);
    if (ui_button(b11, "DIVERT", sim_can_divert(s, selected), ALERT, mouse, clicked))
        sim_cmd_divert(s, selected);
}

static void draw_sidebar(Simulation *s, int selected, double speed,
                         bool paused, Vector2 mouse, bool clicked)
{
    int x0 = WIN_W - SIDEBAR_W;
    DrawRectangle(x0, 0, SIDEBAR_W, STAGE_H, PANEL);
    DrawLine(x0, 0, x0, STAGE_H, CARD_LINE);

    int px = x0 + 22, right = WIN_W - 22;
    char buf[64], clk[8];

    DrawText("SECTOR CONTROL", px, 22, 22, INK);
    wall_clock(s->now, clk, sizeof(clk));
    snprintf(buf, sizeof(buf), "LOCAL %s", clk);
    DrawText(buf, right - MeasureText(buf,13), 28, 13, ACCENT);
    DrawText("approach + ground radar", px, 48, 12, INK_DIM);

    /* weather pill */
    Color wc = wx_color(s->weather);
    snprintf(buf, sizeof(buf), "WX %s", weather_name(s->weather));
    int pw = MeasureText(buf, 12) + 18;
    DrawRectangleRounded((Rectangle){(float)(right-pw),68,(float)pw,20}, 0.5f, 6, fade(wc,45));
    DrawRectangleRoundedLines((Rectangle){(float)(right-pw),68,(float)pw,20}, 0.5f, 6, wc);
    DrawText(buf, right - pw + 9, 72, 12, wc);
    DrawLine(px, 100, right, 100, CARD_LINE);

    draw_stats(s, px, right);

    int y = 312;
    DrawLine(px, y, right, y, CARD_LINE); y += 14;
    draw_resources(s, px, right, &y);

    draw_flight_strip(s, selected, px, right, STAGE_H - 308, mouse, clicked);

    int fy = STAGE_H - 78;
    DrawLine(px, fy - 12, right, fy - 12, CARD_LINE);
    snprintf(buf, sizeof(buf), "TIME WARP %.0fx %s", speed, paused ? "PAUSED" : "");
    DrawText(buf, px, fy, 15, paused ? AMBER : ACCENT); fy += 22;
    DrawText("SPACE pause   1-4 warp   R reset   F1 help", px, fy, 11, INK_DIM); fy += 15;
    DrawText("CLICK select   E H D G  issue clearances", px, fy, 11, INK_DIM);
}

/* ------------------------------------------------------------------ */
/*  bottom console: event log + throughput sparkline                   */
/* ------------------------------------------------------------------ */

static void draw_console(const Simulation *s, const Throughput *tp)
{
    int top = STAGE_H;
    DrawRectangle(0, top, WIN_W, CONSOLE_H, CONSOLE_BG);
    DrawLine(0, top, WIN_W, top, CARD_LINE);

    /* --- event log (left) --- */
    int lx = 20, ly = top + 12;
    DrawText("EVENT LOG", lx, ly, 12, INK_DIM);
    DrawText("live", lx + 90, ly, 11, fade(TEAL, 200));
    ly += 22;
    for (int i = 0; i < 5; i++) {
        SimLogEntry e;
        if (!sim_log_at(s, i, &e)) break;
        Color c = e.severity == 2 ? ALERT : e.severity == 1 ? AMBER : INK_DIM;
        char clk[8]; wall_clock(e.t, clk, sizeof(clk));
        DrawText(clk, lx, ly + i*18, 12, fade(c, 150));
        DrawText(e.msg, lx + 48, ly + i*18, 12, i == 0 ? INK : c);
    }

    /* --- throughput sparkline (right, under the sidebar) --- */
    int rx = WIN_W - SIDEBAR_W + 22, rright = WIN_W - 22, ry = top + 14;
    DrawText("THROUGHPUT", rx, ry, 12, INK_DIM);
    char tot[40];
    snprintf(tot, sizeof(tot), "%d dep", s->stats.flights_departed);
    DrawText(tot, rright - MeasureText(tot,11), ry, 11, fade(TEAL,200));
    ry += 22;

    int gw = rright - rx, gh = CONSOLE_H - 56;
    DrawRectangleRounded((Rectangle){(float)rx,(float)ry,(float)gw,(float)gh}, 0.08f, 6, fade(CARD,120));
    int peak = 1;
    for (int i = 0; i < tp->n; i++) if (tp->buckets[i] > peak) peak = tp->buckets[i];
    int show = tp->n < HIST_N ? tp->n : HIST_N;
    float bw = (float)gw / HIST_N;
    for (int i = 0; i < show; i++) {
        int v = tp->buckets[tp->n - show + i];
        float bh = (gh - 8) * (float)v / peak;
        float bx = rx + i*bw;
        DrawRectangle((int)bx + 1, (int)(ry + gh - 4 - bh), (int)bw - 1, (int)bh,
                      fade(ACCENT, 200));
    }
    DrawText("departures / 2 sim-min", rx, ry + gh - 14, 10, fade(INK_DIM,160));
}

/* ------------------------------------------------------------------ */
/*  help overlay                                                       */
/* ------------------------------------------------------------------ */

static void draw_help(void)
{
    DrawRectangle(0, 0, WIN_W, WIN_H, (Color){4, 7, 14, 220});
    int w = 640, h = 460, x = (WIN_W - w)/2, y = (WIN_H - h)/2;
    DrawRectangleRounded((Rectangle){(float)x,(float)y,(float)w,(float)h}, 0.04f, 8, PANEL);
    DrawRectangleRoundedLines((Rectangle){(float)x,(float)y,(float)w,(float)h}, 0.04f, 8, ACCENT);

    int tx = x + 36, ty = y + 30;
    DrawText("SECTOR CONTROL — REFERENCE", tx, ty, 22, INK); ty += 40;

    DrawText("CONTROLLER DECISIONS", tx, ty, 13, ACCENT); ty += 24;
    const char *cmds[4][2] = {
        {"Click", "select an aircraft on the scope"},
        {"E", "Expedite — priority for the next runway"},
        {"H", "Hold — send an inbound into the pattern"},
        {"D / G", "Divert to alternate / wave off a landing"},
    };
    for (int i = 0; i < 4; i++) {
        DrawText(cmds[i][0], tx, ty, 14, AMBER);
        DrawText(cmds[i][1], tx + 90, ty, 14, INK_DIM); ty += 22;
    }
    ty += 14;
    DrawText("VIEW", tx, ty, 13, ACCENT); ty += 24;
    DrawText("SPACE pause    1 2 3 4  warp 1x 5x 25x 100x", tx, ty, 14, INK_DIM); ty += 22;
    DrawText("R reset scenario    F1 toggle this help    ESC quit", tx, ty, 14, INK_DIM); ty += 34;

    DrawText("BLIP LEGEND", tx, ty, 13, ACCENT); ty += 24;
    struct { Color c; const char *t; } leg[5] = {
        {ACCENT, "inbound / on final"}, {AMBER, "holding"}, {GROUND, "on the ground"},
        {TEAL, "departing"}, {ALERT, "emergency / lost"},
    };
    for (int i = 0; i < 5; i++) {
        int lx = tx + (i % 3) * 200, lyy = ty + (i / 3) * 24;
        DrawCircleV((Vector2){(float)(lx+6),(float)(lyy+7)}, 5, leg[i].c);
        DrawText(leg[i].t, lx + 18, lyy, 13, INK_DIM);
    }
}

/* ------------------------------------------------------------------ */
/*  visual easing + picking + sampling                                 */
/* ------------------------------------------------------------------ */

static void update_visuals(Simulation *s, Scope sc, float dt)
{
    for (int i = 0; i < s->flight_count; i++) {
        Aircraft *a = &s->flights[i];
        if (a->state == AC_DONE || a->state == AC_LOST || a->state == AC_DIVERTED) continue;
        if (a->state == AC_HOLDING || a->state == AC_INBOUND)
            a->bearing_deg += (a->state == AC_HOLDING ? 18.0 : 4.0) * dt;
        double tb, tr; target_for(a, &tb, &tr);
        Vector2 tgt = polar_to_screen(sc, tb, tr);
        if (a->x == 0 && a->y == 0) { a->x = tgt.x; a->y = tgt.y; }
        float k = 1.0f - expf(-3.0f * dt);
        a->x += (tgt.x - a->x) * k;
        a->y += (tgt.y - a->y) * k;
    }
}

static int pick_aircraft(const Simulation *s, Vector2 p)
{
    int best = -1; float best_d2 = 22.0f * 22.0f;
    for (int i = 0; i < s->flight_count; i++) {
        const Aircraft *a = &s->flights[i];
        if (a->state == AC_DONE || a->state == AC_LOST || a->state == AC_DIVERTED) continue;
        float dx = (float)a->x - p.x, dy = (float)a->y - p.y, d2 = dx*dx + dy*dy;
        if (d2 < best_d2) { best_d2 = d2; best = i; }
    }
    return best;
}

static void throughput_sample(Throughput *tp, const Simulation *s)
{
    while (s->now >= tp->next_t) {
        int v = s->stats.flights_departed - tp->last_dep;
        tp->last_dep = s->stats.flights_departed;
        if (tp->n < HIST_N) tp->buckets[tp->n++] = v;
        else { memmove(tp->buckets, tp->buckets+1, (HIST_N-1)*sizeof(int));
               tp->buckets[HIST_N-1] = v; }
        tp->next_t += 2.0;   /* one bucket per 2 sim minutes */
    }
}

/* ------------------------------------------------------------------ */
/*  scene composition (shared by run_app and run_snapshot)             */
/* ------------------------------------------------------------------ */

static void draw_scene(Simulation *s, Scope sc, float sweep, int selected,
                       double speed, bool paused, const Throughput *tp,
                       Vector2 mouse, bool clicked, bool help)
{
    DrawRectangleGradientV(0, 0, WIN_W, STAGE_H, BG_TOP, BG_BOT);
    draw_scope_grid(sc, sweep, s->weather);

    for (int i = 0; i < s->flight_count; i++) {
        Aircraft *a = &s->flights[i];
        if (a->state == AC_DONE || a->state == AC_LOST || a->state == AC_DIVERTED) continue;
        draw_blip((Vector2){(float)a->x,(float)a->y}, color_for(a), a, i == selected);
    }

    DrawText("RANGE 10 NM  ·  CH 1 APPROACH", 18, 18, 14, fade(ACCENT,170));
    DrawText("Click a target to issue a clearance  ·  F1 for help",
             18, STAGE_H - 28, 13, INK_DIM);

    draw_sidebar(s, selected, speed, paused, mouse, clicked);
    draw_console(s, tp);
    if (help) draw_help();
}

/* ------------------------------------------------------------------ */
/*  main application loop                                              */
/* ------------------------------------------------------------------ */

int run_app(uint64_t seed, int num_runways, int num_gates,
            double mean_interarrival, double horizon)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WIN_W, WIN_H, "Sector Control — ATC Discrete-Event Radar");
    SetTargetFPS(60);

    Simulation sim;
    sim_init(&sim, seed, num_runways, num_gates, mean_interarrival, horizon);

    Scope sc = scope_geometry();
    double speed = 25.0;
    bool   paused = false, help = false;
    float  sweep = 0.0f;
    int    selected = -1;
    Throughput tp = {0}; tp.next_t = 2.0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 mouse = GetMousePosition();
        bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_ONE))   speed = 1.0;
        if (IsKeyPressed(KEY_TWO))   speed = 5.0;
        if (IsKeyPressed(KEY_THREE)) speed = 25.0;
        if (IsKeyPressed(KEY_FOUR))  speed = 100.0;
        if (IsKeyPressed(KEY_F1))    help = !help;
        if (IsKeyPressed(KEY_R)) {
            sim_free(&sim);
            sim_init(&sim, ++seed, num_runways, num_gates, mean_interarrival, horizon);
            selected = -1;
            memset(&tp, 0, sizeof(tp)); tp.next_t = 2.0;
        }

        if (clicked && mouse.x < WIN_W - SIDEBAR_W && mouse.y < STAGE_H)
            selected = pick_aircraft(&sim, mouse);

        if (selected >= 0) {
            if (IsKeyPressed(KEY_E)) sim_cmd_prioritize(&sim, selected);
            if (IsKeyPressed(KEY_H)) sim_cmd_hold(&sim, selected);
            if (IsKeyPressed(KEY_D)) sim_cmd_divert(&sim, selected);
            if (IsKeyPressed(KEY_G)) sim_cmd_go_around(&sim, selected);
        }

        if (!paused) {
            double target = sim.now + speed * dt;
            AtcEvent peek; int guard = 0;
            while (pq_peek(&sim.pq, &peek) && peek.execution_time <= target && guard < 100000) {
                sim_step(&sim); guard++;
            }
            if (sim.now < target) sim.now = target;
        }
        throughput_sample(&tp, &sim);

        if (selected >= 0) {
            AircraftState st = sim.flights[selected].state;
            if (st == AC_DONE || st == AC_LOST || st == AC_DIVERTED) selected = -1;
        }

        update_visuals(&sim, sc, paused ? 0.0f : dt);
        sweep -= dt * 1.4f;

        BeginDrawing();
        ClearBackground(BG_BOT);
        draw_scene(&sim, sc, sweep, selected, speed, paused, &tp, mouse, clicked, help);
        EndDrawing();
    }

    sim_free(&sim);
    CloseWindow();
    return 0;
}

/* ------------------------------------------------------------------ */
/*  one-frame snapshot for the README preview                          */
/* ------------------------------------------------------------------ */

int run_snapshot(uint64_t seed, int num_runways, int num_gates,
                 double mean_interarrival, double horizon,
                 double at_minute, const char *outfile)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WIN_W, WIN_H, "Sector Control — snapshot");
    SetTargetFPS(60);

    Simulation sim;
    sim_init(&sim, seed, num_runways, num_gates, mean_interarrival, horizon);

    AtcEvent peek;
    while (pq_peek(&sim.pq, &peek) && peek.execution_time <= at_minute)
        sim_step(&sim);
    if (sim.now < at_minute) sim.now = at_minute;

    Scope sc = scope_geometry();
    Throughput tp = {0}; tp.next_t = 2.0;
    throughput_sample(&tp, &sim);

    float sweep = -0.6f;
    for (int f = 0; f < 30; f++) {
        update_visuals(&sim, sc, 1.0f/60.0f);
        sweep -= (1.0f/60.0f) * 1.4f;
        BeginDrawing();
        ClearBackground(BG_BOT);
        draw_scene(&sim, sc, sweep, -1, 25.0, false, &tp,
                   (Vector2){-1,-1}, false, false);
        EndDrawing();
    }

    TakeScreenshot(outfile);   /* writes relative to the working dir */

    sim_free(&sim);
    CloseWindow();
    return 0;
}
