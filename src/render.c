#include "render.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  LAYOUT CONSTANTS                                                   */
/* ================================================================== */

#define WIN_W        1400
#define WIN_H        960
#define PANEL_W      400              /* right-hand dashboard width    */
#define CONSOLE_H    130              /* bottom event-log strip        */
#define SCOPE_PAD    48
#define STAGE_H      (WIN_H - CONSOLE_H)

#define PAD_OUTER    18               /* dashboard inner padding       */
#define GAP_SECTION  9                /* space between dashboard cards */
#define HEADER_H     16               /* section eyebrow label height  */
#define ROW_H        16               /* key/value row height          */

#define FS_CAPTION    10
#define FS_LABEL      11
#define FS_BODY       13
#define FS_SUBHEAD    15
#define FS_VALUE_MD   20
#define FS_VALUE_LG   24
#define FS_TITLE      20

#define HIST_N 90

/* ================================================================== */
/*  PALETTE — dark navy operations theme, cyan/blue accent family      */
/* ================================================================== */

static const Color NAVY_950   = {  9,  13,  21, 255 };
static const Color NAVY_900   = { 13,  18,  29, 255 };
static const Color NAVY_800   = { 18,  25,  40, 255 };
static const Color NAVY_700   = { 25,  34,  53, 255 };
static const Color NAVY_600   = { 37,  49,  73, 255 };

static const Color SCOPE_BG   = {  8,  12,  20, 255 };
static const Color SCOPE_EDGE = { 34,  54,  86, 255 };
static const Color RING       = { 30,  52,  82, 255 };
static const Color RING_FAINT = { 19,  31,  50, 255 };

/* Prefixed to avoid colliding with raylib's own built-in Color macros
 * (raylib predefines RED, GREEN, BLUE, ORANGE, VIOLET, etc.).            */
static const Color ATC_CYAN    = {  56, 189, 248, 255 };  /* primary accent / inbound */
static const Color ATC_BLUE    = {  77, 138, 246, 255 };  /* departures / reserved    */
static const Color ATC_GREEN   = {  52, 199, 141, 255 };  /* success / occupied       */
static const Color ATC_AMBER   = { 247, 181,  56, 255 };  /* caution / holding        */
static const Color ATC_ORANGE  = { 249, 131,  60, 255 };  /* go-around                */
static const Color ATC_RED     = { 239,  83,  80, 255 };  /* emergency / critical     */
static const Color ATC_VIOLET  = { 167, 139, 250, 255 };  /* ground ops               */

static const Color INK        = { 228, 234, 245, 255 };
static const Color INK_DIM    = { 142, 155, 180, 255 };
static const Color GATE_VACANT_C = { 54,  64,  82, 255 };
static const Color WHITE_SEL  = { 247, 250, 255, 255 };

/* ================================================================== */
/*  small helpers                                                      */
/* ================================================================== */

typedef struct { float cx, cy, radius; } Scope;

typedef struct {
    Rectangle radar;
    Rectangle panel;
    Rectangle console;
} Layout;

typedef struct { int buckets[HIST_N]; int n; double next_t; long last_events; } PerfHistory;

static Color fade(Color c, unsigned char a) { c.a = a; return c; }

static void wall_clock(double sim_min, char *out, int n)
{
    int total = 360 + (int)sim_min;     /* simulated day starts 06:00 */
    int hh = (total / 60) % 24, mm = total % 60;
    snprintf(out, n, "%02d:%02d", hh, mm);
}

static void text_center(const char *s, int cx, int cy, int sz, Color col)
{
    DrawText(s, cx - MeasureText(s, sz)/2, cy, sz, col);
}

static Layout compute_layout(void)
{
    Layout L;
    L.radar   = (Rectangle){ 0, 0, (float)(WIN_W - PANEL_W), (float)STAGE_H };
    L.panel   = (Rectangle){ (float)(WIN_W - PANEL_W), 0, (float)PANEL_W, (float)STAGE_H };
    L.console = (Rectangle){ 0, (float)STAGE_H, (float)WIN_W, (float)CONSOLE_H };
    return L;
}

