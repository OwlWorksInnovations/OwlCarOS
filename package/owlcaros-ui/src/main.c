/*
 * owlcaros-ui - native framebuffer dashboard for OwlCarOS
 *
 * Draws directly to /dev/fb0 and reads keyboard events from evdev.
 * No X11, Wayland, SDL or other graphics stack required.
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
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_INPUTS      16
#define MAX_TRACKS      64
#define TRACK_PATH      512
#define TRACK_NAME      96
#define BT_A2DP_PIDFILE "/var/run/bluealsa-aplay.pid"
#define BT_A2DP_SERVICE "/etc/init.d/S46bluealsa-aplay"

#define SCREEN_HOME   0
#define SCREEN_MEDIA  1

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

/*
 * 16x16 dot-matrix pictograms, one bit per LED.
 *
 * Media is a compact cassette (two reels and a label strip). Navigation
 * is a heading arrow inside a bezel, like a cluster gauge. Phone is a
 * handset with a screen and keypad, settings is a cog. Everything is
 * drawn from lit dots so the tiles match the red LED matrix of the TT
 * cluster.
 */
static const uint16_t icon_media[16] = {
    0x0000, 0x0000, 0x0000, 0x7ffe, 0x4002, 0x5e7a, 0x565a, 0x5e7a,
    0x4002, 0x4ff2, 0x4ff2, 0x4002, 0x7ffe, 0x0000, 0x0000, 0x0000,
};

static const uint16_t icon_navigation[16] = {
    0x0000, 0x0000, 0x07e0, 0x1838, 0x301c, 0x6186, 0x63c6, 0xc7e3,
    0xcff3, 0x67e6, 0x63c6, 0x318c, 0x1818, 0x07e0, 0x0000, 0x0000,
};

static const uint16_t icon_phone[16] = {
    0x0000, 0x0000, 0x0000, 0x0ff8, 0x0808, 0x0be8, 0x0be8, 0x0808,
    0x0aa8, 0x0808, 0x0aa8, 0x0808, 0x0ff8, 0x0000, 0x0000, 0x0000,
};

static const uint16_t icon_settings[16] = {
    0x0000, 0x0000, 0x0000, 0x0c30, 0x0c30, 0x3ffc, 0x7ffe, 0x6c36,
    0x6c36, 0x7ffe, 0x3ffc, 0x0c30, 0x0c30, 0x0000, 0x0000, 0x0000,
};

struct fb {
    int fd;
    uint8_t *mem;
    size_t mem_len;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    unsigned xres;
    unsigned yres;
    unsigned line_len;
    unsigned bytes_pp;
    uint8_t *back;
};

struct track {
    char path[TRACK_PATH];
    char name[TRACK_NAME];
    double duration;
};

struct media {
    struct track tracks[MAX_TRACKS];
    int track_count;
    int selected;        /* 0 = Bluetooth entry, 1..n = local tracks */
    int source;          /* SRC_NONE / SRC_LOCAL / SRC_BT */
    int local_index;     /* 0-based index when source == SRC_LOCAL */
    int paused;
    pid_t pid;
    double started;      /* monotonic time the current play segment began */
    double elapsed;      /* accumulated playback time before that segment */
    int bt_was_stopped;  /* we stopped the A2DP player for local audio */
    char mixer[48];
    int volume;
    int muted;
    char bt_title[TRACK_NAME];
    char bt_artist[TRACK_NAME];
    char bt_status[24];
};

struct ui {
    struct fb fb;
    int ttyfd;
    int screen;
    int selected;
    char status[64];
    struct media media;
};

static volatile sig_atomic_t g_running = 1;

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

static uint32_t color_pack(const struct fb *fb, uint32_t rgb)
{
    unsigned r = (rgb >> 16) & 0xff;
    unsigned g = (rgb >> 8) & 0xff;
    unsigned b = rgb & 0xff;
    uint32_t v = 0;

    if (fb->var.red.length)
        v |= ((r >> (8 - fb->var.red.length)) & ((1u << fb->var.red.length) - 1))
             << fb->var.red.offset;
    if (fb->var.green.length)
        v |= ((g >> (8 - fb->var.green.length)) & ((1u << fb->var.green.length) - 1))
             << fb->var.green.offset;
    if (fb->var.blue.length)
        v |= ((b >> (8 - fb->var.blue.length)) & ((1u << fb->var.blue.length) - 1))
             << fb->var.blue.offset;
    return v;
}

static void fill_rect(struct fb *fb, int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > (int)fb->xres)
        w = (int)fb->xres - x;
    if (y + h > (int)fb->yres)
        h = (int)fb->yres - y;
    if (w <= 0 || h <= 0)
        return;

    for (int yy = y; yy < y + h; yy++) {
        uint8_t *p = fb->back + (size_t)yy * fb->line_len
                     + (size_t)x * fb->bytes_pp;
        for (int xx = 0; xx < w; xx++) {
            for (unsigned i = 0; i < fb->bytes_pp; i++)
                p[i] = (uint8_t)(color >> (8 * i));
            p += fb->bytes_pp;
        }
    }
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

