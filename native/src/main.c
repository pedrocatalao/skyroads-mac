/* main.c — sky2.c's main() flow, English retail version. */
#include "assets.h"
#include "platform.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

duint main_menu(duint draw);
duint gomenu(void);
duint intro(void);
int game(int the_end);                 /* game_play.c */

enum { NO_CRASH = 0, ABORT = 7 };

const char *cfg_path(void) {
    static char buf[1200];
    if (!buf[0]) snprintf(buf, sizeof buf, "%sskyroads.cfg", plat_pref_path());
    return buf;
}

int main(int argc, char **argv) {
    const char *base = plat_base_path();
    set_data_dir(argc > 1 ? argv[1] : (base ? base : "."));
    if (plat_init("SkyRoads", 3) != 0) {
        fprintf(stderr, "SDL init failed\n");
        return 1;
    }

    void audio_init(void);
    audio_init();
    srand((unsigned)(plat_now() * 1e6));
    load_cfg();
    play_song(0);
    load_data();
    load_trekdat();
    check_error();
    initvid();

    /* Esc during the intro leaves the logo screen up and the menu draws
     * over it (draw=0); a completed intro faded to black (draw=1). */
    duint menu_draw = intro() ? 0 : 1;
mm:
    main_menu(menu_draw);
    menu_draw = 1;
    start_alloc();
    load_game_data();
    for (;;) {
        start_alloc();
        if (gomenu()) {                /* Esc from road select -> main menu */
            free_memory();
            free_memory();
            goto mm;
        }
        /* sky2.c:214-218 — pick a random road song (2..13), avoid repeats */
        {
            enum { ROAD_MUSICS = 12 };
            static duint last_muzak = (duint)-1;
            duint m = (duint)(rand() % ROAD_MUSICS);
            if (m == last_muzak) m = (m + 1) % ROAD_MUSICS;
            last_muzak = m;
            play_song(2 + m);
        }
        road_len = load_road(Cur + 1);
        load_background(Cur / 3);
        check_error();
        int i;
        duint done = 0;
        for (duint k = 0; k < WORLDS * 3; k++)
            if (cfg.road_completed[k]) done++;
        do {
            i = game(!cfg.road_completed[Cur] && done == WORLDS * 3 - 1);
            if (i == NO_CRASH) {
                cfg.road_completed[Cur]++;
                Cur++;
                save_cfg();
            }
        } while (i != ABORT && i != NO_CRASH);
        free_memory();
    }
}
