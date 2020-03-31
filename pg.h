#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct  Canvas          Canvas;
typedef struct  Point           Point;
typedef union   Rect            Rect;
typedef struct  CTM             CTM;
typedef struct  Colour          Colour;
typedef struct  Path            Path;
typedef struct  Font            Font;
typedef struct  Box             Box;
typedef struct  IntRect         IntRect;
typedef struct  Bitmap          Bitmap;
typedef struct  OpenTypeFont    OpenTypeFont;
typedef struct  TextBoxData     TextBoxData;

struct Colour {
    float       r;
    float       g;
    float       b;
    float       a;
};

struct Point {
    float       x;
    float       y;
};

union Rect {
    struct { float ax, ay, bx, by; };
    struct { Point a, b; };
};

struct CTM {
    float       a;
    float       b;
    float       c;
    float       d;
    float       e;
    float       f;
};

struct Path {
    int         np;
    int         capacity;
    int         homeindex;  // First point of current subpath.
    bool        open;
    uint8_t     *shapes;    // 0=Move, 1=Line, 2=Curve3, 3=Curve4
    Point       *pts;
};

typedef struct CanvasMethods {
    void        (*free)(Canvas *g);
    Canvas      *(*subcanvas)(Canvas *parent, int ax, int ay, int bx, int by);
    void        (*setctm)(Canvas *g, CTM ctm);
    void        (*clear)(Canvas *g, Colour colour);
    void        (*clean)(Canvas *g);
    void        (*close)(Canvas *g);
    void        (*move)(Canvas *g, Point a);
    void        (*line)(Canvas *g, Point b);
    void        (*curve3)(Canvas *g, Point b, Point c);
    void        (*curve4)(Canvas *g, Point b, Point c, Point d);
    void        (*fill)(Canvas *g, Colour colour);
    void        (*stroke)(Canvas *g, float stroke, Colour colour);
    void        (*strokefill)(Canvas *g, float stroke, Colour cs, Colour cf);
} CanvasMethods;

struct Canvas {
    const CanvasMethods *_;
    int         width;
    int         height;

    Rect        clip;
    CTM         ctm;
};

struct Bitmap {
    Canvas      g;
    int         stride;
    uint32_t    *pixels;
    bool        ownpixels;
    Path        *path;
};

typedef struct FontMethods {
    void        (*free)(Font *font);
    void        (*setctm)(Font *font, CTM ctm);
    void        (*glyph)(Canvas *g, Font *font, Point p, unsigned glyph);
} FontMethods;

struct Font {
    const FontMethods *_;
    void        *data;
    size_t      datasize;

    CTM         ctm;

    float       em;
    float       ascent;
    float       descent;
    unsigned    nglyphs;
    uint16_t    *mapping;
};

struct OpenTypeFont {
    Font        f;
    void        *cmap;
    void        *glyf;
    void        *loca;
    void        *hmtx;
    bool        longloca;
    unsigned    nhmetrics;
};

typedef struct BoxMethods {
    void        (*key)(Box *box, unsigned code, unsigned mod);
    void        (*chars)(Box *box, char *text);
    void        (*clicked)(Box *box, int x, int y);
    void        (*draw)(Box *box, Canvas *g);
    void        (*pack)(Box *box);
} BoxMethods;

struct Box {
    const BoxMethods  *_;

    int         x;
    int         y;
    int         width;
    int         height;
    bool        fixed;

    bool        clean;

    Box         *parent;
    Box         *next;
    Box         *children;

    uintptr_t   user;
    uintptr_t   sys;
};

struct IntRect {
    int         ax;
    int         ay;
    int         bx;
    int         by;
};

struct TextBoxData {
    char        *buf;
    int         caret;
    int         max;
};


/*
    Canvas management.
*/
Canvas *pgnewbmp(int width, int height);
Canvas *pgborrowbmp(uint32_t *pixels, int stride, int width, int height);

Canvas *pgsubcanvas(Canvas *parent, int ax, int ay, int width, int height);

void *pgfree(Canvas *g);


/*
    Drawing.
*/
Canvas *pgclean(Canvas *g);
Canvas *pgclose(Canvas *g);
Canvas *pgmove(Canvas *g, Point p);
Canvas *pgline(Canvas *g, Point b);
Canvas *pgcurve3(Canvas *g, Point b, Point c);
Canvas *pgcurve4(Canvas *g, Point b, Point c, Point d);

Canvas *pgclear(Canvas *g, Colour colour);
Canvas *pgfill(Canvas *g, Colour colour);
Canvas *pgstroke(Canvas *g, float stroke, Colour colour);
Canvas *pgstrokefill(Canvas *g, float stroke, Colour cs, Colour cf);

Canvas *pgstrokeline(Canvas *g, float stroke, Colour colour, Point a, Point b);
Canvas *pgfillrect(Canvas *g, Colour colour, Rect r);
Canvas *pgstrokerect(Canvas *g, float stroke, Colour colour, Rect r);