static Scope scope_geometry(Rectangle r)
{
    Scope s;
    s.cx = r.x + r.width * 0.5f;
    s.cy = r.y + r.height * 0.5f;
    s.radius = fminf(r.width, r.height) * 0.5f - SCOPE_PAD;
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

static Vector2 heading_vector(double heading_deg)
{
    double rad = heading_deg * (PI / 180.0);
    Vector2 v = { (float)sin(rad), (float)(-cos(rad)) };
    return v;
}

/* ================================================================== */
/*  semantic colour mappers (presentation-only classification)         */
/* ================================================================== */

static Color wx_color(Weather w)
{
    return w == WX_STORM ? ATC_RED : w == WX_WINDY ? ATC_AMBER : ATC_GREEN;
}

static Color runway_color(RunwayState st)
{
    switch (st) {
        case RW_LANDING: return ATC_CYAN;
        case RW_TAKEOFF: return ATC_BLUE;
        case RW_LOCKED:  return ATC_AMBER;
        default:         return INK_DIM;
    }
}

static const char *runway_status_label(RunwayState st)
{
    switch (st) {
        case RW_LANDING: return "LANDING";
        case RW_TAKEOFF: return "TAKEOFF";
        case RW_LOCKED:  return "LOCKED";
        default:         return "IDLE";
    }
}

static Color gate_color(GateState st)
{
    switch (st) {
        case GATE_OCCUPIED: return ATC_GREEN;
        case GATE_RESERVED: return ATC_BLUE;
        default:             return GATE_VACANT_C;
    }
}

static const char *wake_label(char w)
{
    switch (w) {
        case 'L': return "LIGHT";
        case 'M': return "MEDIUM";
        case 'H': return "HEAVY";
        case 'J': return "SUPER";
        default:  return "UNK";
    }
}

/* Radar blip colour: cyan = inbound/final, blue = departure ops,
 * violet = ground movement, amber = holding, red = emergency/lost.  */
static Color aircraft_color(const Aircraft *a)
{
    if (a->emergency || a->state == AC_LOST) return ATC_RED;
    switch (a->state) {
        case AC_TAXI_IN: case AC_AT_GATE: case AC_TAXI_OUT: return ATC_VIOLET;
        case AC_HOLDING:                                    return ATC_AMBER;
        case AC_DEPARTING: case AC_AIRBORNE_OUT:            return ATC_BLUE;
        default:                                            return ATC_CYAN;
    }
}

/* Event-log row colour, classified from the (unmodified) log text.
 * Falls back to severity banding for message types outside the five
 * headline categories the dashboard calls out.                       */
static Color event_category_color(const SimLogEntry *e)
{
    const char *m = e->msg;
    if (strstr(m, "EMERGENCY") || strstr(m, "LOST"))            return ATC_RED;
    if (strstr(m, "GO-AROUND") || strstr(m, "go around"))        return ATC_ORANGE;
    if (strstr(m, "holding pattern"))                            return ATC_AMBER;
    if (strstr(m, "departed"))                                   return ATC_BLUE;
    if (strstr(m, "inbound"))                                    return ATC_GREEN;
    return e->severity == 2 ? ATC_RED : e->severity == 1 ? ATC_AMBER : INK_DIM;
}

/* ================================================================== */
/*  per-aircraft visual target driven by the sim state                 */
/* ================================================================== */

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

/* ================================================================== */
/*  reusable low-level widgets                                         */
/* ================================================================== */

static void draw_section_label(int x, int y, const char *text)
{
    DrawText(text, x, y, FS_LABEL, INK_DIM);
}

static void draw_kpi_card(Rectangle r, const char *label, const char *value, Color vcol)
{
    DrawRectangleRounded(r, 0.18f, 6, NAVY_700);
    DrawRectangleRoundedLines(r, 0.18f, 6, NAVY_600);
    DrawText(label, (int)r.x + 10, (int)r.y + 7, FS_CAPTION, INK_DIM);
    DrawText(value, (int)r.x + 10, (int)r.y + 20, FS_VALUE_MD, vcol);
}

static void draw_progress_bar(Rectangle r, float frac, Color c)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    DrawRectangleRounded(r, 1.0f, 4, fade(NAVY_600, 210));
    if (frac > 0.004f) {
        Rectangle f = r; f.width *= frac;
        DrawRectangleRounded(f, 1.0f, 4, c);
    }
}

static int draw_kv_row(int x, int y, int w, const char *label, const char *value, Color vcol)
{
    DrawText(label, x, y, FS_CAPTION, INK_DIM);
    int tw = MeasureText(value, FS_BODY);
    DrawText(value, x + w - tw, y - 2, FS_BODY, vcol);
    return ROW_H;
}

static bool draw_action_button(Rectangle r, const char *label, bool enabled,
                                Color accent, Vector2 mouse, bool clicked)
{
    bool hot = enabled && CheckCollisionPointRec(mouse, r);
    if (hot) {
        Rectangle glow = { r.x - 2, r.y - 2, r.width + 4, r.height + 4 };
        DrawRectangleRounded(glow, 0.34f, 6, fade(accent, 30));
    }
    Color fill = enabled ? (hot ? accent : fade(accent, 44)) : fade(NAVY_700, 200);
    Color edge = enabled ? accent : NAVY_600;
    Color txt  = enabled ? (hot ? NAVY_950 : accent) : fade(INK_DIM, 140);
    DrawRectangleRounded(r, 0.30f, 6, fill);
    DrawRectangleRoundedLines(r, 0.30f, 6, edge);
    text_center(label, (int)(r.x + r.width / 2), (int)(r.y + r.height / 2 - 5), 11, txt);
    return enabled && hot && clicked;
}

static void draw_sparkline(Rectangle r, const int *values, int n, Color c)
{
    DrawRectangleRounded(r, 0.10f, 6, fade(NAVY_700, 160));
    if (n <= 0) return;
    int peak = 1;
    for (int i = 0; i < n; i++) if (values[i] > peak) peak = values[i];
    int show = n < HIST_N ? n : HIST_N;
    float bw = r.width / HIST_N;
    for (int i = 0; i < show; i++) {
        int v = values[n - show + i];
        float bh = (r.height - 6) * (float)v / (float)peak;
        int   bwi = (int)bw > 1 ? (int)bw - 1 : 1;
        float bx = r.x + i * bw;
        DrawRectangle((int)bx + 1, (int)(r.y + r.height - 3 - bh), bwi, (int)bh, fade(c, 200));
    }
}

/* ================================================================== */
/*  DrawRadar — scope, aircraft, compass, airport marker                */
/* ================================================================== */

