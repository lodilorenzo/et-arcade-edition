# Publishing

This repository contains the current modified application source, custom UI art and static website. Original game archives, Omni-bot runtime binaries, installers, reference checkouts, identities, profiles, credentials, logs and build output are excluded. Keep the original engine and vendor license/copyright notices.

## Website

GitHub Pages publishes `docs/` from the `main` branch at:

https://lodilorenzo.github.io/et-arcade-edition/

Keep `docs/.nojekyll`. All local asset links are relative, so the project URL works without a custom domain or JavaScript build tool. Commit website updates to `main`; GitHub Pages rebuilds automatically. Run `node tests/site_layout.mjs` before publishing layout changes.

The finished gameplay trailers exclude the development-only Rust segment. The page links directly to the v1.0.0 portable ZIP, matching source ZIP and SHA-256 checksums. Update these versioned links when publishing a new release.

## Source and releases

See README.md for building and packaging. `tools/export_source.py` exports a new source-only snapshot outside the current folder; it intentionally excludes the website. Preserve the public README and `docs/` when updating this repository from a private development export.

Before pushing, review staged files for credentials, personal data and distribution rights. The exporter's filename filters and size checks are safeguards, not a complete security/licensing audit. Never commit `runtime/`, original `.pk3` archives or player identity files.

The project owner has confirmed permission to redistribute the bundled Omni-bot runtime for this release; it is not relicensed under the engine's GPL. Each portable release must provide the matching complete GPL source and retain third-party notices. Original ET game data must remain player-supplied.
