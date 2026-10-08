/* doomgeneric_vita.c – Chex Quest Collection on PS Vita */
#define VITA_GAME_DATA_DIR "ux0:/data/chexquestcollection/"

#include "d_event.h"
#include "deh_str.h"
#include "doomdef.h"
#include "doomgeneric.h"
#include "doomfeatures.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "doomtype.h"
#include "g_game.h"
#include "p_saveg.h"
#include "r_defs.h"
#include "i_sound.h"
#include "m_argv.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"

extern void D_PostEvent(event_t *ev);
/* The engine keeps these internal in this tree; the Vita port drives the loop
   and walks the thing list to interpolate the picture. */
extern void D_Display(void);
extern void TryRunTics(void);
extern thinker_t thinkercap;
extern int numsectors;
extern sector_t *sectors;
void P_MobjThinker(mobj_t *mobj);
#include "opl3.h"
#include "launcher_art.h"
#include "launcher_qr.h"
#include <stdint.h>
#include <math.h>
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TICRATE 35
#define SCREENWIDTH 320
#define SCREENHEIGHT 200
#define VITA_W 960
#define VITA_H 544
#define OUTPUT_RATE 48000
#define AUDIO_GRANULARITY 256
#define MIX_CHANNELS 16

byte *I_VideoBuffer = NULL;
int screenvisible = 1, screensaver_mode = 0, vanilla_keyboard_mapping = 0;
int usegamma = 0, usemouse = 0, snd_musicdevice = 3; /* 3 = SB/OPL for music */
int mouse_acceleration = 0, mouse_threshold = 0;

static SceUID fb_memuid;
static void *fb_base = NULL;
static int display_ready = 0, frame_count = 0;
static uint32_t cmap[256];

/* Frame pacing. 35 is one frame per game tic, the original behaviour. 60 is
   one frame per vsync with the view interpolated between the last two tics,
   so movement is continuous instead of stepping 35 times per second. */
#define FPS_CLASSIC 35
#define FPS_SMOOTH 60
static int fps_target = FPS_SMOOTH;
static void settings_load(void);
static void settings_save(void);
static int launcher_frame = 0;
static const unsigned char *menu_music_data = NULL;
static int menu_music_length = 0;
static int menu_music_position = 0;
static int menu_music_rate_phase = 0;
static volatile int menu_music_active = 0;
/* Defined with the SFX engine below; used by return_to_launcher(). */
static volatile int sfx_running;
static uint32_t base_time = 0;

static uint32_t get_ms(void) { return sceKernelGetProcessTimeLow() / 1000; }

static void debug_log(const char *msg) {
  FILE *f = fopen(VITA_GAME_DATA_DIR "debug.log", "a");
  if (f) {
    fprintf(f, "%s\n", msg);
    fclose(f);
  }
}
static void debug_logf(const char *fmt, ...) {
  char buf[512];
  va_list a;
  va_start(a, fmt);
  vsnprintf(buf, sizeof(buf), fmt, a);
  va_end(a);
  debug_log(buf);
}

static void fatal_error(const char *message) {
  debug_logf("FATAL: %s", message);
  sceKernelExitProcess(1);
}

static void init_display(void) {
  int sz = (960 * 544 * 4 + 0xFFFFF) & ~0xFFFFF;
  SceDisplayFrameBuf fb;
  int ret;
  fb_memuid = sceKernelAllocMemBlock(
      "framebuffer", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, sz, NULL);
  if (fb_memuid < 0)
    fb_memuid = sceKernelAllocMemBlock(
        "framebuffer", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, sz, NULL);
  if (fb_memuid < 0) {
    debug_logf("Alloc failed: 0x%08X", fb_memuid);
    return;
  }
  sceKernelGetMemBlockBase(fb_memuid, &fb_base);
  memset(fb_base, 0, sz);
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  fb.base = fb_base;
  fb.pitch = 960;
  fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
  fb.width = 960;
  fb.height = 544;
  ret = sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
  debug_logf("SetFrameBuf: 0x%08X", ret);
  sceDisplayWaitVblankStart();
  display_ready = 1;
}

/* Input */
#define KQUEUE_SZ 64
#define DEADZONE 35
static struct {
  int pressed;
  unsigned char key;
} kq[KQUEUE_SZ];
static int kq_r = 0, kq_w = 0;
static SceCtrlData pad_prev;
static int input_init = 0, analog_held[6];
static int quicksave_cooldown = 0, quickload_cooldown = 0;
static int current_weapon = 1, weapon_cycle_cooldown = 0;
static unsigned char pending_weapon_release = 0; /* key to release next tic */
/* D-Pad sinistra/destra: ripetizione tenendo premuto (tick di gioco ≈ 35 Hz) */
static int weapon_l_charge = 0, weapon_r_charge = 0;
static int weapon_touch_prev_in_bar = 0;
static int save_directory_ready = 0;
#define WEAPON_KEY_GAP 3       /* min tra un cambio arma e il successivo */
#define WEAPON_HOLD_INITIAL 12 /* attesa prima della prima ripetizione */
#define WEAPON_HOLD_REPEAT 4   /* tra una ripetizione e l'altra mentre tieni */

void I_InitGraphics(void);
void I_FinishUpdate(void);

static const char menu_glyphs[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-./<abcdefghijklmnopqrstuvwxyz>+";
/* 5 columns x 9 rows per glyph; bit 0 is the top row, bit 6 the baseline. */
#define MENU_FONT_ROWS 9
static const unsigned short menu_font[][5] = {
    {0x07e,0x011,0x011,0x011,0x07e}, /* A */
    {0x07f,0x049,0x049,0x049,0x036}, /* B */
    {0x03e,0x041,0x041,0x041,0x022}, /* C */
    {0x07f,0x041,0x041,0x022,0x01c}, /* D */
    {0x07f,0x049,0x049,0x049,0x041}, /* E */
    {0x07f,0x009,0x009,0x009,0x001}, /* F */
    {0x03e,0x041,0x049,0x049,0x07a}, /* G */
    {0x07f,0x008,0x008,0x008,0x07f}, /* H */
    {0x000,0x041,0x07f,0x041,0x000}, /* I */
    {0x020,0x040,0x041,0x03f,0x001}, /* J */
    {0x07f,0x008,0x014,0x022,0x041}, /* K */
    {0x07f,0x040,0x040,0x040,0x040}, /* L */
    {0x07f,0x002,0x00c,0x002,0x07f}, /* M */
    {0x07f,0x004,0x008,0x010,0x07f}, /* N */
    {0x03e,0x041,0x041,0x041,0x03e}, /* O */
    {0x07f,0x009,0x009,0x009,0x006}, /* P */
    {0x03e,0x041,0x051,0x021,0x05e}, /* Q */
    {0x07f,0x009,0x019,0x029,0x046}, /* R */
    {0x046,0x049,0x049,0x049,0x031}, /* S */
    {0x001,0x001,0x07f,0x001,0x001}, /* T */
    {0x03f,0x040,0x040,0x040,0x03f}, /* U */
    {0x01f,0x020,0x040,0x020,0x01f}, /* V */
    {0x03f,0x040,0x038,0x040,0x03f}, /* W */
    {0x063,0x014,0x008,0x014,0x063}, /* X */
    {0x007,0x008,0x070,0x008,0x007}, /* Y */
    {0x061,0x051,0x049,0x045,0x043}, /* Z */
    {0x03e,0x045,0x049,0x051,0x03e}, /* 0 */
    {0x000,0x042,0x07f,0x040,0x000}, /* 1 */
    {0x062,0x051,0x049,0x049,0x046}, /* 2 */
    {0x022,0x041,0x049,0x049,0x036}, /* 3 */
    {0x018,0x014,0x012,0x07f,0x010}, /* 4 */
    {0x02f,0x049,0x049,0x049,0x031}, /* 5 */
    {0x03e,0x049,0x049,0x049,0x032}, /* 6 */
    {0x001,0x071,0x009,0x005,0x003}, /* 7 */
    {0x036,0x049,0x049,0x049,0x036}, /* 8 */
    {0x026,0x049,0x049,0x049,0x03e}, /* 9 */
    {0x000,0x036,0x036,0x000,0x000}, /* : */
    {0x000,0x040,0x040,0x000,0x000}, /* - */
    {0x000,0x060,0x060,0x000,0x000}, /* . */
    {0x060,0x010,0x008,0x004,0x003}, /* / */
    {0x011,0x00a,0x004,0x000,0x000}, /* < */
    {0x074,0x054,0x07c,0x078,0x000}, /* a */
    {0x07f,0x044,0x064,0x03c,0x000}, /* b */
    {0x078,0x044,0x044,0x044,0x000}, /* c */
    {0x030,0x07c,0x044,0x064,0x07f}, /* d */
    {0x010,0x07c,0x054,0x054,0x058}, /* e */
    {0x008,0x008,0x07f,0x009,0x009}, /* f */
    {0x080,0x1fc,0x154,0x15c,0x1cc}, /* g */
    {0x07f,0x004,0x00c,0x078,0x000}, /* h */
    {0x044,0x07d,0x07c,0x040,0x000}, /* i */
    {0x104,0x104,0x1fd,0x000,0x000}, /* j */
    {0x07f,0x018,0x06c,0x044,0x000}, /* k */
    {0x041,0x07f,0x07f,0x040,0x000}, /* l */
    {0x07c,0x00c,0x07c,0x00c,0x07c}, /* m */
    {0x07c,0x004,0x00c,0x078,0x000}, /* n */
    {0x030,0x07c,0x044,0x044,0x038}, /* o */
    {0x1fc,0x044,0x064,0x03c,0x000}, /* p */
    {0x030,0x07c,0x044,0x064,0x1fc}, /* q */
    {0x07c,0x00c,0x004,0x00c,0x000}, /* r */
    {0x05c,0x054,0x074,0x020,0x000}, /* s */
    {0x004,0x004,0x07f,0x044,0x044}, /* t */
    {0x07c,0x040,0x060,0x07c,0x000}, /* u */
    {0x004,0x03c,0x060,0x070,0x00c}, /* v */
    {0x03c,0x070,0x018,0x070,0x07c}, /* w */
    {0x040,0x06c,0x018,0x038,0x044}, /* x */
    {0x104,0x13c,0x0e0,0x070,0x00c}, /* y */
    {0x064,0x074,0x04c,0x044,0x000}, /* z */
    {0x042,0x024,0x018,0x018,0x000}, /* > */
    {0x010,0x010,0x07c,0x010,0x010}, /* + */
};

static void draw_menu_text_scaled(int x, int y, const char *text, byte color,
                                  int scale)
{
    int n, col, row, px, py;
    for (; *text; ++text)
    {
        int index = -1;
        char ch = *text;
        for (n = 0; n < (int)(sizeof(menu_glyphs) - 1); ++n)
            if (menu_glyphs[n] == ch) { index = n; break; }
        if (index >= 0)
            for (col = 0; col < 5; ++col)
                for (row = 0; row < MENU_FONT_ROWS; ++row)
                    if (menu_font[index][col] & (1u << row))
                        for (py = 0; py < scale; ++py)
                            for (px = 0; px < scale; ++px)
                            {
                                int yy = y + row * scale + py;
                                int xx = x + col * scale + px;
                                if (xx >= 0 && xx < SCREENWIDTH &&
                                    yy >= 0 && yy < SCREENHEIGHT)
                                    I_VideoBuffer[yy * SCREENWIDTH + xx] = color;
                            }
        x += 6 * scale;
    }
}

static void draw_menu_text(int x, int y, const char *text, byte color)
{
    draw_menu_text_scaled(x, y, text, color, 1);
}

static int menu_text_width(const char *text, int scale)
{
    int n = (int)strlen(text);
    return n > 0 ? (n * 6 - 1) * scale : 0;
}

static void draw_menu_text_right(int right, int y, const char *text, byte color,
                                 int scale)
{
    draw_menu_text_scaled(right - menu_text_width(text, scale), y, text, color,
                          scale);
}

static void launcher_rect(int x, int y, int w, int h, byte color)
{
    int row;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREENWIDTH) w = SCREENWIDTH - x;
    if (y + h > SCREENHEIGHT) h = SCREENHEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (row = 0; row < h; ++row)
        memset(I_VideoBuffer + (y + row) * SCREENWIDTH + x, color, w);
}

static void launcher_logo_pixel(int x, int y, int width, int height,
                                const unsigned char *logo)
{
    int px, py;
    for (py = 0; py < height; ++py) {
        for (px = 0; px < width; ++px) {
            int pos = (py * width + px) * 2;
            unsigned char alpha = logo[pos + 1];
            if (!alpha || alpha < 128) continue;
            if (x + px >= 0 && x + px < SCREENWIDTH && y + py >= 0 && y + py < SCREENHEIGHT)
                I_VideoBuffer[(y + py) * SCREENWIDTH + x + px] = logo[pos];
        }
    }
}

/* ------------------------------------------------------------------ *
 * Launcher palette, layout and data-file bookkeeping                  *
 * ------------------------------------------------------------------ */

/* Launcher colours are RGB332 indices: launcher_rgb() unpacks them. */
#define L_RGB(r, g, b) \
    ((byte)((((r) >> 5) << 5) | (((g) >> 5) << 2) | ((b) >> 6)))
