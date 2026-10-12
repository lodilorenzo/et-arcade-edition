# Ultrawide overlay backdrop

- Generated with Pi `codex_generate_image` (OpenAI/Codex tool), not the API CLI.
- `ultrawide_storm-source.png`: original generated image, verified RGB 1940×811.
- `ultrawide_storm.jpg`: runtime-compatible 2048×1024 power-of-two JPEG,
  quality 95, no chroma subsampling. The panorama is uniformly fitted into
  content rectangle (0,84,2048,856); unused top/bottom rows are excluded by UV
  sampling. The native renderer rejects non-power-of-two images and PNG.
  Displayed artwork is not stretched to match the texture-storage dimensions.
- Backend reported 1940×811 / low quality / opaque. This is not native 3440×1440
  detail: it scales uniformly to that monitor. Narrower/wider displays crop the
  excess scene using UV coordinates; neither axis is stretched independently.
- Original forest mask/cloud textures supplied as style references only. The
  generated panorama contains no controls, logos or lettering. Native logos,
  fonts, panels and mouse hitboxes remain separate. Original PK3s are unchanged.
- One static panorama is shared by menu/overlay backgrounds. In-world rendering,
  HUD, campaign/tactical maps and levelshot data are not AI-generated.

## Final generation prompt

Use case: stylized-concept. Asset type: actual production ultrawide background
texture for Wolfenstein: Enemy Territory menu/overlay screens, NOT a mockup.
Image 1 is the original game's dark forest background MASK, reference for the
conifer silhouette and black lower portion; Image 2 is the original grey
storm-cloud texture, reference for colour and vintage rendering style. Generate
one finished opaque scene combining that mood, with new artwork extending
naturally across a panoramic 3440x1440 canvas (43:18, the actual ultrawide monitor
marketed as 21:9). Do not stretch a 4:3 composition. Smoky grey/olive storm sky,
restrained distant lightning, dark sparse conifer silhouettes at outer sides
and along bottom, dark smoky near-black lower third. The middle 55% must stay
calm, subdued and low-contrast for separately drawn native text and menu panels.
Keep cloud and tree proportions natural. Edge-to-edge image, no repeating side
strips, no mirrored scene, no black pillarboxes. Avoid bright white hotspots.
Output wide, ideally 3440x1440 pixels. Absolutely no logos, lettering, emblems,
people, weapons, buttons, panels, borders, cursor, HUD or watermark. This image
is background artwork only; native game UI remains separate.
