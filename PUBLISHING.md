# Publishing

This repository contains the current modified application source, custom UI art and static website. Original game archives, Omni-bot runtime binaries, installers, reference checkouts, identities, profiles, credentials, logs and build output are excluded. Keep the original engine and vendor license/copyright notices.

## Website

GitHub Pages publishes `docs/` from the `main` branch at:

https://lodilorenzo.github.io/et-arcade-edition/

Keep `docs/.nojekyll`. All local asset links are relative, so the project URL works without a custom domain or JavaScript build tool. Commit website updates to `main`; GitHub Pages rebuilds automatically. Run `node tests/site_layout.mjs` before publishing layout changes.

The finished gameplay trailers exclude the development-only Rust segment. There is no public portable release yet; the page links to the Releases listing and the current source ZIP, not nonexistent local download files. Update the release notice and links when an actual portable build is published.

## Source and releases

See README.md for building and packaging. `tools/export_source.py` exports a new source-only snapshot outside the current folder; it intentionally excludes the website. Preserve the public README and `docs/` when updating this repository from a private development export.

Before pushing, review staged files for credentials, personal data and distribution rights. The exporter's filename filters and size checks are safeguards, not a complete security/licensing audit. Never commit `runtime/`, original `.pk3` archives or player identity files.

Before distributing a portable binary package, confirm the Omni-bot runtime's redistribution terms and provide the matching complete GPL source alongside the release. The original ET game data must remain player-supplied.
