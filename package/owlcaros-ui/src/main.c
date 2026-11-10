/*
 * owlcaros-ui - SDL2 dashboard for OwlCarOS
 *
 * Uses SDL2 with KMS/DRM and reads keyboard events from evdev.
 *
 *
 * The visual language is inspired by the red dot-matrix instrument
 * cluster of the 2004 Audi TT (8N): black background, red LEDs and
 * warm white markings.
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <signal.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_INPUTS      16
#define MAX_ENTRIES     400
#define MAX_PLAYLIST    400
#define BT_TEXT         160
#define TRACK_PATH      512
#define TRACK_NAME      96
#define BT_A2DP_PIDFILE "/var/run/bluealsa-aplay.pid"
#define BT_A2DP_SERVICE "/etc/init.d/S46bluealsa-aplay"

#define SCREEN_HOME   0
#define SCREEN_MEDIA  1
#define SCREEN_SYSTEM 2

/* Pages of the Settings screen. */
#define SYS_MENU            0
#define SYS_MONITOR         1
#define SYS_CONFIRM         2   /* reboot / shutdown */
#define SYS_INSTALL_PICK    3   /* choose the target drive */
#define SYS_INSTALL_CONFIRM 4
#define SYS_INSTALL_RUN     5   /* progress and result */

#define SRC_NONE      0
#define SRC_LOCAL     1
#define SRC_BT        2

/*
 * 5x7 bitmap font. Each glyph is 7 rows, one byte per row.
 * Bit 4 is the left-most pixel, bit 0 the right-most.
 * Lower-case characters are folded to upper-case by draw_char().
 */
static const uint8_t font5x7[96][7] = {
    [' ' - 32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['!' - 32] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},
    ['%' - 32] = {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13},
    ['\'' - 32] = {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00},
    ['(' - 32] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02},
    [')' - 32] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08},
    ['+' - 32] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
    [',' - 32] = {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08},
    ['-' - 32] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    ['.' - 32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},
    ['/' - 32] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00},
    ['0' - 32] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    ['1' - 32] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['2' - 32] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    ['3' - 32] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    ['4' - 32] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    ['5' - 32] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    ['6' - 32] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    ['7' - 32] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    ['8' - 32] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    ['9' - 32] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    [':' - 32] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00},
    ['<' - 32] = {0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01},
    ['=' - 32] = {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00},
    ['>' - 32] = {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10},
    ['?' - 32] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
    ['A' - 32] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['B' - 32] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    ['C' - 32] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    ['D' - 32] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    ['E' - 32] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    ['F' - 32] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    ['G' - 32] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
    ['H' - 32] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['I' - 32] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['J' - 32] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
    ['K' - 32] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    ['L' - 32] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    ['M' - 32] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    ['N' - 32] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    ['O' - 32] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['P' - 32] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    ['Q' - 32] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    ['R' - 32] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    ['S' - 32] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    ['T' - 32] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    ['U' - 32] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['V' - 32] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    ['W' - 32] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
    ['X' - 32] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    ['Y' - 32] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    ['Z' - 32] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    ['_' - 32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},
    ['|' - 32] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
};

struct fb {
    SDL_Window *window;
    SDL_Renderer *renderer;
    unsigned xres;
    unsigned yres;
};

#define ENT_BT      0   /* the Bluetooth audio source */
#define ENT_DIR     1   /* a folder that can be opened */
#define ENT_FILE    2   /* a playable WAV/MP3 file */
#define ENT_IMPORT  3   /* "copy USB music to the library" action */

/* One row of the media browser. */
struct entry {
    char path[TRACK_PATH];
    char name[TRACK_NAME];
    int type;
};

struct media {
    /* Browser: what the list shows right now. */
    struct entry entries[MAX_ENTRIES];
    int entry_count;
    int selected;
    char dir[TRACK_PATH];      /* "" = top level (sources) */

    /* Playlist: the audio files of the folder playback started in. */
    char playlist[MAX_PLAYLIST][TRACK_PATH];
    int play_count;
    int play_index;            /* -1 when nothing local is loaded */
    char play_name[TRACK_NAME];
    double play_duration;

    int source;          /* SRC_NONE / SRC_LOCAL / SRC_BT */
    int paused;
    pid_t pid;
    double started;      /* monotonic time the current play segment began */
    double elapsed;      /* accumulated playback time before that segment */
    int bt_was_stopped;  /* we stopped the A2DP player for local audio */
    char mixer[48];
    int volume;
    int muted;

    /* Bluetooth state read from BlueZ over D-Bus. */
    int bt_connected;
    char bt_device[TRACK_NAME];   /* phone name */
    char bt_player[160];          /* MediaPlayer1 object path, "" if none */
    char bt_title[BT_TEXT];
    char bt_artist[BT_TEXT];
    char bt_album[BT_TEXT];
    char bt_status[24];           /* playing / paused / stopped ... */

    char import_status[64];       /* last line from the USB import job */
};

struct proc_row {
    int pid;
    unsigned long rss_kb;
    char name[32];
};

struct sys_info {
    int cpu_percent;
    int mem_percent;
    unsigned long mem_total_kb;
    unsigned long mem_used_kb;
    unsigned long uptime_seconds;
    int process_count;
    char load[32];
    struct proc_row top[5];
    int top_count;
};

#define MAX_DISKS 8

/* A drive the installer may erase. */
struct install_disk {
    char dev[64];
    char label[128];
};

struct install {
    struct install_disk disks[MAX_DISKS];
    int count;
    int selected;
    char status[96];     /* last line reported by the install job */
};

struct ui {
    struct fb fb;
    int ttyfd;
    int screen;
    int selected;
    int system_selected;
    int system_page; /* SYS_MENU ... SYS_INSTALL_RUN */
    int system_action; /* 1=reboot, 2=shutdown */
    int confirm_selected; /* 0=cancel, 1=do it */
    struct install install;
    struct sys_info system;
    char status[64];
    struct media media;
};

static volatile sig_atomic_t g_running = 1;
static TTF_Font *g_fonts[4];
static int g_ttf_ready;
static double g_next_bt_refresh; /* when to poll BlueZ again */
static const char *const font_path =
    "/usr/share/fonts/dejavu/DejaVuSansCondensed.ttf";

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static int imax(int a, int b)
{
    return a > b ? a : b;
}

static int iclamp(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static double mono_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void set_draw_color(struct fb *fb, uint32_t color)
{
    SDL_SetRenderDrawColor(fb->renderer, (Uint8)(color >> 16),
                           (Uint8)(color >> 8), (Uint8)color, 255);
}

static void fill_rect(struct fb *fb, int x, int y, int w, int h,
                      uint32_t color)
{
    SDL_Rect rect;

    if (w <= 0 || h <= 0)
        return;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    set_draw_color(fb, color);
    SDL_RenderFillRect(fb->renderer, &rect);
}

static void draw_char(struct fb *fb, int x, int y, char ch, int scale,
                      uint32_t color)
{
    unsigned char uch = (unsigned char)ch;

    if (uch >= 'a' && uch <= 'z')
        uch = (unsigned char)(uch - 'a' + 'A');
    if (uch < 32 || uch > 127)
        uch = '?';

    const uint8_t *glyph = font5x7[uch - 32];
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1u << (4 - col)))
                fill_rect(fb, x + col * scale, y + row * scale,
                          scale, scale, color);
        }
    }
}

static TTF_Font *font_for_scale(int scale)
{
    return g_fonts[iclamp(scale, 1, 3)];
}

static int font_height(int scale)
{
    TTF_Font *font = font_for_scale(scale);

    return font != NULL ? TTF_FontHeight(font) : 7 * scale;
}

