# Tome distribution: where a spell's tomes come from

The entry point for anyone picking up this system. It says what the system does, where each part lives, how to
check it and how to change a rule. The details are elsewhere:
- each file's header comment;
- the design and its history: `docs/superpowers/specs/2026-11-14-tome-distribution-design.md`;
- the in-game flow: `docs/ARCHITECTURE.md`, "Tomes nothing hands out".

Status (2026-11-15): released as `v1.9.2+f1.0.1` on `fork/main`, verified in game. The harness and the game give
the same counts on the author's load order.

## What it does

After the game data loads, a worker thread reads the load order's plugin files and the Data folder's configs and
scripts. It follows every spell tome up to the places a player can get it (vendor chests, containers, NPCs, SPID
lines, quest rewards, recipes) and puts each spell with a tome into one **segment**. The player switches
segments on or off the tree. A spell in a switched-off segment stays out of the tree, but:
- its tome still teaches it;
- it is still registered with Spell Research;
- Gate Review can include it again.

## Segments

The first match wins, in this order. The enum order in `TomeReachSwitches.h` is this order, and so are the
author's Python `RULES`.

| # | Key (config, JSON) | Rule | Default |
|---|---|---|---|
| 1 | `unreachable` | no route: nothing hands out its tomes | always off |
| 2 | `spellpackVendor` | at a shop of its own mod (see below), with 3 or fewer open vendors | off |
| 3 | `ritualQuest` | some vendor's stock of it is unlocked by an `MGRitual*` global (the College master rituals) | on |
| 4 | `standardSale` | 3+ open vendor chests | on |
| 5 | `standardLoot` | loot at 20+ sites or in 5+ regions, or SPID by race, faction, class or NPC type | on |
| 6 | `regionalLoot` | loot at 2+ sites | off |
| 7 | `uniqueVendor` | 1-2 open vendor chests, or stock another quest unlocks | off |
| 8 | `uniqueReward` | one site's loot, hand-placed, or a quest reward | off |
| 9 | `crafted` | only a recipe makes it | off |
| 10 | `review` | anything else | always on (Gate Review lists them) |

