#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <pg.h>

SDL_Window  *rootw;
Canvas      *rootg;

Box         *root;
Box         *mat;
Font        *template;
unsigned    codepoint = 'j';

Path        *curpath;
Point       *curpoint;

bool        showgrid;

float       baseline;
float       ascent;
float       descent;
float       gridsz = 25;

Colour      templatefg = {.75, .75, .75, 1};

Colour      guidefg = {1, .75, .75, 1};
Colour      gridfg = {.75, .75, 1, 1};

void keypress(Box *box, unsigned code, unsigned mod) {
    (void) box;
    if (code == 0x14 && mod & 0x11) // ^Q.
        exit(0);
    if (code == 0x1a && mod & 0x11) // ^W.
        exit(0);
    if (code == 0x04 + 'G' - 'A') {
        showgrid = !showgrid;
        mat->clean = false;
    }
}

void codepointchanged(Box *box, const char *text) {
    pgbox_textbox.chars(box, text);
    char *result = ((TextBoxData*) box->sys)->buf;
    codepoint = *result;
    mat->clean = false;
}

void templatechanged(Box *box, const char *text) {
    pgbox_textbox.chars(box, text);
    char *path = ((TextBoxData*) box->sys)->buf;
    Font *tmp = pgfontfile(path, 0);
    if (tmp) {
        pgfreefont(template);
        template = tmp;
        mat->clean = false;
    }
}

void drawmat(Box *box, Canvas *g) {
    (void) box;
    pgclear(g, rgb(1, 1, 1));

    if (template) {
        pgscalefont(template, 500.0f, 500.0f);
        Point   p = pt(0, template->ascent);
        p = pgapplyctm(template->ctm, p);
        p = pgaddpt(p, pt(0, -500));
        pgchar(g, template, p, codepoint);
        pgfill(g, templatefg);
    }

    if (showgrid) {
        for (int i = gridsz; i < g->width; i += gridsz) {
            pgmove(g, pt(i, 0));
            pgline(g, pt(i, g->height));
        }
        for (int i = gridsz; i < g->height; i += gridsz) {
            pgmove(g, pt(0, i));
            pgline(g, pt(g->width, i));
        }
        pgstroke(g, 1, gridfg);
    }

    ascent = template->ascent * template->ctm.d;
    baseline = (template->ascent + template->descent) * template->ctm.d;
    descent = template->descent * template->ctm.d;

    pgstrokeline(g, 1, guidefg, pt(0, ascent), pt(g->width, ascent));
    pgstrokeline(g, 1, guidefg, pt(0, baseline), pt(g->width, baseline));
    pgstrokeline(g, 1, guidefg, pt(0, descent), pt(g->width, descent));
}

Box *textbox(void (*changed)(Box *box, const char *text), const char *text) {
    Box *box = pgtextbox(pgtextboxdata(text));
    box->fixed = true;
    box->height = pgmeasure(pgthemefont(), "M").y * 2;
    pgoverridebox(box, (BoxMethods) { .chars = changed });
    return box;
}


void init() {

    curpath = pgpath(0);

    root = pgstackbox(false);
    pgoverridebox(root,
        (BoxMethods) {
            .key = keypress,
        });

    mat = pgbox(0);
    pgoverridebox(mat,
        (BoxMethods) {
            .draw = drawmat,
        });

    Box *horiz = pgstackbox(true);
    Box *vert = pgstackbox(false);

    Box *sidebar = pgstackbox(false);
    sidebar->fixed = true;
    sidebar->width = 250;

    Box *templatebox = textbox(templatechanged, "/usr/share/fonts/TTF/cour.ttf");
    Box *codepointbox = textbox(codepointchanged, "a");

    pgaddbox(sidebar, templatebox);
    pgaddbox(sidebar, codepointbox);
    pgaddbox(sidebar, pgbox(0));

    Box *editarea = pgbox(0);
    editarea->fixed = true;
    editarea->height = 250;

    pgaddbox(horiz, sidebar);
    pgaddbox(horiz, mat);
    pgaddbox(vert, horiz);
    pgaddbox(vert, editarea);
    pgaddbox(root, vert);

    templatechanged(templatebox, "");
    codepointchanged(codepointbox, "");

}

void updatewindow() {
    SDL_Surface *screen = SDL_GetWindowSurface(rootw);
    SDL_LockSurface(screen);
    memmove(
        screen->pixels,
        ((Bitmap*) rootg)->pixels,
        screen->pitch * screen->h);
    SDL_UnlockSurface(screen);
    SDL_UpdateWindowSurface(rootw);
}

void update() {
    pgdrawbox(rootg, root);
    updatewindow();
}

void resized(SDL_Window *rootw) {
    int     width;
    int     height;

    SDL_GetWindowSize(rootw, &width, &height);
    root->width = width;
    root->height = height;

    // Re-create root canvas.
    pgfree(rootg);
    rootg = pgnewbmp(width, height);
    pgpack(root);
    fflush(stdout);
    pgdrawbox(rootg, root);
    updatewindow();
}


unsigned sdlmod(unsigned sdl) {
    return
        + (sdl & KMOD_LCTRL? 0x01: 0)
        + (sdl & KMOD_LSHIFT? 0x02: 0)
        + (sdl & KMOD_LALT? 0x04: 0)
        + (sdl & KMOD_LGUI? 0x08: 0)
        + (sdl & KMOD_RCTRL? 0x10: 0)
        + (sdl & KMOD_RSHIFT? 0x20: 0)
        + (sdl & KMOD_RALT? 0x40: 0)
        + (sdl & KMOD_RGUI? 0x80: 0);
}

int main(void) {
    init();

    SDL_Init(SDL_INIT_VIDEO);
    rootw =
        SDL_CreateWindow(
            "Demo",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            1000,
            1000,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    for (SDL_Event e; SDL_WaitEvent(&e); )
        switch (e.type) {
        case SDL_QUIT:
            return 0;

        case SDL_WINDOWEVENT:
            switch (e.window.event) {

            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_EXPOSED:
                resized(rootw);
                break;
            }
            break;

        case SDL_MOUSEBUTTONUP:
            pgboxclicked(
                pglocate(root, e.button.x, e.button.y),
                e.button.x,
                e.button.y);
            update();
            break;

        case SDL_KEYDOWN:
            pgboxkey(
                pggetfocus()? pggetfocus(): root,
                e.key.keysym.scancode,
                sdlmod(e.key.keysym.mod));
            update();
            break;

        case SDL_TEXTINPUT:
            pgboxchars(pggetfocus()? pggetfocus(): root, e.text.text);
            update();
            break;
        }
}
