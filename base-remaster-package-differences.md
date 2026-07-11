Differences between Base data and Remaster data.

## Summary

**Remaster is a small standalone demo, not a full game data set.** It ships its
own tiny `O.pbo` (848 KB vs Base's 182 MB) and is missing 26 of Base's 27
`AddOns/` PBOs entirely (no vehicles/weapons besides what's in the trimmed
`O.pbo`). It adds a dedicated demo mission (`demo.wrp` + `Missions/Demo.Demo`)
that doesn't exist in Base at all, plus new fonts, loose `.ogg` music, updated
stringtables (with three new UTF-8 stringtable variants), and an `APL-SA`
license notice scattered through nearly every folder.

`packages/Combined/` is a hand-built merge of the two, assembled this
session. See "How Combined/ is built" below for the general recipe and the
exact steps applied so far.

## How Combined/ is built

The recipe, in order:

1. **Start from a full copy of Remaster.** Remaster is the actively
   maintained package (SDL3 input strings, current `CfgLanguages`/Russian
   support, current engine-specific fixes like the `CfgSFX` music-track
   patch) -- it's the right base layer, not an afterthought overlay.
2. **Overlay anything from Base that doesn't already exist in Remaster.**
   Pure additions only at this step -- files/classes Remaster never shipped
   (because they belong to demo-cut content) get added back, without
   touching anything Remaster already provides a (possibly updated/fixed)
   version of.
3. **Rectify the cases where a flat "add what's missing" isn't enough**,
   because Remaster's *existing* version of something is itself a
   demo-specific cut that needs Base's full version instead, or because a
   config needs an actual field-level merge rather than a whole-file
   choice. Each of these is its own decision, documented per-folder below:
   - `AddOns/`: keep Remaster's `O.pbo` (smaller, possibly re-textured --
     see the open question below) and `cwr_logo.pbo`, add all 26 of Base's
     other PBOs that Remaster doesn't ship at all.
   - `DTA/`: Remaster's versions of `Data.pbo`/`Data3D.pbo`/`LandText.pbo`/
     `Sound.pbo`/`Voice.pbo`/`anims.pbo`/etc. are demo-trimmed, not just
     smaller -- they're missing the actual models/textures/sounds the
     restored `AddOns/` and `CfgVehicles` content needs. A flat "add what's
     missing" step would leave those gaps. Resolution: replace the whole
     folder with Base's, full stop (see DTA/ section).
   - `BIN/CONFIG.BIN`: neither "use Remaster's" nor "use Base's" wholesale
     is right -- Remaster's has the new `CfgLanguages`/`CfgFonts` Russian
     entry and the engine-relevant `CfgSFX` fix; Base's has the full
     `CfgVehicles`/`CfgVoice`/`CfgGroups` roster. Resolution: a real
     class-level config merge (`PoseidonTools config merge`, Base as base,
     Remaster as overlay, **no** `--force-overwrite` -- see the dedicated
     section below for why the flag matters here).
   - `BIN/STRINGTABLE.CSV` and everything else in `BIN/`: keep Remaster's
     as-is (has the `STR_SDL_SCANCODE_*` keys the new input system needs).
   - `Worlds/`, `Templates/`, `SPTemplates/`: pure additions -- Remaster has
     none of these directories at all, so Base's copy straight over with no
     conflict.

Net effect: `Combined/` should end up looking like Remaster everywhere
Remaster already covers something well, and like Base everywhere Remaster
demo-trimmed something the user wants back -- with `CONFIG.BIN` as the one
place that's genuinely both, merged at the class level rather than picked
as a whole file.