How the rules are measured:
- **Spellpack vendor (shop):** a plugin's vendor-sold tome spells (3 or more of them; the base game and DLC
  plugins are excluded) form a shop when up to 3 vendor chests sell 75%+ of them. Only entries dedicated to the
  plugin count: the tome itself, or a list at least half of whose spells are that plugin's. A spell that more than
  4 chests sell through dedicated entries counts toward no shop. The 3-open-vendors cap keeps widely sold spells
  out (one mod's spells had 149 vendors).
- **Gates:** a leveled list a vendor chest holds is gated when its chance of nothing (LVLD) is 100 and it has a
  global (LVLG). How the global's editor id decides:
  - `PC*` (vanilla's skill thresholds): open;
  - `MGRitual*`: ritual;
  - anything else: quest.

  A chest counts by the most open entry it holds for the spell. A tome the chest holds directly is open.
- **Sites and regions:** loot is counted by placed references (containers and placed NPCs) per (region, site).
  A site is the location under its hold; its region is the hold, the top location or the worldspace. A mod's own
  worldspace or location tree is one region (`realm <plugin>`). Developer test cells are dropped.

Counts on the author's load order (1,874 spells in game): 106 unreachable, 292 spellpack vendor, 53 ritual quest,
839 standard sale, 45 standard loot, 102 regional loot, 34 unique vendor, 302 unique reward, 101 crafted, 0 review;
16 shops.

## Where things live

Model (pure C++, shared by the plugin and the tools): `plugins/spelllearning/src/tomereach/`

| File | Part |
|---|---|
| `TomeReachPlugin`, `TomeReachReader` | plugin files into the spell store: winning records, placements; leveled-list gates (`listGates`), global editor ids (`globalIds`) |
| `TomeReachData`, `TomeReachArchive` | Data folder files as the game sees them: loose files over BSAs, archives in the game's order (`ArchiveOrder`) |
| `TomeReachDistr`, `TomeReachConfig` | SPID and SkyPatcher configs |
| `TomeReachPex`, `TomeReachScripts` | compiled scripts: which properties reach AddItem (rewards, restocks) or AddForm (lists filled at run time) |
| `TomeReachGraph`, `TomeReachSites` | the reach graph (each item up to its roots) and sites and regions |
| `TomeReachClasses` | routes per tome: class (generic, faction, vendor, encounter, placed, reward, crafted, other, none) and subtype; vendor chests with the entries they hold |
| `TomeReachSegments` | shops, gates, a segment per spell |
| `TomeReachSwitches.h` | the segment enum, keys, names, defaults, `OnTree` |
| `TomeReachModel` | the whole chain, one object |
| `TomeReachSources` | in game: runs the model, reads config.json `tomeSources`, writes the scan's `source` object (`SourceJson`) |
| `TomeReach.cpp/.h` | the worker thread: the model first, the older reach pass (`TomeReachPass`, `TomeReachRecords`) only when the model cannot run |

Consumers:
- scan: `src/spellscanner/SpellScannerScan.cpp` writes `source: { segment, class, subtype, detail, shop? }`
  per spell, and `tomeUnreachable` / `questOnly`;
- Tree Graft: `include/treebuilder/GraftCandidates.h` `OffTreeBySource`;
- panel:
  - `modules/tomeSourceSetting.js`: the switches, `offTree`, Gate Review's reason text;
  - `modules/gateReview.js`;
  - `isTreeCandidate` in `modules/proceduralTreeBuilder.js`;
- panel UI: "Where Spells Come From", at the top of the Complex tree builder in `index.html`. It is mirrored in
  `dev-harness.html`, shown after a scan (`cppCallbacks.js`), and folds by its header;
- strings: `lang/en.json` and `lang/en.js`, `settings.tomeSources.<key>` / `<key>Desc`.

Config: config.json `tomeSources: { "<key>": bool }`. Missing or non-boolean values take the defaults, and
unknown keys (older segment names) are ignored. Not part of the settings presets.

## Checking it

- `build/tools/Release/tome-reach-test.exe --self-test`: the reader on made-up plugins (56 checks);
  `graft-check --self-test`: the switch rules in Tree Graft; `node run-tests.js` in the panel folder:
  `TomeSourceSetting`, Gate Review, the tome filter, and that dev-harness.html matches index.html.
- The harness on a real load order:
  - `lab/tome-reach/make_inputs.py <MO2 base> <profile> [<game folder>] > inputs.json`;
  - then `tome-reach-test -i inputs.json --graph g.tsv --sources s.tsv`.

  The sources dump has these rows:

  | Row | Content |
  |---|---|
  | C | tome routes |
  | D | each spell's best route |
  | V | vendor entries |
  | P | shops |
  | B | spell, rule number (enum + 1), segment |

  Its last line prints the counts per segment.
- In game: `SpellLearning.log` logs a `TomeReach: distribution model:` line, with the same counts as the harness
  on the same plugins. `debug.json` makes the panel's scans write `spell_scan_output.json`.
- Parity: the author keeps the Python model in their modlist workspace and compares the harness output with it
  row by row (C, D, V, P and B). On 2026-11-15 every row was equal (3,841 routes, 96,701 vendor entries, 16 shops,
  1,877 buckets). The route detail text is meant for people and is not compared.

## Changing a rule

The Python model is the spec. Change it there first, and port the change once the author has settled the counts.

To add, remove or rename a segment, change all of these:
1. `TomeReachSwitches.h`: the enum (in rule order), `kSegmentCount`, names, keys, switch members and defaults,
   `Switch()`, `OnTree`.
2. `TomeReachSegments.cpp`: the rule chain at the end of `ClassifySpells`, and its header comment.
3. `tools/tome-reach-sources.cpp`: `PythonRule` is the enum + 1, so it stays right as long as the order matches.
4. `tools/graft-check.cpp`: the switch cases.
5. `modules/tomeSourceSetting.js`: `SEGMENTS`, `DEFAULTS`, `NAMES`; and `modules/tomeSourceSettingTest.js`.
6. `lang/en.json` and `lang/en.js`.
7. `index.html` and `dev-harness.html`: one `setting-row` per segment, with the toggle id
   `tomeSource<Key>Toggle` and `checked` on the default-on ones.
8. This file, the design spec and `docs/ARCHITECTURE.md`.

A threshold only needs the constant at the top of `TomeReachClasses.cpp` or `TomeReachSegments.cpp`, plus this
file.

## Pitfalls already met

- **MO2's virtual Data folder:** `std::filesystem::relative` resolves a mod's file to its real folder, so loose
  scripts went unread (119 spells were wrongly unreachable). Data paths are keyed with `lexically_relative`.
- **Archive order:** the game reads archives in its own order (the INI lists, then each plugin's `<stem>.bsa` and
  `- Textures.bsa` in load order, a later one winning), and loose files beat all of them. Reading them in name
  order gave different winners.
- **Which books are tomes:** the game's tome pairs (Spell Research's SpellTomes) decide which books are tomes, not
  the book's DATA. The harness has no pairs, so it goes by DATA.
- **Plugin sets:** the game loads generator patches that the author's spell map leaves out, so the game's counts
  differ slightly from the spell map's. Compare like with like.
- **When the model cannot run** (a plugin it cannot read, or it throws): the older pass answers and no `source`
  objects are written. The panel then falls back on `questOnly`.
