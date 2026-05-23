#include "app.h"
#include <SDL3/SDL.h>
#include <stdio.h>

int main(void) {
    App app = {0};

    if (!app_init(&app)) {
        fprintf(stderr, "app_init failed: %s\n", SDL_GetError());
        app_shutdown(&app);
        return 1;
    }

    app_run(&app);

    app_shutdown(&app);
    return 0;
}