#define L_COL_GOLD      L_RGB(255, 200, 72)
#define L_COL_GOLD_DIM  L_RGB(170, 132, 48)
#define L_COL_WHITE     L_RGB(255, 255, 255)
#define L_COL_TEXT      L_RGB(214, 216, 232)
#define L_COL_TEXT_DIM  L_RGB(140, 142, 160)
#define L_COL_READY     L_RGB(104, 224, 128)
#define L_COL_ALERT     L_RGB(236, 84, 72)
#define L_COL_LINE      L_RGB(104, 110, 148)

/* Geometry shared with scripts/prepare_launcher.py, which bakes the bands. */
#define L_ROW_TOP 32
#define L_ROW_H 33
#define L_ROW_GAP 3
#define L_ROW_X 6
#define L_ROW_W 308
#define L_FOOTER_Y 177
#define L_ICON_SLOT 96
#define L_ITEMS 4
#define L_ITEM_DATA 3

static int launcher_frame_tick = 0;
static int launcher_data_mask = 0;

/* Files the collection needs and the games that use them (bit = file index). */
#define DATA_FILES 4
static const char *const data_file_names[DATA_FILES] = {
    "CHEX.WAD", "CHEX2.WAD", "chex3v.wad", "chex3.deh"
};
static const byte data_file_games[DATA_FILES] = { 0x03, 0x02, 0x04, 0x04 };

static int data_file_present(const char *name)
{
    char path[128];
    SceUID fd;
    snprintf(path, sizeof(path), "%s%s", VITA_GAME_DATA_DIR, name);
    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    sceIoClose(fd);
    return 1;
}

static void launcher_refresh_data(void)
{
    int i, mask = 0;
    for (i = 0; i < DATA_FILES; ++i)
        if (data_file_present(data_file_names[i]))
            mask |= 1 << i;
    launcher_data_mask = mask;
}

static byte game_required_mask(int game)
{
    byte mask = 0;
    int i;
    for (i = 0; i < DATA_FILES; ++i)
        if (data_file_games[i] & (1 << game))
            mask |= 1 << i;
    return mask;
}

static int launcher_game_ready(int game)
{
    byte need = game_required_mask(game);
    return (launcher_data_mask & need) == need;
}

static int launcher_ready_count(void)
{
    int i, count = 0;
    for (i = 0; i < L_ITEM_DATA; ++i)
        if (launcher_game_ready(i))
            ++count;
    return count;
}

/* ------------------------------------------------------------------ *
 * Native 960x544 UI: overlays, the data/QR screen and the controls     *
 * ------------------------------------------------------------------ */

static uint32_t ui_rgb(int r, int g, int b)
{
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

#define UI_BG ui_rgb(22, 26, 42)
#define UI_PANEL ui_rgb(32, 38, 60)
#define UI_BAR ui_rgb(12, 14, 24)
#define UI_GOLD ui_rgb(255, 200, 72)
#define UI_GOLD_DIM ui_rgb(170, 132, 48)
#define UI_TEXT ui_rgb(226, 228, 240)
#define UI_DIM ui_rgb(150, 154, 174)
#define UI_READY ui_rgb(104, 224, 128)
#define UI_ALERT ui_rgb(236, 84, 72)
#define UI_WHITE ui_rgb(255, 255, 255)
#define UI_BLACK ui_rgb(0, 0, 0)

static void ui_rect(int x, int y, int w, int h, uint32_t color)
{
    uint32_t *dst = (uint32_t *)fb_base;
    int row, col;
    if (!dst || !display_ready)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > VITA_W) w = VITA_W - x;
    if (y + h > VITA_H) h = VITA_H - y;
    for (row = 0; row < h; ++row)
        for (col = 0; col < w; ++col)
            dst[(y + row) * VITA_W + x + col] = color;
}

static void ui_text(int x, int y, const char *text, uint32_t color, int scale)
{
    uint32_t *dst = (uint32_t *)fb_base;
    if (!dst || !display_ready)
        return;
    for (; *text; ++text) {
        int index = -1, n;
        for (n = 0; n < (int)(sizeof(menu_glyphs) - 1); ++n)
            if (menu_glyphs[n] == *text) { index = n; break; }
        if (index >= 0) {
            int col, row, px, py;
            for (col = 0; col < 5; ++col)
                for (row = 0; row < MENU_FONT_ROWS; ++row)
                    if (menu_font[index][col] & (1u << row))
                        for (py = 0; py < scale; ++py)
                            for (px = 0; px < scale; ++px) {
                                int xx = x + col * scale + px;
                                int yy = y + row * scale + py;
                                if (xx >= 0 && xx < VITA_W && yy >= 0 && yy < VITA_H)
                                    dst[yy * VITA_W + xx] = color;
                            }
        }
        x += 6 * scale;
    }
}

static int ui_text_width(const char *text, int scale)
{
    int n = (int)strlen(text);
    return n > 0 ? (n * 6 - 1) * scale : 0;
}

static void ui_text_right(int right, int y, const char *text, uint32_t color,
                          int scale)
{
    ui_text(right - ui_text_width(text, scale), y, text, color, scale);
}

static void ui_present(void)
{
    SceDisplayFrameBuf dfb;
    if (!display_ready || !fb_base)
        return;
    memset(&dfb, 0, sizeof(dfb));
    dfb.size = sizeof(dfb);
    dfb.base = fb_base;
    dfb.pitch = 960;
    dfb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    dfb.width = 960;
    dfb.height = 544;
    sceDisplaySetFrameBuf(&dfb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
}

static void ui_wait_for_press(void)
{
    SceCtrlData pad, previous;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    previous = pad; /* a button that opened the screen must not close it at once */
    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if ((pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_START)) &&
            !(previous.buttons & (SCE_CTRL_CROSS | SCE_CTRL_START)))
            return;
        previous = pad;
        sceKernelDelayThread(16000);
    }
}

/* QR code for the game-data download (see scripts/prepare_qr.py). */
#define QR_MODULE 9

static void draw_qr_card(int x, int y)
{
    int row, col;
    ui_rect(x - 33, y - 33, (LAUNCHER_QR_MODULES * QR_MODULE) + 66,
            (LAUNCHER_QR_MODULES * QR_MODULE) + 66, UI_WHITE);
    for (row = 0; row < LAUNCHER_QR_MODULES; ++row)
        for (col = 0; col < LAUNCHER_QR_MODULES; ++col)
            if (launcher_qr_rows[row][col] == '#')
                ui_rect(x + col * QR_MODULE, y + row * QR_MODULE, QR_MODULE,
                        QR_MODULE, UI_BLACK);
}

static void data_screen_file(int x, int y, int index)
{
    int present = (launcher_data_mask >> index) & 1;
    ui_text(x, y, data_file_names[index], present ? UI_READY : UI_ALERT, 2);
}

/* "missing_game" < 0 opens the plain download screen, otherwise it explains
   which game is waiting for its files. */
static void show_data_screen(int missing_game)
{
    char line[64];
    const char *url =
        "https://www.mediafire.com/file/dexl4gb4wbi0fmj/chexquestcollection.zip/file";
    int x = 470, y = 286, i, present = 0;
    SceCtrlData pad;

    launcher_refresh_data();
    ui_rect(0, 0, VITA_W, VITA_H, UI_BG);
    ui_rect(0, 0, VITA_W, 64, UI_BAR);
    ui_rect(0, 63, VITA_W, 2, UI_GOLD);
    ui_text(28, 18, "GET THE GAME DATA", UI_GOLD, 3);
    ui_text_right(VITA_W - 28, 26, "CHEX QUEST COLLECTION", UI_DIM, 2);

    draw_qr_card(59, 121);

    if (missing_game >= 0) {
        snprintf(line, sizeof(line), "CHEX QUEST %d HAS NO DATA YET", missing_game + 1);
        ui_text(x, 76, line, UI_ALERT, 2);
        ui_text(x, 120, "SCAN THIS CODE", UI_GOLD, 3);
    } else {
        ui_text(x, 76, "SCAN THIS CODE", UI_GOLD, 3);
    }
    ui_text(x, 150, "WITH YOUR PHONE", UI_TEXT, 2);
    ui_rect(x, 178, 460, 2, UI_GOLD_DIM);
    ui_text(x, 194, "THEN COPY THE FILES", UI_TEXT, 2);
    ui_text(x, 214, "TO THIS FOLDER:", UI_TEXT, 2);
    ui_text(x, 240, "ux0:/data/", UI_GOLD, 2);
    ui_text(x, 258, "chexquestcollection/", UI_GOLD, 2);

    for (i = 0; i < DATA_FILES; ++i) {
        if (i == 2) { x = 470; y += 30; }
        data_screen_file(x, y, i);
        x += ui_text_width(data_file_names[i], 2) + 28;
        if ((launcher_data_mask >> i) & 1)
            ++present;
    }
    snprintf(line, sizeof(line), "%d OF %d FILES ALREADY THERE", present, DATA_FILES);
    ui_text(470, 352, line, present == DATA_FILES ? UI_READY : UI_ALERT, 2);

    ui_text(470, 390, "OR TYPE THIS LINK:", UI_DIM, 2);
    ui_text(470, 412, url, UI_TEXT, 1);

    ui_rect(0, 496, VITA_W, VITA_H - 496, UI_BAR);
    ui_rect(0, 494, VITA_W, 2, UI_GOLD_DIM);
    ui_text(28, 514, "PRESS X TO GO BACK", UI_WHITE, 2);
    ui_text_right(VITA_W - 28, 514, "FAN PROJECT - NO GAME DATA INCLUDED", UI_DIM, 2);

    ui_present();

    /* Wake the display up from a possible blank and swallow the current input. */
    sceCtrlPeekBufferPositive(0, &pad, 1);
    (void)pad;
    ui_wait_for_press();
}

/* Shows the controls and the frame pacing setting. X (or left/right) changes
   the framerate, START goes back to the launcher. */
static void show_options_screen(void);

static void options_draw(void)
{
    char line[40];
    launcher_refresh_data();
    ui_rect(0, 0, VITA_W, VITA_H, UI_BG);
    ui_rect(0, 0, VITA_W, 64, UI_BAR);
    ui_rect(0, 63, VITA_W, 2, UI_GOLD);
    ui_text(28, 18, "OPTIONS", UI_GOLD, 3);
    ui_text_right(VITA_W - 28, 26, "CHEX QUEST COLLECTION", UI_DIM, 2);

    ui_text(48, 96, "IN GAME", UI_GOLD, 2);
    ui_rect(48, 122, 400, 2, UI_GOLD_DIM);
    ui_text(48, 140, "LEFT STICK: MOVE", UI_TEXT, 2);
    ui_text(48, 164, "RIGHT STICK: TURN", UI_TEXT, 2);
    ui_text(48, 188, "X: USE", UI_TEXT, 2);
    ui_text(48, 212, "SQUARE OR R: FIRE", UI_TEXT, 2);
    ui_text(48, 236, "L: RUN", UI_TEXT, 2);
    ui_text(48, 260, "TRIANGLE: AUTOMAP", UI_TEXT, 2);
    ui_text(48, 284, "START: MENU", UI_TEXT, 2);
    ui_text(48, 320, "UP: QUICK SAVE", UI_TEXT, 2);
    ui_text(48, 344, "DOWN: QUICK LOAD", UI_TEXT, 2);
    ui_text(48, 368, "LEFT/RIGHT: WEAPONS", UI_TEXT, 2);

    ui_text(520, 96, "IN MENUS", UI_GOLD, 2);
    ui_rect(520, 122, 400, 2, UI_GOLD_DIM);
    ui_text(520, 140, "UP/DOWN: MOVE", UI_TEXT, 2);
    ui_text(520, 164, "LEFT/RIGHT: CHANGE", UI_TEXT, 2);
    ui_text(520, 188, "X: SELECT", UI_TEXT, 2);
    ui_text(520, 212, "START: CLOSE", UI_TEXT, 2);
    ui_text(520, 236, "SAVES AND LOADS", UI_DIM, 2);
    ui_text(520, 260, "WORK IN GAME ONLY", UI_DIM, 2);

    ui_text(520, 320, "BACK TO LAUNCHER", UI_GOLD, 2);
    ui_text(520, 344, "HOLD L+R+SELECT", UI_TEXT, 2);
    ui_text(520, 368, "FOR ONE SECOND", UI_DIM, 2);

    /* Frame pacing of the game itself. */
    ui_text(48, 392, "PERFORMANCE", UI_GOLD, 2);
    ui_rect(48, 418, 400, 2, UI_GOLD_DIM);
    ui_text(48, 436, "FRAMERATE", UI_TEXT, 2);
    snprintf(line, sizeof(line), "%d FPS", fps_target);
    ui_text(48, 462, line, UI_WHITE, 2);
    if (fps_target == FPS_SMOOTH)
        ui_text(180, 462, "SMOOTH (INTERPOLATED)", UI_READY, 2);
    else
        ui_text(180, 462, "CLASSIC (ONE FRAME PER TIC)", UI_DIM, 2);
    ui_text(520, 436, "CHANGES TAKE EFFECT", UI_DIM, 2);
    ui_text(520, 462, "WHEN A GAME STARTS", UI_DIM, 2);

    ui_rect(0, 496, VITA_W, VITA_H - 496, UI_BAR);
    ui_rect(0, 494, VITA_W, 2, UI_GOLD_DIM);
    ui_text(28, 514, "X OR LEFT/RIGHT: CHANGE", UI_WHITE, 2);
    ui_text_right(VITA_W - 28, 514, "START: GO BACK", UI_DIM, 2);

    ui_present();
}