static void draw_airport_icon(Scope sc)
{
    float r = 7.0f;
    DrawLineEx((Vector2){ sc.cx - r, sc.cy }, (Vector2){ sc.cx + r, sc.cy }, 2.0f, INK);
    DrawLineEx((Vector2){ sc.cx, sc.cy - r }, (Vector2){ sc.cx, sc.cy + r }, 2.0f, INK);
    DrawCircleLines((int)sc.cx, (int)sc.cy, r + 5, fade(INK, 130));
}

static void draw_scope_grid(Scope sc, float sweep_angle, Weather wx)
{
    DrawCircleV((Vector2){ sc.cx, sc.cy }, sc.radius + 10, fade(SCOPE_EDGE, 55));
    DrawCircleV((Vector2){ sc.cx, sc.cy }, sc.radius + 6, SCOPE_BG);

    for (int nm = 2; nm <= 10; nm += 2) {
        float rr = (float)nm / 10.0f * sc.radius;
        DrawCircleLines((int)sc.cx, (int)sc.cy, rr, (nm % 4 == 0) ? RING : RING_FAINT);
        if (nm % 4 == 0) {
            char lbl[8]; snprintf(lbl, sizeof(lbl), "%dNM", nm);
            DrawText(lbl, (int)(sc.cx + rr - 14), (int)sc.cy + 4, 11, fade(ATC_CYAN, 90));
        }
    }

    for (int deg = 0; deg < 360; deg += 30) {
        double rad = (deg - 90) * (PI / 180.0);
        Vector2 t0 = { sc.cx + cosf(rad) * (sc.radius + 2), sc.cy + sinf(rad) * (sc.radius + 2) };
        Vector2 t1 = { sc.cx + cosf(rad) * (sc.radius + 9), sc.cy + sinf(rad) * (sc.radius + 9) };
        DrawLineEx(t0, t1, 1.4f, fade(ATC_CYAN, 110));
    }

    const char *card[4] = { "N", "E", "S", "W" };
    for (int i = 0; i < 4; i++) {
        double rad = (i * 90 - 90) * (PI / 180.0);
        Vector2 p = { sc.cx + cosf(rad) * (sc.radius + 22) - 4, sc.cy + sinf(rad) * (sc.radius + 22) - 7 };
        DrawText(card[i], (int)p.x, (int)p.y, 14, fade(ATC_CYAN, 160));
    }

    /* sweep wedge, tinted by weather */
    Color wc = wx_color(wx);
    int segs = 44;
    for (int i = 0; i < segs; i++) {
        float a0 = sweep_angle - (float)i * 0.8f * DEG2RAD;
        float a1 = sweep_angle - (float)(i + 1) * 0.8f * DEG2RAD;
        unsigned char alpha = (unsigned char)(34 * (1.0f - (float)i / segs));
        Vector2 p0 = { sc.cx + cosf(a0) * sc.radius, sc.cy + sinf(a0) * sc.radius };
        Vector2 p1 = { sc.cx + cosf(a1) * sc.radius, sc.cy + sinf(a1) * sc.radius };
        DrawTriangle((Vector2){ sc.cx, sc.cy }, p1, p0, fade(wc, alpha));
    }
    Vector2 tip = { sc.cx + cosf(sweep_angle) * sc.radius, sc.cy + sinf(sweep_angle) * sc.radius };
    DrawLineEx((Vector2){ sc.cx, sc.cy }, tip, 1.5f, fade(ATC_CYAN, 200));

    DrawCircleLines((int)sc.cx, (int)sc.cy, sc.radius, SCOPE_EDGE);
    draw_airport_icon(sc);
}

/* Only the callsign is shown by default — no per-blip state text.
 * Selection adds a glowing pulsed ring and a brighter label.          */
static void draw_aircraft_blip(Vector2 p, Color c, const Aircraft *a, bool selected)
{
    float t = (float)GetTime();
    bool blink = a->emergency;

    if (blink) {
        float pr = 13 + 4 * sinf(t * 6.0f);
        DrawCircleV(p, pr, fade(ATC_RED, (unsigned char)(70 + 50 * sinf(t * 6.0f))));
    }

    Vector2 hv = heading_vector(a->heading_deg);
    float tick = selected ? 22.0f : 11.0f;
    unsigned char tickA = selected ? 220 : 110;
    Vector2 tip = { p.x + hv.x * tick, p.y + hv.y * tick };
    DrawLineEx(p, tip, selected ? 2.0f : 1.0f, fade(c, tickA));

    if (selected) {
        float pr = 15 + 3.0f * sinf(t * 4.0f);
        DrawCircleV(p, pr + 6, fade(ATC_CYAN, 26));
        DrawCircleV(p, pr + 3, fade(ATC_CYAN, 48));
        DrawCircleLines((int)p.x, (int)p.y, pr, WHITE_SEL);
        DrawCircleLines((int)p.x, (int)p.y, pr + 1, fade(WHITE_SEL, 130));
    }

    DrawCircleV(p, 8, fade(c, 40));
    DrawCircleV(p, 4, blink ? fade(ATC_RED, (unsigned char)(170 + 70 * sinf(t * 6.0f))) : c);
    DrawCircleLines((int)p.x, (int)p.y, 7, c);

    int bx = (int)p.x + 12, by = (int)p.y - 8;
    if (selected) DrawText(a->callsign, bx, by, 15, WHITE_SEL);
    else          DrawText(a->callsign, bx, by, 11, fade(c, 235));
}

