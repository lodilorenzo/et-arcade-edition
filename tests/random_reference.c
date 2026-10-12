// Compile the real upstream C implementation under separate names.
// Neither Rust-replacement define is set for this translation unit.
#define Q_rand C_Q_rand
#define Q_random C_Q_random
#define Q_crandom C_Q_crandom
#define ClampChar C_ClampChar
#define ClampShort C_ClampShort
#define Q_fabs C_Q_fabs
#define Q_rsqrt C_Q_rsqrt
#define LerpAngle C_LerpAngle
#include "../engine/src/game/q_math.c"
