/* Every text style of the launcher, the ones its sources define, prepared at
 * one scale by one drawing, as the home screen and the pause menu share it
 * when the game's window has the home screen's scale: all fit in the
 * drawing's atlases, and none of their text would be left undrawn.  The list
 * is generated from the sources at configure (launcher_styles.h).  And a
 * drawing over a game larger than its output is clipped to the output's band,
 * never in the margin above or below it. */
#include "launcher_styles.h"

#include <SDL3/SDL.h>
#include <stdio.h>

int main(void)
{
    static const OraclesUiTextStyle *const all[] = { ORACLES_LAUNCHER_STYLES };
    const int count = (int)(sizeof all / sizeof all[0]);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *window = SDL_CreateWindow("atlases", 1920, 1080, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER) : NULL;
    OraclesUiDraw *draw = renderer ? oracles_ui_draw_create(renderer) : NULL;
    if (!draw) { fprintf(stderr, "no drawing: %s\n", SDL_GetError()); return 1; }
    int failures = 0;
    static const int sizes[3][2] = { { 1920, 1080 }, { 960, 540 }, { 2880, 1800 } };
    for (int s = 0; s < 3; s++) {
        const int glyphs = oracles_ui_draw_prepare(draw, sizes[s][0], sizes[s][1], 0.0f, all, count, "Ag");
        if (glyphs < 0) { fprintf(stderr, "FAIL: %d styles do not fit the atlases at %dx%d\n", count, sizes[s][0], sizes[s][1]); failures++; }
    }
    oracles_ui_draw_band(draw, 100, 600);
    SDL_Rect clip = { 0, 0, 0, 0 };
    oracles_ui_draw_begin_over(draw, 960, 800, 2.0f, 0.0);
    SDL_GetRenderClipRect(renderer, &clip);
    if (!oracles_ui_draw_overflows(draw) || clip.x != 0 || clip.y != 100 || clip.w != 960 || clip.h != 600) {
        fprintf(stderr, "FAIL: a scene larger than its output clipped to %d,%d %dx%d, not its band 0,100 960x600\n", clip.x, clip.y, clip.w, clip.h);
        failures++;
    }
    oracles_ui_draw_begin_over(draw, 960, 800, 0.0f, 0.0);
    SDL_GetRenderClipRect(renderer, &clip);
    if (oracles_ui_draw_overflows(draw) || clip.y < 100 || clip.y + clip.h > 700) {
        fprintf(stderr, "FAIL: a scene that fits clipped to %d,%d %dx%d, out of its band\n", clip.x, clip.y, clip.w, clip.h);
        failures++;
    }
    oracles_ui_draw_destroy(draw);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (failures) return 1;
    printf("test_launcher_atlases: ok (%d styles)\n", count);
    return 0;
}