static void draw_text(struct fb *fb, int x, int y, const char *text, int scale,
                      uint32_t color)
{
    int cx = x;

    for (; *text; text++) {
        draw_char(fb, cx, y, *text, scale, color);
        cx += 6 * scale;
    }
}

static int text_width(const char *text, int scale)
{
    int n = (int)strlen(text);

    return n > 0 ? (n * 6 - 1) * scale : 0;
}

static void draw_text_centered(struct fb *fb, int x, int y, int w,
                               const char *text, int scale, uint32_t color)
{
    draw_text(fb, x + (w - text_width(text, scale)) / 2, y, text, scale, color);
}

static void draw_text_ellipsis(struct fb *fb, int x, int y, int maxw,
                               const char *text, int scale, uint32_t color)
{
    int maxchars = maxw / (6 * scale);
    char buf[TRACK_NAME];

    if (maxchars < 4) {
        draw_text(fb, x, y, "", scale, color);
        return;
    }

    snprintf(buf, sizeof(buf), "%s", text);
    if ((int)strlen(buf) > maxchars) {
        buf[maxchars - 2] = '\0';
        strncat(buf, "..", sizeof(buf) - strlen(buf) - 1);
    }
    draw_text(fb, x, y, buf, scale, color);
}

/*
 * Draw a pictogram as a grid of glowing LED dots. The full cell is
 * painted with the dim glow colour and a smaller inset cell with the
 * bright colour, leaving a one-pixel gap that reads as a dot matrix.
 */
static void draw_icon(struct fb *fb, int x, int y, int dot,
                      const uint16_t *rows, uint32_t on, uint32_t glow)
{
    for (int r = 0; r < 16; r++) {
        uint16_t bits = rows[r];

        for (int c = 0; c < 16; c++) {
            if (!(bits & (1u << (15 - c))))
                continue;
            fill_rect(fb, x + c * dot, y + r * dot, dot, dot, glow);
            if (dot > 2)
                fill_rect(fb, x + c * dot + 1, y + r * dot + 1,
                          dot - 2, dot - 2, on);
            else
                fill_rect(fb, x + c * dot, y + r * dot, dot, dot, on);
        }
    }
}

static void fb_present(struct fb *fb)
{
    memcpy(fb->mem, fb->back, (size_t)fb->line_len * fb->yres);
}

static int fb_init(struct fb *fb)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;

    fb->fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fb->fd < 0) {
        perror("owlcaros-ui: open /dev/fb0");
        return -1;
    }
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var) < 0) {
        perror("owlcaros-ui: FBIOGET_VSCREENINFO");
        goto fail;
    }
    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fix) < 0) {
        perror("owlcaros-ui: FBIOGET_FSCREENINFO");
        goto fail;
    }

    fb->xres = fb->var.xres;
    fb->yres = fb->var.yres;
    fb->bytes_pp = (fb->var.bits_per_pixel + 7) / 8;
    fb->line_len = fb->fix.line_length;
    fb->mem_len = fb->fix.smem_len ? fb->fix.smem_len
                                   : (size_t)fb->line_len * fb->yres;

    fb->mem = mmap(NULL, fb->mem_len, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fb->fd, 0);
    if (fb->mem == MAP_FAILED) {
        fb->mem = NULL;
        perror("owlcaros-ui: mmap");
        goto fail;
    }

    fb->back = malloc((size_t)fb->line_len * fb->yres);
    if (fb->back == NULL) {
        perror("owlcaros-ui: malloc");
        goto fail;
    }
    memset(fb->back, 0, (size_t)fb->line_len * fb->yres);
    return 0;

fail:
    if (fb->mem != NULL)
        munmap(fb->mem, fb->mem_len);
    free(fb->back);
    if (fb->fd >= 0)
        close(fb->fd);
    fb->mem = NULL;
    fb->back = NULL;
    fb->fd = -1;
    return -1;
}