static void show_options_screen(void)
{
    SceCtrlData pad, previous;
    int changed;

    options_draw();
    sceCtrlPeekBufferPositive(0, &pad, 1);
    previous = pad; /* the button that opened the screen must not act at once */
    for (;;) {
        changed = 0;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if ((pad.buttons & SCE_CTRL_START) && !(previous.buttons & SCE_CTRL_START))
            return;
        if ((pad.buttons & SCE_CTRL_CROSS) && !(previous.buttons & SCE_CTRL_CROSS)) {
            fps_target = (fps_target == FPS_SMOOTH) ? FPS_CLASSIC : FPS_SMOOTH;
            changed = 1;
        }
        if ((pad.buttons & SCE_CTRL_RIGHT) && !(previous.buttons & SCE_CTRL_RIGHT)) {
            changed = fps_target != FPS_SMOOTH;
            fps_target = FPS_SMOOTH;
        }
        if ((pad.buttons & SCE_CTRL_LEFT) && !(previous.buttons & SCE_CTRL_LEFT)) {
            changed = fps_target != FPS_CLASSIC;
            fps_target = FPS_CLASSIC;
        }
        if (changed) {
            settings_save();
            options_draw();
        }
        previous = pad;
        sceKernelDelayThread(16000);
    }
}

/* ------------------------------------------------------------------ *
 * In-game overlays: quick save/load feedback and the launcher shortcut *
 * ------------------------------------------------------------------ */

#define TOAST_MS 1600
#define EXIT_HOLD_MS 1000

static char toast_text[48] = "";
static uint32_t toast_until = 0;
static uint32_t exit_hold_start = 0;
static int exit_hold_percent = 0;

static void show_toast(const char *text)
{
    snprintf(toast_text, sizeof(toast_text), "%s", text);
    toast_until = get_ms() + TOAST_MS;
}

/* The engine cannot restart in place, so the launcher comes back by reloading
   the application. */
static void return_to_launcher(void)
{
    debug_log("L+R+SELECT: reloading the launcher");
    menu_music_active = 0;
    sfx_running = 0;
    sceKernelDelayThread(100000);
    sceAppMgrLoadExec("app0:/eboot.bin", NULL, NULL);
    debug_log("sceAppMgrLoadExec failed; quitting");
    sceKernelExitProcess(0);
}

static void draw_game_overlays(void)
{
    uint32_t now = get_ms();
    if (toast_until) {
        if (now < toast_until) {
            int w = ui_text_width(toast_text, 2) + 48;
            int x = (VITA_W - w) / 2, y = VITA_H - 104;
            ui_rect(x + 4, y + 4, w, 44, UI_BLACK);
            ui_rect(x, y, w, 44, UI_PANEL);
            ui_rect(x, y, w, 2, UI_GOLD);
            ui_rect(x, y + 42, w, 2, UI_GOLD);
            ui_text(x + 24, y + 16, toast_text, UI_WHITE, 2);
        } else {
            toast_until = 0;
        }
    }
    if (exit_hold_percent > 0) {
        int w = 480, x = (VITA_W - w) / 2, y = 40;
        ui_rect(x + 4, y + 4, w, 64, UI_BLACK);
        ui_rect(x, y, w, 64, UI_PANEL);
        ui_rect(x, y, w, 2, UI_GOLD);
        ui_text(x + 20, y + 12, "RETURN TO LAUNCHER", UI_WHITE, 2);
        ui_rect(x + 20, y + 40, w - 40, 12, UI_PANEL);
        ui_rect(x + 20, y + 40, (w - 40) * exit_hold_percent / 100, 12, UI_GOLD);
    }
}

/* ------------------------------------------------------------------ *
 * Launcher screen (320x200 indexed buffer)                            *
 * ------------------------------------------------------------------ */

static const char *const launcher_titles[L_ITEMS] = {
    "CHEX QUEST 1", "CHEX QUEST 2", "CHEX QUEST 3", "DATA FILES"
};

static void launcher_border(int x, int y, int w, int h, byte color)
{
    launcher_rect(x, y, w, 1, color);
    launcher_rect(x, y + h - 1, w, 1, color);
    launcher_rect(x, y, 1, h, color);
    launcher_rect(x + w - 1, y, 1, h, color);
}

static void launcher_qr_badge(int x, int y, byte color)
{
    static const char *const badge[13] = {
        "#######...#..", "#.....#..#...", "#.###.#.#....", "#.###.#..#...",
        "#.###.#....#.", "#.....#..#...", "#######.#....", "..........#..",
        "#..#..#......", "..#..##.#....", "...#.....#...", "..#..#..#....",
        "#....#....#.."
    };
    int row, col, px, py;
    for (row = 0; row < 13; ++row)
        for (col = 0; col < 13; ++col)
            if (badge[row][col] == '#')
                for (py = 0; py < 2; ++py)
                    for (px = 0; px < 2; ++px) {
                        int xx = x + col * 2 + px;
                        int yy = y + row * 2 + py;
                        if (xx >= 0 && xx < SCREENWIDTH && yy >= 0 && yy < SCREENHEIGHT)
                            I_VideoBuffer[yy * SCREENWIDTH + xx] = color;
                    }
}

static void draw_launcher(int selected)
{
    static const unsigned char *logos[3] = {
        (const unsigned char *)launcher_logo_cq1,
        (const unsigned char *)launcher_logo_cq2,
        (const unsigned char *)launcher_logo_cq3
    };
    static const int logo_w[3] = { LAUNCHER_LOGO_CQ1_W, LAUNCHER_LOGO_CQ2_W, LAUNCHER_LOGO_CQ3_W };
    static const int logo_h[3] = { LAUNCHER_LOGO_CQ1_H, LAUNCHER_LOGO_CQ2_H, LAUNCHER_LOGO_CQ3_H };
    /* The selection breathes so the highlighted row is obvious at a glance. */
    byte accent = ((launcher_frame_tick / 12) % 3 == 1) ? L_COL_GOLD_DIM : L_COL_GOLD;
    char line[32];
    int i, title_x = L_ROW_X + 8 + L_ICON_SLOT + 10;

    launcher_frame = 1;
    memcpy(I_VideoBuffer, launcher_bg, SCREENWIDTH * SCREENHEIGHT);

    /* Header: big collection title and how many games are ready to play. */
    draw_menu_text_scaled(10, 6, "CHEX QUEST", L_COL_GOLD, 2);
    draw_menu_text(140, 12, "COLLECTION", L_COL_WHITE);
    snprintf(line, sizeof(line), "READY %d/3", launcher_ready_count());
    draw_menu_text_right(SCREENWIDTH - 10, 12, line,
                         launcher_ready_count() == 3 ? L_COL_READY : L_COL_ALERT, 1);

    for (i = 0; i < L_ITEMS; ++i) {
        int top = L_ROW_TOP + i * (L_ROW_H + L_ROW_GAP);
        int is_data = (i == L_ITEM_DATA);
        int ready = is_data || launcher_game_ready(i);
        const char *status;
        byte status_col, title_col;

        if (i == selected) {
            launcher_border(L_ROW_X - 1, top - 1, L_ROW_W + 2, L_ROW_H + 2, L_COL_GOLD_DIM);
            launcher_border(L_ROW_X, top, L_ROW_W, L_ROW_H, accent);
            launcher_rect(L_ROW_X, top, 4, L_ROW_H, accent);
            title_col = L_COL_WHITE;
        } else {
            launcher_border(L_ROW_X, top, L_ROW_W, L_ROW_H, L_COL_LINE);
            title_col = L_COL_TEXT;
        }

        if (is_data) {
            launcher_qr_badge(L_ROW_X + 8 + (L_ICON_SLOT - 26) / 2,
                              top + (L_ROW_H - 26) / 2,
                              i == selected ? L_COL_GOLD : L_COL_TEXT_DIM);
            status = "QR CODE";
            status_col = i == selected ? L_COL_GOLD : L_COL_GOLD_DIM;
        } else {
            launcher_logo_pixel(L_ROW_X + 8 + (L_ICON_SLOT - logo_w[i]) / 2,
                                top + (L_ROW_H - logo_h[i]) / 2, logo_w[i],
                                logo_h[i], logos[i]);
            status = ready ? "READY" : "MISSING";
            status_col = ready ? L_COL_READY : L_COL_ALERT;
        }
        draw_menu_text_scaled(title_x, top + 7, launcher_titles[i], title_col, 2);
        draw_menu_text_right(L_ROW_X + L_ROW_W - 8, top + 13, status, status_col, 1);
    }

    draw_menu_text(L_ROW_X + 2, L_FOOTER_Y + 4, "UP/DOWN: CHOOSE   X: LAUNCH", L_COL_TEXT);
    draw_menu_text_right(SCREENWIDTH - 10, L_FOOTER_Y + 4, "TRIANGLE: EXIT", L_COL_TEXT_DIM, 1);
    draw_menu_text(L_ROW_X + 2, L_FOOTER_Y + 14, "SQUARE: DATA FILES", L_COL_GOLD_DIM);
    draw_menu_text_right(SCREENWIDTH - 10, L_FOOTER_Y + 14, "SELECT: OPTIONS", L_COL_TEXT_DIM, 1);
    I_FinishUpdate();
}

static void kq_push(int p, unsigned char k) {
  int n = (kq_w + 1) % KQUEUE_SZ;
  if (n == kq_r)
    return;
  kq[kq_w].pressed = p;
  kq[kq_w].key = k;
  kq_w = n;
}

/* While a menu, the intermission or the title screen is up the D-pad becomes
   arrow keys, so it scrolls the menus instead of saving or cycling weapons. */
static void dpad_arrow(int now, int was, unsigned char key) {
  if (now && !was)
    kq_push(1, key);
  if (!now && was)
    kq_push(0, key);
}
static void analog_axis(int val, int nk, int pk, int *nh, int *ph) {
  int wn = val<-DEADZONE, wp = val> DEADZONE;
  if (wn && !*nh) {
    kq_push(1, nk);
    *nh = 1;
  }
  if (!wn && *nh) {
    kq_push(0, nk);
    *nh = 0;
  }
  if (wp && !*ph) {
    kq_push(1, pk);
    *ph = 1;
  }
  if (!wp && *ph) {
    kq_push(0, pk);
    *ph = 0;
  }
}

static void pulse_weapon_digit(void) {
  kq_push(1, (unsigned char)('0' + current_weapon));
  pending_weapon_release = (unsigned char)('0' + current_weapon);
  weapon_cycle_cooldown = WEAPON_KEY_GAP;
}

