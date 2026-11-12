/*
 * HELL GATE - the framework's sample first-person shooter. There is no game code here: the
 * game is hellgate.game (types, state machines, the player, the weapon), played by the
 * framework runtime in the sector world. A hellgate.game in the user files folder (next to the
 * exe, the app's files folder on Android) replaces the built-in one, so it can be edited.
 */
#include "fwrt.h"
#include "files.h"
#include "hellgate_game.h"

extern const FwWorld sector_world;
void sector_world_init(void);

static char user[16384];

static void init(void)
{
    int n = plat_file_load("hellgate.game", user, (int)sizeof(user) - 1);
    sector_world_init();
    if (n > 0) fw_runtime_init(&sector_world, user, n);
    else fw_runtime_init(&sector_world, HELLGATE, (int)sizeof(HELLGATE) - 1);
}

const App game_app = FW_RUNTIME_APP("HELL GATE", init, "hellgate");
