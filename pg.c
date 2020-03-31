#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <pg.h>

#define FLATNESS 1.01f
#define BEZ_LIMIT 7

#define new(t,...) memcpy(malloc(sizeof(t)), &(t) { __VA_ARGS__ }, sizeof(t))

typedef struct {
    float       *buf;
    int         stride;
    Rect        clip;
    Rect        dirty;
    CTM         ctm;
    Point       *tmp;
} BitmapBuf;

static Font     *themefont;
static float    themefontsz = 14.0f * 96 / 72;
// static Colour   themebg = {1, .975, .925, 1};
static Colour   themebg = {1, 1, 1, 1};
static Colour   themefg = {.2, .2, .2, 1};
static Colour   themeaccent = {1, .2, .5, 1};

static Box      *focus;


/*

    Untility.

*/


static inline Colour blend(Colour bg, Colour fg, float a) {
    if (a == 1)
        return fg;
    if (a == 0)
        return bg;

    float   na = 1 - a;
    float   r = fg.r * a + bg.r * na;
    float   g = fg.g * a + bg.g * na;
    float   b = fg.b * a + bg.b * na;
    return rgba(r, g, b, a);
}

static inline float clamp(float a, float b, float c) {
    return fmaxf(a, fminf(b, c));
}

static inline uint32_t blendinto(uint32_t bg, Colour fg, float a) {
    return packrgb(blend(unpackrgb(bg), fg, a));
}