static void draw_text(struct fb *fb, int x, int y, const char *text, int scale,
                      uint32_t color)
{
    TTF_Font *font = font_for_scale(scale);
    SDL_Color foreground = {
        (Uint8)(color >> 16), (Uint8)(color >> 8), (Uint8)color, 255
    };
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect dst;

    if (text == NULL || text[0] == '\0')
        return;
    if (font == NULL) {
        int cx = x;
        for (; *text; text++) {
            draw_char(fb, cx, y, *text, scale, color);
            cx += 6 * scale;
        }
        return;
    }

    surface = TTF_RenderUTF8_Blended(font, text, foreground);
    if (surface == NULL)
        return;
    texture = SDL_CreateTextureFromSurface(fb->renderer, surface);
    if (texture != NULL) {
        dst.x = x;
        dst.y = y;
        dst.w = surface->w;
        dst.h = surface->h;
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(fb->renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static int text_width(const char *text, int scale)
{
    TTF_Font *font = font_for_scale(scale);
    int width = 0;

    if (font != NULL && TTF_SizeUTF8(font, text, &width, NULL) == 0)
        return width;
    return (int)strlen(text) > 0 ? ((int)strlen(text) * 6 - 1) * scale : 0;
}

static void draw_text_centered(struct fb *fb, int x, int y, int w,
                               const char *text, int scale, uint32_t color)
{
    draw_text(fb, x + (w - text_width(text, scale)) / 2, y, text, scale, color);
}

static void draw_text_ellipsis(struct fb *fb, int x, int y, int maxw,
                               const char *text, int scale, uint32_t color)
{
    char buf[512];
    size_t len;

    snprintf(buf, sizeof(buf) - 4, "%s", text);
    if (text_width(buf, scale) <= maxw) {
        draw_text(fb, x, y, buf, scale, color);
        return;
    }

    /* Drop whole UTF-8 characters from the end until "text..." fits. */
    len = strlen(buf);
    while (len > 0) {
        len--;
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80)
            len--;
        while (len > 0 && buf[len - 1] == ' ')
            len--;
        memcpy(buf + len, "...", 4);
        if (text_width(buf, scale) <= maxw)
            break;
    }
    if (len > 0)
        draw_text(fb, x, y, buf, scale, color);
}

static void fb_present(struct fb *fb)
{
    SDL_RenderPresent(fb->renderer);
}

static void close_fonts(void)
{
    for (int i = 1; i <= 3; i++) {
        if (g_fonts[i] != NULL) {
            TTF_CloseFont(g_fonts[i]);
            g_fonts[i] = NULL;
        }
    }
    if (g_ttf_ready) {
        TTF_Quit();
        g_ttf_ready = 0;
    }
}

static int fb_init(struct fb *fb)
{
    static const int font_sizes[4] = { 0, 13, 18, 26 };
    SDL_DisplayMode mode;
    SDL_RendererInfo renderer_info;
    int width = 1024;
    int height = 600;

    memset(fb, 0, sizeof(*fb));
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "owlcaros-ui: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "owlcaros-ui: TTF_Init: %s\n", TTF_GetError());
        SDL_Quit();
        return -1;
    }
    g_ttf_ready = 1;
    for (int i = 1; i <= 3; i++) {
        g_fonts[i] = TTF_OpenFont(font_path, font_sizes[i]);
        if (g_fonts[i] == NULL) {
            fprintf(stderr, "owlcaros-ui: open font %s: %s\n",
                    font_path, TTF_GetError());
            close_fonts();
            SDL_Quit();
            return -1;
        }
        TTF_SetFontHinting(g_fonts[i], TTF_HINTING_NORMAL);
    }

    if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
        width = mode.w;
        height = mode.h;
    }

    fb->window = SDL_CreateWindow("OwlCarOS", SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, width, height,
                                  SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (fb->window == NULL) {
        fprintf(stderr, "owlcaros-ui: SDL_CreateWindow: %s\n", SDL_GetError());
        goto fail;
    }

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    fb->renderer = SDL_CreateRenderer(fb->window, -1,
                                      SDL_RENDERER_ACCELERATED
                                      | SDL_RENDERER_PRESENTVSYNC);
    if (fb->renderer == NULL) {
        fprintf(stderr, "owlcaros-ui: accelerated SDL renderer: %s\n",
                SDL_GetError());
        goto fail;
    }
    if (SDL_GetRendererInfo(fb->renderer, &renderer_info) < 0
        || !(renderer_info.flags & SDL_RENDERER_ACCELERATED)) {
        fprintf(stderr, "owlcaros-ui: SDL did not provide a GPU renderer\n");
        goto fail;
    }

    SDL_GetWindowSize(fb->window, &width, &height);
    fb->xres = (unsigned)width;
    fb->yres = (unsigned)height;
    fprintf(stderr, "owlcaros-ui: GPU renderer %s at %ux%u\n",
            renderer_info.name, fb->xres, fb->yres);
    return 0;

fail:
    if (fb->renderer != NULL)
        SDL_DestroyRenderer(fb->renderer);
    if (fb->window != NULL)
        SDL_DestroyWindow(fb->window);
    close_fonts();
    SDL_Quit();
    return -1;
}

static void fb_close(struct fb *fb)
{
    if (fb->renderer != NULL)
        SDL_DestroyRenderer(fb->renderer);
    if (fb->window != NULL)
        SDL_DestroyWindow(fb->window);
    close_fonts();
    SDL_Quit();
}

static int tty_graphics(void)
{
    static const char *const paths[] = { "/dev/tty1", "/dev/tty0", NULL };
    int fd;

    for (int i = 0; paths[i] != NULL; i++) {
        fd = open(paths[i], O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (ioctl(fd, KDSETMODE, KD_GRAPHICS) == 0)
            return fd;
        close(fd);
    }

    perror("owlcaros-ui: KDSETMODE KD_GRAPHICS");
    return -1;
}

static void tty_text(int fd)
{
    if (fd >= 0) {
        ioctl(fd, KDSETMODE, KD_TEXT);
        close(fd);
    }
}

static int input_open(int *fds, int max)
{
    int count = 0;

    for (int i = 0; i < 32 && count < max; i++) {
        char path[64];
        unsigned long evbit[(EV_MAX + 1 + sizeof(unsigned long) * 8 - 1)
                            / (sizeof(unsigned long) * 8)];

        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        memset(evbit, 0, sizeof(evbit));
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0
            || !(evbit[EV_KEY / (sizeof(unsigned long) * 8)]
                 & (1UL << (EV_KEY % (sizeof(unsigned long) * 8))))) {
            close(fd);
            continue;
        }

        fds[count++] = fd;
    }

    return count;
}

static void run_cmd(const char *fmt, ...)
{
    char cmd[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);

    int rc = system(cmd);

    (void)rc;
}

static int pidfile_alive(const char *path)
{
    FILE *f = fopen(path, "r");
    long pid = 0;
    int ok;

    if (f == NULL)
        return 0;
    ok = fscanf(f, "%ld", &pid) == 1;
    fclose(f);
    return ok && pid > 0 && kill((pid_t)pid, 0) == 0;
}

static int bt_aplay_available(void)
{
    return access(BT_A2DP_SERVICE, X_OK) == 0;
}

static int bt_aplay_running(void)
{
    return pidfile_alive(BT_A2DP_PIDFILE);
}

static void bt_aplay(int start)
{
    run_cmd("%s %s >/dev/null 2>&1", BT_A2DP_SERVICE, start ? "start" : "stop");
}

static int bt_adapter_present(void)
{
    return access("/sys/class/bluetooth/hci0", F_OK) == 0;
}

/*
 * Bluetooth metadata and remote control.
 *
 * BlueZ exposes the phone's AVRCP player as an org.bluez.MediaPlayer1
 * object. We read every BlueZ object with one ObjectManager call through
 * dbus-send and pick out the connected phone and its player. The same
 * player object takes Play/Pause/Next/Previous method calls.
 */
struct bt_player_tmp {
    char path[160];
    char title[BT_TEXT];
    char artist[BT_TEXT];
    char album[BT_TEXT];
    char status[24];
};

static void bt_commit_player(struct media *m, const struct bt_player_tmp *t,
                             int *have)
{
    if (t->path[0] == '\0')
        return;
    /* Prefer a player that is actually playing over an idle one. */
    if (*have && strcasecmp(t->status, "playing") != 0)
        return;
    snprintf(m->bt_player, sizeof(m->bt_player), "%s", t->path);
    snprintf(m->bt_title, sizeof(m->bt_title), "%s", t->title);
    snprintf(m->bt_artist, sizeof(m->bt_artist), "%s", t->artist);
    snprintf(m->bt_album, sizeof(m->bt_album), "%s", t->album);
    snprintf(m->bt_status, sizeof(m->bt_status), "%s", t->status);
    *have = 1;
}

/* Copy the text between the first quote in s and the last quote. */
static void quoted_value(const char *s, char *out, size_t n)
{
    const char *a = strchr(s, '"');
    const char *b = a ? strrchr(a + 1, '"') : NULL;
    size_t len;

    out[0] = '\0';
    if (a == NULL || b == NULL)
        return;
    len = (size_t)(b - a - 1);
    if (len >= n)
        len = n - 1;
    memcpy(out, a + 1, len);
    out[len] = '\0';
}

/* Parse "dbus-send --print-reply" output of GetManagedObjects. */
static void bt_parse(FILE *p, struct media *m)
{
    enum { IF_OTHER, IF_PLAYER, IF_DEVICE } iface = IF_OTHER;
    struct bt_player_tmp pl;
    char line[512];
    char object[160] = "";
    char key[48] = "";
    char dev_name[TRACK_NAME] = "";
    int dev_connected = 0;
    int have_player = 0;

    memset(&pl, 0, sizeof(pl));
    m->bt_connected = 0;
    m->bt_device[0] = '\0';
    m->bt_player[0] = '\0';
    m->bt_title[0] = '\0';
    m->bt_artist[0] = '\0';
    m->bt_album[0] = '\0';
    m->bt_status[0] = '\0';

    while (fgets(line, sizeof(line), p) != NULL) {
        const char *s = line;
        int new_object;
        int new_iface = 0;
        char value[BT_TEXT];

        while (*s == ' ' || *s == '\t')
            s++;

        new_object = strncmp(s, "object path \"", 13) == 0;
        if (strncmp(s, "string \"", 8) == 0) {
            quoted_value(s, value, sizeof(value));
            if (strncmp(value, "org.", 4) == 0)
                new_iface = 1;
            else
                snprintf(key, sizeof(key), "%.47s", value);
        }

        if (new_object || new_iface) {
            /* Leaving the previous interface block: store what it held. */
            if (iface == IF_PLAYER)
                bt_commit_player(m, &pl, &have_player);
            if (iface == IF_DEVICE && dev_connected && !m->bt_connected) {
                m->bt_connected = 1;
                snprintf(m->bt_device, sizeof(m->bt_device), "%s", dev_name);
            }
            memset(&pl, 0, sizeof(pl));
            dev_name[0] = '\0';
            dev_connected = 0;
            key[0] = '\0';
            iface = IF_OTHER;
        }
        if (new_object) {
            quoted_value(s, object, sizeof(object));
            continue;
        }
        if (new_iface) {
            if (strcmp(value, "org.bluez.MediaPlayer1") == 0) {
                iface = IF_PLAYER;
                snprintf(pl.path, sizeof(pl.path), "%s", object);
            } else if (strcmp(value, "org.bluez.Device1") == 0) {
                iface = IF_DEVICE;
            }
            continue;
        }

        if (strncmp(s, "variant", 7) != 0 || iface == IF_OTHER)
            continue;

        if (iface == IF_DEVICE) {
            if (strcmp(key, "Connected") == 0)
                dev_connected = strstr(s, "boolean true") != NULL;
            else if (strcmp(key, "Alias") == 0 && strstr(s, "string \""))
                quoted_value(strstr(s, "string \""), dev_name,
                             sizeof(dev_name));
            continue;
        }

        if (strstr(s, "string \"") == NULL)
            continue;
        quoted_value(strstr(s, "string \""), value, sizeof(value));
        if (strcmp(key, "Status") == 0)
            snprintf(pl.status, sizeof(pl.status), "%.23s", value);
        else if (strcmp(key, "Title") == 0)
            snprintf(pl.title, sizeof(pl.title), "%s", value);
        else if (strcmp(key, "Artist") == 0)
            snprintf(pl.artist, sizeof(pl.artist), "%s", value);
        else if (strcmp(key, "Album") == 0)
            snprintf(pl.album, sizeof(pl.album), "%s", value);
    }

    if (iface == IF_PLAYER)
        bt_commit_player(m, &pl, &have_player);
    if (iface == IF_DEVICE && dev_connected && !m->bt_connected) {
        m->bt_connected = 1;
        snprintf(m->bt_device, sizeof(m->bt_device), "%s", dev_name);
    }
}

static void bt_metadata_refresh(struct media *m)
{
    FILE *p;

    if (!bt_adapter_present()) {
        m->bt_connected = 0;
        m->bt_player[0] = '\0';
        m->bt_title[0] = '\0';
        m->bt_artist[0] = '\0';
        m->bt_album[0] = '\0';
        m->bt_status[0] = '\0';
        return;
    }

    p = popen("dbus-send --system --print-reply --reply-timeout=1500 "
              "--dest=org.bluez / "
              "org.freedesktop.DBus.ObjectManager.GetManagedObjects "
              "2>/dev/null", "r");
    if (p == NULL)
        return;
    bt_parse(p, m);
    pclose(p);
}

/* Send Play, Pause, Next or Previous to the phone. Returns 0 if it has
 * no remote-controllable player. */
static int bt_control(const struct media *m, const char *method)
{
    if (m->bt_player[0] == '\0')
        return 0;
    run_cmd("dbus-send --system --type=method_call --dest=org.bluez "
            "'%s' org.bluez.MediaPlayer1.%s >/dev/null 2>&1",
            m->bt_player, method);
    return 1;
}

static int bt_is_playing(const struct media *m)
{
    return strcasecmp(m->bt_status, "playing") == 0;
}

static int alsa_playback_running(void)
{
    char path[96];

    for (int card = 0; card < 4; card++) {
        for (int pcm = 0; pcm < 4; pcm++) {
            char line[128];
            int running = 0;
            FILE *f;

            snprintf(path, sizeof(path),
                     "/proc/asound/card%d/pcm%dp/sub0/status", card, pcm);
            f = fopen(path, "r");
            if (f == NULL)
                continue;
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, "state: RUNNING") != NULL)
                    running = 1;
            }
            fclose(f);
            if (running)
                return 1;
        }
    }

    return 0;
}

static void audio_refresh(struct ui *ui)
{
    struct media *m = &ui->media;
    char cmd[192];
    char line[256];
    int vol = -1;
    int on = 0;
    int off = 0;
    FILE *p;

    snprintf(cmd, sizeof(cmd), "amixer sget '%s' 2>/dev/null", m->mixer);
    p = popen(cmd, "r");
    if (p == NULL)
        return;

    while (fgets(line, sizeof(line), p)) {
        char *pct = strchr(line, '[');

        while (pct != NULL) {
            if (pct[1] >= '0' && pct[1] <= '9') {
                char *end;
                long v = strtol(pct + 1, &end, 10);

                if (*end == '%') {
                    vol = (int)v;
                    break;
                }
            }
            pct = strchr(pct + 1, '[');
        }
        if (strstr(line, "[off]") != NULL)
            off = 1;
        if (strstr(line, "[on]") != NULL)
            on = 1;
    }
    pclose(p);

    if (vol >= 0)
        m->volume = iclamp(vol, 0, 100);
    if (on)
        m->muted = 0;
    else if (off)
        m->muted = 1;
}

static void audio_apply(struct ui *ui);