static void do_poll_input(void) {
  SceCtrlData pad;
  int i;
  if (!input_init) {
    if (!save_directory_ready) {
      sceIoMkdir(VITA_GAME_DATA_DIR "cfg/", 0777);
      sceIoMkdir(VITA_GAME_DATA_DIR "cfg/cq1/", 0777);
      sceIoMkdir(VITA_GAME_DATA_DIR "cfg/cq2/", 0777);
      sceIoMkdir(VITA_GAME_DATA_DIR "cfg/cq3/", 0777);
      sceIoMkdir(VITA_GAME_DATA_DIR "saves/", 0777);
      sceIoMkdir(VITA_GAME_DATA_DIR "saves/cq1/", 0777);
      sceIoMkdir(VITA_GAME_DATA_DIR "saves/cq2/", 0777);
      sceIoMkdir(VITA_GAME_DATA_DIR "saves/cq3/", 0777);
      save_directory_ready = 1;
    }
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                             SCE_TOUCH_SAMPLING_STATE_START);
    memset(&pad_prev, 0, sizeof(pad_prev));
    memset(analog_held, 0, sizeof(analog_held));
    input_init = 1;
  }
  sceCtrlPeekBufferPositive(0, &pad, 1);
  if (weapon_cycle_cooldown > 0)
    weapon_cycle_cooldown--;

  /* Face buttons + triggers */

  {
    struct {
      unsigned btn;
      unsigned char key;
    } bm[] = {{SCE_CTRL_CROSS, KEY_USE},
              {SCE_CTRL_SQUARE, KEY_FIRE},
              {SCE_CTRL_CIRCLE, KEY_RALT},
              {SCE_CTRL_TRIANGLE, KEY_TAB},
              {SCE_CTRL_RTRIGGER, KEY_FIRE},
              {SCE_CTRL_LTRIGGER, KEY_RSHIFT},
              {SCE_CTRL_START, KEY_ESCAPE},
              {SCE_CTRL_SELECT, KEY_ENTER},
              {0, 0}};
    for (i = 0; bm[i].btn; i++) {
      int now = (pad.buttons & bm[i].btn) != 0;
      int was = (pad_prev.buttons & bm[i].btn) != 0;
      if (now && !was)
        kq_push(1, bm[i].key);
      if (!now && was)
        kq_push(0, bm[i].key);
    }
  }

  /* Release weapon key from previous tic */
  if (pending_weapon_release) {
    kq_push(0, pending_weapon_release);
    pending_weapon_release = 0;
  }

  /* D-pad Up quicksaves; Down quickloads; Left/Right cycle weapons, but only
     while playing: menus, the intermission and the pause screen scroll. */
  {
    int up = (pad.buttons & SCE_CTRL_UP) != 0;
    int down = (pad.buttons & SCE_CTRL_DOWN) != 0;
    int up_was = (pad_prev.buttons & SCE_CTRL_UP) != 0;
    int down_was = (pad_prev.buttons & SCE_CTRL_DOWN) != 0;
    int l = (pad.buttons & SCE_CTRL_LEFT) != 0;
    int r = (pad.buttons & SCE_CTRL_RIGHT) != 0;
    int lw = (pad_prev.buttons & SCE_CTRL_LEFT) != 0;
    int rw = (pad_prev.buttons & SCE_CTRL_RIGHT) != 0;
    int menu_focus = (gamestate != GS_LEVEL) || !usergame || menuactive;
    int can_weapon = 0;

    if (quicksave_cooldown > 0) quicksave_cooldown--;
    if (quickload_cooldown > 0) quickload_cooldown--;

    if (menu_focus) {
      dpad_arrow(up, up_was, KEY_UPARROW);
      dpad_arrow(down, down_was, KEY_DOWNARROW);
      dpad_arrow(l, lw, KEY_LEFTARROW);
      dpad_arrow(r, rw, KEY_RIGHTARROW);
      weapon_l_charge = 0;
      weapon_r_charge = 0;
    } else {
      if (up && !up_was && quicksave_cooldown == 0) {
        G_SaveGame(0, "VITA SAVE");
        debug_logf("Quicksave slot 0, dir=%s", savegamedir ? savegamedir : "(null)");
        show_toast("QUICK SAVED");
        quicksave_cooldown = TICRATE;
      }
      if (down && !down_was && quickload_cooldown == 0) {
        char *path = P_SaveGameFile(0);
        FILE *save_file = path ? fopen(path, "rb") : NULL;
        if (save_file) {
          fclose(save_file);
          G_LoadGame(path);
          debug_logf("Quickload: %s", path);
          show_toast("QUICK LOADED");
        } else {
          debug_log("Quickload ignored: no save file");
          show_toast("NO QUICK SAVE YET");
        }
        quickload_cooldown = TICRATE;
      }
      can_weapon = weapon_cycle_cooldown == 0;
    }

    if (!r) {
      weapon_r_charge = 0;
    } else {
      if (!rw) {
        if (can_weapon) {
          current_weapon = current_weapon >= 7 ? 1 : current_weapon + 1;
          pulse_weapon_digit();
        }
        weapon_r_charge = WEAPON_HOLD_INITIAL;
      } else if (can_weapon && weapon_r_charge > 0) {
        weapon_r_charge--;
        if (weapon_r_charge == 0) {
          current_weapon = current_weapon >= 7 ? 1 : current_weapon + 1;
          pulse_weapon_digit();
          weapon_r_charge = WEAPON_HOLD_REPEAT;
        }
      }
    }

    if (!l) {
      weapon_l_charge = 0;
    } else {
      if (!lw) {
        if (can_weapon) {
          current_weapon = current_weapon <= 1 ? 7 : current_weapon - 1;
          pulse_weapon_digit();
        }
        weapon_l_charge = WEAPON_HOLD_INITIAL;
      } else if (can_weapon && weapon_l_charge > 0) {
        weapon_l_charge--;
        if (weapon_l_charge == 0) {
          current_weapon = current_weapon <= 1 ? 7 : current_weapon - 1;
          pulse_weapon_digit();
          weapon_l_charge = WEAPON_HOLD_REPEAT;
        }
      }
    }
  }

  /* Analog sticks: left = move/strafe, right = turn */
  analog_axis(pad.ly - 128, KEY_UPARROW, KEY_DOWNARROW, &analog_held[0],
              &analog_held[1]);
  analog_axis(pad.lx - 128, KEY_STRAFE_L, KEY_STRAFE_R, &analog_held[2],
              &analog_held[3]);
  analog_axis(pad.rx - 128, KEY_LEFTARROW, KEY_RIGHTARROW, &analog_held[4],
              &analog_held[5]);

  /* Front touch: barra alta = arma (un colpo per tap; niente ripetizioni ogni frame) */
  {
    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    int in_bar = touch.reportNum > 0 && (touch.report[0].y / 2 < 60);
    if (in_bar) {
      int slot =
          ((int)(touch.report[0].x / 2)) / ((int)(VITA_W / 7));
      if (slot >= 0 && slot < 7 && !weapon_touch_prev_in_bar &&
          weapon_cycle_cooldown == 0 && !pending_weapon_release) {
        current_weapon = slot + 1;
        kq_push(1, (unsigned char)('1' + slot));
        kq_push(0, (unsigned char)('1' + slot));
        weapon_cycle_cooldown = WEAPON_KEY_GAP;
      }
      weapon_touch_prev_in_bar = 1;
    } else {
      weapon_touch_prev_in_bar = 0;
    }
  }

  /* L+R+SELECT held for a moment: leave the game and reload the launcher. */
  {
    const unsigned int exit_combo =
        SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;
    if ((pad.buttons & exit_combo) == exit_combo) {
      uint32_t now = get_ms();
      if (!exit_hold_start)
        exit_hold_start = now;
      exit_hold_percent = (int)((now - exit_hold_start) * 100 / EXIT_HOLD_MS);
      if (exit_hold_percent >= 100)
        return_to_launcher();
    } else if (exit_hold_start) {
      exit_hold_start = 0;
      exit_hold_percent = 0;
    }
  }
  pad_prev = pad;
}

/* SFX ENGINE */
typedef struct {
  const byte *data;
  int length, pos_fixed, step_fixed;
  int vol_left, vol_right, handle, active, lumpnum;
} mix_channel_t;

static mix_channel_t mix_ch[MIX_CHANNELS];
static int sfx_port = -1;
static SceUID sfx_thread_id = -1, sfx_mutex = -1;
static volatile int sfx_running = 0;
static volatile int sfx_master_vol = 15;
static int next_handle = 1, audio_ready = 0;
static int16_t __attribute__((aligned(64))) sfx_buf[2][AUDIO_GRANULARITY * 2];
static int sfx_buf_idx = 0;

#define SFX_CACHE_MAX 128
typedef struct {
  int lumpnum;
  const byte *samples;
  int length, samplerate;
} sfx_cache_entry_t;
static sfx_cache_entry_t sfx_cache[SFX_CACHE_MAX];
static int sfx_cache_count = 0;

static sfx_cache_entry_t *sfx_cache_get(int lumpnum) {
  int i;
  byte *raw;
  int rawlen, format, rate, nsamples;
  for (i = 0; i < sfx_cache_count; i++)
    if (sfx_cache[i].lumpnum == lumpnum)
      return &sfx_cache[i];
  rawlen = W_LumpLength(lumpnum);
  if (rawlen < 8)
    return NULL;
  raw = W_CacheLumpNum(lumpnum, PU_STATIC);
  if (!raw)
    return NULL;
  format = raw[0] | (raw[1] << 8);
  rate = raw[2] | (raw[3] << 8);
  nsamples = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);
  if (format != 3)
    return NULL;
  if (rate < 4000 || rate > 48000)
    rate = 11025;
  if (nsamples > rawlen - 8)
    nsamples = rawlen - 8;
  if (nsamples <= 0)
    return NULL;
  {
    const byte *pcm = raw + 8;
    int pcm_len = nsamples;
    if (pcm_len > 32) {
      pcm += 16;
      pcm_len -= 32;
    }
    if (sfx_cache_count >= SFX_CACHE_MAX)
      return NULL;
    i = sfx_cache_count++;
    sfx_cache[i].lumpnum = lumpnum;
    sfx_cache[i].samples = pcm;
    sfx_cache[i].length = pcm_len;
    sfx_cache[i].samplerate = rate;
  }
  return &sfx_cache[i];
}

/* OPL3 MUSIC ENGINE */
#define GENMIDI_NUM_INSTRS 175
#define GENMIDI_HEADER "#OPL_II#"
#define GENMIDI_FLAG_FIXED 0x0001
#define OPL_NUM_VOICES 9
#define PERCUSSION_CHAN 15

#pragma pack(push, 1)
typedef struct {
  uint8_t tremolo, attack, sustain, waveform, scale, level;
} genmidi_op_t;
typedef struct {
  genmidi_op_t modulator;
  uint8_t feedback;
  genmidi_op_t carrier;
  uint8_t unused;
  int16_t base_note_offset;
} genmidi_voice_t;
typedef struct {
  uint16_t flags;
  uint8_t fine_tuning, fixed_note;
  genmidi_voice_t voices[2];
} genmidi_instr_t;
#pragma pack(pop)

static const uint8_t opl_mod_offset[9] = {0x00, 0x01, 0x02, 0x08, 0x09,
                                          0x0A, 0x10, 0x11, 0x12};
static const uint8_t opl_car_offset[9] = {0x03, 0x04, 0x05, 0x0B, 0x0C,
                                          0x0D, 0x13, 0x14, 0x15};
static const uint16_t opl_freq_table[12] = {0x157, 0x16B, 0x181, 0x198,
                                            0x1B0, 0x1CA, 0x1E5, 0x202,
                                            0x220, 0x241, 0x263, 0x287};

typedef struct {
  int active, mus_channel, note, volume;
  uint32_t age;
} opl_voice_t;
typedef struct {
  int volume, patch, pitch_bend;
} opl_mus_chan_t;
typedef struct {
  opl3_chip chip;
  genmidi_instr_t *genmidi;
  int genmidi_loaded;
  opl_voice_t voices[OPL_NUM_VOICES];
  opl_mus_chan_t channels[16];
  uint32_t voice_age;
  const byte *mus_data;
  int mus_len, mus_pos, score_start, score_len;
  int playing, looping, delay_left, tick_samples, tick_counter, music_volume;
} opl_music_t;

static opl_music_t opl_music;
static SceUID mus_mutex = -1;
static uint8_t opl_reg_b0[9];

static void opl_write(uint16_t reg, uint8_t val) {
  OPL3_WriteReg(&opl_music.chip, reg, val);
}

static void load_genmidi(void) {
  int lump;
  byte *data;
  int len;
  opl_music.genmidi_loaded = 0;
  lump = W_CheckNumForName("GENMIDI");
  if (lump < 0) {
    debug_log("GENMIDI not found");
    return;
  }
  len = W_LumpLength(lump);
  data = W_CacheLumpNum(lump, PU_STATIC);
  if (len < 8 + (int)sizeof(genmidi_instr_t) * GENMIDI_NUM_INSTRS)
    return;
  if (memcmp(data, GENMIDI_HEADER, 8) != 0)
    return;
  opl_music.genmidi = (genmidi_instr_t *)(data + 8);
  opl_music.genmidi_loaded = 1;
}

static void opl_write_operator(int slot, genmidi_op_t *op, int vol) {
  int l, fl;
  opl_write(0x20 + slot, op->tremolo);
  if (vol >= 0) {
    l = 0x3F - (op->level & 0x3F);
    l = (l * vol) / 127;
    fl = 0x3F - l;
    if (fl < 0)
      fl = 0;
    if (fl > 0x3F)
      fl = 0x3F;
    opl_write(0x40 + slot, (op->scale & 0xC0) | fl);
  } else {
    opl_write(0x40 + slot, (op->scale & 0xC0) | (op->level & 0x3F));
  }
  opl_write(0x60 + slot, op->attack);
  opl_write(0x80 + slot, op->sustain);
  opl_write(0xE0 + slot, op->waveform & 0x07);
}

static void opl_set_instrument(int voice, genmidi_voice_t *gv, int volume) {
  int is_add = gv->feedback & 0x01;
  opl_write(0xC0 + voice, (gv->feedback & 0x0F) | 0x30);
  opl_write_operator(opl_mod_offset[voice], &gv->modulator,
                     is_add ? volume : -1);
  opl_write_operator(opl_car_offset[voice], &gv->carrier, volume);
}

static void opl_update_volume(int voice, int volume, genmidi_voice_t *gv) {
  int l, fl;
  l = 0x3F - (gv->carrier.level & 0x3F);
  l = (l * volume) / 127;
  fl = 0x3F - l;
  if (fl < 0)
    fl = 0;
  if (fl > 0x3F)
    fl = 0x3F;
  opl_write(0x40 + opl_car_offset[voice], (gv->carrier.scale & 0xC0) | fl);
  if (gv->feedback & 0x01) {
    l = 0x3F - (gv->modulator.level & 0x3F);
    l = (l * volume) / 127;
    fl = 0x3F - l;
    if (fl < 0)
      fl = 0;
    if (fl > 0x3F)
      fl = 0x3F;
    opl_write(0x40 + opl_mod_offset[voice], (gv->modulator.scale & 0xC0) | fl);
  }
}

static void opl_key_on(int voice, int note) {
  int oct, fn;
  uint16_t freq;
  if (note < 0)
    note = 0;
  if (note > 127)
    note = 127;
  oct = (note / 12) - 1;
  fn = note % 12;
  if (oct < 0)
    oct = 0;
  if (oct > 7)
    oct = 7;
  freq = opl_freq_table[fn];
  opl_write(0xA0 + voice, freq & 0xFF);
  opl_reg_b0[voice] = 0x20 | ((oct & 7) << 2) | ((freq >> 8) & 3);
  opl_write(0xB0 + voice, opl_reg_b0[voice]);
}
static void opl_key_off(int v) {
  opl_reg_b0[v] &= ~0x20;
  opl_write(0xB0 + v, opl_reg_b0[v]);
}
static void opl_silence_voice(int v) {
  opl_reg_b0[v] = 0;
  opl_write(0xB0 + v, 0);
  opl_write(0xA0 + v, 0);
}

