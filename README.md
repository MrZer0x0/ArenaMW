# ArenaMW 0.47 Standalone

ArenaMW is a single-player OpenMW 0.47.0 source tree carrying the gameplay,
animation, interface and rendering work from ArenaMP/EncoreMP, without the
TES3MP networking layer. It builds `openmw`, `openmw-launcher`, the installation
wizard and the INI importer; no client/server browser, master server or RakNet
target is part of the project.

## Included

- EncoreMP gameplay rebalance: combat accuracy and damage, ranged projectile
  recovery, hand-to-hand, armour/unarmoured/creature armour, armorer, magic
  resistance and Willpower, enchanting, alchemy, training, mercantile,
  pickpocketing, stealth cache and ally scaling.
- Tactical AI, collision avoidance, cross-door hostile pursuit and improved
  dialogue/head-tracking rules.
- Dynamic NPC idles, selectable player poses that yield to movement/combat and
  resume when idle, walking styles, animated item, door, container, book and
  scroll interactions, and the bundled animation VFS.
- Arena cinematic dialogue with integrated persuasion and keyboard navigation,
  redesigned HUD, target panel, resource-bar fading, game time, QuickLoot,
  container filtering/capacity, spell trader improvements, interface scaling
  and Russian/English Arena localisation. Embedded persuasion always reopens
  in the centred Arena layout, without a Responses caption covering disposition.
  The HUD FPS counter remains visible;
  the OSG diagnostic overlay is unbound, while F3 and F4 remain the HDR and
  bloom toggles.
- Arena's expanded race/appearance creation layout with standalone Back/Next
  flow, plus TES3MP-style automatic resurrection at the nearest temple or
  divine shrine without opening a death menu.
- HDR, bloom, PBR-compatible lighting, water shaders, ripples, occlusion culling
  and the MaskedOcclusionCulling dependency.
- Escape/main-menu-only pause semantics. Inventory, dialogue, barter,
  containers, books/scrolls, crafting, console and modal message boxes keep the
  simulation running, as do the QuickLoot and HUD overlays.
- Arena launcher hardware detection and quality presets without a server page,
  server browser, multiplayer process or QtNetwork runtime dependency. Portable
  `build.ini` manifests retain build name, data path, language, ordered content,
  groundcover and BSA archives while ignoring all IP/address/port fields.
  The Wizard accepts either the Morrowind root or its `Data Files` directory,
  marks setup complete after a successful run and automatically separates
  grass/groundcover plugins while registering base and additional BSA archives.
  The plugin manager automatically selects `Morrowind.esm` and the matching
  content profile when it opens after setup.
  The Launcher uses ArenaMP's fixed 1024x720 window geometry.
  A standalone template is included as `docs/build.ini.example`.
- EncoreMP 0.93 companion ESP files under `extras/EncoreMP`.

## Build on Windows

The ArenaMP dependency/bootstrap script is retained and adjusted for the
standalone target. It builds the game, launcher, installation wizard and INI
importer; editor, conversion and test targets are disabled in this player
package:

```bash
bash CI/before_script.msvc.sh -c Release -p x64 -v 2022 -N -C -V -i "$PWD/install"
source MSVC2022_64_Ninja/activate_msvc.sh
cmake --build MSVC2022_64_Ninja --config Release --parallel 2
cmake --install MSVC2022_64_Ninja --config Release
```

The workflow `.github/workflows/windows.yml` performs the same build and emits
a Windows x64 ZIP. It also checks that HDR/Bloom/QuickLoot resources are present
and that no TES3MP executable was produced.

## Content setup

Use a legal Morrowind installation as with normal OpenMW. The engine-side
gameplay changes work without an ESP. The files in `extras/EncoreMP` add the
optional Encore content and spell changes; read their companion notes before
enabling them and keep the listed load order.

See `PORTING_NOTES.md` for the separation boundary and validation performed.
The original OpenMW 0.47 README is retained as `README-OPENMW.md`.
