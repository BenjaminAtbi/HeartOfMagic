# Fact sheet: Heart of Magic Librarian 1.10.0 (draft source for the Nexus texts)

Baseline: the Nexus release 1.9.3 (git a5496f5f). Only what a player coming from 1.9.3 notices. Every line below is
in CHANGELOG.md "## Unreleased". Item 5 goes in only if its game test passes (user decision 2026-11-20).

Version: 1.10.0 (proposed: new features since 1.9.3). Install over 1.9.3; no save changes for items 1-4.

1. Staff casts give learning XP (needs the staff mod SCSSoulCharge, the author's own mod; its Nexus page name and
   link: to be filled in when that page is public). A staff cast of a spell bound to a staff now counts as casting
   that spell: the same XP as by hand, once per cast, for concentration, fire-and-forget and summon spells, from either
   hand. A concentration staff's effect is weakened like the spell when the spell is early-learned. Without the staff
   mod nothing changes. A staff's own enchantment (vanilla staves, Staff Enchanting Plus) gives no XP, as before.
   Known: Dynamic Magic Modification Framework 2.0.3 drops the game's own cast report for staff casts; the staff mod
   reports them instead, so the XP works with or without DMMF.
2. The staff mod marks the bound spell's name while the staff is held (default " ★R" / " ★L"); Heart of Magic's
   own stored names never keep that mark.
3. Magic Menu Rewrite block (ships with Heart of Magic): with no learning target in a school, the next-spell rows on
   the progress card now show "???" for a spell below the panel's Reveal Name %, like the tree, its tooltip and the
   node card. A school's starting spell keeps its name in the tooltip, the node card and the rows (and a click opens
   its item card) while the panel's "Show Root Spell Names" is on (the default), as the panel's own tooltip does.
4. Panel drop-down lists (language, design, notification language, Pre Req Master's School Distribution and Pool Source): the open list now follows the
   Font Size setting and the design's colours, instead of a small white list. Escape with a list open closes only
   the list.
5. (Only if its test passes) Spell Research: "Re-judge vanilla spells by their records", Settings > Spell Tome
   Learning, under the Spell Research line, off by default. Spell Research files vanilla and DLC spells under the
   school and level its own import script carries, so an overhaul that changes a vanilla spell's school or level
   (Requiem) was not seen. When on, those spells move to the school and level their records give now, with their
   tome pairs; a spell a plugin lists in its own record entry stays. spell_research_reclassify.json lists the changes.
   With Spell Research Upgrade and Fixes, rebuild its spell cache after turning it on. Turning it off keeps the
   changes until Spell Research's Import runs again.

Not for the player text: internal reviews, tools, logs, file names other than spell_research_reclassify.json.
Credits: no code or assets from other authors were added in 1.10.0. (Engine facts came from Address Library IDs and
our own reading of the game; TheNewGentleman's MIT source was read as a reference for list entries, nothing copied.)
