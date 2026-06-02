#include "app.h"
#include "project_format.h"
#include <SDL3/SDL.h>
#include <stdio.h>

int main(void) {
    App *app = (App *)SDL_calloc(1, sizeof(*app));
    if (!app) {
        fprintf(stderr, "failed to allocate app state\n");
        return 1;
    }

    if (!app_init(app)) {
        fprintf(stderr, "app_init failed: %s\n", SDL_GetError());
        app_shutdown(app);
        SDL_free(app);
        return 1;
    }

    app_run(app);

    app_shutdown(app);
    SDL_free(app);
    return 0;
}
