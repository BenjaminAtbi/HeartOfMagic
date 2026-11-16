# Where a spell's tomes come from: distribution segments for the tree - design

Date 2026-11-14, segments revised 2026-11-15. Branch: fork/feat/tome-distribution, merged into fork/main
(`v1.9.2+f1.0`, revised segments `v1.9.2+f1.0.1`). Status: decisions below are the author's; stages 1-5 done and
verified in game. The current state in one place: docs/TOME_DISTRIBUTION.md.

## Why

The tree takes every spell whose tome "something hands out" (`tomeUnreachable` unset, docs/ARCHITECTURE.md,
"Tomes nothing hands out"). That test counts a tome as reached when any version of any record names it, and it
says nothing about how the player gets it. Two problems on the author's load order (3,229 plugins, 1,877 spells
with a tome):

- **Reach is too generous.** A tome listed only in a QA chest, a cheat crate, an unplaced container, a test cell, or
  a leveled list that an override has since dropped counts as reached. A walk from the winning records up to placed
  containers, NPCs and vendors finds 97 unreachable spells where the pass finds 26.
- **Every reached spell goes in the tree.** A spell whose tome is a boss's drop, a hand-placed find, a quest reward
  or the stock of one mod's merchant is a discovery; on the tree it is just another node. Spells sold everywhere
  belong on the tree; specialty finds are better left to be found (reading the tome still teaches the spell and
  gives XP).

The model below was worked out and tuned against that load order in Python first (the author's modlist
workspace, `scripts/spellmap/reach.py`, `distribution.py`, `shops.py`). This is a port of that model, not a
shared component: the plugin carries its own copy, tuned for the tree builder, with its settings in config.json.

## Decisions (author, 2026-11-14)