static int mixer_rank(const char *name)
{
    static const char *const preferred[] = {
        "Master", "PCM", "Speaker", "Headphone", "Front", NULL
    };

    for (int i = 0; preferred[i] != NULL; i++) {
        if (strcasecmp(name, preferred[i]) == 0)
            return i;
    }
    return 100;
}

static void audio_init(struct ui *ui)
{
    struct media *m = &ui->media;
    char line[256];
    char fallback[48] = "";
    int best = 100;
    FILE *p;

    m->volume = 50;
    m->muted = 0;
    snprintf(m->mixer, sizeof(m->mixer), "Master");

    p = popen("amixer scontrols 2>/dev/null", "r");
    if (p != NULL) {
        while (fgets(line, sizeof(line), p)) {
            char *a = strchr(line, '\'');
            char *b = a ? strchr(a + 1, '\'') : NULL;
            char name[48];
            size_t n;
            int rank;

            if (a == NULL || b == NULL)
                continue;
            n = (size_t)(b - a - 1);
            if (n == 0 || n >= sizeof(name))
                continue;
            memcpy(name, a + 1, n);
            name[n] = '\0';

            rank = mixer_rank(name);
            if (fallback[0] == '\0')
                snprintf(fallback, sizeof(fallback), "%s", name);
            if (rank < best) {
                best = rank;
                snprintf(m->mixer, sizeof(m->mixer), "%s", name);
            }
        }
        pclose(p);
    }

    if (best == 100 && fallback[0] != '\0')
        snprintf(m->mixer, sizeof(m->mixer), "%s", fallback);

    audio_refresh(ui);

    /*
     * A fresh ALSA state can leave the mixer muted or at 0%, which makes
     * a working player look broken. Start every boot at a comfortable
     * level and unmuted; the volume keys take over from there.
     */
    if (m->volume < 20) {
        m->volume = 60;
        m->muted = 0;
        audio_apply(ui);
    }
}

static void audio_apply(struct ui *ui)
{
    struct media *m = &ui->media;

    run_cmd("amixer -q sset '%s' %d%% %s >/dev/null 2>&1",
            m->mixer, m->volume, m->muted ? "mute" : "unmute");
}

static void audio_adjust(struct ui *ui, int delta)
{
    struct media *m = &ui->media;

    m->volume = iclamp(m->volume + delta, 0, 100);
    if (delta != 0)
        m->muted = 0;
    audio_apply(ui);
}

static uint32_t rd32le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
           | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static double wav_duration(const char *path)
{
    unsigned char hdr[12];
    uint32_t byte_rate = 0;
    FILE *f = fopen(path, "rb");

    if (f == NULL)
        return 0;
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)
        || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fclose(f);
        return 0;
    }

    for (;;) {
        unsigned char ch[8];
        uint32_t size;

        if (fread(ch, 1, sizeof(ch), f) != sizeof(ch))
            break;
        size = rd32le(ch + 4);

        if (memcmp(ch, "fmt ", 4) == 0) {
            unsigned char fmt[16];

            if (fread(fmt, 1, sizeof(fmt), f) != sizeof(fmt))
                break;
            byte_rate = rd32le(fmt + 8);
            if (size > 16)
                fseek(f, (long)(size - 16 + (size & 1)), SEEK_CUR);
            else if (size & 1)
                fseek(f, 1, SEEK_CUR);
        } else if (memcmp(ch, "data", 4) == 0) {
            fclose(f);
            return byte_rate ? (double)size / (double)byte_rate : 0;
        } else {
            fseek(f, (long)(size + (size & 1)), SEEK_CUR);
        }
    }

    fclose(f);
    return 0;
}

static int has_ext(const char *path, const char *ext)
{
    size_t n = strlen(path);
    size_t e = strlen(ext);

    return n >= e && strcasecmp(path + n - e, ext) == 0;
}

static int is_audio_file(const char *path)
{
    return has_ext(path, ".wav") || has_ext(path, ".mp3");
}

/*
 * Media browser.
 *
 * The top level lists the sources: Bluetooth, the music library, every
 * mounted USB drive and the built-in sounds. Opening a source or folder
 * lists its sub-folders first and its audio files after them.
 */
#define LIBRARY_DIR  "/root/Music"
#define BUILTIN_DIR  "/usr/share/owlcaros/media"
#define USB_ROOT     "/run/media"
#define IMPORT_TOOL   "/sbin/owlcaros-music-import"
#define IMPORT_STATUS "/run/owlcaros-import.status"
#define IMPORT_LOCK   "/run/owlcaros-import.lock"

static int skip_name(const char *name)
{
    return name[0] == '.'
        || name[0] == '$'
        || strcasecmp(name, "System Volume Information") == 0
        || strcasecmp(name, "lost+found") == 0
        || strcasecmp(name, "LOST.DIR") == 0;
}

/* Display name: the file name without extension, underscores as spaces. */
static void pretty_name(char *out, size_t n, const char *path, int is_file)
{
    const char *base = strrchr(path, '/');
    char *dot;

    base = base ? base + 1 : path;
    snprintf(out, n, "%s", base);
    for (char *c = out; *c; c++) {
        if (*c == '_')
            *c = ' ';
    }
    if (is_file) {
        dot = strrchr(out, '.');
        if (dot != NULL && dot != out)
            *dot = '\0';
    }
}

static void add_entry(struct media *m, int type, const char *path,
                      const char *name)
{
    struct entry *e;

    if (m->entry_count >= MAX_ENTRIES)
        return;
    e = &m->entries[m->entry_count++];
    e->type = type;
    snprintf(e->path, sizeof(e->path), "%s", path);
    snprintf(e->name, sizeof(e->name), "%s", name);
}

