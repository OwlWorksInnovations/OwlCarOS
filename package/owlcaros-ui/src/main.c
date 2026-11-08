/*
 * owlcaros-ui - native framebuffer dashboard for OwlCarOS
 *
 * Draws directly to /dev/fb0 and reads keyboard events from evdev.
 * No X11, Wayland, SDL or other graphics stack required.
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define MAX_INPUTS 16

/*
 * 5x7 bitmap font. Each glyph is 7 rows, one byte per row.
 * Bit 4 is the left-most pixel, bit 0 the right-most.
 * Lower-case characters are folded to upper-case by draw_char().
 */
static const uint8_t font5x7[96][7] = {
    [' ' - 32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['!' - 32] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},
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

struct ui {
    struct fb fb;
    int ttyfd;
    int selected;
    int volume;
    int muted;
    int bt_on;
    int wifi_on;
    char status[64];
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

static void handle_key(struct ui *ui, int code, int *dirty)
{
    static const char *const names[] = {
        "MEDIA", "NAVIGATION", "PHONE", "SETTINGS"
    };

    switch (code) {
    case KEY_LEFT:
        if (ui->selected % 2 == 1)
            ui->selected--;
        *dirty = 1;
        break;
    case KEY_RIGHT:
        if (ui->selected % 2 == 0)
            ui->selected++;
        *dirty = 1;
        break;
    case KEY_UP:
        if (ui->selected >= 2)
            ui->selected -= 2;
        *dirty = 1;
        break;
    case KEY_DOWN:
        if (ui->selected < 2)
            ui->selected += 2;
        *dirty = 1;
        break;
    case KEY_ENTER:
    case KEY_KPENTER:
        snprintf(ui->status, sizeof(ui->status), "%s SELECTED",
                 names[ui->selected]);
        *dirty = 1;
        break;
    case KEY_MUTE:
        ui->muted = !ui->muted;
        *dirty = 1;
        break;
    case KEY_VOLUMEUP:
        if (ui->volume < 100)
            ui->volume += 5;
        *dirty = 1;
        break;
    case KEY_VOLUMEDOWN:
        if (ui->volume > 0)
            ui->volume -= 5;
        *dirty = 1;
        break;
    default:
        break;
    }
}

static void draw_scene(struct ui *ui)
{
    struct fb *fb = &ui->fb;
    const int w = (int)fb->xres;
    const int h = (int)fb->yres;
    const uint32_t c_bg = color_pack(fb, 0x0A0F14);
    const uint32_t c_panel = color_pack(fb, 0x111A22);
    const uint32_t c_selected = color_pack(fb, 0x0F3038);
    const uint32_t c_accent = color_pack(fb, 0x00C8B4);
    const uint32_t c_amber = color_pack(fb, 0xFFB000);
    const uint32_t c_text = color_pack(fb, 0xE6E6E6);
    const uint32_t c_dim = color_pack(fb, 0x6E7F8A);
    const int top_h = imax(h / 12, 40);
    const int bottom_h = imax(h / 18, 26);
    const int margin = imax(w / 64, 12);
    const int gap = imax(w / 128, 8);

    static const char *const tiles[4] = {
        "MEDIA", "NAVIGATION", "PHONE", "SETTINGS"
    };
    static const char *const subtitles[4] = {
        "AUDIO / BLUETOOTH", "MAPS & ROUTES", "HANDS-FREE", "SYSTEM"
    };

    fill_rect(fb, 0, 0, w, h, c_bg);

    /* Top bar */
    fill_rect(fb, 0, 0, w, top_h, c_panel);
    fill_rect(fb, 0, top_h - 2, w, 2, c_accent);

    int title_scale = iclamp(w / 420, 2, 4);
    draw_text(fb, margin, (top_h - 7 * title_scale) / 2, "OWLCAROS",
              title_scale, c_accent);

    char clockbuf[16];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(clockbuf, sizeof(clockbuf), "%H:%M", &tm_now);
    int clock_scale = title_scale;
    draw_text(fb, w - margin - text_width(clockbuf, clock_scale),
              (top_h - 7 * clock_scale) / 2, clockbuf, clock_scale, c_amber);

    /* Main tiles */
    int area_y = top_h + margin;
    int area_h = h - top_h - bottom_h - 2 * margin;
    int tile_w = (w - 2 * margin - gap) / 2;
    int tile_h = (area_h - gap) / 2;
    int label_scale = iclamp(w / 320, 2, 5);
    int sub_scale = iclamp(w / 900, 1, 2);

    for (int i = 0; i < 4; i++) {
        int tx = margin + (i % 2) * (tile_w + gap);
        int ty = area_y + (i / 2) * (tile_h + gap);
        int selected = (i == ui->selected);
        uint32_t border = selected ? c_accent : c_dim;

        fill_rect(fb, tx, ty, tile_w, tile_h,
                  selected ? c_selected : c_panel);
        fill_rect(fb, tx, ty, tile_w, 2, border);
        fill_rect(fb, tx, ty + tile_h - 2, tile_w, 2, border);
        fill_rect(fb, tx, ty, 2, tile_h, border);
        fill_rect(fb, tx + tile_w - 2, ty, 2, tile_h, border);

        int lh = 7 * label_scale;
        int sh = 7 * sub_scale;
        int block_h = lh + 6 + sh;
        int by = ty + (tile_h - block_h) / 2;

        draw_text_centered(fb, tx, by, tile_w, tiles[i], label_scale,
                           selected ? c_accent : c_text);
        draw_text_centered(fb, tx, by + lh + 6, tile_w, subtitles[i],
                           sub_scale, c_dim);

        if (selected) {
            int bar_w = imax(tile_w / 6, 24);
            fill_rect(fb, tx + (tile_w - bar_w) / 2, ty + tile_h - 18,
                      bar_w, 3, c_amber);
        }
    }

    /* Bottom status bar */
    fill_rect(fb, 0, h - bottom_h, w, bottom_h, c_panel);
    fill_rect(fb, 0, h - bottom_h, w, 2, c_accent);

    char status[192];
    snprintf(status, sizeof(status),
             "BT %s   |   WIFI %s   |   VOL %s%3d   |   %s",
             ui->bt_on ? "ON " : "OFF",
             ui->wifi_on ? "ON " : "OFF",
             ui->muted ? "MUTE " : "",
             ui->volume,
             ui->status[0] ? ui->status : "READY");

    int status_scale = iclamp(w / 1280, 1, 2);
    int sy = h - bottom_h + (bottom_h - 7 * status_scale) / 2;
    draw_text(fb, margin, sy, status, status_scale, c_dim);

    fb_present(fb);
}

int main(void)
{
    struct ui ui;
    int inputs[MAX_INPUTS];
    int input_count;
    int dirty = 1;
    time_t last_second = 0;

    memset(&ui, 0, sizeof(ui));
    ui.volume = 50;
    snprintf(ui.status, sizeof(ui.status), "READY");

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

    while (g_running) {
        if (dirty) {
            draw_scene(&ui);
            dirty = 0;
            last_second = time(NULL);
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        for (int i = 0; i < input_count; i++) {
            FD_SET(inputs[i], &rfds);
            if (inputs[i] > maxfd)
                maxfd = inputs[i];
        }

        struct timeval tv = { 1, 0 };
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);

        if (ready > 0) {
            for (int i = 0; i < input_count; i++) {
                if (!FD_ISSET(inputs[i], &rfds))
                    continue;

                struct input_event ev;
                ssize_t n;
                while ((n = read(inputs[i], &ev, sizeof(ev)))
                       == (ssize_t)sizeof(ev)) {
                    if (ev.type == EV_KEY && ev.value == 1)
                        handle_key(&ui, ev.code, &dirty);
                }
            }
        } else if (ready == 0) {
            time_t now = time(NULL);
            if (now != last_second)
                dirty = 1;
        }
    }

    fprintf(stderr, "owlcaros-ui: exiting\n");
    tty_text(ui.ttyfd);
    for (int i = 0; i < input_count; i++)
        close(inputs[i]);
    fb_close(&ui.fb);
    return 0;
}
