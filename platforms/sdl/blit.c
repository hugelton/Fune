/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "blit.h"

static SDL_Texture *tex;
static int tex_w, tex_h;

int sdl_blit_init(SDL_Renderer *r, int w, int h)
{
    tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    tex_w = w; tex_h = h;
    return tex != NULL;
}
/* the canvas onto its place (w x h of the renderer's logical size); a scaled canvas's pixels (pw x ph) go up whole, the texture
 * made again when their number changes */
void sdl_blit(SDL_Renderer *r, const GCanvas *c)
{
    const int pw = c->s > 1 ? c->pw : c->w, ph = c->s > 1 ? c->ph : c->h;
    if (pw != tex_w || ph != tex_h) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pw, ph);
        tex_w = pw; tex_h = ph;
        if (!tex) return;
    }
    SDL_UpdateTexture(tex, NULL, c->px, pw * 4);
    SDL_RenderCopy(r, tex, NULL, &(SDL_Rect){0, 0, c->w, c->h});
}
int sdl_blit_save(const char *path, const GCanvas *c)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(c->px, c->w, c->h, 32, c->w * 4, SDL_PIXELFORMAT_ARGB8888);
    int ok;
    if (!s) return -1;
    ok = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return ok;
}
void sdl_blit_shutdown(void)
{
    if (tex) SDL_DestroyTexture(tex);
    tex = NULL;
}