static inline Point midpoint(Point a, Point b) {
    return pt((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}

static inline float distance(Point a, Point b) {
    float   dx = a.x - b.x;
    float   dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

static inline uint16_t pkw(const uint8_t *ptr) {    // Peek word (16-bit).
    return (ptr[0] << 8) + ptr[1];
}

static inline uint32_t pkd(const uint8_t *ptr) {    // Peek dword (32-bit).
    return (pkw(ptr) << 16) + pkw(ptr + 2);
}

static inline unsigned utf8tail(uint8_t **in, int tail, unsigned min) {
    unsigned c = *(*in)++;
    int     got;
    for (got = 0; (**in & 0xc0) == 0x80; got++)
        c = (c << 6) + (*(*in)++ & 0x3f);
    return got == tail && c >= min? c: 0xfffd;
}

unsigned pgfromutf8(uint8_t **in) {
    if (**in < 0x80)
        return *(*in)++;
    if (**in < 0xd0)
        return utf8tail(in, 1, 0x80) & 0x07ff;
    if (**in < 0xf0)
        return utf8tail(in, 2, 0x800) & 0xffff;
    return utf8tail(in, 3, 0x10000) & 0x10ffff;
}

uint8_t *pgtoutf8(uint8_t **out, unsigned c) {
    if (c < 0x80)
        *(*out)++ = c;
    else if (c < 0x0800) {
        *(*out)++ = 0xc0 + (c >> 6);
        *(*out)++ = 0x80 + (c & 0x3f);
    } else if (c < 0x10000) {
        *(*out)++ = 0xe0 + (c >> 12);
        *(*out)++ = 0x80 + (c >> 6 & 0x3f);
        *(*out)++ = 0x80 + (c & 0x3f);
    } else if (c < 0x10ffff) {
        *(*out)++ = 0xe0 + (c >> 18);
        *(*out)++ = 0x80 + (c >> 12 & 0x3f);
        *(*out)++ = 0x80 + (c >> 6 & 0x3f);
        *(*out)++ = 0x80 + (c & 0x3f);
    }
    return *out;
}


/*

    Canvas management.

*/


Canvas *pgsubcanvas(Canvas *g, int ax, int ay, int bx, int by) {
    if (g)
        return g->_->subcanvas(g, ax, ay, bx, by);
    return 0;
}

void *pgfree(Canvas *g) {
    if (g) {
        g->_->free(g);
        free(g);
    }
    return 0;
}

Canvas* pgclean(Canvas *g) {
    if (g)
        g->_->clean(g);
    return g;
}

Canvas* pgclose(Canvas *g) {
    if (g)
        g->_->close(g);
    return g;
}

Canvas* pgmove(Canvas *g, Point a) {
    if (g)
        g->_->move(g, a);
    return g;
}

Canvas* pgline(Canvas *g, Point b) {
    if (g)
        g->_->line(g, b);
    return g;
}

Canvas* pgcurve3(Canvas *g, Point b, Point c) {
    if (g)
        g->_->curve3(g, b, c);
    return g;
}

Canvas* pgcurve4(Canvas *g, Point b, Point c, Point d) {
    if (g)
        g->_->curve4(g, b, c, d);
    return g;
}

Canvas* pgclear(Canvas *g, Colour colour) {
    if (g)
        g->_->clear(g, colour);
    return g;
}

Canvas* pgfill(Canvas *g, Colour colour) {
    if (g) {
        g->_->fill(g, colour);
        pgclean(g);
    }
    return g;
}

Canvas* pgstroke(Canvas *g, float stroke, Colour colour) {
    if (g) {
        g->_->stroke(g, stroke, colour);
        pgclean(g);
    }
    return g;
}

Canvas* pgstrokefill(Canvas *g, float stroke, Colour cs, Colour cf) {
    if (g) {
        g->_->strokefill(g, stroke, cs, cf);
        pgclean(g);
    }
    return g;
}

Canvas* pgstrokeline(Canvas *g, float stroke, Colour colour, Point a, Point b) {
    if (g) {
        pgmove(g, a);
        pgline(g, b);
        pgstroke(g, stroke, colour);
    }
    return g;
}

Canvas* pgfillrect(Canvas *g, Colour colour, Rect r) {
    if (g) {
        pgmove(g, r.a);
        pgline(g, pt(r.bx, r.ay));
        pgline(g, r.b);
        pgline(g, pt(r.ax, r.by));
        pgclose(g);
        pgfill(g, colour);
    }
    return g;
}

Canvas* pgstrokerect(Canvas *g, float stroke, Colour colour, Rect r) {
    if (g) {
        pgstrokeline(g, stroke, colour, r.a, pt(r.bx, r.ay));
        pgstrokeline(g, stroke, colour, pt(r.bx, r.ay), r.b);
        pgstrokeline(g, stroke, colour, r.b, pt(r.ax, r.by));
        pgstrokeline(g, stroke, colour, pt(r.ax, r.by), r.a);
    }
    return g;
}

Canvas* pgctm(Canvas *g, CTM ctm) {
    if (g) {
        g->_->setctm(g, ctm);
        g->ctm = ctm;
    }
    return g;
}

Canvas *pgidentity(Canvas *g) {
    CTM m = { 1, 0, 0, 1, 0, 0 };
    return pgctm(g, pgmulctm(g->ctm, m));
}

Canvas *pgtranslate(Canvas *g, float x, float y) {
    CTM m = { 1, 0, 0, 1, x, y };
    return pgctm(g, pgmulctm(g->ctm, m));
}

Canvas *pgscale(Canvas *g, float x, float y) {
    CTM m = { x, 0, 0, y, 0, 0 };
    return pgctm(g, pgmulctm(g->ctm, m));
}

Canvas *pgrotate(Canvas *g, float rad) {
    float   sinx = sinf(rad);
    float   cosx = cosf(rad);
    CTM m = { cosx, sinx, -sinx, cosx, 0, 0 };
    return pgctm(g, pgmulctm(g->ctm, m));
}


/*

    Paths.

*/

static void addpoint(Path *path, Point p) {
    if (path->np + 1 >= path->capacity) {
        path->capacity = path->capacity? path->capacity * 2: 32;
        path->shapes = realloc(path->shapes, path->capacity);
        path->pts = realloc(path->pts, path->capacity * sizeof *path->pts);
    }
    path->pts[path->np++] = p;
}

Path *pgpath(int capacity) {
    return new(Path,
        .np = 0,
        .capacity = capacity,
        .homeindex = 0,
        .open = false,
        .shapes = malloc(capacity),
        .pts = malloc(capacity * sizeof(Point)));
}

Path *pgpmove(Path *path, Point a) {
    if (path) {
        path->open = true;
        path->homeindex = path->np;
        addpoint(path, a);
        path->shapes[path->np - 1] = 0;
    }
    return path;
}

Path *pgpline(Path *path, Point b) {
    if (path && path->open) {
        addpoint(path, b);
        path->shapes[path->np - 1] = 1;
    }
    return path;
}

Path *pgpcurve3(Path *path, Point b, Point c) {
    if (path && path->open) {
        addpoint(path, b);
        addpoint(path, c);
        path->shapes[path->np - 2] = 2;
    }
    return path;
}

Path *pgpcurve4(Path *path, Point b, Point c, Point d) {
    if (path && path->open) {
        addpoint(path, b);
        addpoint(path, c);
        addpoint(path, d);
        path->shapes[path->np - 3] = 3;
    }
    return path;
}

Path *pgpclose(Path *path) {
    if (path && path->open) {
        pgpline(path, path->pts[path->homeindex]);
        path->open = false;
    }
    return path;
}

Path *pgpclean(Path *path) {
    if (path) {
        path->np = 0;
        path->open = false;
    }
    return path;
}


/*

    Bitmap Canvas.

*/


static const CanvasMethods bitmapmethods;

static Canvas *
bmp_new(uint32_t *pixels, int stride, int width, int height) {
    bool    ownpixels = pixels == 0;
    if (ownpixels)
        pixels = calloc(stride * height, sizeof *pixels);
    if (!pixels)
        return 0;

    return new(Bitmap,
        {
            &bitmapmethods,
            width,
            height,

            {{ 0, 0, width, height }},
            { 1, 0, 0, 1, 0, 0 },
        },
        stride,
        pixels,
        ownpixels,
        pgpath(0),
    );
}

static Canvas *
bmp_subcanvas(Canvas *parent, int ax, int ay, int width, int height) {
    int     bx = ax + width;
    int     by = ay + height;
    bool    valid =
                ax >= 0 &&
                bx >= 0 &&
                ax <= bx &&
                bx <= parent->width &&
                ay >= 0 &&
                by >= 0 &&
                ay <= by &&
                by <= parent->height;
    if (!valid)
        return 0;

    Bitmap      *bmp = (Bitmap*) parent;
    uint32_t    *pixels = bmp->pixels + ay * bmp->stride + ax;
    return bmp_new(pixels, bmp->stride, width, height);
}

Canvas *pgborrowbmp(uint32_t *pixels, int stride, int width, int height) {
    return bmp_new(pixels, stride, width, height);
}

Canvas *pgnewbmp(int width, int height) {
    return bmp_new(0, width, width, height);
}

static Path *bmp_path(Canvas *g) {
    return ((Bitmap*) g)->path;
}

static IntRect bmp_dirtyrect(Rect dirty, Rect clip) {
    if (dirty.ax >= dirty.bx || dirty.ay >= dirty.by)
        return (IntRect) { 0, 0, 0, 0 };

    return (IntRect) {
        trunc(fmaxf(clip.ax, dirty.ax)),
        trunc(fmaxf(clip.ay, dirty.ay)),
        ceilf(fminf(clip.bx, dirty.bx)),
        ceilf(fminf(clip.by, dirty.by)),
    };
}

static void bmp_free(Canvas *g) {
    Bitmap  *bmp = (Bitmap *) g;
    if (bmp->ownpixels)
        free(bmp->pixels);
}

static void bmp_clean(Canvas *g) {
    pgpclean(bmp_path(g));
}

static void bmp_close(Canvas *g) {
    if (g && bmp_path(g)->open) {
        Path *path = bmp_path(g);
        pgpline(path, path->pts[path->homeindex]);
    }
}

static void bmp_move(Canvas *g, Point a) {
    pgpmove(bmp_path(g), a);
}

static void bmp_line(Canvas *g, Point b) {
    pgpline(bmp_path(g), b);
}

static void bmp_curve3(Canvas *g, Point b, Point c) {
    pgpcurve3(bmp_path(g), b, c);
}

static void bmp_curve4(Canvas *g, Point b, Point c, Point d) {
    pgpcurve4(bmp_path(g), b, c, d);
}

static void bmp_setctm(Canvas *g, CTM ctm) {
    (void) g;
    (void) ctm;
}

static inline void bmp_accum(
    IntRect     r,
    int         stride,
    Colour      colour,
    uint32_t * restrict p,
    float * restrict b)
{
    p += r.ay * stride;
    b += r.ay * stride;
    for (int y = r.ay; y < r.by; y++) {
        float   a = 0;
        for (int x = r.ax; x < r.bx; x++) {
            a += b[x];
            p[x] = blendinto(p[x], colour, fminf(fabsf(a), 1));
            b[x] = 0;
        }
        p += stride;
        b += stride;
    }
}

static void bmp_edge(BitmapBuf *g, Point a, Point b) {

    a = pgapplyctm(g->ctm, a);
    b = pgapplyctm(g->ctm, b);

    // Pixels are centred on (.5, .5) in screen co-ordinates.
    a.x += 0.5f;
    a.y += 0.5f;
    b.x += 0.5f;
    b.y += 0.5f;

    // Clip line iff no vertical points are on screen.
    if (fmaxf(a.y, b.y) < g->clip.ay)
        return;
    if (fminf(a.y, b.y) > g->clip.by)
        return;

    float   sign = 1;
    if (b.y < a.y) {
        Point   t = a;
        a = b;
        b = t;
        sign = -1;
    }

    g->dirty = (Rect) {{
        fminf(g->dirty.ax, fminf(a.x, b.x)),
        fminf(g->dirty.ay, fminf(a.y, b.y)),
        fmaxf(g->dirty.bx, fmaxf(a.x, b.x)),
        fmaxf(g->dirty.by, fmaxf(a.y, b.y)),
    }};

    float   x = a.x;
    float   dxdy = (b.x - a.x) / (b.y - a.y);
    float   maxy = fminf(ceilf(b.y), g->clip.by);
    float   miny = fmaxf(floorf(a.y), g->clip.ay);
    float   cax = g->clip.ax;
    float   cbx = truncf(g->clip.bx) - 1;
    if (miny != floorf(a.y))
        x += dxdy * (miny - floorf(a.y));

    for (float y = miny; y < maxy; y++) {
        float * restrict buf = g->buf + (int) y * g->stride;
        float   dy = fminf(b.y, y + 1) - fmaxf(a.y, y);
        float   nextx = x + dxdy * dy;
        float   lx = fminf(x, nextx);
        float   rx = fmaxf(x, nextx);
        x = nextx;

        if (floorf(lx) == floorf(rx)) {
            float   fx = floorf(lx) + 1;
            float   area = 0.5f * ((fx - lx) + (fx - rx)) * dy;
            buf[(int) clamp(cax, lx, cbx)] += sign * area;
            buf[(int) clamp(cax, lx + 1, cbx)] += sign * (dy - area);
        }
        else {
            float   dydx = dy / (rx - lx);
            float   mx = floorf(lx) + 1;
            float   my = (mx - lx) * dydx;
            float   larea = 0.5f * (mx - lx) * my;
            float   shade = my;
            float   fx = floorf(rx);

            buf[(int) clamp(cax, lx, cbx)] += sign * larea;
            buf[(int) clamp(cax, mx, cbx)] += sign * (shade - larea);
            for (int x = clamp(cax, mx, cbx); x < clamp(cax, fx, cbx); x++) {
                buf[(int) x] += sign * dydx;
                shade += dydx;
            }
            buf[(int) clamp(cax, fx, cbx)] += sign * (dy - shade);
        }
    }
}

static int
flatten3(Point *out, Point a, Point b, Point c, int lim) {
    float   dcontrol = distance(a, b) + distance(b, c);
    float   dstraight = distance(a, c);
    bool    flat = dcontrol < dstraight * FLATNESS;

    if (lim == 0 || flat) {
        *out = c;
        return 1;
    }
    else {
        Point   ab = midpoint(a, b);
        Point   bc = midpoint(b, c);
        Point   abc = midpoint(ab, bc);
        int     n = flatten3(out, a, ab, abc, lim - 1);
        return n + flatten3(out + n, abc, bc, c, lim - 1);
    }
}

static int
flatten4(Point *out, Point a, Point b, Point c, Point d, int lim) {
    float   dcontrol = distance(a, b) + distance(b, c) + distance(c, d);
    float   dstraight = distance(a, d);
    bool    flat = dcontrol < dstraight * FLATNESS;

    if (lim == 0 || flat) {
        *out = d;
        return 1;
    }
    else {
        Point   ab = midpoint(a, b);
        Point   bc = midpoint(b, c);
        Point   cd = midpoint(c, d);
        Point   abc = midpoint(ab, bc);
        Point   bcd = midpoint(bc, cd);
        Point   abcd = midpoint(abc, bcd);
        int     n = flatten4(out, a, ab, abc, abcd, lim - 1);
        return n + flatten4(out + n, abcd, bcd, cd, d, lim - 1);
    }
}

static IntRect bmp_trace(BitmapBuf *g, Path *path) {
    Point   cur;
    int     n;

    for (int i = 0; i < path->np; cur = path->pts[i - 1])
        switch (path->shapes[i]) {
        case 0: // Move.
            i++;
            break;
        case 1: // Line.
            bmp_edge(g, cur, path->pts[i]);
            i++;
            break;
        case 2: // Curve3
            n = flatten3(g->tmp, cur, path->pts[i], path->pts[i + 1],
                BEZ_LIMIT);
            for (int i = 0; i < n; i++) {
                bmp_edge(g, cur, g->tmp[i]);
                cur = g->tmp[i];
            }
            i += 2;
            break;
        case 3: // Curve4
            n = flatten4(g->tmp, cur, path->pts[i], path->pts[i + 1],
                path->pts[i + 2], BEZ_LIMIT);
            for (int i = 0; i < n; i++) {
                bmp_edge(g, cur, g->tmp[i]);
                cur = g->tmp[i];
            }
            i += 3;
            break;
        }

    return bmp_dirtyrect(g->dirty, g->clip);
}

static void bmp_thick(BitmapBuf *g, float stroke, Point p0, Point p1) {
    float   dx = p1.x - p0.x;
    float   dy = p1.y - p0.y;
    float   scale =  stroke * 0.5f / sqrtf(dx * dx + dy * dy);
    Point   n = pt(-dy * scale, dx * scale);
    Point   a = pgsubpt(p0, n);
    Point   b = pgaddpt(p0, n);
    Point   c = pgsubpt(p1, n);
    Point   d = pgaddpt(p1, n);
    bmp_edge(g, c, a);
    bmp_edge(g, b, d);
    bmp_edge(g, a, b);
    bmp_edge(g, d, c);
}

static IntRect bmp_tracelines(BitmapBuf *g, float stroke, Path *path) {
    Point   cur;
    int     n;

    for (int i = 0; i < path->np; cur = path->pts[i - 1])
        switch (path->shapes[i]) {
        case 0: // Move.
            i++;
            break;
        case 1: // Line.
            (void) stroke;
            bmp_thick(g, stroke, cur, path->pts[i]);
            i++;
            break;
        case 2: // Curve3
            n = flatten3(g->tmp, cur, path->pts[i], path->pts[i + 1],
                BEZ_LIMIT);
            for (int i = 0; i < n; i++) {
                bmp_thick(g, stroke, cur, g->tmp[i]);
                cur = g->tmp[i];
            }
            i += 2;
            break;
        case 3: // Curve4
            n = flatten4(g->tmp, cur, path->pts[i], path->pts[i + 1],
                path->pts[i + 2], BEZ_LIMIT);
            for (int i = 0; i < n; i++) {
                bmp_thick(g, stroke, cur, g->tmp[i]);
                cur = g->tmp[i];
            }
            i += 3;
            break;
        }

    return bmp_dirtyrect(g->dirty, g->clip);
}

static inline BitmapBuf initbitmapbuf(Bitmap *bmp, Point *tmp) {
    return (BitmapBuf) {
        .buf = calloc(bmp->stride * bmp->g.height, sizeof(float)),
        .stride = bmp->stride,
        .clip = bmp->g.clip,
        .dirty = {{ bmp->g.width, bmp->g.height, 0, 0 }},
        .ctm = bmp->g.ctm,
        .tmp = tmp,
    };
}

static void bmp_fill(Canvas *g, Colour colour) {
    Bitmap      *bmp = (Bitmap*) g;
    Point       tmp[1 << BEZ_LIMIT];
    BitmapBuf   buf = initbitmapbuf(bmp, tmp);
    IntRect     r = bmp_trace(&buf, bmp->path);
    bmp_accum(r, bmp->stride, colour, bmp->pixels, buf.buf);
    free(buf.buf);
}

static void bmp_stroke(Canvas *g, float stroke, Colour colour) {
    Bitmap      *bmp = (Bitmap*) g;
    Point       tmp[1 << BEZ_LIMIT];
    BitmapBuf   buf = initbitmapbuf(bmp, tmp);
    IntRect     r = bmp_tracelines(&buf, stroke, bmp->path);
    bmp_accum(r, bmp->stride, colour, bmp->pixels, buf.buf);
    free(buf.buf);
}

static void bmp_strokefill(Canvas *g, float stroke, Colour cs, Colour cf) {
    bmp_fill(g, cf);
    bmp_stroke(g, stroke, cs);
}

static void bmp_clear(Canvas *g, Colour colour) {
    Bitmap      *bmp = (Bitmap *) g;
    IntRect     r = {
                    truncf(bmp->g.clip.ax),
                    truncf(bmp->g.clip.ay),
                    ceilf(bmp->g.clip.bx),
                    ceilf(bmp->g.clip.by)
                };
    uint32_t * restrict p = bmp->pixels + r.ay * bmp->stride;
    uint32_t    c = packrgb(colour);
    for (int y = r.ay; y < r.by; y++) {
        for (int x = r.ax; x < r.bx; x++)
            p[x] = c;
        p += bmp->stride;
    }
}

static const CanvasMethods bitmapmethods = {
    bmp_free,
    bmp_subcanvas,
    bmp_setctm,
    bmp_clear,
    bmp_clean,
    bmp_close,
    bmp_move,
    bmp_line,
    bmp_curve3,
    bmp_curve4,
    bmp_fill,
    bmp_stroke,
    bmp_strokefill,
};


/*

    Fonts.

*/


static Font *otf_openfont(void * restrict data, size_t size, int index);

Font *pgfontfile(const char *file, int index) {
    struct stat stat;
    int         fd = open(file, O_RDONLY);
    int         err = fstat(fd, &stat);
    if (fd < 0 || err)
        return 0;

    void *data = mmap(0, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED)
        return 0;
    return otf_openfont(data, stat.st_size, index);
}

void pgfreefont(Font *font) {
    if (font) {
        font->_->free(font);
        munmap(font->data, font->datasize);
    }
}

Font *pgfontctm(Font *font, CTM ctm) {
    if (font) {
        font->_->setctm(font, ctm);
        font->ctm = ctm;
    }
    return font;
}

Font *pgscalefont(Font *font, float xpx, float ypx) {
    if (xpx == 0)
        xpx = ypx;
    if (ypx == 0)
        ypx = xpx;

    if (font && font->em && xpx && ypx) {
        CTM ctm = { xpx / font->em, 0, 0, ypx / font->em, 0, 0 };
        pgfontctm(font, ctm);
    }
    return font;
}

Point pgglyph(Canvas *g, Font *font, Point p, unsigned glyph) {
    if (g && font && glyph < font->nglyphs) {
        font->_->glyph(g, font, p, glyph);
        return pgglyphadvance(font, p, glyph);
    }
    return p;
}


Point pgchar(Canvas *g, Font *font, Point p, unsigned c) {
    if (g && font && c < 65536)
        return pgglyph(g, font, p, font->mapping[c & 0xffff]);
    return p;
}

Point pgstring(Canvas *g, Font *font, Point p, const char *str) {
    if (g && font && str) {
        for (uint8_t *s = (uint8_t*) str; *s; )
            p = pgchar(g, font, p, pgfromutf8(&s));
    }
    return p;
}

Point pgvprintf(Canvas *g, Font *font, Point p, const char *fmt, va_list ap) {
    if (g && font && fmt) {
        char        small[1024];
        va_list     tmp;
        va_copy(tmp, ap);
        size_t      n = snprintf(0, 0, fmt, tmp) + 1;
        va_end(tmp);
        char        *buf = n < sizeof small? small: malloc(n);
        snprintf(buf, n, fmt, ap);
        Point       nextp = pgstring(g, font, p, buf);
        if (buf != small)
            free(buf);
        return nextp;
    }
    return p;
}

Point pgprintf(Canvas *g, Font *font, Point p, const char *fmt, ...) {
    if (g && font && fmt) {
        va_list     ap;
        va_start(ap, fmt);
        Point       nextp = pgvprintf(g, font, p, fmt, ap);
        va_end(ap);
        return nextp;
    }
    return p;
}

Point pgglyphadvance(Font *font, Point p, unsigned glyph) {
    if (!font || glyph >= font->nglyphs)
        return p;

    OpenTypeFont    *otf = (OpenTypeFont*) font;
    int             index = glyph >= otf->nhmetrics? otf->nhmetrics - 1: glyph;
    float           adv = pkw(otf->hmtx + index * 4);
    Point           d = pgapplyctm(font->ctm, pt(adv, 0));
    return pt(p.x + d.x, p.y + d.y);
}

Point pgcharadvance(Font *font, Point p, unsigned c) {
    if (font)
        return pgglyphadvance(font, p, font->mapping[c & 0xffff]);
    return p;
}

Point pgmeasure(Font *font, const char *text) {
    Point   p = { 0, 0 };
    for (uint8_t *s = (uint8_t*) text; *s; )
        p = pgcharadvance(font, p, pgfromutf8(&s));

    Point   vert = pgapplyctm(font->ctm, pt(0, font->em));
    return pt(p.x + vert.x, p.y + vert.y);
}


/*

    OpenType Fonts.

*/


#define c4(a,b,c,d) ((a << 24) + (b << 16) + (c << 8) + d)

void otf_free(Font *font) {
    (void) font;
}

void otf_setcm(Font *font, CTM ctm) {
    (void) font;
    (void) ctm;
}

void otf_glyph(Canvas *g, Font *font, Point p, unsigned glyph) {
    OpenTypeFont    *otf = (OpenTypeFont*) font;

    CTM         ctm = {1, 0, 0, -1, 0, font->ascent + font->descent };
    ctm = pgmulctm(ctm, font->ctm);
    ctm.e += p.x;       // Canvas co-ordinates; not scaled.
    ctm.f += p.y;       // Canvas co-ordinates; not scaled.

    unsigned    offset = otf->longloca
                        ? pkd(otf->loca + glyph * 4)
                        : pkw(otf->loca + glyph * 2) * 2;
    unsigned    next =  otf->longloca
                        ? pkd(otf->loca + (glyph + 1) * 4)
                        : pkw(otf->loca + (glyph + 1) * 2) * 2;
    uint8_t * restrict ptr = otf->glyf + offset;
    int         ncontours = offset == next
                        ? 0
                        : (int16_t) pkw(ptr);
    ptr += 10;

    if (ncontours == 0) {
    }
    else if (ncontours > 0) {
        uint8_t     *ends = ptr;
        unsigned    instrlen = pkw(ends + ncontours * 2);
        uint8_t     *flags = ends + ncontours * 2 + 2 + instrlen;
        unsigned    npoints = pkw(ends + (ncontours - 1) * 2) + 1;

        // Determine the length of the flag and x arrays.
        uint8_t     *fp = flags;
        unsigned    xsize = 0;
        for (unsigned i = 0; i < npoints; ) {
            uint8_t     f = *fp++;
            unsigned    n = f & 0x08? *fp++ + 1: 1;
            unsigned    xs =    f & 0x02? 1:    // x is short.
                                f & 0x10? 0:    // x is same.
                                2;
            xsize += xs * n;
            i += n;
        }

        // Output glyph data.
        uint8_t     *endp = ends;
        unsigned    next = 0;
        uint8_t     *xp = fp;
        uint8_t     *yp = xp + xsize;
        Point       home = {0, 0};
        Point       oldp = {0, 0};
        Point       p = {0, 0};
        float       funitx = 0;
        float       funity = 0;
        bool        curving = false;
        for (unsigned i = 0; i < npoints; ) {
            uint8_t     f = *flags++;
            unsigned    n = f & 0x08? *flags++ + 1: 1;

            // Flag may be repeated.
            for ( ; n-- > 0; i++) {
                int     dx =    f & 0x02? (f & 0x10? *xp++: -*xp++):
                                f & 0x10? 0:
                                (xp += 2, (int16_t) pkw(xp - 2));
                int     dy =    f & 0x04? (f & 0x20? *yp++: -*yp++):
                                f & 0x20? 0:
                                (yp += 2, (int16_t) pkw(yp - 2));
                bool    startscontour = i == next;
                bool    controlpoint = f & 1;

                oldp = p;
                funitx += dx;
                funity += dy;
                p = pgapplyctm(ctm, pt(funitx, funity));

                if (startscontour) {                // Start of contour.
                    next = pkw(endp) + 1;
                    endp += 2;

                    if (curving)
                        pgcurve3(g, oldp, home);
                    pgclose(g);
                    pgmove(g, p);
                    home = p;
                    curving = false;
                }
                else if (controlpoint && curving) { // Curve-to-line.
                    pgcurve3(g, oldp, p);
                    curving = false;
                }
                else if (controlpoint)              // Line-to-line.
                    pgline(g, p);
                else if (curving) {                 // Curve-to-curve.
                    Point   m = midpoint(oldp, p);
                    pgcurve3(g, oldp, m);
                } else                              // Line-to-curve.
                    curving = true;
            }
        }

        // Finish the contour as if a line point were specified.
        if (npoints != 0) {
            if (curving)
                pgcurve3(g, p, home);
            pgclose(g);
        }
    } else {

    }

}

static const FontMethods otfmethods = {
    otf_free,
    otf_setcm,
    otf_glyph,
};

static Font *otf_openfont(void * restrict data, size_t size, int index) {
    (void) index;

    // The tables we need.
    uint8_t     *cmap = 0;
    uint8_t     *glyf = 0;
    uint8_t     *head = 0;
    uint8_t     *hhea = 0;
    uint8_t     *loca = 0;
    uint8_t     *maxp = 0;
    uint8_t     *hmtx = 0;

    // The values we get from them.
    float       ascent = 0;
    float       descent = 0;
    uint16_t    *mapping = 0;
    int         nglyphs = 0;
    float       em = 0;
    bool        longloca = false;
    int         nhmetrics = 0;
    uint32_t    cmapsize = 0;
    uint32_t    hmtxsize = 0;

    // sfnt file header.
    uint8_t     *ptr = data;
    uint32_t    signature = pkd(ptr);
    int         ntables = pkw(ptr + 4);
    ptr += 12;
    if (signature != 0x10000 && signature != c4('t','r','u','e'))
        goto fail;

    // Scan list of tables and store the ones we need.
    for (int i = 0; i < ntables; i++) {
        uint32_t    tag = pkd(ptr);
        uint32_t    offset = pkd(ptr + 8);
        uint32_t    length = pkd(ptr + 12);
        uint8_t     *at = (uint8_t*) data + pkd(ptr + 8);
        ptr += 16;

        if (offset > size || size - offset < length)
            goto fail;

        switch (tag) {
        case c4('c','m','a','p'):
            cmap = at;
            cmapsize = length;
            break;
        case c4('g','l','y','f'):
            glyf = at;
            break;
        case c4('h','e','a','d'):
            if (length != 54)
                goto fail;
            head = at;
            break;
        case c4('h','h','e','a'):
            if (length != 36)
                goto fail;
            hhea = at;
            break;
        case c4('h','m','t','x'):
            hmtx = at;
            hmtxsize = length;
            break;
        case c4('l','o','c','a'):
            loca = at;
            break;
        case c4('m','a','x','p'):
            if (pkd(at) == 0x10000 && length != 32)
                goto fail;
            if (pkd(at) == 0x5000 && length != 6)
                goto fail;
            maxp = at;
            break;
        }
    }

    if (!cmap || !glyf || !head || !hhea || !hmtx || !loca || !maxp)
        goto fail;

    // maxp table.
    if (pkd(maxp) != 0x10000 && pkd(maxp) != 0x5000)
        goto fail;
    nglyphs = pkw(maxp + 4);
    if (nglyphs >= 65536)
        goto fail;

    // head table.
    if (pkd(head) != 0x10000 || pkd(head + 12) != 0x5F0F3CF5)
        goto fail;
    if (pkw(head + 50) > 1)
        goto fail;
    em = pkw(head + 18);
    longloca = pkw(head + 50);

    // hhea table.
    ascent = pkw(hhea + 2);
    descent = pkw(hhea + 4);
    nhmetrics = pkw(hhea + 34);
    if (nhmetrics > nglyphs)
        goto fail;

    // hmtx table.
    if ((int) hmtxsize < nhmetrics * 4 + (nglyphs - nhmetrics) * 2)
        goto fail;

    // cmap table.
    if (pkw(cmap) != 0) // Version.
        goto fail;

    ptr = cmap + 4;
    for (unsigned n = pkw(cmap + 2), i = 0; i < n; i++) {
        uint16_t    platform = pkw(ptr);
        uint16_t    encoding = pkw(ptr + 2);
        uint32_t    offset = pkd(ptr + 4);
        ptr += 8;

        if (offset > cmapsize)
            goto fail;

        // Windows or Unicode / Unicode 2.0 (BMP-only)
        bool unicode =
            (platform == 0 && encoding == 3) ||
            (platform == 3 && encoding == 1);
        if (!unicode)
            continue;

        ptr = cmap + offset;
        if (pkw(ptr) != 4)
            continue;

        mapping = calloc(65536, sizeof *mapping);

        unsigned n2 = pkw(ptr + 6);
        uint8_t     *ends = ptr + 14;
        uint8_t     *starts = ends + n2 + 2;
        uint8_t     *deltas = starts + n2;
        uint8_t     *offsets = deltas + n2;
        for (unsigned i = 0; i < n2; i += 2) {
            unsigned    end = pkw(ends + i);
            unsigned    start = pkw(starts + i);
            unsigned    delta = pkw(deltas + i);
            unsigned    offset = pkw(offsets + i);
            if (offset)
                for (unsigned c = start; c <= end; c++) {
                    uint16_t index = i / 2 + offset / 2 + (c - start);
                    if (index > cmapsize - 2)
                        goto fail;
                    unsigned g = pkw(offsets + index * 2);
                    mapping[c] = g? (delta + g) & 0xffff: 0;
                }
            else
                for (unsigned c = start; c <= end; c++)
                    mapping[c] = (delta + c) & 0xffff;
        }
        break;
    }

    if (!mapping)
        goto fail;

    return new(OpenTypeFont,
        {
            &otfmethods,

            data,
            size,

            {1, 0, 0, 1, 0, 0},

            em,
            ascent,
            descent,
            nglyphs,
            mapping,
        },
        cmap,
        glyf,
        loca,
        hmtx,
        longloca,
        nhmetrics);
fail:
    free(mapping);
    return 0;
}


/*

    Boxes.

*/


static IntRect boxrect(Box *box) {
    int     x = box->x;
    int     y = box->y;
    for (Box *i = box->parent; i; i = i->parent) {
        x += i->x;
        y += i->y;
    }
    return (IntRect) { x, y, x + box->width, y + box->height };
}

Font *pgthemefont() {
    if (!themefont) {
        themefont = pgfontfile("/usr/share/fonts/TTF/georgia.ttf", 0);
        pgscalefont(themefont, themefontsz, 0);
    }
    return themefont;
}

Colour pgthemefg() {
    return themefg;
}

Colour pgthemebg() {
    return themebg;
}

Colour pgthemeaccent() {
    return themeaccent;
}


Box *pglocate(Box *box, int x, int y) {
    if (box)
        for (Box *i = box->children; i; i = i->next) {
            bool within =
                i->x <= x &&
                i->y <= y &&
                x <= i->x + i->width &&
                y <= i->y + i->height;
            if (within)
                return pglocate(i, x - i->x, y - i->y);
        }
    return box;
}

Canvas *pgboxsubcanvas(Canvas *g, Box *box) {
    if (g && box) {
        IntRect     r = boxrect(box);
        return pgsubcanvas(g, r.ax, r.ay, r.bx - r.ax, r.by - r.ay);
    }
    return 0;
}

void pgfocus(Box *box) {
    if (box)
        focus = box;
}

Box *pggetfocus() {
    return focus;
}

void pgaddbox(Box *parent, Box *child) {
    if (parent && child) {
        pgremovebox(child);

        Box     **p = &parent->children;
        while (*p)
            p = &(*p)->next;
        *p = child;

        child->parent = parent;
    }
}

void pgremovebox(Box *child) {
    if (child && child->parent) {

        if (child == pggetfocus())
            pgfocus(child->parent);

        Box     **p = &child->parent->children;
        while (*p && *p != child)
            p = &(*p)->next;

        if (*p)
            *p = (*p)->next;

        child->parent = 0;
    }
}

void pgboxclicked(Box *box, int x, int y) {
    if (box) {
        if (box->_->clicked)
            box->_->clicked(box, x, y);
        else if (box->parent)
            pgboxclicked(box->parent, x, y);
    }
}

void pgboxkey(Box *box, unsigned code, unsigned mod) {
    if (box) {
        if (box->_->key)
            box->_->key(box, code, mod);
        else if (box->parent)
            pgboxkey(box->parent, code, mod);
    }
}

void pgboxchars(Box *box, char *text) {
    if (box) {
        if (box->_->key)
            box->_->chars(box, text);
        else if (box->parent)
            pgboxchars(box->parent, text);
    }
}

void pgdrawbox(Canvas *g, Box *box) {
    if (g && box) {
        if (box->_->draw && !box->clean) {
            Canvas *sub = pgboxsubcanvas(g, box);
            box->clean = true;
            box->_->draw(box, sub);
            pgfree(sub);
        }

        for (Box *i = box->children; i; i = i->next)
            pgdrawbox(g, i);
    }
}

void pgpack(Box *box) {
    if (box) {
        box->clean = false;
        if (box->_->pack)
            box->_->pack(box);
    }
}


static void horizpack(Box *box) {
    if (box) {
        int     remaining = box->width;
        int     n = 0;
        int     x = 0;
        for (Box *i = box->children; i; i = i->next) {
            if (i->fixed)
                remaining -= i->height;
            else
                n++;
        }
        for (Box *i = box->children; i; i = i->next) {
            i->x = x;
            i->y = 0;
            i->height = box->height;
            if (!i->fixed)
                i->width = remaining / n;
            x += i->width;
            pgpack(i);
        }
    }
}

static void vertpack(Box *box) {
    if (box) {
        int     remaining = box->height;
        int     n = 0;
        int     y = 0;
        for (Box *i = box->children; i; i = i->next) {
            if (i->fixed)
                remaining -= i->height;
            else
                n++;
        }
        for (Box *i = box->children; i; i = i->next) {
            i->x = 0;
            i->y = y;
            i->width = box->width;
            if (!i->fixed)
                i->height = remaining / n;
            y += i->height;
            pgpack(i);
        }
    }
}

static void label_draw(Box *box, Canvas *g) {
    if ((void*) box->sys) {
        Font        *font = pgthemefont();
        char        *text = (char*) box->sys;
        Point       sz = pgmeasure(font, text);
        Point       at = pt(box->width / 2 - sz.x / 2,
                            box->height / 2 - sz.y / 2);

        pgclear(g, themebg);
        pgstring(g, font, at, text);
        pgfill(g, themefg);
    }
}

Box *pgstackbox(bool horizontal) {
    return horizontal
        ? new(Box, ._ = &pgbox_horizstack)
        : new(Box, ._ = &pgbox_vertstack);
}

Box *pglabel(char *text) {
    return new(Box,
        ._ = &pgbox_label,
        .sys = (uintptr_t) text);
}

Box *pgbutton(char *text) {
    return new(Box,
        ._ = &pgbox_button,
        .sys = (uintptr_t) text);
}

TextBoxData *pgtextboxdata(char *text) {
    if (!text)
        text = "";

    int     len = strlen(text);
    int     max = len > 256? len: 256;
    char    *buf = strcpy(malloc(max + 1), text);
    return new(TextBoxData, buf, len, max);
}

static void textbox_draw(Box *box, Canvas *g) {
    if ((void*) box->sys) {
        Font        *font = pgthemefont();
        TextBoxData *data = (TextBoxData*) box->sys;
        char        *text = data->buf;
        Point       p = pt(0, 0);

        pgclear(g, themebg);

        p = pgmeasure(font, "i");
        p.y = g->height * 0.5f - p.y * 0.5f;

        pgstring(g, font, p, text);
        pgfill(g, themefg);

        for (int i = 0; i < data->caret; i++)
            p = pgcharadvance(font, p, text[i]);
        pgstrokeline(g, 2, themeaccent, p, pt(p.x, p.y + themefontsz));

        pgstrokerect(g, 1, themefg,
            (Rect) {{
                0.5f,
                0.5f,
                box->width - 1.5f,
                box->height - 1.5f
            }});
    }
}

static void textbox_delete(TextBoxData *data) {
    int             len = strlen(data->buf);
    memmove(
        data->buf + data->caret,
        data->buf + data->caret + 1,
        len - data->caret);
}

static void textbox_key(Box *box, unsigned code, unsigned mod) {
    TextBoxData *data = (TextBoxData*) box->sys;

    switch (code) {

    case 0x04:  // ^A
        if (mod & 0x11) {
            data->caret = 0;
            box->clean = false;
        }
        break;

    case 0x08:  // ^E
        if (mod & 0x11) {
            data->caret = strlen(data->buf);
            box->clean = false;
        }
        break;

    case 0x2a:  // Backspace.
        if (data->caret > 0) {
            data->caret--;
            textbox_delete(data);
            box->clean = false;
        }
        break;

    case 0x4a:  // Home.
        data->caret = 0;
        box->clean = false;
        break;

    case 0x4c:  // Delete.
        textbox_delete(data);
        box->clean = false;
        break;

    case 0x4d:  // End.
        data->caret = strlen(data->buf);
        box->clean = false;
        break;

    case 0x4f:  // Right.
        if (data->caret < (int) strlen(data->buf))
            data->caret++;
        box->clean = false;
        break;

    case 0x50:  // Left.
        if (data->caret > 0)
            data->caret--;
        box->clean = false;
        break;
    default:
        pgboxkey(box->parent, code, mod);
    }


}

static void textbox_chars(Box *box, char *text) {
    TextBoxData     *data = (TextBoxData*) box->sys;
    int             len = strlen(data->buf);
    memmove(
        data->buf + data->caret + 1,
        data->buf + data->caret,
        len - data->caret);
    data->buf[data->caret++] = *text;
    box->clean = false;
}

static void textbox_clicked(Box *box, int x, int y) {
    (void) x;
    (void) y;
    pgfocus(box);
}

Box *pgtextbox(TextBoxData *data) {
    return new(Box,
        &pgbox_textbox,
        .sys = (uintptr_t) (data? data: pgtextboxdata(0)));
}

static void button_draw(Box *box, Canvas *g) {
    if ((void*) box->sys) {
        Font        *font = pgthemefont();
        char        *text = (char*) box->sys;
        Point       sz = pgmeasure(font, text);
        Point       at = pt(box->width / 2 - sz.x / 2,
                            box->height / 2 - sz.y / 2);

        pgclear(g, themebg);
        pgstring(g, font, at, text);
        pgfill(g, pgthemeaccent());
        pgstrokerect(g, 2, themefg,
            (Rect) {{
                1.5f,
                1.5f,
                box->width - 2.5f,
                box->height - 2.5f
            }});
    }
}


/*

    Box Defaults.

*/

const BoxMethods pgbox_default = {

};

const BoxMethods pgbox_horizstack = {
    .pack = horizpack,
};

const BoxMethods pgbox_vertstack = {
    .pack = vertpack,
};

const BoxMethods pgbox_label = {
    .draw = label_draw,
};

const BoxMethods pgbox_textbox = {
    .chars = textbox_chars,
    .key = textbox_key,
    .clicked = textbox_clicked,
    .draw = textbox_draw
};

const BoxMethods pgbox_button = {
    .draw = button_draw
};