Point pgchar(Canvas *g, Font *font, Point p, unsigned c);
Point pgstring(Canvas *g, Font *font, Point p, const char *str);
Point pgvprintf(Canvas *g, Font *font, Point p, const char *fmt, va_list ap);
Point pgprintf(Canvas *g, Font *font, Point p, const char *fmt, ...);
Point pgglyph(Canvas *g, Font *font, Point p, unsigned glyph);

Canvas *pgctm(Canvas *g, CTM ctm);
Canvas *pgidentity(Canvas *g);
Canvas *pgtranslate(Canvas *g, float x, float y);
Canvas *pgscale(Canvas *g, float x, float y);
Canvas *pgrotate(Canvas *g, float rad);


/*
    Fonts.
*/
Font *pgfontfile(const char *file, int index);
void pgfreefont(Font *font);
Font *pgfontctm(Font *font, CTM ctm);
Font *pgscalefont(Font *font, float xpx, float ypx);
Point pgglyphadvance(Font *font, Point p, unsigned glyph);
Point pgcharadvance(Font *font, Point p, unsigned c);
Point pgmeasure(Font *font, const char *text);


/*
    Paths.
*/
Path *pgpath(int capacity);
Path *pgpmove(Path *path, Point a);
Path *pgpline(Path *path, Point b);
Path *pgpcurve3(Path *path, Point b, Point c);
Path *pgpcurve4(Path *path, Point b, Point c, Point d);
Path *pgpclose(Path *path);
Path *pgpclean(Path *path);


/*
    Boxes.
*/
const BoxMethods pgbox_default;
const BoxMethods pgbox_horizstack;
const BoxMethods pgbox_vertstack;
const BoxMethods pgbox_label;
const BoxMethods pgbox_textbox;
const BoxMethods pgbox_button;

void pgfocus(Box *box);
Box *pggetfocus();
void pgaddbox(Box *parent, Box *child);
void pgremovebox(Box *child);
void pgpack(Box *box);

Box *pglocate(Box *box, int x, int y);
Canvas *pgboxsubcanvas(Canvas *g, Box *box);

void pgboxclicked(Box *box, int x, int y);
void pgboxkey(Box *box, unsigned code, unsigned mod);
void pgboxchars(Box *box, char *text);
void pgdrawbox(Canvas *g, Box *box);

Box *pgstackbox(bool horizontal);
Box *pglabel(char *text);
Box *pgtextbox(TextBoxData *data);
Box *pgbutton(char *text);

TextBoxData *pgtextboxdata(char *text);

Font *pgthemefont();
Colour pgthemefg();
Colour pgthemebg();
Colour pgthemeaccent();


/*
    Inline Utility Functions.
*/

unsigned pgfromutf8(uint8_t **in);
uint8_t *pgtoutf8(uint8_t **out, unsigned c);

static inline Point pt(float x, float y);
static inline Point pgaddpt(Point a, Point b);
static inline Point pgsubpt(Point a, Point b);
static inline Colour rgba(float r, float g, float b, float a);
static inline Colour rgb(float r, float g, float b);
static inline uint32_t packrgb(Colour colour);
static inline Colour unpackrgb(uint32_t colour);
static inline Point pgapplyctm(CTM ctm, Point p);
static inline CTM pgmulctm(CTM x, CTM y);


static inline Point pt(float x, float y) {
    return (Point){ x, y };
}

static inline Point pgaddpt(Point a, Point b) {
    return (Point){ a.x + b.x, a.y + b.y };
}

static inline Point pgsubpt(Point a, Point b) {
    return (Point){ a.x - b.x, a.y - b.y };
}

static inline Colour rgba(float r, float g, float b, float a) {
    return (Colour){ r, g, b, a };
}

static inline Colour rgb(float r, float g, float b) {
    return rgba(r, g, b, 1);
}

static inline uint32_t packrgb(Colour colour) {
    return  ((int) (colour.r * 255) << 16) +
            ((int) (colour.g * 255) << 8) +
            ((int) (colour.b * 255) << 0) +
            ((int) (colour.a * 255) << 24);
}

static inline Colour unpackrgb(uint32_t colour) {
    return rgba(
        (colour >> 16 & 255) / 255.0f,
        (colour >> 8 & 255) / 255.0f,
        (colour >> 0 & 255) / 255.0f,
        (colour >> 24 & 255) / 255.0f);
}

static inline Point pgapplyctm(CTM ctm, Point p) {
    return pt(
        ctm.a * p.x + ctm.c * p.y + ctm.e,
        ctm.b * p.x + ctm.d * p.y + ctm.f);
}

static inline CTM pgmulctm(CTM x, CTM y) {
    return (CTM) {
        (x.a * y.a) + (x.b * y.c) + (0 * y.e),
        (x.a * y.b) + (x.b * y.d) + (0 * y.f),
        (x.c * y.a) + (x.d * y.c) + (0 * y.e),
        (x.c * y.b) + (x.d * y.d) + (0 * y.f),
        (x.e * y.a) + (x.f * y.c) + (1 * y.e),
        (x.e * y.b) + (x.f * y.d) + (1 * y.f),
    };
}