static int opl_alloc_voice(int ch, int pri) {
  int i, best;
  uint32_t oldest;
  (void)pri;
  for (i = 0; i < OPL_NUM_VOICES; i++)
    if (!opl_music.voices[i].active)
      return i;
  best = 0;
  oldest = 0xFFFFFFFF;
  for (i = 0; i < OPL_NUM_VOICES; i++)
    if (opl_music.voices[i].age < oldest) {
      oldest = opl_music.voices[i].age;
      best = i;
    }
  opl_key_off(best);
  opl_music.voices[best].active = 0;
  return best;
}

static genmidi_voice_t *get_voice_instr(int vi) {
  int ch, patch;
  if (!opl_music.genmidi_loaded)
    return NULL;
  ch = opl_music.voices[vi].mus_channel;
  patch = opl_music.channels[ch].patch;
  if (ch == PERCUSSION_CHAN) {
    int n = opl_music.voices[vi].note;
    if (n >= 35 && n <= 81)
      patch = 128 + n - 35;
    else
      return NULL;
  }
  if (patch < 0 || patch >= GENMIDI_NUM_INSTRS)
    return NULL;
  return &opl_music.genmidi[patch].voices[0];
}

static void mus_opl_note_on(int channel, int note, int volume) {
  int voice, patch, mn;
  genmidi_instr_t *inst;
  genmidi_voice_t *gv;
  if (!opl_music.genmidi_loaded)
    return;
  patch = opl_music.channels[channel].patch;
  if (channel == PERCUSSION_CHAN) {
    if (note < 35 || note > 81)
      return;
    patch = 128 + note - 35;
  }
  if (patch < 0 || patch >= GENMIDI_NUM_INSTRS)
    return;
  inst = &opl_music.genmidi[patch];
  gv = &inst->voices[0];
  voice = opl_alloc_voice(channel, volume >= 0 ? volume : 64);
  if (inst->flags & GENMIDI_FLAG_FIXED)
    mn = inst->fixed_note;
  else {
    mn = note;
    int off = (int)(int16_t)gv->base_note_offset;
    if (off > -48 && off < 48)
      mn += off;
  }
  if (mn < 0)
    mn = 0;
  if (mn > 127)
    mn = 127;
  if (volume < 0)
    volume = opl_music.channels[channel].volume;
  if (volume > 127)
    volume = 127;
  opl_set_instrument(voice, gv, volume);
  opl_key_on(voice, mn);
  opl_music.voices[voice].active = 1;
  opl_music.voices[voice].mus_channel = channel;
  opl_music.voices[voice].note = note;
  opl_music.voices[voice].volume = volume;
  opl_music.voices[voice].age = opl_music.voice_age++;
}

static void mus_opl_note_off(int ch, int note) {
  int i;
  for (i = 0; i < OPL_NUM_VOICES; i++)
    if (opl_music.voices[i].active && opl_music.voices[i].mus_channel == ch &&
        opl_music.voices[i].note == note) {
      opl_key_off(i);
      opl_music.voices[i].active = 0;
    }
}
static void mus_opl_all_off(int ch) {
  int i;
  for (i = 0; i < OPL_NUM_VOICES; i++)
    if (opl_music.voices[i].mus_channel == ch && opl_music.voices[i].active) {
      opl_key_off(i);
      opl_music.voices[i].active = 0;
    }
}
static byte mus_rb(void) {
  if (opl_music.mus_pos >= opl_music.mus_len)
    return 0;
  return opl_music.mus_data[opl_music.mus_pos++];
}

static void mus_process_event(void) {
  byte ev, channel, type;
  int last, i;
  if (!opl_music.playing)
    return;
  if (opl_music.mus_pos >= opl_music.score_start + opl_music.score_len) {
    if (opl_music.looping) {
      opl_music.mus_pos = opl_music.score_start;
      for (i = 0; i < OPL_NUM_VOICES; i++) {
        opl_silence_voice(i);
        opl_music.voices[i].active = 0;
      }
    } else
      opl_music.playing = 0;
    return;
  }
  ev = mus_rb();
  channel = ev & 0x0F;
  type = (ev >> 4) & 0x07;
  last = ev & 0x80;
  switch (type) {
  case 0: {
    byte n = mus_rb();
    mus_opl_note_off(channel, n & 0x7F);
    break;
  }
  case 1: {
    byte nb = mus_rb();
    int n = nb & 0x7F, v = -1;
    if (nb & 0x80) {
      v = mus_rb() & 0x7F;
      opl_music.channels[channel].volume = v;
    }
    mus_opl_note_on(channel, n, v);
    break;
  }
  case 2: {
    byte pb = mus_rb();
    opl_music.channels[channel].pitch_bend = pb;
    break;
  }
  case 3: {
    byte sys = mus_rb();
    if (sys == 10 || sys == 11 || sys == 14)
      mus_opl_all_off(channel);
    break;
  }
  case 4: {
    byte ctrl = mus_rb(), val = mus_rb();
    if (ctrl == 0)
      opl_music.channels[channel].patch = val;
    else if (ctrl == 3) {
      opl_music.channels[channel].volume = val & 0x7F;
      for (i = 0; i < OPL_NUM_VOICES; i++) {
        if (opl_music.voices[i].active &&
            opl_music.voices[i].mus_channel == channel) {
          genmidi_voice_t *gv = get_voice_instr(i);
          if (gv) {
            int cv = (opl_music.voices[i].volume * (val & 0x7F)) / 127;
            if (cv > 127)
              cv = 127;
            opl_update_volume(i, cv, gv);
          }
        }
      }
    }
    break;
  }
  case 5:
  case 6:
    if (opl_music.looping) {
      opl_music.mus_pos = opl_music.score_start;
      for (i = 0; i < OPL_NUM_VOICES; i++) {
        opl_silence_voice(i);
        opl_music.voices[i].active = 0;
      }
    } else
      opl_music.playing = 0;
    return;
  default:
    break;
  }
  if (last) {
    int delay = 0;
    byte db;
    do {
      db = mus_rb();
      delay = (delay << 7) | (db & 0x7F);
    } while (db & 0x80);
    opl_music.delay_left = delay;
  }
}

static void mus_opl_tick(void) {
  if (!opl_music.playing)
    return;
  while (opl_music.delay_left <= 0 && opl_music.playing)
    mus_process_event();
  if (opl_music.delay_left > 0)
    opl_music.delay_left--;
}

static int opl_diag_done = 0;
static void opl_mix_into(int32_t *accum, int ns) {
  int s, mv;
  if (!opl_music.playing)
    return;
  mv = opl_music.music_volume;
  if (mv <= 0)
    return;
  for (s = 0; s < ns; s++) {
    int16_t buf[4];
    opl_music.tick_counter--;
    if (opl_music.tick_counter <= 0) {
      opl_music.tick_counter = opl_music.tick_samples;
      mus_opl_tick();
    }
    memset(buf, 0, sizeof(buf));
    OPL3_GenerateResampled(&opl_music.chip, buf);
    /* OPL output — * 2 gives clean output without clipping */
    accum[s * 2 + 0] += ((int32_t)buf[0] * mv * 2) / 15;
    accum[s * 2 + 1] += ((int32_t)buf[1] * mv * 2) / 15;
    /* One-time diagnostic */
    if (!opl_diag_done && (buf[0] != 0 || buf[1] != 0)) {
      opl_diag_done = 1;
      debug_logf("OPL output: first non-zero sample buf[0]=%d buf[1]=%d mv=%d",
                 buf[0], buf[1], mv);
    }
  }
}

/* Combined audio mixing */
static void mix_into(int16_t *out, int nsamples) {
  int i, ch, mvol;
  int32_t accum[AUDIO_GRANULARITY * 2];
  memset(accum, 0, nsamples * 2 * sizeof(int32_t));
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  mvol = sfx_master_vol;
  if (mvol < 0)
    mvol = 0;
  if (mvol > 15)
    mvol = 15;
  for (i = 0; i < nsamples; i++) {
    int32_t al = 0, ar = 0;
    for (ch = 0; ch < MIX_CHANNELS; ch++) {
      mix_channel_t *c = &mix_ch[ch];
      int pos, sample;
      if (!c->active || !c->data)
        continue;
      pos = c->pos_fixed >> 16;
      if (pos >= c->length) {
        c->active = 0;
        continue;
      }
      sample = ((int)c->data[pos] - 128) * 256;
      al += (sample * c->vol_left) >> 8;
      ar += (sample * c->vol_right) >> 8;
      c->pos_fixed += c->step_fixed;
    }
    accum[i * 2 + 0] += (al * mvol) / 15;
    accum[i * 2 + 1] += (ar * mvol) / 15;
  }
  sceKernelUnlockMutex(sfx_mutex, 1);
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    opl_mix_into(accum, nsamples);
    sceKernelUnlockMutex(mus_mutex, 1);
  }
  for (i = 0; i < nsamples * 2; i++) {
    int32_t v = accum[i];
    if (v > 32767)
      v = 32767;
    if (v < -32768)
      v = -32768;
    out[i] = (int16_t)v;
  }
}

/* Short blip used by the launcher when the selection moves. */
static volatile int menu_beep_frames = 0;
static int menu_beep_phase = 0;

static void launcher_play_beep(void) {
  menu_beep_frames = OUTPUT_RATE / 24;
  menu_beep_phase = 0;
}

static int sfx_thread_func(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  while (sfx_running) {
    int16_t *buf = sfx_buf[sfx_buf_idx];
    int sample;
    mix_into(buf, AUDIO_GRANULARITY);
    if (menu_beep_frames > 0) {
      for (sample = 0; sample < AUDIO_GRANULARITY && menu_beep_frames > 0;
           ++sample, --menu_beep_frames) {
        int32_t tone = ((menu_beep_phase++ / 34) & 1) ? 7000 : -7000;
        int32_t left = (int32_t)buf[sample * 2] + tone;
        int32_t right = (int32_t)buf[sample * 2 + 1] + tone;
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        buf[sample * 2] = (int16_t)left;
        buf[sample * 2 + 1] = (int16_t)right;
      }
    }
    if (menu_music_active && menu_music_data && menu_music_length > 0) {
      for (sample = 0; sample < AUDIO_GRANULARITY; ++sample) {
        int16_t music_sample = ((int)menu_music_data[menu_music_position] - 128) * 256;
        int32_t mixed_left = (int32_t)buf[sample * 2] + music_sample;
        int32_t mixed_right = (int32_t)buf[sample * 2 + 1] + music_sample;
        if (mixed_left > 32767) mixed_left = 32767;
        if (mixed_left < -32768) mixed_left = -32768;
        if (mixed_right > 32767) mixed_right = 32767;
        if (mixed_right < -32768) mixed_right = -32768;
        buf[sample * 2] = (int16_t)mixed_left;
        buf[sample * 2 + 1] = (int16_t)mixed_right;
        menu_music_rate_phase += 22050;
        if (menu_music_rate_phase >= OUTPUT_RATE) {
          menu_music_rate_phase -= OUTPUT_RATE;
          if (++menu_music_position == menu_music_length)
            menu_music_position = 0;
        }
      }
    }
    sceAudioOutOutput(sfx_port, buf);
    sfx_buf_idx ^= 1;
  }
  return 0;
}