static void DrawRadar(const Simulation *s, Scope sc, float sweep_angle, int selected)
{
    draw_scope_grid(sc, sweep_angle, s->weather);
    for (int i = 0; i < s->flight_count; i++) {
        const Aircraft *a = &s->flights[i];
        if (a->state == AC_DONE || a->state == AC_LOST || a->state == AC_DIVERTED) continue;
        draw_aircraft_blip((Vector2){ (float)a->x, (float)a->y }, aircraft_color(a), a, i == selected);
    }
}

/* ================================================================== */
/*  dashboard sections                                                  */
/* ================================================================== */

static int count_in_sector(const Simulation *s)
{
    int n = 0;
    for (int i = 0; i < s->flight_count; i++) {
        AircraftState st = s->flights[i].state;
        if (st != AC_DONE && st != AC_LOST && st != AC_DIVERTED) n++;
    }
    return n;
}

static int DrawSectorControl(const Simulation *s, Rectangle bounds)
{
    int x = (int)bounds.x, y = (int)bounds.y, right = (int)(bounds.x + bounds.width);
    char clk[8], buf[32];

    DrawText("SECTOR CONTROL", x, y, FS_TITLE, INK);
    y += 26;

    wall_clock(s->now, clk, sizeof(clk));
    snprintf(buf, sizeof(buf), "LOCAL %s", clk);
    DrawText(buf, x, y + 2, FS_VALUE_LG, ATC_CYAN);

    Color wc = wx_color(s->weather);
    char wxbuf[24]; snprintf(wxbuf, sizeof(wxbuf), "WX %s", weather_name(s->weather));
    int pw = MeasureText(wxbuf, FS_CAPTION) + 18;
    Rectangle badge = { (float)(right - pw), (float)y + 3, (float)pw, 20 };
    DrawRectangleRounded(badge, 0.5f, 8, fade(wc, 42));
    DrawRectangleRoundedLines(badge, 0.5f, 8, wc);
    DrawText(wxbuf, (int)badge.x + 9, (int)badge.y + 5, FS_CAPTION, wc);
    y += 26;

    return y - (int)bounds.y;
}

static int DrawAirportStatus(const Simulation *s, Rectangle bounds)
{
    int x = (int)bounds.x, y = (int)bounds.y, w = (int)bounds.width;
    draw_section_label(x, y, "AIRPORT STATUS");
    y += HEADER_H;

    double sc = sim_score(s);
    struct { const char *label; char buf[16]; Color c; } k[6];
    snprintf(k[0].buf, 16, "%d", count_in_sector(s));            k[0].label = "ACTIVE";      k[0].c = ATC_CYAN;
    snprintf(k[1].buf, 16, "%d", s->stats.flights_departed);      k[1].label = "DEPARTED";    k[1].c = ATC_GREEN;
    snprintf(k[2].buf, 16, "%.0f", sc);                           k[2].label = "SCORE";       k[2].c = sc >= 0 ? ATC_GREEN : ATC_RED;
    snprintf(k[3].buf, 16, "%d", s->stats.emergencies);           k[3].label = "EMERGENCIES"; k[3].c = s->stats.emergencies ? ATC_AMBER : INK_DIM;
    snprintf(k[4].buf, 16, "%d", s->stats.flights_lost);          k[4].label = "LOST";        k[4].c = s->stats.flights_lost ? ATC_RED : INK_DIM;
    snprintf(k[5].buf, 16, "%d", s->stats.go_arounds);            k[5].label = "GO-AROUNDS";  k[5].c = s->stats.go_arounds ? ATC_ORANGE : INK_DIM;

    int gap = 6, cw = (w - gap) / 2, ch = 42;
    for (int i = 0; i < 6; i++) {
        int cx = x + (i % 2) * (cw + gap);
        int cy = y + (i / 2) * (ch + gap);
        draw_kpi_card((Rectangle){ (float)cx, (float)cy, (float)cw, (float)ch }, k[i].label, k[i].buf, k[i].c);
    }
    y += 3 * ch + 2 * gap;

    return y - (int)bounds.y;
}

static int DrawRunwayPanel(const Simulation *s, Rectangle bounds)
{
    int x = (int)bounds.x, y = (int)bounds.y, w = (int)bounds.width;
    char buf[40];

    snprintf(buf, sizeof(buf), "RUNWAY UTILISATION  %.0f%%", sim_runway_utilisation(s) * 100.0);
    DrawText(buf, x, y, FS_CAPTION, INK_DIM);
    y += 14;
    draw_progress_bar((Rectangle){ (float)x, (float)y, (float)w, 8 }, (float)sim_runway_utilisation(s), ATC_CYAN);
    y += 12;

    int rn = s->num_runways, rgap = 8;
    int rw_w = (w - (rn - 1) * rgap) / (rn > 0 ? rn : 1);
    for (int i = 0; i < rn; i++) {
        Color c = runway_color(s->runways[i].state);
        Rectangle r = { (float)(x + i * (rw_w + rgap)), (float)y, (float)rw_w, 30 };
        DrawRectangleRounded(r, 0.26f, 6, fade(c, 34));
        DrawRectangleRoundedLines(r, 0.26f, 6, c);
        char lbl[8]; snprintf(lbl, sizeof(lbl), "%02d", i + 1);
        DrawText(lbl, (int)r.x + 8, (int)r.y + 5, FS_BODY, c);
        DrawText(runway_status_label(s->runways[i].state), (int)r.x + 8, (int)r.y + 17, 9, fade(c, 225));
    }
    y += 30;

    return y - (int)bounds.y;
}

