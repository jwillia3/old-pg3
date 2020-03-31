#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <pg.h>

SDL_Window  *rootw;
Box         *root;
Canvas      *rootg;


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
    pgdrawbox(rootg, root);
    updatewindow();
}

void okclicked(Box *box, int x, int y) {
    (void) box;
    (void) x;
    (void) y;
    exit(0);
}

void keypress(Box *box, unsigned code, unsigned mod) {
    (void) box;
    (void) mod;

    if (code == 0x29) // Escape key.
        exit(0);
}

void update() {
    pgdrawbox(rootg, root);
    updatewindow();
}

void init() {
    static BoxMethods  rootmethods;
    static BoxMethods  okmethods;

    // Vertical stackbox.
    // Override key.
    root = pgstackbox(false);
    rootmethods = *root->_;
    rootmethods.key = keypress;
    root->_ = &rootmethods;

    pgaddbox(root, pglabel("- Empty -"));

    Box *mid = pgstackbox(true);
    Box *textbox = pgtextbox(pgtextboxdata("Input box"));
    Box *okbutton = pgbutton("OK");

    okmethods = *okbutton->_;
    okbutton->_ = &okmethods;
    okmethods.clicked = okclicked;

    pgaddbox(root, mid);
    pgaddbox(mid, textbox);
    pgaddbox(mid, okbutton);

    pgaddbox(root, pglabel("~ None ~"));

    pgfocus(textbox);
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
            800,
            600,
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