static int dir_has_entries(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int found = 0;

    if (d == NULL)
        return 0;
    while ((e = readdir(d)) != NULL) {
        if (!skip_name(e->d_name)) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

static int entry_cmp(const void *a, const void *b)
{
    const struct entry *ea = a;
    const struct entry *eb = b;

    if (ea->type != eb->type)
        return ea->type - eb->type;
    return strcasecmp(ea->name, eb->name);
}

static void list_top(struct media *m)
{
    DIR *d;
    struct dirent *e;
    int first_usb;
    int usb = 0;

    add_entry(m, ENT_BT, "", "BLUETOOTH AUDIO");
    add_entry(m, ENT_DIR, LIBRARY_DIR, "MUSIC LIBRARY");

    first_usb = m->entry_count;
    d = opendir(USB_ROOT);
    if (d != NULL) {
        while ((e = readdir(d)) != NULL) {
            char path[TRACK_PATH];
            char name[TRACK_NAME];
            struct stat st;

            if (skip_name(e->d_name))
                continue;
            snprintf(path, sizeof(path), "%s/%s", USB_ROOT, e->d_name);
            if (stat(path, &st) < 0 || !S_ISDIR(st.st_mode))
                continue;
            snprintf(name, sizeof(name), "USB DRIVE (%.60s)", e->d_name);
            add_entry(m, ENT_DIR, path, name);
            usb++;
        }
        closedir(d);
    }
    if (usb > 1)
        qsort(&m->entries[first_usb], (size_t)usb, sizeof(m->entries[0]),
              entry_cmp);
    if (usb > 0 && access(IMPORT_TOOL, X_OK) == 0)
        add_entry(m, ENT_IMPORT, "", "COPY USB MUSIC TO LIBRARY");

    if (dir_has_entries(BUILTIN_DIR))
        add_entry(m, ENT_DIR, BUILTIN_DIR, "BUILT-IN SOUNDS");
}

static int list_dir(struct media *m, const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    if (d == NULL)
        return 0;

    while ((e = readdir(d)) != NULL) {
        char path[TRACK_PATH];
        char name[TRACK_NAME];
        struct stat st;
        int n;

        if (skip_name(e->d_name))
            continue;
        n = snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (n < 0 || n >= (int)sizeof(path))
            continue;
        if (stat(path, &st) < 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            pretty_name(name, sizeof(name), path, 0);
            add_entry(m, ENT_DIR, path, name);
        } else if (S_ISREG(st.st_mode) && is_audio_file(path)) {
            pretty_name(name, sizeof(name), path, 1);
            add_entry(m, ENT_FILE, path, name);
        }
    }
    closedir(d);

    qsort(m->entries, (size_t)m->entry_count, sizeof(m->entries[0]),
          entry_cmp);
    return 1;
}

/* Rebuild the list for the current folder. */
static void media_list(struct media *m)
{
    m->entry_count = 0;
    if (m->dir[0] != '\0' && !list_dir(m, m->dir)) {
        /* The folder vanished (USB drive removed): back to the sources. */
        m->dir[0] = '\0';
        m->selected = 0;
    }
    if (m->dir[0] == '\0')
        list_top(m);
    m->selected = iclamp(m->selected, 0, imax(m->entry_count - 1, 0));
}

static void media_select_path(struct media *m, const char *path, int type)
{
    for (int i = 0; i < m->entry_count; i++) {
        if (m->entries[i].type == type
            && strcmp(m->entries[i].path, path) == 0) {
            m->selected = i;
            return;
        }
    }
}

/* Periodic refresh that keeps the cursor on the same item. */
static void media_rescan(struct media *m)
{
    char keep[TRACK_PATH] = "";
    int type = -1;

    if (m->selected >= 0 && m->selected < m->entry_count) {
        snprintf(keep, sizeof(keep), "%s", m->entries[m->selected].path);
        type = m->entries[m->selected].type;
    }
    media_list(m);
    if (type >= 0)
        media_select_path(m, keep, type);
}

static int is_source_root(const char *path)
{
    size_t n = strlen(USB_ROOT);

    if (strcmp(path, LIBRARY_DIR) == 0 || strcmp(path, BUILTIN_DIR) == 0)
        return 1;
    return strncmp(path, USB_ROOT "/", n + 1) == 0
        && strchr(path + n + 1, '/') == NULL;
}

static void media_open(struct media *m, const char *path)
{
    snprintf(m->dir, sizeof(m->dir), "%s", path);
    m->selected = 0;
    media_list(m);
}

/* Go up one folder. Returns 0 when already at the top level. */
static int media_up(struct media *m)
{
    char old[TRACK_PATH];
    char *slash;

    if (m->dir[0] == '\0')
        return 0;

    snprintf(old, sizeof(old), "%s", m->dir);
    slash = strrchr(m->dir, '/');
    if (is_source_root(m->dir) || slash == NULL || slash == m->dir)
        m->dir[0] = '\0';
    else
        *slash = '\0';

    m->selected = 0;
    media_list(m);
    media_select_path(m, old, ENT_DIR);
    return 1;
}

/* "MUSIC LIBRARY / Rock / Live" for the folder being shown. */
static void media_location(const struct media *m, char *out, size_t n)
{
    const char *rest = "";
    char label[TRACK_NAME] = "";
    size_t len = strlen(USB_ROOT);
    size_t used;

    if (m->dir[0] == '\0') {
        snprintf(out, n, "SOURCES");
        return;
    }

    if (strncmp(m->dir, LIBRARY_DIR, strlen(LIBRARY_DIR)) == 0) {
        snprintf(label, sizeof(label), "MUSIC LIBRARY");
        rest = m->dir + strlen(LIBRARY_DIR);
    } else if (strncmp(m->dir, BUILTIN_DIR, strlen(BUILTIN_DIR)) == 0) {
        snprintf(label, sizeof(label), "BUILT-IN SOUNDS");
        rest = m->dir + strlen(BUILTIN_DIR);
    } else if (strncmp(m->dir, USB_ROOT "/", len + 1) == 0) {
        const char *dev = m->dir + len + 1;
        const char *end = strchr(dev, '/');
        int dev_len = end ? (int)(end - dev) : (int)strlen(dev);

        snprintf(label, sizeof(label), "USB DRIVE (%.*s)",
                 dev_len > 60 ? 60 : dev_len, dev);
        rest = end ? end : "";
    } else {
        rest = m->dir;
    }

    snprintf(out, n, "%s", label);
    used = strlen(out);
    for (const char *c = rest; *c && used + 4 < n; c++) {
        if (*c == '/') {
            memcpy(out + used, " / ", 3);
            used += 3;
        } else {
            out[used++] = *c == '_' ? ' ' : *c;
        }
    }
    out[used] = '\0';
}

static void local_stop(struct ui *ui)
{
    struct media *m = &ui->media;

    if (m->pid > 0) {
        kill(m->pid, SIGCONT);
        kill(m->pid, SIGTERM);
        for (int i = 0; i < 10; i++) {
            if (waitpid(m->pid, NULL, WNOHANG) == m->pid) {
                m->pid = -1;
                break;
            }
            usleep(50000);
        }
        if (m->pid > 0) {
            kill(m->pid, SIGKILL);
            waitpid(m->pid, NULL, 0);
            m->pid = -1;
        }
    }

    if (m->source == SRC_LOCAL)
        m->source = SRC_NONE;
    m->paused = 0;
}

static void bt_resume(struct ui *ui)
{
    struct media *m = &ui->media;

    if (m->bt_was_stopped && bt_aplay_available()) {
        bt_aplay(1);
        m->bt_was_stopped = 0;
    }
}

/* Start playlist item `index`. */
static void local_start(struct ui *ui, int index)
{
    struct media *m = &ui->media;
    const char *path;
    pid_t pid;

    if (index < 0 || index >= m->play_count)
        return;
    path = m->playlist[index];

    local_stop(ui);

    /* Local audio owns the sound card: quiet the phone and release the
     * ALSA device held by the Bluetooth player. */
    if (bt_is_playing(m))
        bt_control(m, "Pause");
    if (bt_aplay_running() && bt_aplay_available()) {
        bt_aplay(0);
        m->bt_was_stopped = 1;
        usleep(200000);
    }

    pid = fork();
    if (pid == 0) {
        int fd = open("/dev/null", O_RDONLY);

        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (has_ext(path, ".mp3"))
            execlp("mpg123", "mpg123", "-q", "--", path, (char *)NULL);
        else
            execlp("aplay", "aplay", "-q", path, (char *)NULL);
        _exit(127);
    }

    if (pid < 0) {
        snprintf(ui->status, sizeof(ui->status), "PLAYER ERROR");
        return;
    }

    m->pid = pid;
    m->source = SRC_LOCAL;
    m->play_index = index;
    m->paused = 0;
    m->elapsed = 0;
    m->started = mono_now();
    pretty_name(m->play_name, sizeof(m->play_name), path, 1);
    m->play_duration = has_ext(path, ".wav") ? wav_duration(path) : 0;
    ui->status[0] = '\0';
}

static double local_elapsed(const struct media *m)
{
    if (m->paused)
        return m->elapsed;
    return m->elapsed + (mono_now() - m->started);
}

static int local_active(const struct media *m)
{
    return m->source == SRC_LOCAL && m->pid > 0
        && m->play_index >= 0 && m->play_index < m->play_count;
}

static void local_pause_toggle(struct ui *ui)
{
    struct media *m = &ui->media;

    if (!local_active(m))
        return;
    if (m->paused) {
        kill(m->pid, SIGCONT);
        m->paused = 0;
        m->started = mono_now();
    } else {
        m->elapsed = local_elapsed(m);
        kill(m->pid, SIGSTOP);
        m->paused = 1;
    }
}

/* Is this browser row the file that is playing right now? */
static int entry_is_playing(const struct media *m, const struct entry *e)
{
    return e->type == ENT_FILE && local_active(m)
        && strcmp(m->playlist[m->play_index], e->path) == 0;
}

/* Play a file row. The playlist becomes every audio file in its folder. */
static void play_entry(struct ui *ui, int entry_index)
{
    struct media *m = &ui->media;
    int start = 0;

    if (entry_is_playing(m, &m->entries[entry_index])) {
        local_pause_toggle(ui);
        return;
    }

    m->play_count = 0;
    for (int i = 0; i < m->entry_count && m->play_count < MAX_PLAYLIST; i++) {
        if (m->entries[i].type != ENT_FILE)
            continue;
        if (i == entry_index)
            start = m->play_count;
        snprintf(m->playlist[m->play_count], TRACK_PATH, "%s",
                 m->entries[i].path);
        m->play_count++;
    }
    local_start(ui, start);
}

static void local_skip(struct ui *ui, int dir)
{
    struct media *m = &ui->media;
    int idx;

    if (m->play_count == 0)
        return;

    idx = m->play_index + dir;
    if (idx < 0)
        idx = m->play_count - 1;
    if (idx >= m->play_count)
        idx = 0;
    local_start(ui, idx);
}

static void bt_start(struct ui *ui)
{
    struct media *m = &ui->media;

    local_stop(ui);
    bt_resume(ui);
    m->source = SRC_BT;

    if (!bt_aplay_available()) {
        snprintf(ui->status, sizeof(ui->status), "BLUETOOTH AUDIO SERVICE MISSING");
        return;
    }
    if (!bt_adapter_present()) {
        snprintf(ui->status, sizeof(ui->status), "NO BLUETOOTH ADAPTER");
        return;
    }
    if (!bt_aplay_running())
        bt_aplay(1);
    bt_metadata_refresh(m);
    if (m->bt_player[0] && !bt_is_playing(m))
        bt_control(m, "Play");
    g_next_bt_refresh = 0;
    ui->status[0] = '\0';
}

/*
 * Transport controls act on whatever is playing, not on the row the
 * cursor happens to be on: skip a Bluetooth track on the phone while
 * Bluetooth is the source, a local file otherwise.
 */
static void transport_skip(struct ui *ui, int dir)
{
    struct media *m = &ui->media;

    if (m->source == SRC_BT) {
        if (bt_control(m, dir > 0 ? "Next" : "Previous"))
            ui->status[0] = '\0';
        else
            snprintf(ui->status, sizeof(ui->status),
                     "PHONE OFFERS NO REMOTE CONTROL");
        g_next_bt_refresh = mono_now() + 0.6;
        return;
    }
    local_skip(ui, dir);
}

static void transport_toggle(struct ui *ui)
{
    struct media *m = &ui->media;

    if (local_active(m)) {
        local_pause_toggle(ui);
    } else if (m->source == SRC_BT) {
        if (!bt_control(m, bt_is_playing(m) ? "Pause" : "Play"))
            snprintf(ui->status, sizeof(ui->status),
                     "PHONE OFFERS NO REMOTE CONTROL");
        g_next_bt_refresh = mono_now() + 0.6;
    } else if (m->bt_connected) {
        bt_start(ui);
    } else if (m->play_count > 0) {
        local_start(ui, m->play_index >= 0 ? m->play_index : 0);
    }
}

static void transport_stop(struct ui *ui)
{
    struct media *m = &ui->media;

    if (m->source == SRC_BT) {
        bt_control(m, "Pause");
        g_next_bt_refresh = mono_now() + 0.6;
    } else {
        local_stop(ui);
        bt_resume(ui);
    }
}

static void media_poll(struct ui *ui)
{
    struct media *m = &ui->media;
    int status;
    pid_t r;

    if (m->pid <= 0)
        return;

    r = waitpid(m->pid, &status, WNOHANG);
    if (r != m->pid)
        return;

    (void)status;
    m->pid = -1;
    m->paused = 0;

    if (m->source == SRC_LOCAL) {
        int next = m->play_index + 1;

        m->source = SRC_NONE;
        if (next < m->play_count) {
            local_start(ui, next);
        } else {
            bt_resume(ui);
            snprintf(ui->status, sizeof(ui->status), "END OF FOLDER");
        }
    }
}

/* Copy music from every mounted USB drive into the library. The shell
 * job runs in the background and reports through a status file. */
static void import_start(struct ui *ui)
{
    struct media *m = &ui->media;

    if (access(IMPORT_LOCK, F_OK) == 0)
        return;
    snprintf(m->import_status, sizeof(m->import_status), "STARTING");
    run_cmd("%s >/dev/null 2>&1 &", IMPORT_TOOL);
}

static void import_refresh(struct media *m)
{
    FILE *f = fopen(IMPORT_STATUS, "r");

    if (f == NULL)
        return;
    if (fgets(m->import_status, sizeof(m->import_status), f) != NULL)
        m->import_status[strcspn(m->import_status, "\r\n")] = '\0';
    fclose(f);
}

static int battery_percent(void)
{
    DIR *dir = opendir("/sys/class/power_supply");
    struct dirent *entry;
    int total = 0;
    int count = 0;

    if (dir == NULL)
        return -1;

    while ((entry = readdir(dir)) != NULL) {
        char type_path[160];
        char capacity_path[160];
        char type[32];
        FILE *f;
        int percent;

        if (entry->d_name[0] == '.')
            continue;

        snprintf(type_path, sizeof(type_path),
                 "/sys/class/power_supply/%s/type", entry->d_name);
        f = fopen(type_path, "r");
        if (f == NULL)
            continue;
        if (fgets(type, sizeof(type), f) == NULL) {
            fclose(f);
            continue;
        }
        fclose(f);
        type[strcspn(type, "\r\n")] = '\0';
        if (strcasecmp(type, "Battery") != 0)
            continue;

        snprintf(capacity_path, sizeof(capacity_path),
                 "/sys/class/power_supply/%s/capacity", entry->d_name);
        f = fopen(capacity_path, "r");
        if (f == NULL)
            continue;
        if (fscanf(f, "%d", &percent) == 1
            && percent >= 0 && percent <= 100) {
            total += percent;
            count++;
        }
        fclose(f);
    }

    closedir(dir);
    return count > 0 ? total / count : -1;
}

static void draw_top_bar(struct fb *fb, int w, int h, uint32_t panel,
                         uint32_t red, uint32_t red_bright,
                         const char *right)
{
    int top_h = imax(h / 12, 40);
    int scale = iclamp(w / 420, 2, 3);
    int y = (top_h - font_height(scale)) / 2;
    int margin = imax(w / 64, 12);
    const char *center = "OWLCAROS";

    fill_rect(fb, 0, 0, w, top_h, panel);
    fill_rect(fb, 0, top_h - 1, w, 1, red);

    draw_text_centered(fb, 0, y, w, center, scale, red_bright);
    draw_text(fb, w - margin - text_width(right, scale), y, right, scale, red);
}


static void draw_battery_icon(struct fb *fb, int x, int y, int scale,
                              int percent, uint32_t outline, uint32_t fill)
{
    int body_w = 14 * scale;
    int body_h = 8 * scale;
    int border = scale;
    int inner_w = body_w - 2 * border;
    int inner_h = body_h - 2 * border;

    fill_rect(fb, x, y, body_w, border, outline);
    fill_rect(fb, x, y + body_h - border, body_w, border, outline);
    fill_rect(fb, x, y, border, body_h, outline);
    fill_rect(fb, x + body_w - border, y, border, body_h, outline);
    fill_rect(fb, x + body_w, y + body_h / 3, 2 * scale,
              imax(body_h / 3, scale), outline);

    if (percent >= 0 && inner_w > 0 && inner_h > 0) {
        int fill_w = inner_w * percent / 100;

        if (fill_w > 0)
            fill_rect(fb, x + border, y + border, fill_w, inner_h, fill);
    }
}

static void draw_phone_icon(struct fb *fb, int x, int y, int scale,
                            uint32_t color)
{
    int w = 8 * scale;
    int h = 14 * scale;
    int t = scale;

    fill_rect(fb, x + t, y, w - 2 * t, t, color);
    fill_rect(fb, x, y + t, t, h - 2 * t, color);
    fill_rect(fb, x + w - t, y + t, t, h - 2 * t, color);
    fill_rect(fb, x + t, y + h - t, w - 2 * t, t, color);
    fill_rect(fb, x + 2 * t, y + 2 * t, w - 4 * t, h - 5 * t, color);
    fill_rect(fb, x + 3 * t, y + h - 2 * t, 2 * t, t, color);
}

static void draw_bottom_bar(struct fb *fb, const struct ui *ui, int w, int h,
                            uint32_t panel, uint32_t red, uint32_t red_dim)
{
    const struct media *m = &ui->media;
    const char *left = ui->screen == SCREEN_MEDIA ? "TUNING"
                     : ui->screen == SCREEN_SYSTEM ? "SYSTEM" : "MENU";
    char sound[32];
    char clockbuf[16];
    char batterybuf[8];
    int bottom_h = imax(h / 18, 26);
    int scale = iclamp(w / 360, 1, 2);
    int margin = imax(w / 64, 12);
    int text_h = font_height(scale);
    int sy = h - bottom_h + (bottom_h - text_h) / 2;
    int phone_w = 8 * scale;
    int phone_h = 14 * scale;
    time_t now = time(NULL);
    struct tm tm_now;
    int percent = battery_percent();
    int clock_w;
    int battery_w;
    int icon_w = 16 * scale;
    int group_w;
    int group_x;
    int icon_y;

    if (m->muted)
        snprintf(sound, sizeof(sound), "SOUND MUTE");
    else
        snprintf(sound, sizeof(sound), "SOUND %d", m->volume);

    localtime_r(&now, &tm_now);
    strftime(clockbuf, sizeof(clockbuf), "%H:%M", &tm_now);
    if (percent >= 0)
        snprintf(batterybuf, sizeof(batterybuf), "%d%%", percent);
    else
        snprintf(batterybuf, sizeof(batterybuf), "--%%");

    fill_rect(fb, 0, h - bottom_h, w, bottom_h, panel);
    fill_rect(fb, 0, h - bottom_h, w, 1, red);

    clock_w = text_width(clockbuf, scale);
    battery_w = text_width(batterybuf, scale);
    group_w = clock_w + 5 * scale + icon_w + 3 * scale + battery_w
              + 5 * scale + phone_w;
    group_x = (w - group_w) / 2;
    icon_y = h - bottom_h + (bottom_h - 8 * scale) / 2;

    draw_text(fb, margin, sy, left, scale, red_dim);
    draw_text(fb, group_x, sy, clockbuf, scale, red);
    draw_battery_icon(fb, group_x + clock_w + 5 * scale, icon_y, scale,
                      percent, red_dim, red);
    draw_text(fb, group_x + clock_w + 5 * scale + icon_w + 3 * scale,
              sy, batterybuf, scale, red);
    draw_phone_icon(fb, group_x + clock_w + 5 * scale + icon_w + 3 * scale
                    + battery_w + 5 * scale,
                    h - bottom_h + (bottom_h - phone_h) / 2, scale, red);
    draw_text(fb, w - margin - text_width(sound, scale), sy, sound, scale,
              red_dim);
}


static void draw_line(struct fb *fb, int x0, int y0, int x1, int y1,
                      uint32_t color)
{
    set_draw_color(fb, color);
    SDL_RenderDrawLine(fb->renderer, x0, y0, x1, y1);
}

static void draw_quadratic(struct fb *fb, int x0, int y0, int cx, int cy,
                           int x1, int y1, uint32_t color)
{
    const int steps = 48;
    int px = x0;
    int py = y0;
    long denom = (long)steps * steps;

    for (int i = 1; i <= steps; i++) {
        long mt = steps - i;
        long x = mt * mt * x0 + 2L * mt * i * cx + (long)i * i * x1;
        long y = mt * mt * y0 + 2L * mt * i * cy + (long)i * i * y1;
        int nx = (int)(x / denom);
        int ny = (int)(y / denom);

        draw_line(fb, px, py, nx, ny, color);
        px = nx;
        py = ny;
    }
}

static void draw_bracket(struct fb *fb, int base_x, int apex_x, int top_y,
                         int anchor_y, int bottom_y, uint32_t color)
{
    draw_quadratic(fb, base_x, top_y, apex_x, (top_y + anchor_y) / 2,
                   apex_x, anchor_y, color);
    draw_quadratic(fb, apex_x, anchor_y, apex_x,
                   (anchor_y + bottom_y) / 2, base_x, bottom_y, color);
}

static void draw_filled_circle(struct fb *fb, int cx, int cy, int radius,
                               uint32_t color)
{
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = radius;
        while (dx * dx + dy * dy > radius * radius)
            dx--;
        fill_rect(fb, cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

static void draw_radio_frame(struct fb *fb, int x, int y, int w, int h,
                             int anchor_y, int list_x, int list_w,
                             uint32_t bright, uint32_t dim)
{
    int top = y + 18;
    int bottom = y + h - 18;
    int left_base = x + w / 12;
    int left_apex = x + w / 7 - 4;
    int right_base = x + (11 * w) / 12;
    int right_apex = x + (6 * w) / 7 + 4;
    int dot_x = list_x - 8;

    if (anchor_y < top + 8)
        anchor_y = top + 8;
    if (anchor_y > bottom - 8)
        anchor_y = bottom - 8;

    fill_rect(fb, x, y, w, 1, bright);
    fill_rect(fb, x + 18, y + 5, w - 36, 1, dim);
    fill_rect(fb, x, y + h - 1, w, 1, bright);
    fill_rect(fb, x + 18, y + h - 6, w - 36, 1, dim);

    draw_bracket(fb, left_base, left_apex, top, anchor_y, bottom, bright);
    draw_bracket(fb, left_base + 13, left_apex + 13,
                 top, anchor_y, bottom, dim);
    draw_bracket(fb, right_base, right_apex, top, anchor_y, bottom, bright);
    draw_bracket(fb, right_base - 13, right_apex - 13,
                 top, anchor_y, bottom, dim);

    draw_line(fb, x, top, left_base, top, dim);
    draw_line(fb, right_base, top, x + w, top, dim);
    draw_line(fb, x, bottom, left_base, bottom, dim);
    draw_line(fb, right_base, bottom, x + w, bottom, dim);

    draw_line(fb, x, anchor_y, dot_x - 6, anchor_y, dim);
    draw_line(fb, dot_x + 6, anchor_y, list_x, anchor_y, bright);
    draw_line(fb, list_x + list_w, anchor_y,
              x + w, anchor_y, dim);
    draw_filled_circle(fb, dot_x, anchor_y, 6, bright);
}

/*
 * Shared look for every screen: the main-menu palette, the lens frame and
 * a list whose selected row is a solid bar. Screens only decide what goes
 * above and inside the list.
 */
#define C_BG       0xff100303u
#define C_PANEL    0xff180505u
#define C_RED      0xffF05B57u
#define C_BRIGHT   0xffFF514Cu
#define C_DIM      0xffA94340u
#define C_FAINT    0xff4A1513u
#define C_SELTEXT  0xff260706u

struct layout {
    int w, h;
    int top_h, bottom_h;
    int fx, fy, fw, fh;      /* lens frame */
    int scale;               /* body text size */
    int row_h;
    int list_x, list_w;      /* list column inside the lens */
};

static void layout_init(const struct fb *fb, struct layout *L)
{
    int margin;

    L->w = (int)fb->xres;
    L->h = (int)fb->yres;
    L->top_h = imax(L->h / 12, 40);
    L->bottom_h = imax(L->h / 18, 26);
    margin = imax(L->w / 64, 12);
    L->fx = margin;
    L->fy = L->top_h + 8;
    L->fw = imax(L->w - 2 * margin, 1);
    L->fh = imax(L->h - L->top_h - L->bottom_h - 18, 1);
    L->scale = iclamp(L->w / 560, 2, 3);
    L->row_h = font_height(L->scale) + 9;
    L->list_w = L->fw * 72 / 100;
    L->list_x = (L->w - L->list_w) / 2;
}

static void screen_begin(struct ui *ui, const struct layout *L,
                         const char *name, int anchor_y)
{
    struct fb *fb = &ui->fb;

    fill_rect(fb, 0, 0, L->w, L->h, C_BG);
    draw_top_bar(fb, L->w, L->h, C_PANEL, C_RED, C_BRIGHT, name);
    draw_radio_frame(fb, L->fx, L->fy, L->fw, L->fh, anchor_y,
                     L->list_x, L->list_w, C_BRIGHT, C_DIM);
}

static void screen_end(struct ui *ui, const struct layout *L)
{
    draw_bottom_bar(&ui->fb, ui, L->w, L->h, C_PANEL, C_BRIGHT, C_DIM);
}

/* One list row; `tag` is optional small text at the right edge. */
static void draw_row(struct fb *fb, const struct layout *L, int y,
                     const char *text, const char *tag, int selected)
{
    int text_y = y + (L->row_h - font_height(L->scale)) / 2;
    int tag_w = 0;

    if (selected)
        fill_rect(fb, L->list_x, y + 1, L->list_w, L->row_h - 2, C_BRIGHT);
    if (tag != NULL && tag[0] != '\0') {
        tag_w = text_width(tag, 1) + 18;
        draw_text(fb, L->list_x + L->list_w - 14 - text_width(tag, 1),
                  y + (L->row_h - font_height(1)) / 2, tag, 1,
                  selected ? C_SELTEXT : C_DIM);
    }
    draw_text_ellipsis(fb, L->list_x + 14, text_y, L->list_w - 28 - tag_w,
                       text, L->scale, selected ? C_SELTEXT : C_RED);
}

static void draw_hint(struct fb *fb, const struct layout *L, const char *text)
{
    draw_text_centered(fb, L->fx, L->fy + L->fh - 14 - font_height(1),
                       L->fw, text, 1, C_DIM);
}

static void draw_home(struct ui *ui)
{
    static const char *const labels[4] = {
        "MEDIA - Bluetooth and Local Audio",
        "NAVIGATION - Maps and Routes",
        "PHONE - Hands-Free",
        "SETTINGS - System"
    };
    struct fb *fb = &ui->fb;
    struct layout L;
    int list_y;

    layout_init(fb, &L);
    list_y = L.fy + L.fh / 2 - (L.row_h * 4) / 2;

    screen_begin(ui, &L, "MENU",
                 list_y + ui->selected * L.row_h + L.row_h / 2);
    for (int i = 0; i < 4; i++)
        draw_row(fb, &L, list_y + i * L.row_h, labels[i], NULL,
                 i == ui->selected);
    if (ui->status[0] != '\0' && strcmp(ui->status, "READY") != 0)
        draw_hint(fb, &L, ui->status);
    screen_end(ui, &L);
}
static void fmt_time(char *buf, size_t n, double seconds)
{
    int s = (int)seconds;

    if (s < 0)
        s = 0;
    snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

static void upper_copy(char *out, size_t n, const char *in)
{
    size_t i = 0;

    for (; in[i] != '\0' && i + 1 < n; i++) {
        unsigned char c = (unsigned char)in[i];

        out[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : (char)c;
    }
    out[i] = '\0';
}

static void draw_media(struct ui *ui)
{
    struct fb *fb = &ui->fb;
    struct media *m = &ui->media;
    struct layout L;
    char source[TRACK_NAME + 16] = "NO SOURCE";
    char state[32] = "";
    char title[BT_TEXT] = "";
    char sub[2 * BT_TEXT + 8] = "";
    char location[TRACK_PATH];
    char count[32];
    char tag[64];
    int active = 0;
    double elapsed = 0;
    double duration = 0;
    int h1, h2, h3;
    int x, iw, y;
    int title_y, sub_y, bar_y, head_y, list_y, rows, top;

    layout_init(fb, &L);
    h1 = font_height(1);
    h2 = font_height(L.scale);
    h3 = font_height(3);
    x = L.list_x + 14;
    iw = L.list_w - 28;

    /* What is playing. */
    if (local_active(m)) {
        snprintf(source, sizeof(source), "LOCAL MUSIC");
        snprintf(title, sizeof(title), "%s", m->play_name);
        snprintf(sub, sizeof(sub), "Track %d of %d", m->play_index + 1,
                 m->play_count);
        snprintf(state, sizeof(state), "%s", m->paused ? "PAUSED" : "PLAYING");
        elapsed = local_elapsed(m);
        duration = m->play_duration;
        active = !m->paused;
    } else if (m->source == SRC_BT) {
        if (m->bt_device[0])
            snprintf(source, sizeof(source), "BLUETOOTH - %s", m->bt_device);
        else
            snprintf(source, sizeof(source), "BLUETOOTH");

        if (m->bt_title[0]) {
            snprintf(title, sizeof(title), "%s", m->bt_title);
            if (m->bt_artist[0] && m->bt_album[0])
                snprintf(sub, sizeof(sub), "%s - %s", m->bt_artist,
                         m->bt_album);
            else
                snprintf(sub, sizeof(sub), "%s",
                         m->bt_artist[0] ? m->bt_artist : m->bt_album);
        } else if (m->bt_connected) {
            snprintf(title, sizeof(title), "Bluetooth Audio");
            snprintf(sub, sizeof(sub), "Play something on your phone");
        } else {
            snprintf(title, sizeof(title), "Waiting for phone");
            snprintf(sub, sizeof(sub), "Pair with OwlCarOS in Bluetooth settings");
        }

        if (m->bt_status[0])
            upper_copy(state, sizeof(state), m->bt_status);
        else
            snprintf(state, sizeof(state), "%s",
                     alsa_playback_running() ? "STREAMING"
                     : m->bt_connected ? "CONNECTED" : "NOT CONNECTED");
        active = bt_is_playing(m) || alsa_playback_running();
    } else {
        snprintf(title, sizeof(title), "Nothing playing");
        snprintf(sub, sizeof(sub), "Choose Bluetooth or a folder below");
    }
    if (ui->status[0] != '\0' && strcmp(ui->status, "READY") != 0
        && strcmp(ui->status, "MEDIA") != 0)
        snprintf(sub, sizeof(sub), "%s", ui->status);

    /* Vertical layout: now-playing block on top, the list below it. */
    y = L.fy + 26;
    title_y = y + h1 + 6;
    sub_y = title_y + h3 + 2;
    bar_y = sub_y + h2 + 10;
    head_y = bar_y + 6 + h1 + 14;
    list_y = head_y + h1 + 8;
    rows = (L.fy + L.fh - 22 - h1 - list_y) / L.row_h;
    if (rows < 1)
        rows = 1;

    top = m->selected - rows / 2;
    if (top > m->entry_count - rows)
        top = m->entry_count - rows;
    if (top < 0)
        top = 0;

    screen_begin(ui, &L, "MEDIA",
                 list_y + (m->selected - top) * L.row_h + L.row_h / 2);

    draw_text_ellipsis(fb, x, y, iw - text_width(state, 1) - 16, source, 1,
                       C_DIM);
    draw_text(fb, x + iw - text_width(state, 1), y, state, 1,
              active ? C_BRIGHT : C_DIM);
    draw_text_ellipsis(fb, x, title_y, iw, title, 3, C_BRIGHT);
    draw_text_ellipsis(fb, x, sub_y, iw, sub, L.scale, C_RED);

    fill_rect(fb, x, bar_y, iw, 4, C_FAINT);
    if (duration > 0) {
        char timebuf[16];
        char durbuf[16];
        int frac = iclamp((int)(elapsed * 1000.0 / duration), 0, 1000);

        fill_rect(fb, x, bar_y, iw * frac / 1000, 4, C_BRIGHT);
        fmt_time(timebuf, sizeof(timebuf), elapsed);
        fmt_time(durbuf, sizeof(durbuf), duration);
        draw_text(fb, x, bar_y + 8, timebuf, 1, C_DIM);
        draw_text(fb, x + iw - text_width(durbuf, 1), bar_y + 8, durbuf, 1,
                  C_DIM);
    } else if (local_active(m)) {
        char timebuf[16];

        fmt_time(timebuf, sizeof(timebuf), elapsed);
        draw_text(fb, x, bar_y + 8, timebuf, 1, C_DIM);
    }

    /* List header: where we are and how far down. */
    media_location(m, location, sizeof(location));
    if (m->entry_count > 0)
        snprintf(count, sizeof(count), "%d / %d", m->selected + 1,
                 m->entry_count);
    else
        snprintf(count, sizeof(count), "EMPTY");
    draw_text_ellipsis(fb, x, head_y, iw - text_width(count, 1) - 16,
                       location, 1, C_DIM);
    draw_text(fb, x + iw - text_width(count, 1), head_y, count, 1, C_DIM);

    for (int i = 0; i < rows && top + i < m->entry_count; i++) {
        const struct entry *e = &m->entries[top + i];

        tag[0] = '\0';
        switch (e->type) {
        case ENT_BT:
            if (m->source == SRC_BT)
                snprintf(tag, sizeof(tag), "%s",
                         bt_is_playing(m) ? "PLAYING" : "SELECTED");
            else if (m->bt_connected)
                snprintf(tag, sizeof(tag), "PHONE CONNECTED");
            break;
        case ENT_DIR:
            snprintf(tag, sizeof(tag), "FOLDER");
            break;
        case ENT_FILE:
            if (entry_is_playing(m, e))
                snprintf(tag, sizeof(tag), "%s",
                         m->paused ? "PAUSED" : "PLAYING");
            break;
        case ENT_IMPORT:
            snprintf(tag, sizeof(tag), "%s", m->import_status);
            break;
        default:
            break;
        }
        draw_row(fb, &L, list_y + i * L.row_h, e->name, tag,
                 top + i == m->selected);
    }

    if (m->entry_count == 0) {
        draw_text(fb, x, list_y + 4,
                  strncmp(m->dir, LIBRARY_DIR, strlen(LIBRARY_DIR)) == 0
                  ? "No music here yet. Plug in a USB drive and copy it over."
                  : "No folders or music files here.",
                  L.scale, C_DIM);
    }

    draw_hint(fb, &L, "ENTER OPEN / PLAY     SPACE PAUSE     LEFT / RIGHT SKIP"
                      "     BACKSPACE UP");
    screen_end(ui, &L);
}

static void system_info_refresh(struct sys_info *info)
{
    static unsigned long long previous_total;
    static unsigned long long previous_idle;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    unsigned long long total, idle_total;
    FILE *f;

    f = fopen("/proc/stat", "r");
    if (f != NULL) {
        if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle, &iowait, &irq,
                   &softirq, &steal) >= 4) {
            total = user + nice + system + idle + iowait + irq + softirq + steal;
            idle_total = idle + iowait;
            if (previous_total && total > previous_total) {
                unsigned long long dt = total - previous_total;
                unsigned long long di = idle_total - previous_idle;
                info->cpu_percent = (int)((dt - di) * 100 / dt);
            }
            previous_total = total;
            previous_idle = idle_total;
        }
        fclose(f);
    }

    info->mem_total_kb = 0;
    info->mem_used_kb = 0;
    f = fopen("/proc/meminfo", "r");
    if (f != NULL) {
        char line[160];
        unsigned long available = 0, free_kb = 0, buffers = 0, cached = 0;
        while (fgets(line, sizeof(line), f) != NULL) {
            if (sscanf(line, "MemTotal: %lu kB", &info->mem_total_kb) == 1)
                continue;
            if (sscanf(line, "MemAvailable: %lu kB", &available) == 1)
                continue;
            if (sscanf(line, "MemFree: %lu kB", &free_kb) == 1)
                continue;
            if (sscanf(line, "Buffers: %lu kB", &buffers) == 1)
                continue;
            if (sscanf(line, "Cached: %lu kB", &cached) == 1)
                continue;
        }
        fclose(f);
        if (info->mem_total_kb) {
            unsigned long unused = available ? available : free_kb + buffers + cached;
            info->mem_used_kb = unused < info->mem_total_kb
                              ? info->mem_total_kb - unused : 0;
            info->mem_percent = (int)(info->mem_used_kb * 100 /
                                      info->mem_total_kb);
        }
    }

    {
        double uptime = 0;
        f = fopen("/proc/uptime", "r");
        if (f != NULL) {
            (void)fscanf(f, "%lf", &uptime);
            fclose(f);
        }
        info->uptime_seconds = (unsigned long)uptime;
    }

    snprintf(info->load, sizeof(info->load), "N/A");
    f = fopen("/proc/loadavg", "r");
    if (f != NULL) {
        (void)fscanf(f, "%31s", info->load);
        fclose(f);
    }

    info->process_count = 0;
    info->top_count = 0;
    {
        DIR *proc = opendir("/proc");
        struct dirent *entry;
        if (proc != NULL) {
            while ((entry = readdir(proc)) != NULL) {
                char path[64], name[32] = "?";
                int pid;
                unsigned long rss = 0;
                FILE *status;
                if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
                    continue;
                pid = atoi(entry->d_name);
                if (pid <= 0)
                    continue;
                info->process_count++;
                snprintf(path, sizeof(path), "/proc/%d/status", pid);
                status = fopen(path, "r");
                if (status == NULL)
                    continue;
                {
                    char line[160];
                    while (fgets(line, sizeof(line), status) != NULL) {
                        if (strncmp(line, "Name:", 5) == 0)
                            (void)sscanf(line + 5, "%31s", name);
                        else if (sscanf(line, "VmRSS: %lu kB", &rss) == 1)
                            break;
                    }
                }
                fclose(status);
                if (!rss)
                    continue;

                int pos = info->top_count;
                if (pos > 4)
                    pos = 4;
                if (info->top_count == 5 && rss <= info->top[4].rss_kb)
                    continue;
                if (info->top_count < 5)
                    info->top_count++;
                while (pos > 0 && rss > info->top[pos - 1].rss_kb) {
                    if (pos < 5)
                        info->top[pos] = info->top[pos - 1];
                    pos--;
                }
                info->top[pos].pid = pid;
                info->top[pos].rss_kb = rss;
                snprintf(info->top[pos].name, sizeof(info->top[pos].name),
                         "%s", name);
            }
            closedir(proc);
        }
    }
}

static void draw_meter(struct fb *fb, int x, int y, int w, int percent)
{
    fill_rect(fb, x, y, w, 10, C_FAINT);
    fill_rect(fb, x, y, w * iclamp(percent, 0, 100) / 100, 10, C_BRIGHT);
}

/*
 * Installer front end. The work is done by /sbin/owlcaros-install; the UI
 * lists the drives it offers, starts it and shows its one-line status.
 */
#define INSTALL_TOOL   "/sbin/owlcaros-install"
#define INSTALL_STATUS "/run/owlcaros-install.status"

static void install_scan(struct install *in)
{
    FILE *p = popen(INSTALL_TOOL " list 2>/dev/null", "r");
    char line[256];

    in->count = 0;
    in->selected = 0;
    if (p == NULL)
        return;

    /* Each line: /dev/name|size in GB|model|bus */
    while (in->count < MAX_DISKS && fgets(line, sizeof(line), p) != NULL) {
        struct install_disk *d = &in->disks[in->count];
        char *dev = line;
        char *gb;
        char *model;
        char *bus;

        line[strcspn(line, "\r\n")] = '\0';
        gb = strchr(dev, '|');
        if (gb == NULL)
            continue;
        *gb++ = '\0';
        model = strchr(gb, '|');
        if (model == NULL)
            continue;
        *model++ = '\0';
        bus = strchr(model, '|');
        if (bus != NULL)
            *bus++ = '\0';

        snprintf(d->dev, sizeof(d->dev), "%s", dev);
        snprintf(d->label, sizeof(d->label), "%.60s - %.8s GB%s (%.20s)",
                 model, gb,
                 bus != NULL && strcmp(bus, "usb") == 0 ? " USB" : "", dev);
        in->count++;
    }
    pclose(p);
}

static void install_refresh(struct install *in)
{
    FILE *f = fopen(INSTALL_STATUS, "r");

    if (f == NULL)
        return;
    if (fgets(in->status, sizeof(in->status), f) != NULL)
        in->status[strcspn(in->status, "\r\n")] = '\0';
    fclose(f);
}

static void install_begin(struct install *in)
{
    unlink(INSTALL_STATUS);
    snprintf(in->status, sizeof(in->status), "STARTING");
    run_cmd("%s run '%s' >/dev/null 2>&1 &", INSTALL_TOOL,
            in->disks[in->selected].dev);
}

static int install_done(const struct install *in)
{
    return strncmp(in->status, "DONE", 4) == 0;
}

static int install_failed(const struct install *in)
{
    return strncmp(in->status, "FAILED", 6) == 0;
}

/* "COPYING SYSTEM 42%" -> 42, or -1 when the status carries no number. */
static int install_percent(const struct install *in)
{
    const char *pct = strchr(in->status, '%');
    const char *digits = pct;

    if (pct == NULL)
        return -1;
    while (digits > in->status && digits[-1] >= '0' && digits[-1] <= '9')
        digits--;
    return digits == pct ? -1 : atoi(digits);
}

/* Heading above a centred list, with an optional line under it. */
static void draw_heading(struct fb *fb, const struct layout *L, int list_y,
                         const char *heading, const char *sub)
{
    int x = L->list_x + 14;
    int iw = L->list_w - 28;
    int h2 = font_height(L->scale);
    int y = list_y - 18 - font_height(3);

    if (sub != NULL && sub[0] != '\0') {
        y -= h2 + 4;
        draw_text_ellipsis(fb, x, y + font_height(3) + 4, iw, sub, L->scale,
                           C_RED);
    }
    draw_text_ellipsis(fb, x, y, iw, heading, 3, C_BRIGHT);
}

static void draw_system(struct ui *ui)
{
    struct fb *fb = &ui->fb;
    struct install *in = &ui->install;
    struct layout L;
    char text[160];
    int h2, h3;

    layout_init(fb, &L);
    h2 = font_height(L.scale);
    h3 = font_height(3);

    if (ui->system_page == SYS_MENU) {
        static const char *const labels[4] = {
            "REBOOT - Restart OwlCarOS",
            "SHUTDOWN - Power Off",
            "SYSTEM INFO - CPU, Memory and Processes",
            "INSTALL - Copy OwlCarOS to an Internal Drive"
        };
        int list_y = L.fy + L.fh / 2 - (L.row_h * 4) / 2;

        screen_begin(ui, &L, "SETTINGS",
                     list_y + ui->system_selected * L.row_h + L.row_h / 2);
        for (int i = 0; i < 4; i++)
            draw_row(fb, &L, list_y + i * L.row_h, labels[i], NULL,
                     i == ui->system_selected);
        draw_hint(fb, &L, "UP / DOWN SELECT     ENTER OPEN     ESC BACK");
    } else if (ui->system_page == SYS_CONFIRM) {
        const char *labels[2];
        int list_y = L.fy + L.fh / 2 - L.row_h;

        labels[0] = "CANCEL - Go Back";
        labels[1] = ui->system_action == 1 ? "REBOOT NOW" : "SHUT DOWN NOW";

        screen_begin(ui, &L, "SETTINGS",
                     list_y + ui->confirm_selected * L.row_h + L.row_h / 2);
        draw_heading(fb, &L, list_y,
                     ui->system_action == 1 ? "Reboot OwlCarOS?"
                                            : "Shut down OwlCarOS?", NULL);
        for (int i = 0; i < 2; i++)
            draw_row(fb, &L, list_y + i * L.row_h, labels[i], NULL,
                     i == ui->confirm_selected);
        draw_hint(fb, &L, "UP / DOWN SELECT     ENTER CONFIRM     ESC CANCEL");
    } else if (ui->system_page == SYS_INSTALL_PICK) {
        int rows = imax(in->count, 1);
        int list_y = L.fy + L.fh / 2 - (L.row_h * rows) / 2 + L.row_h;

        screen_begin(ui, &L, "INSTALL",
                     list_y + in->selected * L.row_h + L.row_h / 2);
        if (in->count > 0) {
            draw_heading(fb, &L, list_y, "Install to which drive?",
                         "Everything on the drive you choose will be erased.");
            for (int i = 0; i < in->count; i++)
                draw_row(fb, &L, list_y + i * L.row_h, in->disks[i].label,
                         NULL, i == in->selected);
            draw_hint(fb, &L, "UP / DOWN SELECT     ENTER CHOOSE     ESC BACK");
        } else {
            draw_heading(fb, &L, list_y, "No drive to install to",
                         "No other drive of 4 GB or more was found.");
            draw_hint(fb, &L, "ESC BACK");
        }
    } else if (ui->system_page == SYS_INSTALL_CONFIRM) {
        static const char *const labels[2] = {
            "CANCEL - Keep the Drive As It Is",
            "ERASE THE DRIVE AND INSTALL"
        };
        int list_y = L.fy + L.fh / 2;

        screen_begin(ui, &L, "INSTALL",
                     list_y + ui->confirm_selected * L.row_h + L.row_h / 2);
        draw_heading(fb, &L, list_y, "Erase this drive?",
                     in->disks[in->selected].label);
        for (int i = 0; i < 2; i++)
            draw_row(fb, &L, list_y + i * L.row_h, labels[i], NULL,
                     i == ui->confirm_selected);
        draw_hint(fb, &L, "ALL FILES ON THIS DRIVE WILL BE LOST     ESC CANCEL");
    } else if (ui->system_page == SYS_INSTALL_RUN) {
        int x = L.list_x + 14;
        int iw = L.list_w - 28;
        int list_y = L.fy + L.fh / 2;

        if (install_done(in)) {
            static const char *const labels[2] = {
                "SHUT DOWN NOW",
                "BACK TO SETTINGS"
            };

            screen_begin(ui, &L, "INSTALL",
                         list_y + ui->confirm_selected * L.row_h
                         + L.row_h / 2);
            draw_heading(fb, &L, list_y, "Installation complete",
                         "Shut down, remove the USB stick, then power on.");
            for (int i = 0; i < 2; i++)
                draw_row(fb, &L, list_y + i * L.row_h, labels[i], NULL,
                         i == ui->confirm_selected);
            draw_hint(fb, &L, "UP / DOWN SELECT     ENTER CONFIRM");
        } else if (install_failed(in)) {
            screen_begin(ui, &L, "INSTALL", list_y);
            draw_heading(fb, &L, list_y, "Installation failed",
                         in->status + (strncmp(in->status, "FAILED - ", 9)
                                       == 0 ? 9 : 0));
            draw_text_ellipsis(fb, x, list_y + 6, iw,
                               "Details are in /var/log/owlcaros-install.log",
                               L.scale, C_DIM);
            draw_hint(fb, &L, "ESC BACK");
        } else {
            int percent = install_percent(in);

            screen_begin(ui, &L, "INSTALL", list_y);
            draw_heading(fb, &L, list_y, "Installing OwlCarOS",
                         in->disks[in->selected].label);
            draw_text_ellipsis(fb, x, list_y + 6, iw, in->status, L.scale,
                               C_BRIGHT);
            if (percent >= 0)
                draw_meter(fb, x, list_y + 6 + h2 + 10, iw, percent);
            draw_hint(fb, &L,
                      "DO NOT POWER OFF OR REMOVE THE USB STICK");
        }
    } else {
        const struct sys_info *si = &ui->system;
        int line_h = h2 + 8;
        int x = L.list_x + 14;
        int iw = L.list_w - 28;
        int label_w = text_width("MEMORY  ", L.scale);
        int value_w = text_width("100%  ", L.scale);
        int bar_x = x + label_w + value_w;
        int bar_w = imax(iw - label_w - value_w, 1);
        int block_h = h3 + 14 + 4 * line_h + 12 + 6 * line_h;
        int y = L.fy + L.fh / 2 - block_h / 2;
        unsigned long days = si->uptime_seconds / 86400;
        unsigned long hours = (si->uptime_seconds / 3600) % 24;
        unsigned long minutes = (si->uptime_seconds / 60) % 60;

        screen_begin(ui, &L, "SETTINGS", y + h3 + 14 + line_h / 2);

        draw_text(fb, x, y, "System Info", 3, C_BRIGHT);
        y += h3 + 14;

        draw_text(fb, x, y, "CPU", L.scale, C_RED);
        snprintf(text, sizeof(text), "%d%%", si->cpu_percent);
        draw_text(fb, x + label_w, y, text, L.scale, C_BRIGHT);
        draw_meter(fb, bar_x, y + (h2 - 10) / 2, bar_w, si->cpu_percent);
        y += line_h;

        draw_text(fb, x, y, "MEMORY", L.scale, C_RED);
        snprintf(text, sizeof(text), "%d%%", si->mem_percent);
        draw_text(fb, x + label_w, y, text, L.scale, C_BRIGHT);
        draw_meter(fb, bar_x, y + (h2 - 10) / 2, bar_w, si->mem_percent);
        y += line_h;

        if (si->mem_total_kb)
            snprintf(text, sizeof(text), "%lu of %lu MB used     %d tasks",
                     si->mem_used_kb / 1024, si->mem_total_kb / 1024,
                     si->process_count);
        else
            snprintf(text, sizeof(text), "Memory unavailable     %d tasks",
                     si->process_count);
        draw_text_ellipsis(fb, x, y, iw, text, L.scale, C_DIM);
        y += line_h;

        snprintf(text, sizeof(text), "Load %s     Up %lu d %02lu:%02lu",
                 si->load, days, hours, minutes);
        draw_text_ellipsis(fb, x, y, iw, text, L.scale, C_DIM);
        y += line_h;

        fill_rect(fb, x, y + 2, iw, 1, C_DIM);
        y += 12;
        draw_text(fb, x, y, "PID", L.scale, C_RED);
        draw_text(fb, x + iw / 6, y, "PROCESS", L.scale, C_RED);
        draw_text(fb, x + iw - text_width("MEMORY", L.scale), y, "MEMORY",
                  L.scale, C_RED);
        y += line_h;
        for (int i = 0; i < 5; i++) {
            uint32_t color = i == 0 ? C_BRIGHT : C_DIM;

            if (i >= si->top_count)
                break;
            snprintf(text, sizeof(text), "%d", si->top[i].pid);
            draw_text(fb, x, y, text, L.scale, color);
            draw_text_ellipsis(fb, x + iw / 6, y, iw / 2, si->top[i].name,
                               L.scale, color);
            snprintf(text, sizeof(text), "%lu MB", si->top[i].rss_kb / 1024);
            draw_text(fb, x + iw - text_width(text, L.scale), y, text,
                      L.scale, color);
            y += line_h;
        }
        draw_hint(fb, &L, "LIVE CPU, MEMORY AND PROCESSES     ESC BACK");
    }

    screen_end(ui, &L);
}

static void draw_scene(struct ui *ui)
{
    SDL_SetRenderDrawColor(ui->fb.renderer, 0, 0, 0, 255);
    SDL_RenderClear(ui->fb.renderer);
    if (ui->screen == SCREEN_MEDIA)
        draw_media(ui);
    else if (ui->screen == SCREEN_SYSTEM)
        draw_system(ui);
    else
        draw_home(ui);
    fb_present(&ui->fb);
}

static int handle_global_key(struct ui *ui, int code)
{
    struct media *m = &ui->media;

    switch (code) {
    case KEY_VOLUMEUP:
        audio_adjust(ui, 5);
        return 1;
    case KEY_VOLUMEDOWN:
        audio_adjust(ui, -5);
        return 1;
    case KEY_MUTE:
        m->muted = !m->muted;
        audio_apply(ui);
        return 1;
    /* Media keys work on every screen and follow the active source. */
    case KEY_NEXTSONG:
        transport_skip(ui, 1);
        return 1;
    case KEY_PREVIOUSSONG:
        transport_skip(ui, -1);
        return 1;
    case KEY_PLAYPAUSE:
    case KEY_PLAYCD:
    case KEY_PAUSECD:
    case KEY_PLAY:
    case KEY_PAUSE:
        transport_toggle(ui);
        return 1;
    case KEY_STOPCD:
    case KEY_STOP:
        transport_stop(ui);
        return 1;
    default:
        return 0;
    }
}

static void handle_home_key(struct ui *ui, int code, int *dirty)
{
    static const char *const names[4] = {
        "MEDIA", "NAVIGATION", "PHONE", "SETTINGS"
    };

    switch (code) {
    case KEY_LEFT:
    case KEY_UP:
        ui->selected = ui->selected > 0 ? ui->selected - 1 : 3;
        ui->status[0] = '\0';
        *dirty = 1;
        break;
    case KEY_RIGHT:
    case KEY_DOWN:
        ui->selected = ui->selected < 3 ? ui->selected + 1 : 0;
        ui->status[0] = '\0';
        *dirty = 1;
        break;
    case KEY_ENTER:
    case KEY_KPENTER:
        ui->status[0] = '\0';
        if (ui->selected == 0) {
            ui->screen = SCREEN_MEDIA;
            media_rescan(&ui->media);
        } else if (ui->selected == 3) {
            ui->screen = SCREEN_SYSTEM;
            ui->system_page = SYS_MENU;
            ui->system_selected = 0;
        } else {
            snprintf(ui->status, sizeof(ui->status), "%s IS COMING SOON",
                     names[ui->selected]);
        }
        *dirty = 1;
        break;
    default:
        break;
    }
}

static void media_activate(struct ui *ui)
{
    struct media *m = &ui->media;
    const struct entry *e;

    if (m->selected < 0 || m->selected >= m->entry_count)
        return;
    e = &m->entries[m->selected];

    switch (e->type) {
    case ENT_BT:
        if (m->source == SRC_BT)
            transport_toggle(ui);
        else
            bt_start(ui);
        break;
    case ENT_DIR:
        media_open(m, e->path);
        break;
    case ENT_FILE:
        play_entry(ui, m->selected);
        break;
    case ENT_IMPORT:
        import_start(ui);
        break;
    default:
        break;
    }
}

static void handle_media_key(struct ui *ui, int code, int *dirty)
{
    struct media *m = &ui->media;
    int last = imax(m->entry_count - 1, 0);

    switch (code) {
    case KEY_ESC:
    case KEY_BACKSPACE:
        ui->status[0] = '\0';
        if (!media_up(m))
            ui->screen = SCREEN_HOME;
        break;
    case KEY_UP:
        m->selected = m->selected > 0 ? m->selected - 1 : last;
        break;
    case KEY_DOWN:
        m->selected = m->selected < last ? m->selected + 1 : 0;
        break;
    case KEY_PAGEUP:
        m->selected = iclamp(m->selected - 8, 0, last);
        break;
    case KEY_PAGEDOWN:
        m->selected = iclamp(m->selected + 8, 0, last);
        break;
    case KEY_HOME:
        m->selected = 0;
        break;
    case KEY_END:
        m->selected = last;
        break;
    case KEY_ENTER:
    case KEY_KPENTER:
        media_activate(ui);
        break;
    case KEY_SPACE:
        transport_toggle(ui);
        break;
    case KEY_LEFT:
        transport_skip(ui, -1);
        break;
    case KEY_RIGHT:
        transport_skip(ui, 1);
        break;
    case KEY_M:
        m->muted = !m->muted;
        audio_apply(ui);
        break;
    default:
        return;
    }
    *dirty = 1;
}

static void handle_system_key(struct ui *ui, int code, int *dirty)
{
    struct install *in = &ui->install;
    int enter = code == KEY_ENTER || code == KEY_KPENTER;
    int back = code == KEY_ESC || code == KEY_BACKSPACE;
    int updown = code == KEY_UP || code == KEY_DOWN;

    switch (ui->system_page) {
    case SYS_MENU:
        if (back)
            ui->screen = SCREEN_HOME;
        else if (code == KEY_UP)
            ui->system_selected = ui->system_selected > 0
                                ? ui->system_selected - 1 : 3;
        else if (code == KEY_DOWN)
            ui->system_selected = ui->system_selected < 3
                                ? ui->system_selected + 1 : 0;
        else if (enter && ui->system_selected == 2) {
            ui->system_page = SYS_MONITOR;
            system_info_refresh(&ui->system);
        } else if (enter && ui->system_selected == 3) {
            install_scan(in);
            ui->system_page = SYS_INSTALL_PICK;
        } else if (enter) {
            ui->system_action = ui->system_selected == 0 ? 1 : 2;
            ui->confirm_selected = 0;
            ui->system_page = SYS_CONFIRM;
        } else {
            return;
        }
        break;

    case SYS_CONFIRM:
        if (back || (enter && ui->confirm_selected == 0))
            ui->system_page = SYS_MENU;
        else if (updown)
            ui->confirm_selected = !ui->confirm_selected;
        else if (enter)
            run_cmd(ui->system_action == 1 ? "/sbin/reboot"
                                           : "/sbin/poweroff");
        else
            return;
        break;

    case SYS_INSTALL_PICK:
        if (back) {
            ui->system_page = SYS_MENU;
        } else if (in->count == 0) {
            return;
        } else if (code == KEY_UP) {
            in->selected = in->selected > 0 ? in->selected - 1
                                            : in->count - 1;
        } else if (code == KEY_DOWN) {
            in->selected = in->selected < in->count - 1 ? in->selected + 1
                                                        : 0;
        } else if (enter) {
            ui->confirm_selected = 0;
            ui->system_page = SYS_INSTALL_CONFIRM;
        } else {
            return;
        }
        break;

    case SYS_INSTALL_CONFIRM:
        if (back || (enter && ui->confirm_selected == 0)) {
            ui->system_page = SYS_INSTALL_PICK;
        } else if (updown) {
            ui->confirm_selected = !ui->confirm_selected;
        } else if (enter) {
            install_begin(in);
            ui->confirm_selected = 0;
            ui->system_page = SYS_INSTALL_RUN;
        } else {
            return;
        }
        break;

    case SYS_INSTALL_RUN:
        if (install_done(in)) {
            if (updown)
                ui->confirm_selected = !ui->confirm_selected;
            else if (enter && ui->confirm_selected == 0)
                run_cmd("/sbin/poweroff");
            else if (enter || back)
                ui->system_page = SYS_MENU;
            else
                return;
        } else if (install_failed(in) && (back || enter)) {
            ui->system_page = SYS_MENU;
        } else {
            /* Running: there is nothing to cancel safely. */
            return;
        }
        break;

    default: /* SYS_MONITOR */
        if (!back)
            return;
        ui->system_page = SYS_MENU;
        break;
    }
    *dirty = 1;
}

static void handle_key(struct ui *ui, int code, int *dirty)
{
    if (handle_global_key(ui, code)) {
        *dirty = 1;
        return;
    }
    if (ui->screen == SCREEN_MEDIA)
        handle_media_key(ui, code, dirty);
    else if (ui->screen == SCREEN_SYSTEM)
        handle_system_key(ui, code, dirty);
    else
        handle_home_key(ui, code, dirty);
}

int main(void)
{
    static struct ui ui;
    int inputs[MAX_INPUTS];
    int input_count;
    int dirty = 1;
    double next_draw = 0;
    double next_media_refresh = 0;
    double next_system_refresh = 0;

    ui.screen = SCREEN_HOME;
    ui.selected = 1;
    ui.media.pid = -1;
    ui.media.source = SRC_NONE;
    ui.media.play_index = -1;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    if (fb_init(&ui.fb) < 0)
        return 1;

    ui.ttyfd = tty_graphics();

    fprintf(stderr, "owlcaros-ui: SDL display %ux%u\n",
            ui.fb.xres, ui.fb.yres);

    input_count = input_open(inputs, MAX_INPUTS);
    fprintf(stderr, "owlcaros-ui: %d input device(s)\n", input_count);

    audio_init(&ui);
    mkdir(LIBRARY_DIR, 0755);
    media_list(&ui.media);
    fprintf(stderr, "owlcaros-ui: mixer '%s' volume %d, %d source(s)\n",
            ui.media.mixer, ui.media.volume, ui.media.entry_count);

    while (g_running) {
        struct media *m = &ui.media;

        SDL_PumpEvents();
        media_poll(&ui);

        double now = mono_now();
        if (now >= g_next_bt_refresh) {
            char before[BT_TEXT + 32];
            char after[BT_TEXT + 32];

            snprintf(before, sizeof(before), "%d%s%s", m->bt_connected,
                     m->bt_status, m->bt_title);
            bt_metadata_refresh(m);
            snprintf(after, sizeof(after), "%d%s%s", m->bt_connected,
                     m->bt_status, m->bt_title);
            g_next_bt_refresh = now + 2.0;

            /* The phone started playing by itself: follow it. */
            if (m->source == SRC_NONE && bt_is_playing(m)) {
                m->source = SRC_BT;
                ui.status[0] = '\0';
            }
            if (strcmp(before, after) != 0)
                dirty = 1;
        }
        if (ui.screen == SCREEN_SYSTEM && ui.system_page == SYS_MONITOR
            && now >= next_system_refresh) {
            system_info_refresh(&ui.system);
            next_system_refresh = now + 1.0;
            dirty = 1;
        }
        if (ui.screen == SCREEN_SYSTEM
            && ui.system_page == SYS_INSTALL_RUN
            && now >= next_system_refresh) {
            install_refresh(&ui.install);
            next_system_refresh = now + 0.5;
            dirty = 1;
        }
        if (ui.screen == SCREEN_MEDIA && now >= next_media_refresh) {
            media_rescan(m);
            import_refresh(m);
            next_media_refresh = now + 2.0;
            dirty = 1;
        }

        if (dirty || now >= next_draw) {
            draw_scene(&ui);
            dirty = 0;
            next_draw = now + (ui.screen == SCREEN_MEDIA ? 0.25 : 0.5);
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;

        for (int i = 0; i < input_count; i++) {
            FD_SET(inputs[i], &rfds);
            if (inputs[i] > maxfd)
                maxfd = inputs[i];
        }

        struct timeval tv = { 0, 80000 };
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);

        if (ready > 0) {
            for (int i = 0; i < input_count; i++) {
                struct input_event ev;
                ssize_t n;

                if (!FD_ISSET(inputs[i], &rfds))
                    continue;

                while ((n = read(inputs[i], &ev, sizeof(ev)))
                       == (ssize_t)sizeof(ev)) {
                    if (ev.type == EV_KEY && ev.value == 1)
                        handle_key(&ui, ev.code, &dirty);
                }
            }
        }
    }

    fprintf(stderr, "owlcaros-ui: exiting\n");
    local_stop(&ui);
    bt_resume(&ui);
    tty_text(ui.ttyfd);
    for (int i = 0; i < input_count; i++)
        close(inputs[i]);
    fb_close(&ui.fb);
    return 0;
}
