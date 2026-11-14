# Where a spell's tomes come from: distribution segments for the tree - design

Date 2026-11-14. Branch: feature/tome-distribution. Status: design, decisions below are the author's; stage 1 in
progress.

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

**Segments**, first match wins: no route -> unreachable; general sale; specialty mod vendor; wide drops (generic
or faction routes other than local / regional); specific (placed, encounter, local / regional faction); quest
reward; crafted; uncommon vendor; anything else -> review. (Python's bucket rules put reward and crafted with
specific; they get their own switches here.)

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
3. **Configs and scripts**: a SPID line parser with filter kinds, SkyPatcher `addToLLs`, VMAD properties, PEX
   reading (loose and BSA) and the property-to-call data flow.
4. **Classes, specialty mod vendors, segments**.
5. **Scan and UI**: a `source` object per spell in the scan (segment, class, subtype, detail); `tomeUnreachable`
   and `questOnly` derived from it; the segment switches in the settings; the tree filters
   (`proceduralTreeBuilder.js`, `GraftCandidates.h`); a "Specialty sources" list in Gate Review; the gate's "out"
   band as a review reason; the other readers of the unreachable set (`LibrarianSpellResearchPatch.cpp`,
   `SpellScannerDuplicates.cpp`, `ScanRecordRules.h`); docs.

The current reach pass keeps running until stage 5 replaces its answer; then it stays as the fallback (decision 6).