**Status as of this writing**: `AddOns/`, `DTA/`, `BIN/CONFIG.BIN`,
`Worlds/`, `Templates/`, `SPTemplates/` have had their rectifying step
applied (details in each section below). Everything else in `Combined/`
(`BIN/` besides `CONFIG.BIN`, `Campaigns/`, `Missions/`, `MPMissions/`,
`Music/`, `demo/`, `dtaExt/`, `fonts/`) is still just Remaster copied
wholesale from step 1 -- nothing from Base has been layered in for those
yet. `Campaigns/`/`Missions/`/`MPMissions/` in particular are likely next:
Base has real campaigns and ~30 missions Remaster doesn't ship at all (pure
additions, no rectifying needed -- see that section below), and those are
probably wanted for the same "full content" reason as the DTA/AddOns work.

## AddOns/

- **Remaster**: only 3 files, 3.5 MB total -- `O.pbo` (848 KB), `cwr_logo.pbo`
  (2.8 MB, new -- not in Base at all), `APL-SA_NOTICE.txt`.
- **Base**: 27 PBOs, 224 MB total -- every vehicle/weapon addon (`6G30`,
  `ABox`, `Apac`, `BISCamel`, `BMP2`, `Bizon`, `Brmd`, `Flags`, `G36a`,
  `Hunter`, `KOLO`, `LaserGuided`, `M2A2`, `MINI`, `Mm-1`, `Noe`, `O_WP`,
  `Steyr`, `XMS`, `ch47`, `humr`, `kozl`, `oh58`, `su25`, `trab`, `vulcan`).
- **`O.pbo` specifically**: Remaster 848 KB vs Base 182 MB. Unknown (per
  original note) whether Remaster's version is a re-textured/remastered
  subset of objects or just a demo-trimmed cut of Base's -- worth a PBO
  content diff if it matters for the renderer work.
- **Combined resolution**: keep Remaster's `O.pbo` + `cwr_logo.pbo`, add all
  26 of Base's other PBOs that don't exist in Remaster at all. Result: 29
  files, 227 MB.

## DTA/

| File | Base | Remaster | Notes |
|---|---|---|---|
| Abel.pbo | 2,270,511 | 2,253,037 | ~17 KB smaller |
| Anim.pbo | 12,157,964 | 12,157,964 | identical |
| Cain.pbo | 5,242,013 | *(missing)* | not in Remaster's demo |
| DTAEXT.PBO | 1,229,913 | *(missing)* | Remaster has a separate top-level `dtaExt/` dir instead (loose `equip/` files, not packed) |
| Data.pbo | 106,274,325 | 62,332,860 | Remaster ~42% smaller |
| Data3D.pbo | 28,029,598 | 19,994,243 | Remaster ~29% smaller |
| Eden.pbo | 3,220,626 | *(missing)* | not in Remaster's demo |
| Fonts.pbo | 1,158,381 | 1,158,381 | identical |
| LandText.pbo | 5,274,412 | 2,110,875 | Remaster ~60% smaller |
| Merged.pbo | 4,808,519 | 4,808,519 | identical |
| Misc.pbo | 396,866 | 396,866 | identical |
| Music.pbo | 45,286,282 | *(missing)* | Remaster uses loose `.ogg` files in a top-level `Music/` dir instead (seen: `10.ogg`, 1.06 MB) |
| Sound.pbo | 18,846,296 | 18,675,851 | nearly identical |
| Voice.pbo | 32,611,522 | 2,064,080 | Remaster ~94% smaller -- likely far fewer voice lines/languages for the demo |
| anims.pbo | 356,759 | 29,105 | Remaster ~92% smaller |
| scripts.pbo | 7,037 | 6,862 | nearly identical |

Remaster's `DTA/` also adds `APL-SA_NOTICE.txt` (license notice, repeated in
most Remaster folders) which doesn't exist anywhere in Base.