static int DrawGatePanel(const Simulation *s, Rectangle bounds)
{
    int x = (int)bounds.x, y = (int)bounds.y, w = (int)bounds.width;
    char buf[40];

    snprintf(buf, sizeof(buf), "GATE UTILISATION  %.0f%%", sim_gate_utilisation(s) * 100.0);
    DrawText(buf, x, y, FS_CAPTION, INK_DIM);
    y += 14;
    draw_progress_bar((Rectangle){ (float)x, (float)y, (float)w, 8 }, (float)sim_gate_utilisation(s), ATC_GREEN);
    y += 12;

    int cols = 6, gap = 5;
    int cw = (w - (cols - 1) * gap) / cols;
    for (int i = 0; i < s->num_gates; i++) {
        int gx = x + (i % cols) * (cw + gap);
        int gy = y + (i / cols) * (18 + gap);
        Rectangle r = { (float)gx, (float)gy, (float)cw, 18 };
        Color c = gate_color(s->gates[i].state);
        DrawRectangleRounded(r, 0.3f, 5, c);
        char gl[4]; snprintf(gl, sizeof(gl), "%d", i + 1);
        Color txt = s->gates[i].state == GATE_VACANT ? INK_DIM : NAVY_950;
        text_center(gl, gx + cw / 2, gy + 3, 11, txt);
    }
    y += ((s->num_gates + cols - 1) / cols) * (18 + gap);

    return y - (int)bounds.y;
}

static int DrawFlightStrip(const Simulation *s, int selected, Rectangle bounds)
{
    int x = (int)bounds.x, y = (int)bounds.y, w = (int)bounds.width, right = x + w;
    draw_section_label(x, y, "SELECTED FLIGHT");
    y += HEADER_H;

    if (selected < 0 || selected >= s->flight_count) {
        Rectangle empty = { (float)x, (float)y, (float)w, 40 };
        DrawRectangleRounded(empty, 0.16f, 6, fade(NAVY_700, 150));
        text_center("No Aircraft Selected", x + w / 2, y + 13, FS_BODY, INK_DIM);
        y += 40;
        return y - (int)bounds.y;
    }

    const Aircraft *a = &s->flights[selected];
    Color c = aircraft_color(a);

    DrawText(a->callsign, x, y, 24, c);
    int cw = MeasureText(a->callsign, 24);
    DrawText(a->ac_type, x + cw + 10, y + 7, FS_SUBHEAD, INK);
    char wk[2] = { a->wake, 0 };
    DrawText(wk, right - 18, y + 3, 16, fade(INK_DIM, 220));
    y += 30;

    char orig[32], dest[24], wait[24], fuel_lbl[24];
    snprintf(orig, sizeof(orig), "%s (%s)", a->origin_city, a->origin);
    bool outbound = (a->state == AC_TAXI_OUT || a->state == AC_DEPARTING || a->state == AC_AIRBORNE_OUT);
    snprintf(dest, sizeof(dest), "%s", outbound ? "OPEN SECTOR" : "TOWER");
    snprintf(wait, sizeof(wait), "%.1f MIN", a->total_wait);

    y += draw_kv_row(x, y, w, "AIRLINE",     a->airline, INK);
    y += draw_kv_row(x, y, w, "ORIGIN",      orig, INK);
    y += draw_kv_row(x, y, w, "DESTINATION", dest, INK);

    Color fc = a->fuel > 50 ? ATC_GREEN : a->fuel > FUEL_EMERGENCY ? ATC_AMBER : ATC_RED;
    snprintf(fuel_lbl, sizeof(fuel_lbl), "FUEL  %.0f%%", a->fuel);
    DrawText(fuel_lbl, x, y, FS_CAPTION, INK_DIM);
    y += 12;
    draw_progress_bar((Rectangle){ (float)x, (float)y, (float)w, 8 }, (float)(a->fuel / FUEL_INITIAL), fc);
    y += 14;

    y += draw_kv_row(x, y, w, "WAKE CATEGORY", wake_label(a->wake), INK);
    y += draw_kv_row(x, y, w, "CURRENT STATE", state_name(a->state), c);
    y += draw_kv_row(x, y, w, "QUEUE WAIT",    wait, INK);
    y += draw_kv_row(x, y, w, "PRIORITY",      a->user_priority ? "EXPEDITED" : "STANDARD",
                     a->user_priority ? ATC_CYAN : INK_DIM);

    return y - (int)bounds.y;
}

