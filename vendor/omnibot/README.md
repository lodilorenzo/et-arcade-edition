# Omni-bot adapter provenance

Interface headers and loader: ET: Legacy commit
`626020f38bab1bf2c9ac5b6c66bfaf921b4a4d78`, from
https://github.com/etlegacy/etlegacy/tree/626020f38bab1bf2c9ac5b6c66bfaf921b4a4d78/vendor/Omnibot.
`native/omnibot.cpp` and `native/omnibot.h` derive from that commit's
`src/game/g_etbot_interface.{cpp,h}`. Original copyright and GPLv3-or-later
notices, including ET additional terms, remain in the files; see `COPYING.txt`.

Local adaptations (2026-10-08): stock ET 2.60 API/weapon translation through
`native/omnibot_compat.h`, independent split-human reservation, local bot count
setup, bounded pre-join loadout selection, and restricted Windows DLL loading.
The game still uses stock entity/session/weapon/network structures and rules.
Runtime provenance and separate licensing limitations: `runtime/omnibot/README.local.md`.