static void fb_close(struct fb *fb)
{
    if (fb->mem != NULL)
        munmap(fb->mem, fb->mem_len);
    free(fb->back);
    if (fb->fd >= 0)
        close(fb->fd);
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

static void bt_control(const char *action)
{
    run_cmd("bluetoothctl player %s >/dev/null 2>&1", action);
}

static void bt_metadata_refresh(struct media *m)
{
    FILE *p = popen("bluetoothctl player show 2>/dev/null", "r");
    char line[256];
    int has_track = 0;

    if (p == NULL)
        return;

    while (fgets(line, sizeof(line), p) != NULL) {
        char *value = NULL;
        char *end;

        if ((value = strstr(line, "Track.Title: ")) != NULL) {
            value += strlen("Track.Title: ");
            has_track = 1;
            end = strpbrk(value, "\r\n");
            if (end) *end = '\0';
            if (strcmp(m->bt_title, value) != 0) {
                snprintf(m->bt_title, sizeof(m->bt_title), "%s", value);
            }
        } else if ((value = strstr(line, "Track.Artist: ")) != NULL) {
            value += strlen("Track.Artist: ");
            end = strpbrk(value, "\r\n");
            if (end) *end = '\0';
            if (strcmp(m->bt_artist, value) != 0) {
                snprintf(m->bt_artist, sizeof(m->bt_artist), "%s", value);
            }
        } else if ((value = strstr(line, "Status: ")) != NULL) {
            value += strlen("Status: ");
            end = strpbrk(value, "\r\n");
            if (end) *end = '\0';
            if (strcmp(m->bt_status, value) != 0) {
                snprintf(m->bt_status, sizeof(m->bt_status), "%s", value);
            }
        }
    }
    pclose(p);
    if (!has_track) {
        m->bt_title[0] = '\0';
        m->bt_artist[0] = '\0';
    }
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

static void add_track(struct media *m, const char *path)
{
    const char *base = strrchr(path, '/');
    struct track *t;
    size_t len;
    char *dot;

    if (m->track_count >= MAX_TRACKS)
        return;
    for (int i = 0; i < m->track_count; i++) {
        if (strcmp(m->tracks[i].path, path) == 0)
            return;
    }

    base = base ? base + 1 : path;
    t = &m->tracks[m->track_count];
    snprintf(t->path, sizeof(t->path), "%s", path);

    len = strlen(base);
    if (len >= sizeof(t->name))
        len = sizeof(t->name) - 1;
    for (size_t i = 0; i < len; i++) {
        char c = base[i];

        if (c == '_' || c == '-')
            c = ' ';
        t->name[i] = c;
    }
    t->name[len] = '\0';

    dot = strrchr(t->name, '.');
    if (dot != NULL)
        *dot = '\0';

    t->duration = has_ext(path, ".wav") ? wav_duration(path) : 0;
    m->track_count++;
}

static void scan_dir(struct media *m, const char *dir, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    if (d == NULL)
        return;

    while ((e = readdir(d)) != NULL) {
        char path[TRACK_PATH];
        struct stat st;
        int n;

        if (e->d_name[0] == '.')
            continue;
        n = snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (n < 0 || n >= (int)sizeof(path))
            continue;
        if (stat(path, &st) < 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            if (depth > 0)
                scan_dir(m, path, depth - 1);
        } else if (S_ISREG(st.st_mode) && is_audio_file(path)) {
            add_track(m, path);
        }

        if (m->track_count >= MAX_TRACKS)
            break;
    }

    closedir(d);
}

static int track_cmp(const void *a, const void *b)
{
    const struct track *ta = a;
    const struct track *tb = b;

    return strcmp(ta->name, tb->name);
}

static void media_scan(struct ui *ui)
{
    static const struct {
        const char *path;
        int depth;
    } dirs[] = {
        { "/media", 2 },
        { "/run/media", 2 },
        { "/mnt", 1 },
        { "/root/Music", 2 },
        { "/usr/share/owlcaros/media", 1 },
    };
    struct media *m = &ui->media;

    m->track_count = 0;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
        scan_dir(m, dirs[i].path, dirs[i].depth);

    qsort(m->tracks, (size_t)m->track_count, sizeof(m->tracks[0]), track_cmp);
}

static void local_stop(struct ui *ui)
{
    struct media *m = &ui->media;

    if (m->pid > 0) {
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
    m->local_index = -1;
}

static void bt_resume(struct ui *ui)
{
    struct media *m = &ui->media;

    if (m->bt_was_stopped && bt_aplay_available()) {
        bt_aplay(1);
        m->bt_was_stopped = 0;
    }
}

static void local_start(struct ui *ui, int index)
{
    struct media *m = &ui->media;
    pid_t pid;

    if (index < 0 || index >= m->track_count)
        return;

    local_stop(ui);

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
        if (has_ext(m->tracks[index].path, ".mp3"))
            execlp("mpg123", "mpg123", "-q", "--",
                   m->tracks[index].path, (char *)NULL);
        else
            execlp("aplay", "aplay", "-q", m->tracks[index].path, (char *)NULL);
        _exit(127);
    }

    if (pid < 0) {
        snprintf(ui->status, sizeof(ui->status), "PLAYER ERROR");
        return;
    }

    m->pid = pid;
    m->source = SRC_LOCAL;
    m->local_index = index;
    m->paused = 0;
    m->elapsed = 0;
    m->started = mono_now();
    m->selected = index + 1;
    snprintf(ui->status, sizeof(ui->status), "PLAYING %.40s",
             m->tracks[index].name);
}

static double local_elapsed(const struct media *m)
{
    if (m->paused)
        return m->elapsed;
    return m->elapsed + (mono_now() - m->started);
}

static void local_toggle(struct ui *ui, int index)
{
    struct media *m = &ui->media;

    if (m->source == SRC_LOCAL && m->local_index == index && m->pid > 0) {
        if (m->paused) {
            kill(m->pid, SIGCONT);
            m->paused = 0;
            m->started = mono_now();
            snprintf(ui->status, sizeof(ui->status), "PLAYING %.40s",
                     m->tracks[index].name);
        } else {
            m->elapsed = local_elapsed(m);
            kill(m->pid, SIGSTOP);
            m->paused = 1;
            snprintf(ui->status, sizeof(ui->status), "PAUSED");
        }
        return;
    }

    if (m->source != SRC_LOCAL)
        bt_resume(ui);
    local_start(ui, index);
}

static void bt_start(struct ui *ui)
{
    struct media *m = &ui->media;

    local_stop(ui);
    bt_resume(ui);
    m->source = SRC_BT;
    m->selected = 0;

    if (!bt_aplay_available()) {
        snprintf(ui->status, sizeof(ui->status), "BT AUDIO SERVICE MISSING");
        return;
    }
    if (!bt_adapter_present()) {
        snprintf(ui->status, sizeof(ui->status), "NO BLUETOOTH ADAPTER");
        return;
    }
    if (!bt_aplay_running())
        bt_aplay(1);
    bt_metadata_refresh(m);
    if (m->bt_title[0])
        bt_control("play");
    snprintf(ui->status, sizeof(ui->status), "BLUETOOTH AUDIO");
}

static void local_skip(struct ui *ui, int dir)
{
    struct media *m = &ui->media;
    int idx;

    if (m->track_count == 0)
        return;

    if (m->source == SRC_LOCAL && m->local_index >= 0)
        idx = m->local_index + dir;
    else
        idx = dir > 0 ? 0 : m->track_count - 1;

    if (idx < 0)
        idx = m->track_count - 1;
    if (idx >= m->track_count)
        idx = 0;

    local_start(ui, idx);
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
        int next = m->local_index + 1;

        m->source = SRC_NONE;
        m->local_index = -1;
        if (next < m->track_count) {
            local_start(ui, next);
        } else {
            bt_resume(ui);
            snprintf(ui->status, sizeof(ui->status), "END OF PLAYLIST");
        }
    }
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
                         uint32_t red, uint32_t red_bright)
{
    int top_h = imax(h / 12, 40);
    int scale = iclamp(w / 420, 2, 3);
    int y = (top_h - 7 * scale) / 2;
    int margin = imax(w / 64, 12);
    const char *left = "MEMORY";
    const char *center = "OWLCAROS";
    const char *right = "SYSTEM";

    fill_rect(fb, 0, 0, w, top_h, panel);
    fill_rect(fb, 0, top_h - 2, w, 2, red);

    draw_text(fb, margin, y, left, scale, red);
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

static void draw_bottom_bar(struct fb *fb, const struct ui *ui, int w, int h,
                            uint32_t panel, uint32_t red, uint32_t red_dim)
{
    const struct media *m = &ui->media;
    const char *left = ui->screen == SCREEN_MEDIA ? "TUNING" : "MENU";
    char sound[32];
    char clockbuf[16];
    char batterybuf[8];
    int bottom_h = imax(h / 18, 26);
    int scale = iclamp(w / 360, 1, 2);
    int margin = imax(w / 64, 12);
    int sy = h - bottom_h + (bottom_h - 7 * scale) / 2;
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
    fill_rect(fb, 0, h - bottom_h, w, 2, red);

    clock_w = text_width(clockbuf, scale);
    battery_w = text_width(batterybuf, scale);
    group_w = clock_w + 5 * scale + icon_w + 3 * scale + battery_w;
    group_x = (w - group_w) / 2;
    icon_y = h - bottom_h + (bottom_h - 8 * scale) / 2;

    draw_text(fb, margin, sy, left, scale, red_dim);
    draw_text(fb, group_x, sy, clockbuf, scale,
              red);
    draw_battery_icon(fb, group_x + clock_w + 5 * scale, icon_y, scale,
                      percent, red_dim, red);
    draw_text(fb, group_x + clock_w + 5 * scale + icon_w + 3 * scale,
              sy, batterybuf, scale, red);
    draw_text(fb, w - margin - text_width(sound, scale), sy, sound, scale,
              red_dim);
}



static void draw_line(struct fb *fb, int x0, int y0, int x1, int y1,
                      uint32_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        fill_rect(fb, x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_radio_frame(struct fb *fb, int x, int y, int w, int h,
                             uint32_t bright, uint32_t dim)
{
    int left = x + w / 4;
    int right = x + (3 * w) / 4;
    int cy = y + h / 2;
    int arc_h = imax(h / 5, 24);
    int curve = imax(w / 50, 12);

    if (arc_h > h / 3)
        arc_h = h / 3;
    if (curve > 36)
        curve = 36;

    fill_rect(fb, x, y, w, 2, bright);
    fill_rect(fb, x, y + h - 2, w, 2, dim);
    fill_rect(fb, x, y, 2, h, dim);
    fill_rect(fb, x + w - 2, y, 2, h, dim);
    fill_rect(fb, x + 8, y + 8, w - 16, 1, dim);
    fill_rect(fb, x + 8, y + h - 9, w - 16, 1, dim);

    draw_line(fb, x + 2, cy - arc_h, left + curve, cy - arc_h, dim);
    draw_line(fb, left + curve, cy - arc_h,
              left + curve / 2, cy - arc_h / 2, bright);
    draw_line(fb, left + curve / 2, cy - arc_h / 2, left, cy, bright);
    draw_line(fb, left, cy, left + curve / 2, cy + arc_h / 2, bright);
    draw_line(fb, left + curve / 2, cy + arc_h / 2,
              left + curve, cy + arc_h, bright);
    draw_line(fb, left + curve, cy + arc_h, x + 2, cy + arc_h, dim);

    draw_line(fb, right - curve, cy - arc_h, x + w - 2, cy - arc_h, dim);
    draw_line(fb, right - curve, cy - arc_h,
              right - curve / 2, cy - arc_h / 2, bright);
    draw_line(fb, right - curve / 2, cy - arc_h / 2, right, cy, bright);
    draw_line(fb, right, cy, right - curve / 2, cy + arc_h / 2, bright);
    draw_line(fb, right - curve / 2, cy + arc_h / 2,
              right - curve, cy + arc_h, bright);
    draw_line(fb, right - curve, cy + arc_h, x + w - 2, cy + arc_h, dim);

    fill_rect(fb, left - 2, cy - 2, 4, 4, bright);
    fill_rect(fb, right - 2, cy - 2, 4, 4, bright);
}

static void draw_home(struct ui *ui)
{
    struct fb *fb = &ui->fb;
    const int w = (int)fb->xres;
    const int h = (int)fb->yres;
    const uint32_t c_bg = color_pack(fb, 0x000000);
    const uint32_t c_panel = color_pack(fb, 0x050000);
    const uint32_t c_panel_sel = color_pack(fb, 0x210000);
    const uint32_t c_red = color_pack(fb, 0xB00000);
    const uint32_t c_red_bright = color_pack(fb, 0xFF0000);
    const uint32_t c_red_dim = color_pack(fb, 0x680000);
    const int top_h = imax(h / 12, 40);
    const int bottom_h = imax(h / 18, 26);
    const int margin = imax(w / 64, 12);
    const int frame_x = margin;
    const int frame_y = top_h + 4;
    const int frame_w = imax(w - 2 * margin, 1);
    const int frame_h = imax(h - top_h - bottom_h - 10, 1);
    const int scale = iclamp(w / 500, 2, 3);
    const int row_h = 7 * scale + 14;
    const int list_h = row_h * 4;
    int curve = imax(frame_w / 50, 12);
    int list_w;
    int list_x;
    int list_y;

    static const char *const titles[4] = {
        "MEDIA", "NAVIGATION", "PHONE", "SETTINGS"
    };
    static const char *const details[4] = {
        "BLUETOOTH AND LOCAL AUDIO", "MAPS AND ROUTES",
        "HANDS-FREE", "SYSTEM"
    };

    if (curve > 36)
        curve = 36;
    list_w = imax(frame_w / 2 - 2 * curve, 1);
    list_x = (w - list_w) / 2;
    list_y = frame_y + frame_h / 2 - list_h / 2;

    fill_rect(fb, 0, 0, w, h, c_bg);
    draw_top_bar(fb, w, h, c_panel, c_red, c_red_bright);
    draw_radio_frame(fb, frame_x, frame_y, frame_w, frame_h,
                     c_red_bright, c_red_dim);

    draw_text_centered(fb, list_x, list_y - 7 * scale - 13,
                       list_w, "SELECT MODE", scale, c_red_bright);
    fill_rect(fb, list_x, list_y - 4, list_w, 1, c_red_dim);

    for (int i = 0; i < 4; i++) {
        int y = list_y + i * row_h;
        int selected = i == ui->selected;
        int title_y = y + (row_h - (7 * scale + 8)) / 2;
        int text_x = list_x + 12;
        int text_w = list_w - 24;
        uint32_t title_color = selected ? c_red_bright : c_red_dim;
        uint32_t detail_color = selected ? c_red : c_red_dim;

        if (selected) {
            fill_rect(fb, list_x + 2, y + 1, list_w - 4, row_h - 2,
                      c_panel_sel);
            fill_rect(fb, list_x + 2, y + 1, list_w - 4, 1, c_red_bright);
            fill_rect(fb, list_x + 2, y + row_h - 2, list_w - 4, 1,
                      c_red_bright);
        } else {
            fill_rect(fb, list_x + 8, y + row_h - 1, list_w - 16, 1,
                      c_red_dim);
        }

        draw_text_ellipsis(fb, text_x, title_y, text_w, titles[i], scale,
                           title_color);
        draw_text_ellipsis(fb, text_x, title_y + 7 * scale + 2,
                           text_w, details[i], 1, detail_color);
    }

    draw_text_centered(fb, frame_x, frame_y + frame_h - 18,
                       frame_w, "UP/DOWN SELECT   ENTER OPEN", 1, c_red_dim);
    draw_bottom_bar(fb, ui, w, h, c_panel, c_red, c_red_dim);
}

static void fmt_time(char *buf, size_t n, double seconds)
{
    int s = (int)seconds;

    if (s < 0)
        s = 0;
    snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

static void draw_vu(struct fb *fb, int x, int y, int w, int h, int active,
                    double t, uint32_t low, uint32_t mid, uint32_t high,
                    uint32_t off)
{
    int bars = 24;
    int gap = imax(w / 240, 2);
    int bw = (w - (bars - 1) * gap) / bars;
    int seg_h = 5;
    int seg_gap = 3;
    int segs = h / (seg_h + seg_gap);

    if (bw < 2) {
        bars = 12;
        gap = 2;
        bw = (w - (bars - 1) * gap) / bars;
    }
    if (bw < 1 || segs < 1)
        return;

    for (int i = 0; i < bars; i++) {
        int level;
        int lit;

        if (active)
            level = 32 + (int)(((long)(t * 11.0) + i * 17 + i * i * 7) % 62);
        else
            level = 5;

        lit = level * segs / 100;
        if (lit < 1)
            lit = 1;

        for (int s = 0; s < lit; s++) {
            int sy = y + h - (s + 1) * (seg_h + seg_gap);
            uint32_t c;

            if (s * 100 >= segs * 75)
                c = high;
            else if (s * 100 >= segs * 45)
                c = mid;
            else
                c = low;

            fill_rect(fb, x + i * (bw + gap), sy, bw, seg_h, c);
        }

        for (int s = lit; s < segs; s++) {
            int sy = y + h - (s + 1) * (seg_h + seg_gap);

            fill_rect(fb, x + i * (bw + gap), sy, bw, 1, off);
        }
    }
}


static void draw_media(struct ui *ui)
{
    struct fb *fb = &ui->fb;
    const int w = (int)fb->xres;
    const int h = (int)fb->yres;
    const uint32_t c_bg = color_pack(fb, 0x000000);
    const uint32_t c_panel = color_pack(fb, 0x050000);
    const uint32_t c_panel_sel = color_pack(fb, 0x210000);
    const uint32_t c_red = color_pack(fb, 0xB00000);
    const uint32_t c_red_bright = color_pack(fb, 0xFF0000);
    const uint32_t c_red_dim = color_pack(fb, 0x680000);
    const uint32_t c_red_faint = color_pack(fb, 0x260000);
    const uint32_t c_glow = color_pack(fb, 0x260000);
    const int top_h = imax(h / 12, 40);
    const int bottom_h = imax(h / 18, 26);
    const int margin = imax(w / 64, 12);
    const int frame_x = margin;
    const int frame_y = top_h + 4;
    const int frame_w = imax(w - 2 * margin, 1);
    const int frame_h = imax(h - top_h - bottom_h - 10, 1);
    const int scale = iclamp(w / 500, 2, 3);
    int curve = imax(frame_w / 50, 12);
    int content_w;
    int content_x;
    struct media *m = &ui->media;
    const struct track *tr = NULL;
    char timebuf[16];
    char durbuf[16];
    char line[TRACK_NAME + 16];
    const char *state = "IDLE";
    const char *source = "NO SOURCE";
    const char *title = "NO SOURCE";
    int active = 0;
    double elapsed = 0;
    double duration = 0;
    int now_y;
    int title_y;
    int status_y;
    int meter_y;
    int list_header_y;
    int rows_y;
    int row_h = 7 * scale + 8;
    int max_rows;
    int rows;
    int items = 1 + m->track_count;
    int top = 0;

    if (curve > 36)
        curve = 36;
    content_w = imax(frame_w / 2 - 2 * curve, 1);
    content_x = (w - content_w) / 2;

    fill_rect(fb, 0, 0, w, h, c_bg);
    draw_top_bar(fb, w, h, c_panel, c_red, c_red_bright);
    draw_radio_frame(fb, frame_x, frame_y, frame_w, frame_h,
                     c_red_bright, c_red_dim);

    if (m->source == SRC_LOCAL && m->local_index >= 0
        && m->local_index < m->track_count) {
        tr = &m->tracks[m->local_index];
        title = tr->name;
        source = "LOCAL";
        state = m->paused ? "PAUSED" : "PLAYING";
        elapsed = local_elapsed(m);
        duration = tr->duration;
        active = !m->paused;
    } else if (m->source == SRC_BT) {
        int streaming = alsa_playback_running();

        title = m->bt_title[0] ? m->bt_title : "BLUETOOTH AUDIO";
        source = "A2DP";
        state = m->bt_status[0] ? m->bt_status
                                : (streaming ? "STREAMING" : "WAITING FOR PHONE");
        active = streaming;
    }

    now_y = frame_y + frame_h / 7;
    title_y = now_y + 7 * scale + 8;
    status_y = title_y + 7 * scale + 5;
    meter_y = status_y + 12;
    list_header_y = frame_y + frame_h / 2 + 8;
    rows_y = list_header_y + 15;
    max_rows = (frame_y + frame_h - 27 - rows_y) / row_h;
    if (max_rows > 5)
        max_rows = 5;
    if (max_rows < 1)
        max_rows = 1;

    draw_text(fb, content_x + 4, now_y, "NOW PLAYING", 1, c_red_dim);
    draw_text(fb, content_x + content_w - text_width(source, 1) - 4,
              now_y, source, 1, c_red);
    draw_text_centered(fb, content_x, title_y, content_w, title,
                       scale, active ? c_red_bright : c_red);
    if (m->source == SRC_BT && m->bt_artist[0]) {
        draw_text_centered(fb, content_x, status_y, content_w, m->bt_artist,
                           1, c_red);
        draw_text_centered(fb, content_x, status_y + 8, content_w, state,
                           1, c_red_dim);
        meter_y += 8;
    } else {
        draw_text_centered(fb, content_x, status_y, content_w, state,
                           1, c_red_dim);
    }

    if (duration > 0 && m->source == SRC_LOCAL) {
        int fraction = iclamp((int)(elapsed * 100.0 / duration), 0, 100);
        int bar_x = content_x + 8;
        int bar_w = content_w - 16;

        fill_rect(fb, bar_x, meter_y, bar_w, 2, c_red_faint);
        fill_rect(fb, bar_x, meter_y, bar_w * fraction / 100, 2,
                  c_red_bright);
        fmt_time(timebuf, sizeof(timebuf), elapsed);
        fmt_time(durbuf, sizeof(durbuf), duration);
        draw_text(fb, bar_x, meter_y + 7, timebuf, 1, c_red_dim);
        draw_text(fb, bar_x + bar_w - text_width(durbuf, 1),
                  meter_y + 7, durbuf, 1, c_red_dim);
    } else if (m->source == SRC_BT) {
        draw_vu(fb, content_x + content_w / 4, meter_y,
                content_w / 2, 22, active, mono_now(),
                c_red_dim, c_red, c_red_bright, c_red_faint);
    } else {
        fill_rect(fb, content_x + 8, meter_y, content_w - 16, 1, c_red_dim);
    }

    draw_line(fb, content_x, list_header_y - 4,
              content_x + content_w, list_header_y - 4, c_red_dim);
    snprintf(line, sizeof(line), "%d TRACK%s", m->track_count,
             m->track_count == 1 ? "" : "S");
    draw_text(fb, content_x + 4, list_header_y, "MEMORY", 1, c_red);
    draw_text(fb, content_x + content_w - text_width(line, 1) - 4,
              list_header_y, line, 1, c_red_dim);

    rows = max_rows;
    if (m->selected >= rows)
        top = m->selected - rows + 1;
    if (top > items - rows && items > rows)
        top = items - rows;
    if (top < 0)
        top = 0;

    for (int i = 0; i < rows; i++) {
        int item = top + i;
        int y = rows_y + i * row_h;
        int selected;
        char label[TRACK_NAME + 8];

        if (item >= items)
            break;
        selected = item == m->selected;

        if (selected) {
            fill_rect(fb, content_x + 2, y, content_w - 4, row_h - 1,
                      c_panel_sel);
            fill_rect(fb, content_x + 2, y, content_w - 4, 1,
                      c_red_bright);
            fill_rect(fb, content_x + 2, y + row_h - 2,
                      content_w - 4, 1, c_red_bright);
        } else {
            fill_rect(fb, content_x + 10, y + row_h - 1,
                      content_w - 20, 1, c_red_faint);
        }

        if (item == 0)
            snprintf(label, sizeof(label), "BLUETOOTH AUDIO");
        else
            snprintf(label, sizeof(label), "%02d  %s", item,
                     m->tracks[item - 1].name);

        draw_text_ellipsis(fb, content_x + 10, y + (row_h - 7 * scale) / 2,
                           content_w - 20, label, scale,
                           selected ? c_red_bright : c_red_dim);

        if (item == 0 && m->source == SRC_BT) {
            const char *tag = alsa_playback_running() ? "PLAY" : "IDLE";

            draw_text(fb, content_x + content_w - 10 - text_width(tag, 1),
                      y + (row_h - 7) / 2, tag, 1, c_red);
        }
    }

    if (m->track_count == 0 && rows > 1) {
        draw_text_centered(fb, content_x, rows_y + row_h, content_w,
                           "NO LOCAL TRACKS", 1, c_red_dim);
        draw_text_centered(fb, content_x, rows_y + row_h + 9, content_w,
                           "ADD WAV OR MP3 TO /MEDIA", 1, c_red_dim);
    }

    draw_text_centered(fb, frame_x, frame_y + frame_h - 18,
                       frame_w, "UP/DOWN SELECT  ENTER PLAY  L/R SKIP  ESC BACK",
                       1, c_red_dim);
    draw_bottom_bar(fb, ui, w, h, c_panel, c_red, c_red_dim);
}

static void draw_scene(struct ui *ui)
{
    if (ui->screen == SCREEN_MEDIA)
        draw_media(ui);
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
        *dirty = 1;
        break;
    case KEY_RIGHT:
    case KEY_DOWN:
        ui->selected = ui->selected < 3 ? ui->selected + 1 : 0;
        *dirty = 1;
        break;
    case KEY_ENTER:
    case KEY_KPENTER:
        if (ui->selected == 0) {
            ui->screen = SCREEN_MEDIA;
            snprintf(ui->status, sizeof(ui->status), "MEDIA");
        } else {
            snprintf(ui->status, sizeof(ui->status), "%s COMING SOON",
                     names[ui->selected]);
        }
        *dirty = 1;
        break;
    default:
        break;
    }
}

static void handle_media_key(struct ui *ui, int code, int *dirty)
{
    struct media *m = &ui->media;

    switch (code) {
    case KEY_ESC:
    case KEY_BACKSPACE:
        ui->screen = SCREEN_HOME;
        *dirty = 1;
        break;
    case KEY_UP:
        if (m->selected > 0)
            m->selected--;
        *dirty = 1;
        break;
    case KEY_DOWN:
        if (m->selected < m->track_count)
            m->selected++;
        *dirty = 1;
        break;
    case KEY_ENTER:
    case KEY_KPENTER:
    case KEY_SPACE:
        if (m->selected == 0) {
            if (m->source == SRC_BT && m->bt_title[0]) {
                bt_control(!strcasecmp(m->bt_status, "playing") ? "pause" : "play");
            } else {
                bt_start(ui);
            }
        } else {
            local_toggle(ui, m->selected - 1);
        }
        *dirty = 1;
        break;
    case KEY_LEFT:
        if (m->selected == 0 && m->source == SRC_BT && m->bt_title[0])
            bt_control("previous");
        else
            local_skip(ui, -1);
        *dirty = 1;
        break;
    case KEY_RIGHT:
        if (m->selected == 0 && m->source == SRC_BT && m->bt_title[0])
            bt_control("next");
        else
            local_skip(ui, 1);
        *dirty = 1;
        break;
    case KEY_M:
        m->muted = !m->muted;
        audio_apply(ui);
        *dirty = 1;
        break;
    default:
        break;
    }
}

static void handle_key(struct ui *ui, int code, int *dirty)
{
    if (handle_global_key(ui, code)) {
        *dirty = 1;
        return;
    }
    if (ui->screen == SCREEN_MEDIA)
        handle_media_key(ui, code, dirty);
    else
        handle_home_key(ui, code, dirty);
}

int main(void)
{
    struct ui ui;
    int inputs[MAX_INPUTS];
    int input_count;
    int dirty = 1;
    double next_draw = 0;
    double next_bt_refresh = 0;
    double next_media_refresh = 0;

    memset(&ui, 0, sizeof(ui));
    ui.screen = SCREEN_HOME;
    ui.selected = 0;
    snprintf(ui.status, sizeof(ui.status), "READY");
    ui.media.pid = -1;
    ui.media.source = SRC_NONE;
    ui.media.local_index = -1;
    ui.media.selected = 0;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    if (fb_init(&ui.fb) < 0)
        return 1;

    ui.ttyfd = tty_graphics();

    fprintf(stderr, "owlcaros-ui: framebuffer %ux%u %ubpp\n",
            ui.fb.xres, ui.fb.yres, ui.fb.var.bits_per_pixel);

    input_count = input_open(inputs, MAX_INPUTS);
    fprintf(stderr, "owlcaros-ui: %d input device(s)\n", input_count);

    audio_init(&ui);
    media_scan(&ui);
    fprintf(stderr, "owlcaros-ui: mixer '%s' volume %d, %d track(s)\n",
            ui.media.mixer, ui.media.volume, ui.media.track_count);

    while (g_running) {
        media_poll(&ui);

        double now = mono_now();
        if (now >= next_bt_refresh) {
            bt_metadata_refresh(&ui.media);
            next_bt_refresh = now + 2.0;
            if (ui.media.source == SRC_BT)
                dirty = 1;
        }
        if (now >= next_media_refresh) {
            char active_path[TRACK_PATH] = "";
            int old_selected = ui.media.selected;
            if (ui.media.source == SRC_LOCAL && ui.media.local_index >= 0
                && ui.media.local_index < ui.media.track_count)
                snprintf(active_path, sizeof(active_path), "%s",
                         ui.media.tracks[ui.media.local_index].path);
            media_scan(&ui);
            if (active_path[0]) {
                ui.media.local_index = -1;
                for (int i = 0; i < ui.media.track_count; i++) {
                    if (strcmp(ui.media.tracks[i].path, active_path) == 0) {
                        ui.media.local_index = i;
                        break;
                    }
                }
            }
            ui.media.selected = iclamp(old_selected, 0, ui.media.track_count);
            next_media_refresh = now + 3.0;
            dirty = 1;
        }

        if (dirty || now >= next_draw) {
            draw_scene(&ui);
            dirty = 0;
            next_draw = now + (ui.screen == SCREEN_MEDIA ? 0.15 : 0.5);
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