static int DrawControllerActions(Simulation *s, int selected, Rectangle bounds,
                                  Vector2 mouse, bool clicked)
{
    int x = (int)bounds.x, y = (int)bounds.y, w = (int)bounds.width;
    draw_section_label(x, y, "CONTROLLER ACTIONS");
    y += HEADER_H;

    int gap = 6, bw = (w - 3 * gap) / 4, bh = 30;
    Rectangle b0 = { (float)x,                    (float)y, (float)bw, (float)bh };
    Rectangle b1 = { (float)(x + (bw+gap)),        (float)y, (float)bw, (float)bh };
    Rectangle b2 = { (float)(x + 2*(bw+gap)),      (float)y, (float)bw, (float)bh };
    Rectangle b3 = { (float)(x + 3*(bw+gap)),      (float)y, (float)bw, (float)bh };

    if (draw_action_button(b0, "HOLD", sim_can_hold(s, selected), ATC_AMBER, mouse, clicked))
        sim_cmd_hold(s, selected);
    if (draw_action_button(b1, "EXPEDITE", sim_can_prioritize(s, selected), ATC_CYAN, mouse, clicked))
        sim_cmd_prioritize(s, selected);
    if (draw_action_button(b2, "GO AROUND", sim_can_go_around(s, selected), ATC_VIOLET, mouse, clicked))
        sim_cmd_go_around(s, selected);
    if (draw_action_button(b3, "DIVERT", sim_can_divert(s, selected), ATC_BLUE, mouse, clicked))
        sim_cmd_divert(s, selected);
    y += bh + 8;

    /* Emergency Override reuses sim_cmd_prioritize — the sim engine has no
     * separate "override" command, so this is the same expedite clearance,
     * exposed here only for aircraft that have already declared a fuel
     * emergency (a->emergency), giving it a distinct, critical-only affordance. */
    bool emerg_ok = selected >= 0 && selected < s->flight_count &&
                    s->flights[selected].emergency && sim_can_prioritize(s, selected);
    Rectangle be = { (float)x, (float)y, (float)w, 28 };
    if (draw_action_button(be, "EMERGENCY OVERRIDE", emerg_ok, ATC_RED, mouse, clicked))
        sim_cmd_prioritize(s, selected);
    y += 28;

    return y - (int)bounds.y;
}

static int DrawPerformancePanel(const Simulation *s, double speed, bool paused,
                                 const PerfHistory *ph, Rectangle bounds)
{
    int x = (int)bounds.x, y = (int)bounds.y, w = (int)bounds.width;
    draw_section_label(x, y, "PERFORMANCE");
    y += HEADER_H;

    draw_sparkline((Rectangle){ (float)x, (float)y, (float)w, 36 }, ph->buckets, ph->n, ATC_CYAN);
    y += 42;

    char events[24], heap[16], spd[24], fps[16], mem[24];
    snprintf(events, sizeof(events), "%ld", s->stats.events_processed);
    snprintf(heap, sizeof(heap), "%d", pq_count(&s->pq));
    snprintf(spd, sizeof(spd), "%.0fx %s", speed, paused ? "PAUSED" : "");
    snprintf(fps, sizeof(fps), "%d", GetFPS());
    double mem_kb = ((double)sizeof(Simulation) + (double)s->pq.capacity * (double)sizeof(AtcEvent)) / 1024.0;
    snprintf(mem, sizeof(mem), "%.0f KB", mem_kb);

    y += draw_kv_row(x, y, w, "EVENTS PROCESSED", events, ATC_CYAN);
    y += draw_kv_row(x, y, w, "HEAP SIZE",        heap,   INK);
    y += draw_kv_row(x, y, w, "SIMULATION SPEED", spd,    paused ? ATC_AMBER : ATC_CYAN);
    y += draw_kv_row(x, y, w, "FPS",              fps,    INK);
    y += draw_kv_row(x, y, w, "MEMORY USAGE",     mem,    INK);

    return y - (int)bounds.y;
}

