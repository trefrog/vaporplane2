#include "app.h"
#include <stdio.h>

int main(void) {
    App app;
    if (!app_init(&app)) {
        fprintf(stderr, "Failed to initialize app\n");
        return 1;
    }
    app_run(&app);
    app_shutdown(&app);
    return 0;
}