**Combined resolution**: this is the folder where Remaster's versions
aren't just smaller, they're genuinely incomplete for what the restored
`AddOns/`/`CfgVehicles` content needs (no point having `T80`/`Mi24`/etc.
back in `CfgVehicles` if `Data3D.pbo` never had their models to begin
with). So this isn't a "merge," it's a full replace: copied all 16 files
from `Base/DTA/` over `Combined/DTA/`, overwriting the 12 that already
existed there (`Abel.pbo`, `Anim.pbo`, `Data.pbo`, `Data3D.pbo`,
`Fonts.pbo`, `LandText.pbo`, `Merged.pbo`, `Misc.pbo`, `Sound.pbo`,
`Voice.pbo`, `anims.pbo`, `scripts.pbo`) and adding the 4 that didn't
(`Cain.pbo`, `DTAEXT.PBO`, `Eden.pbo`, `Music.pbo`). `APL-SA_NOTICE.txt`
was left alone (no Base equivalent to conflict with it). This is also what
fixed a blank main-menu-on-first-load bug after `Worlds/`/`Templates/`
were added below -- the menu's intro world needs landscape textures/
content this folder provides.

## BIN/

- **CONFIG.BIN**: Remaster 319,474 vs Base 337,711 (~5% smaller). Full
  investigation below.
- **RESOURCE.BIN**: identical (139,844 both).
- **STRINGTABLE.CSV**: Remaster 314,328 vs Base 310,934 (Remaster slightly
  *larger* here, opposite of most other files -- consistent with adding new
  demo-specific strings). Full investigation below.
- **Remaster-only additions**: `STRINGTABLE_CREDITS.utf8.csv` (11.6 KB),
  `STRINGTABLE_EDITOR.utf8.csv` (1 KB), `STRINGTABLE_EQUIPMENT.utf8.csv`
  (76.5 KB), `APL-SA_NOTICE.txt`. None of these exist in Base -- looks like
  the stringtable system was split/extended for the remaster (UTF-8 variants
  for credits/editor/equipment text), not just trimmed.

### STRINGTABLE.CSV deep dive

Checked with `PoseidonTools stringtable inspect/lookup`. The key set isn't
really "trimmed vs extended" -- it's a clean rename plus a couple of real
additions:

- **144 keys removed**, all `STR_DIK_*` -- legacy DirectInput key-name
  strings from the original input system.
- **141 keys added**, all `STR_SDL_SCANCODE_*` -- a 1:1 rename reflecting
  the port from DirectInput to SDL3 input handling.