static void DrawDashboard(Simulation *s, int selected, double speed, bool paused,
                          const PerfHistory *ph, Vector2 mouse, bool clicked, Rectangle bounds)
{
    DrawRectangle((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height, NAVY_900);
    DrawLine((int)bounds.x, (int)bounds.y, (int)bounds.x, (int)(bounds.y + bounds.height), NAVY_600);

    Rectangle cur = { bounds.x + PAD_OUTER, bounds.y + PAD_OUTER,
                       bounds.width - 2 * PAD_OUTER, bounds.height - 2 * PAD_OUTER };

    cur.y += DrawSectorControl(s, cur)                         + GAP_SECTION;
    cur.y += DrawAirportStatus(s, cur)                          + GAP_SECTION;
    cur.y += DrawRunwayPanel(s, cur)                            + GAP_SECTION;
    cur.y += DrawGatePanel(s, cur)                               + GAP_SECTION;
    cur.y += DrawFlightStrip(s, selected, cur)                   + GAP_SECTION;
    cur.y += DrawControllerActions(s, selected, cur, mouse, clicked) + GAP_SECTION;
    cur.y += DrawPerformancePanel(s, speed, paused, ph, cur)     + GAP_SECTION;
}

/* ================================================================== */
/*  DrawEventLog — bottom console, colour-coded by event category      */
/* ================================================================== */

static void DrawEventLog(const Simulation *s, Rectangle bounds)
{
    DrawRectangle((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height, NAVY_900);
    DrawLine((int)bounds.x, (int)bounds.y, (int)(bounds.x + bounds.width), (int)bounds.y, NAVY_600);

    int x = (int)bounds.x + 20, y = (int)bounds.y + 12;
    int w = (int)bounds.width - 40;
    draw_section_label(x, y, "EVENT LOG");
    y += HEADER_H + 4;

    int row_h = 20;
    int max_rows = ((int)bounds.height - (y - (int)bounds.y) - 8) / row_h;
    int count = sim_log_count(s);
    if (count > max_rows) count = max_rows;
    if (count < 0) count = 0;

    for (int i = 0; i < count; i++) {
        SimLogEntry e;
        if (!sim_log_at(s, i, &e)) break;
        Color c = event_category_color(&e);
        char clk[8]; wall_clock(e.t, clk, sizeof(clk));

        Rectangle row = { (float)x, (float)(y + i * row_h), (float)w, (float)(row_h - 3) };
        if (i == 0) DrawRectangleRounded(row, 0.3f, 6, fade(c, 34));
        DrawRectangle(x, (int)row.y, 3, (int)row.height, c);
        DrawText(clk, x + 12, (int)row.y + 3, FS_CAPTION, fade(c, 210));
        DrawText(e.msg, x + 64, (int)row.y + 3, FS_LABEL, i == 0 ? INK : fade(c, 235));
    }
}

/* ================================================================== */
/*  help overlay                                                       */
/* ================================================================== */

static void draw_help(void)
{
    DrawRectangle(0, 0, WIN_W, WIN_H, (Color){ 4, 7, 14, 220 });
    int w = 660, h = 480, x = (WIN_W - w) / 2, y = (WIN_H - h) / 2;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, (float)h }, 0.04f, 8, NAVY_800);
    DrawRectangleRoundedLines((Rectangle){ (float)x, (float)y, (float)w, (float)h }, 0.04f, 8, ATC_CYAN);

    int tx = x + 36, ty = y + 30;
    DrawText("SECTOR CONTROL - REFERENCE", tx, ty, 22, INK); ty += 40;

    DrawText("CONTROLLER DECISIONS", tx, ty, 13, ATC_CYAN); ty += 24;
    const char *cmds[5][2] = {
        { "Click",  "select an aircraft on the scope" },
        { "E",      "Expedite -- priority for the next runway" },
        { "H",      "Hold -- send an inbound into the pattern" },
        { "D / G",  "Divert to alternate / wave off a landing" },
        { "button", "Emergency Override -- expedite a declared emergency" },
    };
    for (int i = 0; i < 5; i++) {
        DrawText(cmds[i][0], tx, ty, 14, ATC_AMBER);
        DrawText(cmds[i][1], tx + 90, ty, 14, INK_DIM); ty += 22;
    }
    ty += 14;
    DrawText("VIEW", tx, ty, 13, ATC_CYAN); ty += 24;
    DrawText("SPACE pause    1 2 3 4  warp 1x 5x 25x 100x", tx, ty, 14, INK_DIM); ty += 22;
    DrawText("R reset scenario    F1 toggle this help    ESC quit", tx, ty, 14, INK_DIM); ty += 34;

    DrawText("BLIP LEGEND", tx, ty, 13, ATC_CYAN); ty += 24;
    struct { Color c; const char *t; } leg[5] = {
        { ATC_CYAN, "inbound / on final" }, { ATC_AMBER, "holding" }, { ATC_VIOLET, "on the ground" },
        { ATC_BLUE, "departing" }, { ATC_RED, "emergency / lost" },
    };
    for (int i = 0; i < 5; i++) {
        int lx = tx + (i % 3) * 200, lyy = ty + (i / 3) * 24;
        DrawCircleV((Vector2){ (float)(lx + 6), (float)(lyy + 7) }, 5, leg[i].c);
        DrawText(leg[i].t, lx + 18, lyy, 13, INK_DIM);
    }
}

/* ================================================================== */
/*  visual easing + picking + performance sampling                     */
/* ================================================================== */

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
        float dx = (float)a->x - p.x, dy = (float)a->y - p.y, d2 = dx * dx + dy * dy;
        if (d2 < best_d2) { best_d2 = d2; best = i; }
    }
    return best;
}

static void perf_history_sample(PerfHistory *ph, const Simulation *s)
{
    while (s->now >= ph->next_t) {
        int v = (int)(s->stats.events_processed - ph->last_events);
        ph->last_events = s->stats.events_processed;
        if (ph->n < HIST_N) ph->buckets[ph->n++] = v;
        else { memmove(ph->buckets, ph->buckets + 1, (HIST_N - 1) * sizeof(int));
               ph->buckets[HIST_N - 1] = v; }
        ph->next_t += 2.0;
    }
}

/* ================================================================== */
/*  scene composition (shared by run_app and run_snapshot)             */
/* ================================================================== */

static void draw_scene(Simulation *s, Layout lay, Scope sc, float sweep, int selected,
                       double speed, bool paused, const PerfHistory *ph,
                       Vector2 mouse, bool clicked, bool help)
{
    DrawRectangleGradientV(0, 0, WIN_W, STAGE_H, NAVY_900, NAVY_950);
    DrawRadar(s, sc, sweep, selected);

    DrawText("RANGE 10 NM  ·  CH 1 APPROACH", 18, 18, 14, fade(ATC_CYAN, 170));
    DrawText("Click a target to issue a clearance  ·  F1 for help",
             18, STAGE_H - 28, 13, INK_DIM);

    DrawDashboard(s, selected, speed, paused, ph, mouse, clicked, lay.panel);
    DrawEventLog(s, lay.console);
    if (help) draw_help();
}

/* ================================================================== */
/*  main application loop                                              */
/* ================================================================== */