static void start_audio_system(void) {
  int ret, vols[2], i;
  if (audio_ready)
    return;
  memset(mix_ch, 0, sizeof(mix_ch));
  memset(sfx_buf, 0, sizeof(sfx_buf));
  memset(sfx_cache, 0, sizeof(sfx_cache));
  sfx_cache_count = 0;
  sfx_buf_idx = 0;
  memset(&opl_music, 0, sizeof(opl_music));
  memset(opl_reg_b0, 0, sizeof(opl_reg_b0));
  OPL3_Reset(&opl_music.chip, OUTPUT_RATE);
  opl_write(0x01, 0x20);
  opl_write(0x08, 0x40);
  opl_write(0xBD, 0x00);
  for (i = 0; i < 9; i++)
    opl_silence_voice(i);
  opl_music.music_volume = 15;
  opl_music.tick_samples = OUTPUT_RATE / 140;
  opl_music.tick_counter = opl_music.tick_samples;
  sfx_mutex = sceKernelCreateMutex("sfx_mutex", 0, 0, NULL);
  if (sfx_mutex < 0)
    return;
  mus_mutex = sceKernelCreateMutex("mus_mutex", 0, 0, NULL);
  sfx_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_GRANULARITY,
                                 OUTPUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
  if (sfx_port < 0)
    sfx_port =
        sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, AUDIO_GRANULARITY,
                            OUTPUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
  if (sfx_port < 0) {
    sceKernelDeleteMutex(sfx_mutex);
    sfx_mutex = -1;
    if (mus_mutex >= 0) {
      sceKernelDeleteMutex(mus_mutex);
      mus_mutex = -1;
    }
    return;
  }
  vols[0] = SCE_AUDIO_VOLUME_0DB;
  vols[1] = SCE_AUDIO_VOLUME_0DB;
  sceAudioOutSetVolume(
      sfx_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
  sfx_running = 1;
  sfx_thread_id = sceKernelCreateThread("doom_audio", sfx_thread_func,
                                        0x10000100, 0x10000, 0, 0, NULL);
  if (sfx_thread_id < 0) {
    sfx_running = 0;
    sceAudioOutReleasePort(sfx_port);
    sfx_port = -1;
    sceKernelDeleteMutex(sfx_mutex);
    sfx_mutex = -1;
    if (mus_mutex >= 0) {
      sceKernelDeleteMutex(mus_mutex);
      mus_mutex = -1;
    }
    return;
  }
  ret = sceKernelStartThread(sfx_thread_id, 0, NULL);
  if (ret < 0) {
    sfx_running = 0;
    sceKernelDeleteThread(sfx_thread_id);
    sfx_thread_id = -1;
    sceAudioOutReleasePort(sfx_port);
    sfx_port = -1;
    sceKernelDeleteMutex(sfx_mutex);
    sfx_mutex = -1;
    if (mus_mutex >= 0) {
      sceKernelDeleteMutex(mus_mutex);
      mus_mutex = -1;
    }
    return;
  }
  audio_ready = 1;
  debug_log("Audio OK");
}

/* DG interface */
void DG_Init(void) {
  debug_log("DG_Init called");
  base_time = get_ms();
}
void DG_DrawFrame(
    void) { /* intentionally empty – rendering via I_FinishUpdate */ }
void DG_SleepMs(uint32_t ms) { sceKernelDelayThread(ms * 1000); }
uint32_t DG_GetTicksMs(void) { return get_ms() - base_time; }
int DG_GetKey(int *pressed, unsigned char *key) {
  (void)pressed;
  (void)key;
  return 0;
}
void DG_SetWindowTitle(const char *t) { (void)t; }

/* I_* system */
void I_Init(void) {}
void I_Quit(void) {
  sfx_running = 0;
  if (sfx_thread_id >= 0) {
    sceKernelWaitThreadEnd(sfx_thread_id, NULL, NULL);
    sceKernelDeleteThread(sfx_thread_id);
  }
  if (sfx_port >= 0)
    sceAudioOutReleasePort(sfx_port);
  if (sfx_mutex >= 0)
    sceKernelDeleteMutex(sfx_mutex);
  if (mus_mutex >= 0)
    sceKernelDeleteMutex(mus_mutex);
  sceKernelExitProcess(0);
}
void I_Error(const char *error, ...) {
  char buf[512];
  va_list a;
  va_start(a, error);
  vsnprintf(buf, sizeof(buf), error, a);
  va_end(a);
  debug_logf("I_Error: %s", buf);
  sfx_running = 0;
  sceKernelDelayThread(2000000); /* 2s pause so log flushes */
  sceKernelExitProcess(0);
}
void I_WaitVBL(int c) { sceKernelDelayThread(c * 14286); }
int I_GetTime(void) {
  uint32_t ms = get_ms() - base_time;
  return (int)(ms * TICRATE / 1000);
}
void I_Sleep(int ms) { sceKernelDelayThread(ms * 1000); }
byte *I_ZoneBase(int *size) {
  byte *p;
  *size = 16 * 1024 * 1024;
  p = (byte *)malloc(*size);
  if (!p) {
    debug_log("ZoneBase: 16MB alloc failed, trying 8MB");
    *size = 8 * 1024 * 1024;
    p = (byte *)malloc(*size);
  }
  if (!p) {
    debug_log("FATAL: ZoneBase alloc failed entirely");
    *size = 4 * 1024 * 1024;
    p = (byte *)malloc(*size);
  }
  debug_logf("ZoneBase: allocated %d bytes at %p", *size, (void *)p);
  return p;
}
void I_Tactile(int a, int b, int c) {
  (void)a;
  (void)b;
  (void)c;
}
int I_ConsoleStdout(void) { return 0; }
boolean I_GetMemoryValue(unsigned int o, void *v, int s) {
  (void)o;
  (void)v;
  (void)s;
  return 0;
}
void I_AtExit(void (*f)(void), boolean r) {
  (void)f;
  (void)r;
}
void I_PrintBanner(const char *m) { (void)m; }
void I_PrintDivider(void) {}
void I_PrintStartupBanner(const char *g) { (void)g; }
void I_DisplayFPSDots(boolean d) { (void)d; }
void I_CheckIsScreensaver(void) {}
void I_GraphicsCheckCommandLine(void) {}
void I_SetGrabMouseCallback(void (*f)(boolean g)) { (void)f; }
int I_GetTime_RealTime(void) { return I_GetTime(); }
int I_GetTimeMS(void) { return (int)(get_ms() - base_time); }
void I_InitTimer(void) { base_time = get_ms(); }

/* VIDEO */
void I_InitGraphics(void) {
  int i;
  debug_log("I_InitGraphics: allocating video buffer");
  I_VideoBuffer = (byte *)calloc(SCREENWIDTH * SCREENHEIGHT, 1);
  if (!I_VideoBuffer) {
    debug_log("FATAL: I_VideoBuffer alloc failed");
    sceKernelExitProcess(0);
  }
  for (i = 0; i < 256; i++)
    cmap[i] = 0xFF000000u | ((uint32_t)i << 16) | ((uint32_t)i << 8) | i;
  debug_log("I_InitGraphics: done");
}
void I_SetPalette(byte *pal) {
  int i;
  for (i = 0; i < 256; i++) {
    uint32_t r = pal[i * 3 + 0], g = pal[i * 3 + 1], b = pal[i * 3 + 2];
    cmap[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
  }
}
static uint32_t launcher_rgb(unsigned char index)
{
  uint32_t r = ((index >> 5) & 7) * 255 / 7;
  uint32_t g = ((index >> 2) & 7) * 255 / 7;
  uint32_t b = (index & 3) * 255 / 3;
  return 0xFF000000u | (b << 16) | (g << 8) | r;
}

static uint32_t launcher_lut[256];
static uint32_t blit_line[VITA_W];
static unsigned short blit_col_src[VITA_W];
static unsigned short blit_row_src[VITA_H];
static int blit_tables_ready = 0;

/* Which source column/row every screen column/row comes from. */
static void blit_prepare_tables(void) {
  int i;
  for (i = 0; i < VITA_W; ++i) {
    int sx = (int)(((long long)i * SCREENWIDTH) / VITA_W);
    blit_col_src[i] = (unsigned short)(sx < SCREENWIDTH ? sx : SCREENWIDTH - 1);
  }
  for (i = 0; i < VITA_H; ++i) {
    int sy = (int)(((long long)i * SCREENHEIGHT) / VITA_H);
    blit_row_src[i] = (unsigned short)(sy < SCREENHEIGHT ? sy : SCREENHEIGHT - 1);
  }
  for (i = 0; i < 256; ++i)
    launcher_lut[i] = launcher_rgb((unsigned char)i);
  blit_tables_ready = 1;
}

/* Expand one source row to the full screen width. When the width is a whole
   multiple the row is written from the source pixels directly, which keeps the
   inner loop free of index maths and of the palette branch it used to have. */
static void blit_expand_row(const byte *src, const uint32_t *lut) {
#if (VITA_W % SCREENWIDTH) == 0
  const int copies = VITA_W / SCREENWIDTH;
  uint32_t *dst = blit_line;
  int sx;
  for (sx = 0; sx < SCREENWIDTH; ++sx) {
    uint32_t color = lut[src[sx]];
    int n = copies;
    while (n--)
      *dst++ = color;
  }
#else
  int x;
  for (x = 0; x < VITA_W; ++x)
    blit_line[x] = lut[src[blit_col_src[x]]];
#endif
}

void I_FinishUpdate(void) {
  const uint32_t *lut;
  uint32_t *dst;
  int y;
  if (!display_ready || !I_VideoBuffer || !fb_base)
    return;
  if (!blit_tables_ready)
    blit_prepare_tables();
  /* The launcher screens use their own fixed palette. */
  lut = launcher_frame ? launcher_lut : cmap;
  dst = (uint32_t *)fb_base;
  /* Each source row is expanded once, then copied into every screen row that
     maps onto it: far less work than scaling pixel by pixel every row. */
  for (y = 0; y < VITA_H; ++y) {
    int sy = blit_row_src[y];
    if (y == 0 || sy != blit_row_src[y - 1])
      blit_expand_row(I_VideoBuffer + sy * SCREENWIDTH, lut);
    memcpy(dst + y * VITA_W, blit_line, sizeof(blit_line));
  }
  /* The launcher draws its own screens; only the game gets overlays. */
  if (!launcher_frame)
    draw_game_overlays();
  ui_present();
  frame_count++;
}
void I_ShutdownGraphics(void) {}
void I_StartFrame(void) {}
void I_StartTic(void) {
  event_t event;
  do_poll_input();
  while (kq_r != kq_w) {
    event.type = kq[kq_r].pressed ? ev_keydown : ev_keyup;
    event.data1 = kq[kq_r].key;
    event.data2 = kq[kq_r].key;
    event.data3 = 0;
    D_PostEvent(&event);
    kq_r = (kq_r + 1) % KQUEUE_SZ;
  }
}
void I_UpdateNoBlit(void) {}
void I_ReadScreen(byte *scr) {
  if (I_VideoBuffer)
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}
void I_EnableLoadingDisk(void) {}
void I_BeginRead(void) {}
void I_EndRead(void) {}
void I_SetWindowTitle(char *t) { (void)t; }
void I_BindVideoVariables(void) {}
int I_GetPaletteIndex(int r, int g, int b) {
  (void)r;
  (void)g;
  (void)b;
  return 0;
}
void I_InitScale(void) {}
void I_InitInput(void) {}
void I_ShutdownInput(void) {}
void I_InitJoystick(void) {}
void I_ShutdownJoystick(void) {}
void I_UpdateJoystick(void) {}
void I_BindJoystickVariables(void) {}

/* SOUND interface */
void I_SetChannels(void) {}
void I_SetSfxVolume(int volume) { sfx_master_vol = volume; }
int I_GetSfxLumpNum(sfxinfo_t *sfx) {
  char namebuf[16];
  if (!sfx || !sfx->name || sfx->name[0] == '\0')
    return -1;
  snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
  return W_CheckNumForName(namebuf);
}
void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds) {
  int i, l;
  for (i = 0; i < num_sounds; i++) {
    l = I_GetSfxLumpNum(&sounds[i]);
    if (l >= 0)
      sfx_cache_get(l);
  }
}
int I_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep) {
  int lumpnum, best, i, oldest, handle;
  sfx_cache_entry_t *entry;
  mix_channel_t *c;
  (void)channel;
  if (!audio_ready || !sfx)
    return 0;
  lumpnum = sfx->lumpnum;
  if (lumpnum < 0) {
    lumpnum = I_GetSfxLumpNum(sfx);
    if (lumpnum < 0)
      return 0;
  }
  entry = sfx_cache_get(lumpnum);
  if (!entry || entry->length <= 0)
    return 0;
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  best = 0;
  oldest = 0x7FFFFFFF;
  for (i = 0; i < MIX_CHANNELS; i++) {
    if (!mix_ch[i].active) {
      best = i;
      goto found;
    }
    if (mix_ch[i].handle < oldest) {
      oldest = mix_ch[i].handle;
      best = i;
    }
  }
found:
  c = &mix_ch[best];
  c->data = entry->samples;
  c->length = entry->length;
  c->pos_fixed = 0;
  c->lumpnum = lumpnum;
  c->step_fixed = (int)(((int64_t)entry->samplerate << 16) / OUTPUT_RATE);
  if (c->step_fixed <= 0)
    c->step_fixed = (11025 << 16) / OUTPUT_RATE;
  {
    int vl, vr;
    if (sep < 0 || sep > 255)
      sep = 128;
    vr = (sep * 256) / 255;
    vl = 256 - vr;
    if (vl < 0)
      vl = 0;
    if (vl > 256)
      vl = 256;
    if (vr < 0)
      vr = 0;
    if (vr > 256)
      vr = 256;
    c->vol_left = (vl * vol) / 127;
    c->vol_right = (vr * vol) / 127;
    if (c->vol_left > 256)
      c->vol_left = 256;
    if (c->vol_right > 256)
      c->vol_right = 256;
  }
  handle = next_handle++;
  if (next_handle > 0x7FFFFF00)
    next_handle = 1;
  c->handle = handle;
  c->active = 1;
  sceKernelUnlockMutex(sfx_mutex, 1);
  return handle;
}
void I_StopSound(int handle) {
  int i;
  if (sfx_mutex < 0)
    return;
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  for (i = 0; i < MIX_CHANNELS; i++)
    if (mix_ch[i].active && mix_ch[i].handle == handle) {
      mix_ch[i].active = 0;
      break;
    }
  sceKernelUnlockMutex(sfx_mutex, 1);
}
boolean I_SoundIsPlaying(int handle) {
  int i;
  boolean r = false;
  if (sfx_mutex < 0)
    return false;
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  for (i = 0; i < MIX_CHANNELS; i++)
    if (mix_ch[i].active && mix_ch[i].handle == handle) {
      r = true;
      break;
    }
  sceKernelUnlockMutex(sfx_mutex, 1);
  return r;
}
void I_UpdateSound(void) {}
void I_UpdateSoundParams(int handle, int vol, int sep) {
  int i;
  if (sfx_mutex < 0)
    return;
  sceKernelLockMutex(sfx_mutex, 1, NULL);
  for (i = 0; i < MIX_CHANNELS; i++) {
    if (mix_ch[i].active && mix_ch[i].handle == handle) {
      int vl, vr;
      if (sep < 0 || sep > 255)
        sep = 128;
      vr = (sep * 256) / 255;
      vl = 256 - vr;
      if (vl < 0)
        vl = 0;
      if (vl > 256)
        vl = 256;
      if (vr < 0)
        vr = 0;
      if (vr > 256)
        vr = 256;
      mix_ch[i].vol_left = (vl * vol) / 127;
      mix_ch[i].vol_right = (vr * vol) / 127;
      if (mix_ch[i].vol_left > 256)
        mix_ch[i].vol_left = 256;
      if (mix_ch[i].vol_right > 256)
        mix_ch[i].vol_right = 256;
      break;
    }
  }
  sceKernelUnlockMutex(sfx_mutex, 1);
}
void I_InitMusic(void); /* forward decl */
void I_InitSound(boolean use_sfx_prefix) {
  (void)use_sfx_prefix;
  start_audio_system();
  /* doomgeneric's S_Init does NOT call I_InitMusic,
     so we must initialize music (GENMIDI) here */
  I_InitMusic();
  debug_log("I_InitSound + I_InitMusic complete");
}
void I_ShutdownSound(void) {
  sfx_running = 0;
  if (sfx_thread_id >= 0) {
    sceKernelWaitThreadEnd(sfx_thread_id, NULL, NULL);
    sceKernelDeleteThread(sfx_thread_id);
    sfx_thread_id = -1;
  }
  if (sfx_port >= 0) {
    sceAudioOutReleasePort(sfx_port);
    sfx_port = -1;
  }
  if (sfx_mutex >= 0) {
    sceKernelDeleteMutex(sfx_mutex);
    sfx_mutex = -1;
  }
  if (mus_mutex >= 0) {
    sceKernelDeleteMutex(mus_mutex);
    mus_mutex = -1;
  }
  audio_ready = 0;
}
void I_BindSoundVariables(void) {}