1. **Segments, not thresholds.** Every spell with a tome falls in one segment, and the settings have one switch per
   segment: on the tree or not. The numeric limits (vendor counts, regions, sites, shop coverage) are internal
   constants. Defaults:

   | Segment | What | Default |
   |---|---|---|
   Revised 2026-11-15 (first match wins, in this order):

   | Segment | What | Default |
   |---|---|---|
   | Spellpack vendor | at a shop of its own mod (up to 3 chests that sell 75%+ of the mod's vendor-sold tomes), with 3 or fewer open vendors | off |
   | Ritual quest | stock a College master ritual quest unlocks (`MGRitual*` globals) | on |
   | Standard sale | 3+ open vendor chests | on |
   | Standard loot | loot (containers and placed NPCs) at 20+ sites or in 5+ regions; SPID by race, faction, class or NPC type | on |
   | Regional loot | loot at 2+ sites | off |
   | Unique vendor | 1-2 open vendor chests, or stock another quest unlocks | off |
   | Unique reward | one site's loot, hand-placed, or a quest reward | off |
   | Crafted | only a crafting recipe makes it | off |

   A vendor chest's stock is gated when it comes through a gated list, one with 100% chance of nothing (LVLD) and a
   global (LVLG): `PC*` globals are vanilla's skill thresholds and count as open, `MGRitual*` ones make ritual quest,
   any other a quest. A chest counts by its most open entry for the spell.

   The first version (2026-11-14), replaced by the table above:

   | Segment | What | Default |
   |---|---|---|
   | General sale | sold by 5+ vendor chests, or loot in containers of 3+ regions | on |
   | Wide drops | carried by NPCs of a faction at 20+ sites, of many regions, or of a race / every NPC (SPID) | on |
   | Uncommon vendors | sold by 1-4 vendor chests, not a specialty mod vendor | on |
   | Specialty mod vendors | one or two chests sell 75%+ of a mod's vendor-sold tomes | off |
   | Specific loot | hand-placed, a boss's or a site's loot, a mod's local faction (under 20 sites) | off |
   | Quest rewards | a quest script or alias hands it out | off |
   | Crafted only | only a crafting recipe makes it | off |

   Unreachable spells are never on the tree. The player's Include (Gate Review) puts any one spell back.
2. **The source decides, the gate flags.** The spell gate's "out" band (free to cast, only dismisses a summon) no
   longer takes a spell off the tree: it becomes a review reason like the others. A free spell can be a real one
   (Mysticism's Symmetry, Equilibrium).
3. **Full PEX analysis.** Which script property flows into AddItem / AddForm decides between a quest reward, a
   merchant restock and a script-filled leveled list. Scripts are read loose and from BSAs with our own archive
   reader (v104 zlib, v105 LZ4 frame), in game and offline alike, so the offline harness and the plugin agree.
4. **Files, not game memory, at first.** Everything is read from the plugin files, as the reach pass does now
   (placed references of unloaded cells are not in memory anyway). If the pass is slow in game, optimise then
   with the game's loaded forms and archive loader.
5. **The two non-tree states are the existing flags.** `tomeUnreachable`: no route (unreachable). `questOnly`: a
   route only through segments switched off (the Python model's "unique" bucket). The tree, Tree Graft and the other
   readers keep reading these two flags; the model only changes how they are set.
6. **The old reach pass retires** once the model sets the flags. Its code stays as the fallback (used when the
   model cannot run, e.g. a store that fails to read) and as a cross-check in `tome-reach-test`.

## The model

**Graph** (winning version of every record; a form id resolves through its plugin's master list):
- leveled item lists: each entry's parent is the list (`LVLI` `LVLO`); SkyPatcher `addToLLs` lines add list
  parents; a script that AddForms its own properties into a list property of the same script adds them too;
- containers and NPCs: inventory (`CNTO`), NPC death item (`INAM`), default outfit (`DOFT` -> `OTFT` `INAM`);
  records whose editor id marks a test or debug record are left out;
- SPID `Item` / `DeathItem` lines: a root of their own, judged by their filters;
- quest aliases: created references (`ALCO`) and alias inventories: a reward;
- scripts: properties that reach AddItem: a reward, unless the same script also holds a merchant chest (then it
  restocks the chest);
- recipes (`COBJ`): crafted, with the bench, components and conditions (a condition only the non-player meets
  disables it);
- placements (`REFR`/`ACHR`): the winning version of each reference, deleted ones dropped, with its cell, the
  cell's worldspace, location (`XLCN`) and the location's parent chain (`LCTN` `PNAM`); NPCs are placed through
  their own references, leveled NPC lists (`LVLN`) and NPCs templated on them (`TPLT`), or spawned (a script's
  PlaceAtMe property, an alias's created actor, an explosion's placed object).

A tome's **roots** are the containers, NPCs and SPID lines above it through nested lists. A **site** is the
location under its hold (a dungeon, a town), its **region** the hold, the top location or the worldspace; a mod's
own worldspace or location tree outside the holds is one region (`realm <plugin>`). Placements in developer test
cells are dropped. A container is a **vendor chest** when a faction names it (`FACT` `VENC`, resolved from the
reference to its base) or its editor id says so.

**Classes** per route, the most generic winning (as in Python `distribution.py`): generic (5+ vendor chests,
containers in 3+ regions, 100+ NPCs of mixed factions in 3+ regions, SPID with no / race / keyword filters),
faction (3+ NPCs or containers of one faction at 20+ sites; `local` below 20 sites, `regional` in one region; SPID
faction or class filters), vendor (1-4 vendor chests), encounter (one or two sites, a boss; SPID naming NPCs only),
placed, reward, crafted, other, none.

**Specialty mod vendors**: a plugin's vendor-sold tome spells (sets of 3+, base masters excluded) whose 75% are
sold by at most 2 vendor chests, counting only entries dedicated to the set (the tome, or a list 50%+ of whose
spells are that plugin's), and leaving out spells sold through dedicated entries by more than 3 chests.

Since 2026-11-15 loot counts placed references (containers and NPCs) by site and region: 20+ sites or 5+
regions is generic `loot`, 2+ sites faction `regional`, one site an encounter; the faction-size rules are gone, and
shops take up to 3 chests (decision 1's table).

**Segments** (2026-11-15), first match wins: no route -> unreachable; spellpack vendor; ritual quest; standard
sale; standard loot (generic or faction routes other than regional, vendors, specialty); regional loot; unique
vendor; unique reward (placed, encounter, reward); crafted; anything else -> review. The segments equal Python's
bucket rules one for one (`shops.py --buckets`). First version (2026-11-14): no route -> unreachable; general sale;
specialty mod vendor; wide drops; specific; quest reward; crafted; uncommon vendor; review.

Since 2026-11-16 (the Python model's changes of 2026-11-15 and 16, ported with full parity):
- an NPC carries its effective inventory (template flags Use Inventory for `CNTO` and `DOFT`, Use Traits for
  `INAM`; a leveled NPC template gives its members'); the bases that place an inventory are leveled NPC lists only;
- SkyPatcher `addOnceToLLs` counts like `addToLLs`, and `removeFromLLs` removes the list parent after every
  addition;
- scripts are read on records (the holder types plus `INFO`, `SCEN`, `PACK`) and on placed references (their
  winning version, deleted ones and test cells left out, one set per base and cell); `AddForm` is matched per
  call (receiver and argument from properties within one function, an element of a form list or array counting
  as any member), and PlaceAtMe gives as AddItem does;
- spawned NPCs: a quest alias that creates one, an explosion that places one (`EXPL` `DATA`, counted where the
  records firing it from a destruction stage are), a script's PlaceAtMe / PlaceActorAtMe property (on a placed
  reference: there); with no known placement a stand-in site `spawned`, kept only when nothing else has a site;
- a reference in a world's persistent cell counts in the exterior cell at its position;
- a vendor chest named so must be placed; a SPID class filter makes a faction route;
- a vendor chest's gate is its most open path down to the tome, each path as closed as its most closed gated
  list;
- noise editor ids: `QA` only as written and not before a lower case letter; test cells: `TestCell`, or `QA`
  and a capital or `_`, as written.

## Stages

Each stage ends with the offline harness compared against the Python reference on the author's load order, all
spells at once, differences fixed or written down as deliberate.

1. **Records and placements** (`src/tomereach/`, done 2026-11-14): one plugin walker under the reach pass and the
   reader (`WalkPlugin`, TomeReachPlugin.cpp: header, masters, groups with their cell / worldspace context,
   compressed records, subrecords), and the **spell store** (`ReadSpellStore`, TomeReachReader.cpp): the winning
   versions of the record types the model reads, plus references, cells and locations. A reference is kept while
   reading when its base is a BOOK, SCRL, WEAP, LVLN, CONT or NPC_ (a record's type is the same in every override,
   and its base is always read first), then filtered by the base's winning kind once every plugin is read, so a
   later plugin that changes whether a book teaches or a weapon is a staff is followed. Author's order (3,227
   plugins without the generator patches): 102k records, 94k references kept of 2.9M, 157k cells; 3.4 s; the store
   takes 84 MB, and the peak (256 MB) is the largest plugin file read whole. Keeping every reference until the end
   took 318 MB for the same result. Harness: `tome-reach-test --store` dumps per-tome parents, per-base placement
   counts, cells, locations and vendor chests; equal to the Python reader on every line.
2. **Graph and roots** (done 2026-11-14): `ReachGraph` (TomeReachGraph.cpp: leveled list, inventory and death
   item edges, test and debug records left out, NPC bases through leveled NPC lists and templates, vendor chests,
   recipes with the disabled-recipe condition, quest alias rewards) and `Sites` (TomeReachSites.cpp: region and
   site per placement, mod realms, test cells dropped). Harness: `tome-reach-test --graph` dumps every tome's roots,
   the facts the classes read (vendors, containers and NPCs placed, sites, tome placements, rewards, recipes,
   route) and the site counts per base, and lists the unreachable spells. Against Python's Graph and Sites with
   the stage-3 inputs switched off: equal on every line (232,122 roots, 1,895 tomes, 4,760 site counts, 598
   unreachable spells; the full Python model, with SPID, SkyPatcher and scripts, has 97). Graph and sites take
   1.5 s after the store. `questOnly` follows with the segments in stage 4.
3. **Configs and scripts** (done 2026-11-14): `DataFiles` (TomeReachData.cpp: loose files over BSA members, the
   archives' `scripts` and `strings` folders only; TomeReachArchive.cpp reads v104 zlib and v105 LZ4 archives),
   `ReadDistributionConfigs` / `ResolveDistribution` (TomeReachDistr.cpp: SPID `Item` / `DeathItem` lines with
   their filter kinds, NPC names from localized string tables; SkyPatcher `addToLLs`; form tokens as
   `id~plugin`, `plugin|id` or editor ids, first record wins), VMAD properties (`ParseVmad`, form lists expanded)
   and PEX analysis (TomeReachPex.cpp: disassembly, then the property-to-AddItem / AddForm data flow, memoised
   per function and seed set). A script is read only when a property holds a tome, scroll, staff or leveled item
   list. Against the full Python model: equal on every line (2,074,502 roots, 1,895 tomes, 10,141 site counts, 97
   unreachable spells). Author's order: 220 config files (15 SPID lines, 325 SkyPatcher adds), 293 archives with
   scripts or strings, 568 scripts read; configs 1.1 s, graph with scripts 0.6 s. Deliberate differences: SPID
   configs are read from the data folder's root only, as SPID itself does; an archive member loses to a loose
   file. Among archives, until later the same day (after stage 5), the folder's priority decided, then the
   archive name; since then the game's order (`ArchiveOrder`: the INI lists, then each loaded plugin's archives
   in load order; an archive no plugin loads is not read), the same rule as the author's `bsaorder.py`: 290
   archives and 44,500 member winners equal, the graph and sources byte-identical to stage 5.
4. **Classes, specialty mod vendors, segments** (done 2026-11-14): `Classifier` (TomeReachClasses.cpp: the
   routes of each tome, memoised per root set, with the entries each vendor chest holds) and `ClassifySpells`
   (TomeReachSegments.cpp: specialty mod vendors, a segment per spell, `SegmentSwitches` with the defaults of
   decision 1). Harness: `tome-reach-test --graph <tsv> --sources <tsv>` dumps every tome's routes (class,
   subtype), each spell's most generic route, the vendor chests above each tome with their entries, the specialty
   mod vendors and the segments. Against `distribution.py` and `shops.py`: equal on every line (4,349 tome
   routes, 1,877 spell classes, 96,701 vendor entries, 16 specialty mod vendors, 1,877 buckets: 97 unreachable,
   821 general sale, 274 specialty mod vendor, 13 wide drops, 408 specific loot, 23 quest reward, 101 crafted
   only, 140 uncommon vendor, 0 review). 1.8 s after the graph. Revised segments (2026-11-15), equal on every
   line again: 3,841 tome routes, 96,701 vendor entries, 16 spellpack vendors, 1,877 buckets: 97 unreachable,
   293 spellpack vendor, 54 ritual quest, 852 standard sale, 45 standard loot, 106 regional loot, 33 unique
   vendor, 296 unique reward, 101 crafted, 0 review. Route details are written for people and are
   not compared (they differ from Python's in wording; recipe conditions are not spelled out).
5. **Scan and UI** (done 2026-11-14, not yet tried in game): the worker thread runs the model over the
   plugin files and the Data folder (`ClassifyForGame`, TomeReachSources.cpp, on `Model`, TomeReachModel.cpp,
   which the harness runs too), and the reach pass only when the model cannot run (decision 6; until later the
   same day the pass ran first every time). The model's unreachable segment sets `tomeUnreachable` (a spell
   none of whose tomes is in the model's store is not marked); `questOnly` is a segment switched off (`QuestOnlySpells`
   reads config.json `tomeSources` on each call). The scan writes a `source` object per spell (`segment`,
   `class`, `subtype`, `detail`, `shop`). When the model cannot run, the pass answers as before and no `source`
   objects are written. The switches (`SegmentSwitches`, header-only TomeReachSwitches.h) are read by the DLL,
   Tree Graft (`GraftCandidates.h` `OffTreeBySource`) and the panel (`modules/tomeSourceSetting.js`, at the top
   of the Complex tree builder since 2026-11-15 (before: Settings > Spell Tomes): one toggle per segment, live, so a switch counts at the next build
   without a rescan). The tree filter (`isTreeCandidate`, `IsBuildCandidate`) is the tome candidate rule, then
   the source, with Include bringing any one spell back (unreachable ones too, in Spell Research as well).
   The gate only flags: its "out" band joins "Spells to look over" and no longer leaves a spell out of the
   tree, a saved tree's load (`ValidateAndFixTree`) or Spell Research (`GateLeavesOut` became `GateKept`).
   Gate Review's "Left out by itself" and "Quest only" lists became one "Specialty sources" list, the segment
   and route detail as the reason. `SpellScannerDuplicates.cpp` and `ScanRecordRules.h` read the unreachable
   set, which keeps its meaning: unchanged.
   Choices made here: the review segment stays on the tree (the gate's review band does too) and Gate Review
   lists it to look over (changed 2026-11-16 on fork/feat/tree-builder-ui: review and unknown segments follow a
   new `unreachable` switch, off by default; docs/TOME_DISTRIBUTION.md); archives in game are read in the game's order (stage 3, corrected the same day from
   name order within the Data folder); the game's tome pairs
   (SpellTomes) are the model's tomes, each teaching its pair's spell whatever its DATA says (a non-teaching
   book Spell Research's Synthesizer pairs; since later the same day also a book a DLL rebinds, and a book
   with no pair is no tome); spells of a
   switched-off segment are still registered with Spell Research (their tomes can be found). Checks: the harness
   output is byte-identical to stage 4 (`--graph`, `--sources`), the reach pass's baseline and self-test (51/51)
   unchanged, `graft-check --self-test` with the new rule cases, `node run-tests.js` (TomeSourceSetting,
   GateReview, TomeFilter).

The current reach pass keeps running until stage 5 replaces its answer; then it stays as the fallback (decision 6).
