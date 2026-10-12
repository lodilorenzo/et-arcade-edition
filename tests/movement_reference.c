// GPL-3.0-or-later with additional terms: engine/COPYING.txt.
// Keep test access to static helpers out of the production game/module ABI.
#include "../engine/src/game/bg_pmove.c"

void Replay_Accelerate(pmove_t *move, const pml_t *local, vec3_t direction, float speed, float acceleration) {
    pm = move;
    pml = *local;
    PM_Accelerate(direction, speed, acceleration);
}
