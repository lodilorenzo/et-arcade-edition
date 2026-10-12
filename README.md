# Enemy Territory: Arcade Edition

Wolfenstein: Enemy Territory 2.60b with split-screen, controller support and offline bot matches. This is an unofficial, altered adaptation of the original GPL source, not an official id Software, Splash Damage or ET: Legacy release.

**[Website](https://lodilorenzo.github.io/et-arcade-edition/)** · **[Releases](https://github.com/lodilorenzo/et-arcade-edition/releases)**

**[Download v1.0.0 for Windows 10/11](https://github.com/lodilorenzo/et-arcade-edition/releases/download/v1.0.0/EnemyTerritory-ArcadeEdition-1.0.0.zip)** · **[Matching source ZIP](https://github.com/lodilorenzo/et-arcade-edition/releases/download/v1.0.0/EnemyTerritory-ArcadeEdition-1.0.0-source.zip)**

Extract the portable ZIP, copy `pak0.pk3`, `pak1.pk3`, `pak2.pk3` and `mp_bin.pk3` from your original 2.60b installation into its `etmain/` folder, then run `ET-arcade-edition.exe`. Omni-bot and Visual C++ runtime DLLs are bundled; no installer is needed. Original game data is **not included**.

This source repository does not include the Omni-bot runtime or original game data; those must be supplied separately for development builds.

## What's different from ET

- **Two to four local players**, offline or online, each with their own view, HUD, controller and limbo menu. Three-player layouts show a live command map in the fourth pane.
- **Omni-bot matches on all six stock maps**, preserving the original objectives and scripts. Bots fill empty slots, balance teams and respond to assistance calls; matches support up to 48 players.
- **Up to four Xbox/XInput controllers**, with hot-plugging, radial equipment/assistance wheels and adjustable controller-only aim assistance.
- **Controller-friendly menus and server browser**, filtering out password-protected servers and servers without room for every local player.
- **Local player profiles**, saved names, XP and medals, plus controller text entry.
- **Downloaded-map selection** and support for compatible community maps. The portable release bundles Omni-bot navigation; development builds need a separately installed Omni-bot distribution.
- **Ultrawide and high-refresh display support**, with an expanded horizontal field of view and scaled HUD/menus.
- **Portable launcher** that imports and verifies your own 2.60b game files and keeps settings, profiles and downloads in the package folder.

## Build from source

Supported build platform: **Windows, MSVC, 32-bit x86**. The first portable release targets Windows 10/11 and requires a working OpenGL driver. Visual C++ runtime DLLs are bundled beside the executable; no separate runtime installation is needed.

Install Visual Studio C++ Build Tools with a Windows SDK, CMake 3.24+, Rust/Cargo and Python 3. From a Visual Studio developer shell:

```powershell
git clone https://github.com/lodilorenzo/et-arcade-edition.git
cd et-arcade-edition
rustup target add i686-pc-windows-msvc
cargo test --locked --target i686-pc-windows-msvc
cmake -S . -B build/slice -A Win32 -DET_RUST_RANDOM=ON -DET_RUST_SHARED=ON
cmake --build build/slice --config Release --parallel
ctest --test-dir build/slice -C Release --output-on-failure
```

Choose an installed Visual Studio generator with `-G` if needed. CMake links a Rust library for selected shared gameplay helpers; the engine and game modules remain based on the original C/C++ source. This is not a complete engine rewrite.

Outputs are in `build/slice/Release`: `ET-arcade-edition.exe`, `et.exe`, `etded.exe` and the game/client/UI modules. Compilation and the self-contained CTest checks do not require original game data. The repository includes the modified engine directly; there are no submodules to fetch.

## Run a development build

1. Supply your own Enemy Territory **2.60b** game data: place `pak0.pk3`, `pak1.pk3`, `pak2.pk3` and `mp_bin.pk3` in `runtime/etmain/`.
2. For local bot matches, separately install a compatible Windows/x86 Omni-bot runtime, scripts and navigation in `runtime/omnibot/`. Its `omnibot_et.dll` must be directly inside that folder. The game-side interface is included in `vendor/omnibot/`; the bot runtime is not.
3. Launch the unified menus:

```powershell
python tests/play_game.py --modules build/slice/Release --home output/play
```

Choose **Play Online** for the browser or **Start Match** for local play. Xbox/XInput controllers are optional; keyboard and mouse remain supported.

## Portable packaging

With a compatible Omni-bot runtime installed separately and its redistribution terms checked:

```powershell
python tools/package.py --version 1.0.0
```

This builds the application and writes a portable ZIP and corresponding source ZIP under `dist/`. `--no-build` packages an existing Release build. Original game archives, saved identities and profiles are excluded.

Players extract a portable package, copy `pak0.pk3`, `pak1.pk3`, `pak2.pk3` and `mp_bin.pk3` from their original 2.60b installation into the package's `etmain/` folder, then run `ET-arcade-edition.exe`. Alternatively, run the launcher first and select the original installation to verify and copy its archives automatically. Do not upload imported game data or player identity files to Git.

The project owner has confirmed that permission to redistribute the bundled Omni-bot runtime is obtained. The runtime is not relicensed under the engine's GPL and remains excluded from this source repository.

## Checks

`cargo test` and `ctest` cover shared helpers, input, viewport behavior, downloads and movement replays. Additional integration checks live in `tests/` and may require game data, Omni-bot, Pillow, an OpenGL display or a reference ET installation. They are not all self-contained.

```powershell
python tests/export_source_check.py
python tests/server_browser.py
node tests/site_layout.mjs
```

The browser check uses a private localhost fixture and joins no public server. The website layout check needs Node 22+ and Edge/Chrome; set `BROWSER` to the browser executable if necessary. Close running game instances before interactive checks.

## Repository layout

- `engine/` — modified official Enemy Territory GPL engine and game source.
- `native/` — split-screen, controllers, profiles, launcher, bot adapter and custom UI.
- `rust/` — shared gameplay helpers.
- `vendor/omnibot/` — game-side interface source and notices, not the runtime.
- `tests/`, `tools/` — checks and development/packaging utilities.
- `docs/` — static landing page, screenshots and feature trailers, published by GitHub Pages from `main/docs`.

This is a source snapshot, not the private development workspace or its Git history. Local installers, original game archives, reference checkouts, profiles, logs and build output are omitted. Some artwork/trailer regeneration tools refer to omitted local capture inputs; the custom runtime UI art and finished website assets are included.

## Online safety

Only join servers you trust: downloaded ET mods execute native code. The old network/map parsers have not been security-audited. PunkBuster is unsupported, and servers requiring a particular executable or anti-cheat may reject this build. Online servers supply their own client modules; local aim assistance and profiles do not override server rules.

## License and credits

See [LICENSE.txt](LICENSE.txt) and [engine/COPYING.txt](engine/COPYING.txt) for GPLv3-or-later with id Software's additional terms, and [engine/README.txt](engine/README.txt) for the original notices. Preserve source copyright and third-party notices when redistributing derivatives.

The original game was developed by Splash Damage and released by id Software and Activision in 2003. The engine is derived from [id Software's GPL release](https://github.com/id-Software/Enemy-Territory), revision `40342a9e3690cb5b627a433d4d5cbf30e3c57698`. Selected fixes, identity code and bot integration are adapted from [ET: Legacy](https://www.etlegacy.com/), with their notices retained. See also [vendor/omnibot/COPYING.txt](vendor/omnibot/COPYING.txt) and [vendor/omnibot/README.md](vendor/omnibot/README.md).

Original game data is not GPL and is not distributed here. Website screenshots and trailers illustrate gameplay; they are not installable game assets. Wolfenstein and Enemy Territory are trademarks of their respective owners.