- **2 more keys added**: `STR_MP_LOCKED` / `STR_MP_UNLOCKED` ("Session
  locked/unlocked by %s").
- **+1 language**: Russian (7 languages vs Base's 6). This is also why
  Remaster's `CONFIG.BIN` gained a `CfgLanguages` class and a `Russian`
  sub-class under `CfgFonts` (see below).
- Net unique keys: 2344 (Base) -> 2343 (Remaster) -- essentially a wash,
  renamed/extended for the new input system, not reduced content.

Side-finding, not chased further: the specific `STR_SDL_SCANCODE_*` keys
that logged as "not found in stringtable" at game startup (POWER, RALT,
LGUI, MP_LOCKED, etc.) **are** present in this CSV with valid English text
(confirmed via `stringtable lookup`). So those startup warnings aren't a
content gap in this file -- the running game is likely reading a different
or stale packed copy of the stringtable rather than this loose source file.
Separate bug from the package-diff question, worth its own investigation
later.

### CONFIG.BIN deep dive

Debinarized both with `PoseidonTools config debin` and diffed structurally
(`config debin` -> text -> line-range/class-name diff per top-level class).
Same 50 top-level classes in the same order on both sides, plus Remaster
adds one: `CfgLanguages` (registers Russian). All the size difference is
concentrated in four sections, found by tracking the cumulative line-offset
between matching top-level classes until it changed:

- **`CfgVehicles`**: Base 718 classes, Remaster 489 -- **108 unique class
  names in Base entirely absent from Remaster, zero the other way.** Clean
  subset relationship. The missing classes are exactly what you'd expect
  given the missing `AddOns/` PBOs: weapon pickup objects (`AK47`, `M16`,
  `M60`, `RPGLauncher`, `LAWLauncher`, `HandGrenade`, `Mine`, ...), armor/
  aircraft/boats (`T55G`, `T80`, `M1Abrams`, `Mi17`, `Mi24`, `Cobra`, `A10`,
  `Cessna`, `Scud`, `BoatE`/`BoatW`, ...), ~30 East/Guerilla/some-West
  soldier variants (`SoldierEAA`, `SoldierGMedic`, `SoldierWMortar`, ...,
  plus `Civilian`/`Civilian2`/`Civilian3`, `OfficerG*`), and support
  vehicles/wrecks/misc objects (`UralWreck`, `TruckV3SG*`, `JeepWreck1-3`,
  `MASH`, `ShedSmall`, `HeavyReammoBox*`, `Fire`, `Smoke`, ...).
- **`CfgVoice`**: Remaster ~144 lines shorter -- trimmed voice-line class
  definitions, matching `Voice.pbo`'s ~94% size cut in `DTA/` (fewer
  factions/units need fewer voice sets).
- **`CfgGroups`**: Remaster ~136 lines shorter -- trimmed faction
  squad/group templates, matching the missing East/Guerilla soldier
  classes above.
- **`CfgFonts`**: Remaster +5 lines -- adds a `class Russian { ... }` font
  mapping (`AudreysHandB48`/`AudreysHandI48` -> `ru_audreyshand`).
- **`CfgSFX`**: Remaster +2 lines, but it's a *content change*, not a pure
  addition: one menu-music cue's file reference changed from
  `music\13.ogg` to `music\10.ogg`, plus two new fields
  (`musicTrack="Track10"`, `deviceMusicTrack="Track10"`) -- ties that cue
  to the demo's single shipped loose track (`Music/10.ogg`), since Remaster
  has no `Music.pbo` to provide a real track 13.
- **`CfgWeapons`** and **`CfgWorlds`**: byte-for-byte identical class sets
  on both sides (104 and 1687 classes respectively) -- weapon
  firing-mechanics definitions and world *configs* are independent of which
  `AddOns`/`.wrp` files are actually shipped.

### Merge decision: Combined/BIN/CONFIG.BIN

Built and placed a merged `CONFIG.BIN` at `packages/Combined/BIN/CONFIG.BIN`
via `PoseidonTools config merge <base> <overlay> -o <out>` (base=Base,
overlay=Remaster, **no** `--force-overwrite`). Verified: `CfgVehicles` keeps
Base's full 718-class roster, `CfgLanguages` is present, output is
byte-identical across repeated runs.

**`--force-overwrite` was tested and rejected** -- it makes the overlay win
on values that exist on both sides, not just add new ones. Since Remaster's
demo ships only one loose music file, *every one* of its `Track1`...`Track21`
classes internally points at that same single file as a fallback. With
`--force-overwrite`, that collapsed Base's real 21-track music config
(`Track1`->`01.ogg`, `Track2`->`02.ogg`, etc., all real distinct files in
Base's `Music.pbo`) down to all 21 tracks pointing at the one demo file --
silently destroys real content while looking like a clean merge. The plain
merge (no flag) correctly leaves `Track1` etc. pointing at Base's own files,
since those classes already exist in the base with valid content and the
merge only adds/updates from the overlay where the overlay actually
introduces something new (e.g. `CfgLanguages`, the `Russian` font
sub-class) -- it doesn't blindly clobber pre-existing array/scalar values.

The one thing the plain merge does *not* pick up is `CfgSFX`'s
`music\13.ogg` -> `music\10.ogg` redirect+field-addition above -- but
since the merged config is meant to be used with Base's full `Music.pbo`
(which has a real, distinct track 13), keeping the original reference is
correct, not a gap.

**Recommended pairing** (per the "English-only, want full Base content"
use case this was built for): Remaster's `STRINGTABLE.CSV` (has the
`STR_SDL_SCANCODE_*` keys the new SDL3 input system needs) + this merged
`CONFIG.BIN` (full Base vehicle/weapon/faction roster + Remaster's
`CfgLanguages` addition).

## Campaigns/, Missions/, MPMissions/

- **Campaigns**: Base has the two real campaigns (`1985.pbo` 71 MB,
  `resistance.pbo` 65 MB). Remaster has neither -- only an empty `demo/`
  subdirectory.
- **Missions**: Base has the full single-mission set (`01TakeTheCar.ABEL.pbo`
  through at least `07ShadowKiller.ABEL.pbo`, likely more). Remaster has
  exactly one: `Demo.Demo/` (unpacked, not a PBO) -- this is the actual demo
  scenario (the beach/jeep scene used as the repro for the Metal horizon-band
  bug this session, confirmed via `Remaster/demo/demo.wrp` below).
  Base has no `demo.wrp`/`Demo.Demo` at all -- it's wholly new content built
  for the remaster, not a port of existing Base content.
- **MPMissions**: Base has ~30 real multiplayer missions. Remaster has only
  two placeholder directories (`placeholder-jip.Demo`, `placeholder.Demo`),
  no real MP content.

## Worlds / demo/

- **Base**: `Worlds/` has `abel.wrp`, `cain.wrp`, `eden.wrp`, `intro.wrp` --
  the four original campaign islands.
- **Remaster**: no `Worlds/` directory at all. Instead a top-level `demo/`
  directory with just `demo.wrp` (6.2 MB) + the license notice -- a single
  new, remaster-exclusive map, not one of Base's four islands.

**Combined resolution**: pure addition, no conflict -- copied `Base/Worlds/`
into `Combined/Worlds/` (a new directory; Remaster never had one). `demo/`
was untouched. Combined with the AddOns/CONFIG.BIN restoration, this is what
brings back the missing islands (Kolgujev/Cain and Everon/Eden specifically
-- Malden/Abel and Nogova/Noe were already reachable via Remaster's own
`Abel.pbo` and Base's restored `Noe.pbo` in `AddOns/`).

## fonts/

Identical in both (`OFL.txt` + 5 `.ttf` files, byte-for-byte same sizes) --
the only top-level folder with zero differences.

## SPTemplates/, Templates/ (Base-only)

Base has `SPTemplates/` (5 cooperative-mode template PBOs per island) and
`Templates/` (team-flag-fight/sector-control/etc. templates, 5 variants per
mode per island). Remaster has neither directory at all -- these are
Base/full-game-only mission-editor templates.

**Combined resolution**: pure addition, same as `Worlds/` -- both
directories copied straight from Base into `Combined/`, no conflict since
Remaster never had them.

## Mods/

`Mods/@CustomMods/Addons/editorupdate102.pbo` -- a single third-party mod
addon, unrelated to the Base/Remaster comparison; not part of either
official package.

## Open questions

- Is Remaster's `O.pbo` a genuinely re-textured/remastered subset of
  objects, or just Base's objects with unused ones stripped for the demo's
  smaller footprint? (Original note's question, still unanswered -- would
  need to unpack both PBOs and diff contents, e.g. via `PoseidonTools` or
  `pbo.wcx64` in `dist/`.)
- `Campaigns/`, `Missions/`, `MPMissions/`, `Music/`, `demo/`, `dtaExt/`,
  and `BIN/` (besides `CONFIG.BIN`) are still Remaster copied wholesale --
  not yet rectified. `Campaigns/`/`Missions/`/`MPMissions/` are pure
  additions (no conflict, same as `Worlds/`/`Templates/`) and are the
  likely next step if the goal is "full Base content" -- Base has two real
  campaigns and ~30 missions Remaster doesn't ship.
- ~~CONFIG.BIN's ~5% size decrease in Remaster~~ -- resolved, see the BIN/
  deep dive above: it's the missing-AddOns `CfgVehicles`/`CfgVoice`/
  `CfgGroups` content, not a real gap.
- Why does the running game log `STR_SDL_SCANCODE_*`/`STR_MP_LOCKED` as
  "not found in stringtable" at startup when Remaster's loose
  `BIN/STRINGTABLE.CSV` has them with valid values? Likely reading a
  different/stale packed stringtable at runtime -- separate bug, not yet
  investigated.
