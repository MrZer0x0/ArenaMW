# Porting notes

## Base and scope

- Base engine: OpenMW 0.47.0.
- Feature source: ArenaMP/EncoreMP 0.93.1 tree supplied with the project.
- Build bootstrap: ArenaMP Windows dependency builder, with multiplayer flags
  removed.
- Runtime boundary: local single-player state only.

## Network removal

The standalone tree does not contain the TES3MP client, server, browser, master
server, `components/openmw-mp`, RakNet or multiplayer packet targets. Gameplay
operations formerly waiting for server authority were converted back to local
OpenMW operations, including inventory transfers, dynamically created potions
and enchantments, Soul Trap, merchant/trainer gold, spell casting, death and
summon cleanup, object pickup and animated interactions.

The launcher retains the Arena hardware probe, automatic quality selection,
graphics preset workflow, installation wizard and portable `build.ini`
handling, but launches only `openmw`. Manifest build/data/language/content/
groundcover/archive fields remain supported; server address, host, IP and port
fields are deliberately ignored. OpenMW-CS is excluded from the player build,
so QtNetwork is neither required nor copied into the runtime package.

## Pause policy

Only `GM_MainMenu`, opened by Escape, is a pause boundary. Inventory, dialogue,
barter, containers, books, journal, crafting, training, travel, console and
interactive message boxes do not stop scripts, time, actors, physics or world
updates. QuickLoot and the other HUD overlays remain non-pausing as well.

The audit also restored the local drag-and-drop completion call in the container
window, which had been lost when a multiplayer notification block was removed.

## Ported systems

| Area | Standalone result |
| --- | --- |
| Gameplay | Encore balance formulas, XP modifiers, training, stealth, economy, magic and ally scaling |
| Animation | Dynamic idles/walks, contextual interactions and persistent poses that pause for locomotion/combat then resume |
| Dialogue | Arena cinematic camera/window, integrated persuasion, animated speakers and local topic/service execution |
| AI | Tactical combat, pursuit leash and configurable hostile pursuit through doors |
| HUD/UI | Arena HUD, QuickLoot, target data, character creation, container search/capacity, scaling and localisation; HUD FPS visible, diagnostic OSG overlay unbound, F3/F4 retain HDR/Bloom |
| Death | Automatic local resurrection at the nearest temple/divine shrine; no death menu |
| Rendering | HDR, bloom, lighting/water shaders, ripples and occlusion culling |
| Content | EncoreMP 0.93 base and optional ESP files preserved in `extras/EncoreMP` |
| Build | Game, launcher, installation wizard and INI importer through the Arena-derived MSVC/Ninja bootstrap |

## Validation

- Scanned source/build files for TES3MP namespaces, headers, RakNet references
  and multiplayer build flags.
- Checked brace balance for every changed C/C++ source file.
- Parsed all MyGUI/XML resources.
- Restored Arena's complete TrueType font registry (`Magic Cards`, `Daedric`,
  `MonoFont`, `Russo`) so every window has a bundled fallback when Morrowind's
  bitmap font files are unavailable through the portable data path.
- Checked added CMake source/resource entries against files in the tree.
- Added a Windows CI build/install/package workflow with standalone artifact
  assertions.
- Added explicit standard-library includes required by current MSVC 2022
  (`std::transform`/`std::back_inserter` in the VFS filesystem archive).
- Compared all port-modified gameplay/UI files against both clean OpenMW 0.47
  and the supplied ArenaMP tree to find authority or packet-removal gaps.

A native compile was not run in the preparation environment because CMake and
the OpenMW third-party development packages were unavailable there. The Windows
workflow is the authoritative full compile check.