/* MUSIC interface */
void I_InitMusic(void) {
  debug_log("I_InitMusic: loading GENMIDI");
  load_genmidi();
  debug_logf("I_InitMusic: genmidi_loaded=%d", opl_music.genmidi_loaded);
}
void I_ShutdownMusic(void) {
  int i;
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    opl_music.playing = 0;
    for (i = 0; i < OPL_NUM_VOICES; i++) {
      opl_silence_voice(i);
      opl_music.voices[i].active = 0;
    }
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
void I_SetMusicVolume(int v) {
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    /* Engine sends 0–127, scale to 0–15 for OPL */
    opl_music.music_volume = (v * 15) / 127;
    if (opl_music.music_volume < 1 && v > 0)
      opl_music.music_volume = 1; /* don't mute if engine wants any sound */
    if (opl_music.music_volume > 15)
      opl_music.music_volume = 15;
    debug_logf("I_SetMusicVolume: engine=%d opl=%d", v, opl_music.music_volume);
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
void I_PauseSong(void) {
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    opl_music.playing = 0;
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
void I_ResumeSong(void) {
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    if (opl_music.mus_data)
      opl_music.playing = 1;
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
void I_StopSong(void) {
  int i;
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    opl_music.playing = 0;
    for (i = 0; i < OPL_NUM_VOICES; i++) {
      opl_silence_voice(i);
      opl_music.voices[i].active = 0;
    }
    sceKernelUnlockMutex(mus_mutex, 1);
  }
}
boolean I_MusicIsPlaying(void) {
  boolean r;
  if (mus_mutex >= 0) {
    sceKernelLockMutex(mus_mutex, 1, NULL);
    r = opl_music.playing ? true : false;
    sceKernelUnlockMutex(mus_mutex, 1);
  } else
    r = opl_music.playing ? true : false;
  return r;
}
void *I_RegisterSong(void *data, int len) {
  byte *d = (byte *)data, *md;
  int so, sl, i;
  debug_logf("I_RegisterSong: data=%p len=%d", data, len);
  if (!data || len < 16)
    return NULL;
  if (d[0] != 'M' || d[1] != 'U' || d[2] != 'S' || d[3] != 0x1A) {
    debug_log("I_RegisterSong: not MUS format");
    return (void *)1;
  }
  debug_log("I_RegisterSong: valid MUS data");
  sl = d[4] | (d[5] << 8);
  so = d[6] | (d[7] << 8);
  if (so >= len || so < 12)
    return (void *)1;
  if (sl <= 0 || so + sl > len)
    sl = len - so;
  md = (byte *)malloc(len);
  if (!md)
    return (void *)1;
  memcpy(md, data, len);
  if (mus_mutex >= 0)
    sceKernelLockMutex(mus_mutex, 1, NULL);
  if (opl_music.mus_data) {
    free((void *)opl_music.mus_data);
    opl_music.mus_data = NULL;
  }
  opl_music.playing = 0;
  for (i = 0; i < OPL_NUM_VOICES; i++) {
    opl_silence_voice(i);
    opl_music.voices[i].active = 0;
  }
  opl_music.mus_data = md;
  opl_music.mus_len = len;
  opl_music.score_start = so;
  opl_music.score_len = sl;
  opl_music.mus_pos = so;
  opl_music.delay_left = 0;
  opl_music.tick_counter = opl_music.tick_samples;
  opl_music.voice_age = 0;
  for (i = 0; i < 16; i++) {
    opl_music.channels[i].volume = 100;
    opl_music.channels[i].patch = 0;
    opl_music.channels[i].pitch_bend = 64;
  }
  if (mus_mutex >= 0)
    sceKernelUnlockMutex(mus_mutex, 1);
  return (void *)md;
}
void I_UnRegisterSong(void *handle) {
  int i;
  if (!handle || handle == (void *)1)
    return;
  if (mus_mutex >= 0)
    sceKernelLockMutex(mus_mutex, 1, NULL);
  opl_music.playing = 0;
  for (i = 0; i < OPL_NUM_VOICES; i++) {
    opl_silence_voice(i);
    opl_music.voices[i].active = 0;
  }
  if (opl_music.mus_data == (const byte *)handle) {
    opl_music.mus_data = NULL;
    opl_music.mus_len = 0;
  }
  if (mus_mutex >= 0)
    sceKernelUnlockMutex(mus_mutex, 1);
  free(handle);
}
void I_PlaySong(void *handle, boolean looping) {
  int i;
  debug_logf("I_PlaySong: handle=%p looping=%d", handle, looping);
  if (!handle || handle == (void *)1)
    return;
  if (mus_mutex >= 0)
    sceKernelLockMutex(mus_mutex, 1, NULL);
  if (opl_music.mus_data == (const byte *)handle) {
    opl_music.mus_pos = opl_music.score_start;
    opl_music.delay_left = 0;
    opl_music.looping = looping ? 1 : 0;
    opl_music.tick_counter = opl_music.tick_samples;
    for (i = 0; i < OPL_NUM_VOICES; i++) {
      opl_silence_voice(i);
      opl_music.voices[i].active = 0;
    }
    opl_music.playing = 1;
  }
  if (mus_mutex >= 0)
    sceKernelUnlockMutex(mus_mutex, 1);
}

/* CD stubs */
int I_CDMusInit(void) { return 0; }
void I_CDMusShutdown(void) {}
void I_CDMusUpdate(void) {}
void I_CDMusStop(void) {}
int I_CDMusPlay(int t) {
  (void)t;
  return 0;
}
void I_CDMusSetVolume(int v) { (void)v; }
int I_CDMusFirstTrack(void) { return 0; }
int I_CDMusLastTrack(void) { return 0; }
int I_CDMusTrackLength(int t) {
  (void)t;
  return 0;
}
void I_Endoom(byte *d) { (void)d; }
char *gus_patch_path = "";
int gus_ram_kb = 0;

/* ------------------------------------------------------------------ *
 * Settings file                                                       *
 * ------------------------------------------------------------------ */

#define SETTINGS_PATH VITA_GAME_DATA_DIR "settings.cfg"

static void settings_load(void) {
  char buf[128];
  const char *value;
  int size;
  SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_RDONLY, 0);
  if (fd < 0)
    return;
  size = sceIoRead(fd, buf, sizeof(buf) - 1);
  sceIoClose(fd);
  if (size <= 0)
    return;
  buf[size] = '\0';
  value = strstr(buf, "framerate=");
  if (value) {
    int fps = atoi(value + 10);
    if (fps == FPS_CLASSIC || fps == FPS_SMOOTH)
      fps_target = fps;
  }
  debug_logf("settings: framerate=%d", fps_target);
}

static void settings_save(void) {
  char buf[64];
  SceUID fd;
  snprintf(buf, sizeof(buf), "framerate=%d\n", fps_target);
  fd = sceIoOpen(SETTINGS_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) {
    debug_log("settings: cannot write settings.cfg");
    return;
  }
  sceIoWrite(fd, buf, (unsigned)strlen(buf));
  sceIoClose(fd);
  debug_logf("settings: saved framerate=%d", fps_target);
}

/* ------------------------------------------------------------------ *
 * Frame loop                                                          *
 *                                                                     *
 * Classic mode is the engine as it always was: one frame per game tic, *
 * 35 per second. Smooth mode draws once per vsync and interpolates the *
 * view between the previous and the current tic, so movement is        *
 * continuous at 60 frames per second instead of stepping 35 times.     *
 * ------------------------------------------------------------------ */

typedef struct {
  fixed_t x, y, z;
  angle_t angle;
} view_state_t;

static view_state_t view_prev, view_cur;
static int view_valid = 0, view_had = 0, view_overridden = 0;
static player_t *view_player = NULL;
static fixed_t view_saved_x, view_saved_y, view_saved_z;
static angle_t view_saved_angle;

static fixed_t lerp_fixed(fixed_t a, fixed_t b, uint32_t frac) {
  return a + (fixed_t)((((int64_t)b - (int64_t)a) * (int64_t)frac) >> 16);
}

static angle_t lerp_angle(angle_t a, angle_t b, uint32_t frac) {
  int32_t delta = (int32_t)(b - a);
  return a + (angle_t)(((int64_t)delta * (int64_t)frac) >> 16);
}

/* The renderer takes the camera straight from the player (R_SetupFrame), so
   these are the values that have to be interpolated. */
static int view_sample(view_state_t *s) {
  player_t *p = &players[displayplayer];
  if (gamestate != GS_LEVEL || p->mo == NULL)
    return 0;
  s->x = p->mo->x;
  s->y = p->mo->y;
  s->z = p->viewz;
  s->angle = p->mo->angle;
  return 1;
}

/* Remember the view the last tic produced, keeping the previous one around:
   the frame that gets drawn is always between those two. */
static void view_advance(void) {
  view_state_t s;
  if (!view_sample(&s)) {
    view_valid = 0;
    view_had = 0;
    return;
  }
  if (!view_had) {
    view_prev = s;
    view_cur = s;
    view_had = 1;
  } else {
    view_prev = view_cur;
    view_cur = s;
  }
  view_valid = 1;
}

/* Move the player for the frame being drawn only: the values are restored as
   soon as the frame is on screen, so the simulation never sees them. */
static void view_apply(uint32_t frac) {
  player_t *p = &players[displayplayer];
  if (!view_valid || p->mo == NULL)
    return;
  view_player = p;
  view_saved_x = p->mo->x;
  view_saved_y = p->mo->y;
  view_saved_angle = p->mo->angle;
  view_saved_z = p->viewz;
  p->mo->x = lerp_fixed(view_prev.x, view_cur.x, frac);
  p->mo->y = lerp_fixed(view_prev.y, view_cur.y, frac);
  p->mo->angle = lerp_angle(view_prev.angle, view_cur.angle, frac);
  p->viewz = lerp_fixed(view_prev.z, view_cur.z, frac);
  view_overridden = 1;
}

static void view_restore(void) {
  if (!view_overridden)
    return;
  if (view_player && view_player->mo) {
    view_player->mo->x = view_saved_x;
    view_player->mo->y = view_saved_y;
    view_player->mo->angle = view_saved_angle;
    view_player->viewz = view_saved_z;
  }
  view_overridden = 0;
}

/* ------------------------------------------------------------------ *
 * Interpolation of the things and of the weapon                      *
 *                                                                     *
 * A position only changes when a tic runs. The value from the previous *
 * tic is kept in the mobj (and in the weapon sprite) itself, and the   *
 * frames in between are drawn from the two of them, so monsters,       *
 * projectiles, items and the weapon sway move continuously instead of  *
 * stepping 35 times per second.                                        *
 * ------------------------------------------------------------------ */

/* The engine only builds the thinker list while a level is up: P_SetupLevel
   calls P_InitThinkers for it, and so does the savegame unarchive. Before
   that - the title screen - and after a level is torn down, thinkercap is
   still the zeroed global it starts as, so its next pointer is NULL and
   walking the list would read address zero. */
static thinker_t *thing_list(void) {
  thinker_t *th;
  if (gamestate != GS_LEVEL)
    return NULL;
  th = thinkercap.next;
  if (th == NULL || th == &thinkercap)
    return NULL;
  return th;
}

/* The sectors of a level are only valid while one is loaded. */
static int sectors_ready(void) {
  return gamestate == GS_LEVEL && numsectors > 0 && sectors != NULL;
}

/* Nothing that moved further than this inside a single tic walked there: it
   was spawned, teleported or the level was rebuilt. Drawing in between such
   two positions would smear it across the map, so those are left alone. */
#define INTERP_MAX_STEP (512 << FRACBITS)

static int interp_near(fixed_t a, fixed_t b) {
  int64_t delta = (int64_t)a - (int64_t)b;
  if (delta < 0)
    delta = -delta;
  return delta <= (int64_t)INTERP_MAX_STEP;
}

static void things_snapshot(void) {
  thinker_t *th;
  int i, p;
  for (th = thing_list(); th != NULL && th != &thinkercap; th = th->next) {
    mobj_t *mo;
    if (th->function.acp1 != (actionf_p1)P_MobjThinker)
      continue;
    mo = (mobj_t *)th;
    mo->prev_x = mo->x;
    mo->prev_y = mo->y;
    mo->prev_z = mo->z;
  }
  for (p = 0; p < MAXPLAYERS; ++p)
    for (i = 0; i < NUMPSPRITES; ++i) {
      players[p].psprites[i].prev_sx = players[p].psprites[i].sx;
      players[p].psprites[i].prev_sy = players[p].psprites[i].sy;
    }
  if (sectors_ready())
    for (i = 0; i < numsectors; ++i) {
      sectors[i].prev_floorheight = sectors[i].floorheight;
      sectors[i].prev_ceilingheight = sectors[i].ceilingheight;
    }
}

/* Swaps every live value with the one from the previous tic and, while a
   frame is being drawn, keeps the interpolated value in the live field. The
   second call swaps everything back, so the simulation never sees an
   interpolated position. */
static void things_swap(int interpolate, uint32_t frac) {
  thinker_t *th;
  mobj_t *camera = players[displayplayer].mo;
  int i;
  for (th = thing_list(); th != NULL && th != &thinkercap; th = th->next) {
    mobj_t *mo;
    fixed_t swap;
    if (th->function.acp1 != (actionf_p1)P_MobjThinker)
      continue;
    mo = (mobj_t *)th;
    if (mo == camera)
      continue; /* the view itself is interpolated by view_apply() */
    if (mo->x == mo->prev_x && mo->y == mo->prev_y && mo->z == mo->prev_z)
      continue; /* it did not move: exchanging equal values changes nothing */
    swap = mo->x; mo->x = mo->prev_x; mo->prev_x = swap;
    swap = mo->y; mo->y = mo->prev_y; mo->prev_y = swap;
    swap = mo->z; mo->z = mo->prev_z; mo->prev_z = swap;
    if (interpolate && interp_near(mo->x, mo->prev_x) &&
        interp_near(mo->y, mo->prev_y) && interp_near(mo->z, mo->prev_z)) {
      mo->x = lerp_fixed(mo->x, mo->prev_x, frac);
      mo->y = lerp_fixed(mo->y, mo->prev_y, frac);
      mo->z = lerp_fixed(mo->z, mo->prev_z, frac);
    }
  }
  /* Sector heights: doors, lifts and moving floors. Nothing runs the
     simulation between the two calls, so the renderer is the only reader. */
  if (sectors_ready())
    for (i = 0; i < numsectors; ++i) {
      sector_t *sec = &sectors[i];
      fixed_t swap;
      if (sec->floorheight == sec->prev_floorheight &&
          sec->ceilingheight == sec->prev_ceilingheight)
        continue; /* most sectors never move, doors and lifts are the ones */
      swap = sec->floorheight;
      sec->floorheight = sec->prev_floorheight;
      sec->prev_floorheight = swap;
      swap = sec->ceilingheight;
      sec->ceilingheight = sec->prev_ceilingheight;
      sec->prev_ceilingheight = swap;
      if (interpolate && interp_near(sec->floorheight, sec->prev_floorheight) &&
          interp_near(sec->ceilingheight, sec->prev_ceilingheight)) {
        sec->floorheight = lerp_fixed(sec->floorheight, sec->prev_floorheight, frac);
        sec->ceilingheight =
            lerp_fixed(sec->ceilingheight, sec->prev_ceilingheight, frac);
      }
    }
  for (i = 0; i < NUMPSPRITES; ++i) {
    pspdef_t *psp = &players[displayplayer].psprites[i];
    fixed_t swap;
    if (psp->sx == psp->prev_sx && psp->sy == psp->prev_sy)
      continue;
    swap = psp->sx; psp->sx = psp->prev_sx; psp->prev_sx = swap;
    swap = psp->sy; psp->sy = psp->prev_sy; psp->prev_sy = swap;
    if (interpolate && interp_near(psp->sx, psp->prev_sx) &&
        interp_near(psp->sy, psp->prev_sy)) {
      psp->sx = lerp_fixed(psp->sx, psp->prev_sx, frac);
      psp->sy = lerp_fixed(psp->sy, psp->prev_sy, frac);
    }
  }
}

/* How far the current tic has progressed, in 16.16 fixed point. */
static uint32_t subtic_fraction(void) {
  uint64_t us = sceKernelGetProcessTimeLow();
  uint64_t into_tic = (us * (uint64_t)TICRATE) % 1000000ULL;
  return (uint32_t)((into_tic << 16) / 1000000ULL);
}

static void game_loop_classic(void) {
  for (;;)
    doomgeneric_Tick();
}

static void game_loop_smooth(void) {
  int last_tic, interpolate;
  view_advance();
  last_tic = I_GetTime();
  interpolate = 1;
  for (;;) {
    int now_tic = I_GetTime();
    if (now_tic != last_tic) {
      int before = now_tic;
      last_tic = now_tic;
      /* Remember where everything was, then run the game tics without
         drawing: the frame that follows shows the state they produced,
         interpolated from the one before. */
      things_snapshot();
      I_StartFrame();
      TryRunTics();
      S_UpdateSounds(players[consoleplayer].mo);
      view_advance();
      /* TryRunTics() catches up after a wipe or a hiccup and runs several tics
         at once; there is no picture to show between those, so the frames of
         this tic are drawn from the state the tics produced. */
      interpolate = (I_GetTime() - before) <= 1;
    }
    if (!screenvisible) {
      sceKernelDelayThread(16000);
      continue;
    }
    {
      uint32_t frac = subtic_fraction();
      if (interpolate) {
        view_apply(frac);
        things_swap(1, frac);
      }
      D_Display();
      if (interpolate) {
        things_swap(0, 0);
        view_restore();
      }
    }
  }
}

/* MAIN */
int main(int argc, char **argv) {
  SceAppUtilInitParam ip;
  SceAppUtilBootParam bp;
  int selected = 0;
  (void)argc;
  (void)argv;

  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);
  memset(&ip, 0, sizeof(ip));
  memset(&bp, 0, sizeof(bp));
  sceAppUtilInit(&ip, &bp);
  sceIoMkdir("ux0:/data/", 0777);
  sceIoMkdir(VITA_GAME_DATA_DIR, 0777);
  sceIoRemove(VITA_GAME_DATA_DIR "debug.log");
  debug_log("=== Chex Quest Collection ===");
  settings_load();
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  init_display();
  if (!display_ready) fatal_error("Could not initialize Vita display");
  base_time = get_ms();
  I_InitGraphics();
  draw_launcher(selected);
  {
    SceUID music_file = sceIoOpen("app0:/menu_music.pcm", SCE_O_RDONLY, 0);
    if (music_file >= 0) {
      SceIoStat music_stat;
      memset(&music_stat, 0, sizeof(music_stat));
      if (sceIoGetstat("app0:/menu_music.pcm", &music_stat) >= 0
          && music_stat.st_size > 0 && music_stat.st_size <= 3 * 1024 * 1024) {
        unsigned char *music = (unsigned char *)malloc((size_t)music_stat.st_size);
        if (music) {
          int read_size = sceIoRead(music_file, music, (unsigned int)music_stat.st_size);
          if (read_size == music_stat.st_size) {
            menu_music_data = music;
            menu_music_length = read_size;
            menu_music_position = 0;
            menu_music_rate_phase = 0;
            start_audio_system();
            menu_music_active = audio_ready;
            debug_logf("Menu music loaded: %d bytes, audio=%d", read_size, audio_ready);
          } else {
            free(music);
            debug_logf("Menu music read failed: %d", read_size);
          }
        }
      }
      sceIoClose(music_file);
    } else {
      debug_log("Menu music unavailable; continuing silently");
    }
  }
  /* The launcher blips need audio even when the soundtrack is missing. */
  start_audio_system();

  {
    SceCtrlData previous, pad;
    uint32_t last_draw = 0, last_status = 0;
    int needs_draw = 1;
    memset(&previous, 0, sizeof(previous));
    launcher_refresh_data();
    /* Without game data the download screen is the useful first stop. */
    if (launcher_ready_count() == 0)
      selected = L_ITEM_DATA;
    for (;;) {
      uint32_t now = get_ms();
      sceCtrlPeekBufferPositive(0, &pad, 1);
      if (now - last_status >= 1000) {
        launcher_refresh_data();
        last_status = now;
        needs_draw = 1;
      }
      if ((pad.buttons & (SCE_CTRL_LEFT | SCE_CTRL_UP)) &&
          !(previous.buttons & (SCE_CTRL_LEFT | SCE_CTRL_UP))) {
        selected = (selected + L_ITEMS - 1) % L_ITEMS;
        launcher_play_beep();
        needs_draw = 1;
      }
      if ((pad.buttons & (SCE_CTRL_RIGHT | SCE_CTRL_DOWN)) &&
          !(previous.buttons & (SCE_CTRL_RIGHT | SCE_CTRL_DOWN))) {
        selected = (selected + 1) % L_ITEMS;
        launcher_play_beep();
        needs_draw = 1;
      }
      if (needs_draw || now - last_draw >= 70) {
        draw_launcher(selected);
        launcher_frame_tick++;
        last_draw = now;
        needs_draw = 0;
      }
      if ((pad.buttons & SCE_CTRL_SQUARE) && !(previous.buttons & SCE_CTRL_SQUARE)) {
        show_data_screen(-1);
        needs_draw = 1;
      } else if ((pad.buttons & SCE_CTRL_SELECT) && !(previous.buttons & SCE_CTRL_SELECT)) {
        show_options_screen();
        needs_draw = 1;
      } else if ((pad.buttons & SCE_CTRL_TRIANGLE) && !(previous.buttons & SCE_CTRL_TRIANGLE)) {
        sceKernelExitProcess(0);
      } else if ((pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_START)) &&
                 !(previous.buttons & (SCE_CTRL_CROSS | SCE_CTRL_START))) {
        if (selected == L_ITEM_DATA) {
          show_data_screen(-1);
          needs_draw = 1;
        } else if (!launcher_game_ready(selected)) {
          /* Missing WADs: show where to get them instead of a fatal error. */
          show_data_screen(selected);
          needs_draw = 1;
        } else {
          break;
        }
      }
      previous = pad;
      sceKernelDelayThread(16000);
    }
    /* Stop menu audio and restore Doom's normal palette conversion. */
    menu_music_active = 0;
    launcher_frame = 0;

    {
      char *nargv[8];
      int nargc = 0;
      static char program[] = "ChexQuestCollection";
      static char iwad_arg[] = "-iwad", file_arg[] = "-file", chex3_arg[] = "-chex3";
      static char collection_game_arg[] = "-collection-game";
      static char game_ids[][2] = { "1", "2", "3" };
      static char chex_iwad[] = VITA_GAME_DATA_DIR "CHEX.WAD";
      static char chex2_pwad[] = VITA_GAME_DATA_DIR "CHEX2.WAD";
      static char chex3_iwad[] = "ux0:/data/chexquestcollection/chex3v.wad";
      char patch_path[] = "ux0:/data/chexquestcollection/chex3.deh";
      nargv[nargc++] = program;
      nargv[nargc++] = iwad_arg;
      if (selected == 2) {
        SceUID wadfd = sceIoOpen(chex3_iwad, SCE_O_RDONLY, 0);
        SceUID dehfd = sceIoOpen(patch_path, SCE_O_RDONLY, 0);
        if (wadfd < 0 || dehfd < 0) fatal_error("Chex 3 needs chex3v.wad and chex3.deh in ux0:/data/chexquestcollection/");
        sceIoClose(wadfd); sceIoClose(dehfd);
        nargv[nargc++] = chex3_iwad;
        nargv[nargc++] = chex3_arg;
      } else {
        SceUID wadfd = sceIoOpen(chex_iwad, SCE_O_RDONLY, 0);
        if (wadfd < 0) fatal_error("Chex 1/2 needs CHEX.WAD in ux0:/data/chexquestcollection/");
        sceIoClose(wadfd);
        nargv[nargc++] = chex_iwad;
        if (selected == 1) {
          SceUID pwadfd = sceIoOpen(chex2_pwad, SCE_O_RDONLY, 0);
          if (pwadfd < 0) fatal_error("Chex 2 needs CHEX2.WAD in ux0:/data/chexquestcollection/");
          sceIoClose(pwadfd);
          nargv[nargc++] = file_arg;
          nargv[nargc++] = chex2_pwad;
        }
      }
      nargv[nargc++] = collection_game_arg;
      nargv[nargc++] = game_ids[selected];
      nargv[nargc] = NULL;
      debug_logf("Starting Chex Quest %d", selected + 1);
      doomgeneric_Create(nargc, nargv);
    }
  }
  if (fps_target == FPS_SMOOTH)
    game_loop_smooth();
  else
    game_loop_classic();
  return 0;
}