int run_app(uint64_t seed, int num_runways, int num_gates,
            double mean_interarrival, double horizon)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WIN_W, WIN_H, "Sector Control -- ATC Operations Dashboard");
    SetTargetFPS(60);

    /* Simulation is ~800KB (a 4096-slot Aircraft table) -- too large for a
     * comfortable stack frame on platforms with a small default thread
     * stack (Windows/MSVC reserves 1MB by default), so it is heap-allocated
     * rather than declared as a local.                                    */
    Simulation *sim = malloc(sizeof(Simulation));
    if (!sim) { CloseWindow(); return 1; }
    sim_init(sim, seed, num_runways, num_gates, mean_interarrival, horizon);

    Layout lay = compute_layout();
    Scope  sc  = scope_geometry(lay.radar);
    double speed = 25.0;
    bool   paused = false, help = false;
    float  sweep = 0.0f;
    int    selected = -1;
    PerfHistory ph = {0}; ph.next_t = 2.0;

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
            sim_free(sim);
            sim_init(sim, ++seed, num_runways, num_gates, mean_interarrival, horizon);
            selected = -1;
            memset(&ph, 0, sizeof(ph)); ph.next_t = 2.0;
        }

        if (clicked && mouse.x < lay.radar.width && mouse.y < lay.radar.height)
            selected = pick_aircraft(sim, mouse);

        if (selected >= 0) {
            if (IsKeyPressed(KEY_E)) sim_cmd_prioritize(sim, selected);
            if (IsKeyPressed(KEY_H)) sim_cmd_hold(sim, selected);
            if (IsKeyPressed(KEY_D)) sim_cmd_divert(sim, selected);
            if (IsKeyPressed(KEY_G)) sim_cmd_go_around(sim, selected);
        }

        if (!paused) {
            double target = sim->now + speed * dt;
            AtcEvent peek; int guard = 0;
            while (pq_peek(&sim->pq, &peek) && peek.execution_time <= target && guard < 100000) {
                sim_step(sim); guard++;
            }
            if (sim->now < target) sim->now = target;
        }
        perf_history_sample(&ph, sim);

        if (selected >= 0) {
            AircraftState st = sim->flights[selected].state;
            if (st == AC_DONE || st == AC_LOST || st == AC_DIVERTED) selected = -1;
        }

        update_visuals(sim, sc, paused ? 0.0f : dt);
        sweep -= dt * 1.4f;

        BeginDrawing();
        ClearBackground(NAVY_950);
        draw_scene(sim, lay, sc, sweep, selected, speed, paused, &ph, mouse, clicked, help);
        EndDrawing();
    }

    sim_free(sim);
    free(sim);
    CloseWindow();
    return 0;
}

/* ================================================================== */
/*  one-frame snapshot for the README preview                          */
/* ================================================================== */

int run_snapshot(uint64_t seed, int num_runways, int num_gates,
                 double mean_interarrival, double horizon,
                 double at_minute, const char *outfile)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WIN_W, WIN_H, "Sector Control -- snapshot");
    SetTargetFPS(60);

    Simulation *sim = malloc(sizeof(Simulation));
    if (!sim) { CloseWindow(); return 1; }
    sim_init(sim, seed, num_runways, num_gates, mean_interarrival, horizon);

    AtcEvent peek;
    while (pq_peek(&sim->pq, &peek) && peek.execution_time <= at_minute)
        sim_step(sim);
    if (sim->now < at_minute) sim->now = at_minute;

    Layout lay = compute_layout();
    Scope  sc  = scope_geometry(lay.radar);
    PerfHistory ph = {0}; ph.next_t = 2.0;
    perf_history_sample(&ph, sim);

    /* Render into a fixed WIN_W x WIN_H off-screen texture rather than
     * reading back the visible window. LoadImageFromScreen() sizes its
     * capture from the display's DPI scale, which disagrees with the
     * window's real framebuffer (blank/misaligned output) whenever the
     * window doesn't fit the monitor at that scale -- a render texture
     * sidesteps DPI and window placement entirely.                      */
    RenderTexture2D target = LoadRenderTexture(WIN_W, WIN_H);

    float sweep = -0.6f;
    for (int f = 0; f < 30; f++) {
        update_visuals(sim, sc, 1.0f / 60.0f);
        sweep -= (1.0f / 60.0f) * 1.4f;

        BeginTextureMode(target);
        ClearBackground(NAVY_950);
        draw_scene(sim, lay, sc, sweep, -1, 25.0, false, &ph,
                   (Vector2){ -1, -1 }, false, false);
        EndTextureMode();

        /* Present the same frame on-screen too, purely to pump the window's
         * event loop / frame pacing (SetTargetFPS) while it briefly exists. */
        BeginDrawing();
        ClearBackground(NAVY_950);
        DrawTextureRec(target.texture,
                       (Rectangle){ 0, 0, (float)WIN_W, -(float)WIN_H },
                       (Vector2){ 0, 0 }, WHITE);
        EndDrawing();
    }

    /* Render-texture images are stored bottom-up in GPU memory; flip
     * before export so the PNG comes out right-side up.                 */
    Image frame = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&frame);
    ExportImage(frame, outfile);
    UnloadImage(frame);
    UnloadRenderTexture(target);

    sim_free(sim);
    free(sim);
    CloseWindow();
    return 0;
}
