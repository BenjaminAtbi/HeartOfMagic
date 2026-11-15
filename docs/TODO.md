# TODO

Open changes for the fork, in no particular order. Each item says what to change; finished items move out (into
the commit message and, for the tome distribution, the design spec in `docs/superpowers/specs/`).

## Open

1. **Mouse movement.** Check the mouse movement optimisation, and make the menu's aspect ratio match the
   system's, or change the mouse speed.
2. **Practice keeps unknown names hidden.** When practising a spell, don't reveal its name if the player didn't
   already know it.
3. **Vertical bar trees.** Fix their layout (probably needs a different tree generation algorithm), and fix the
   preview and the graphic elements for them.
4. **Root spells.** A button that teaches the player the root spell of each school.
5. **Spell tags from config files.** Ingest spell tags from config files, which adds implicit support for aspects.
6. **Tags force spells on or off the tree.** Spells with certain tags can be forced onto or off the tree.
7. **Tag locks.** Spells with certain tags stay locked until the player learns at least one of them by other means.
8. **Master spells and the rituals.** Master spells stay locked until the player completes the school's master
   ritual quest.
9. **Insight study model.** A new study model: XP converts into insight points, and insight is spent to learn
   spells, so the player chooses a path of progression rather than casting to learn.
10. **Scan menu UI:**
    - better tooltips and descriptions;
    - the Easy/Complex switch in the same bar as the scan buttons;
    - extra options out of the dropdown menu;
    - expanded whitelist and blacklist menus;
    - stats for each spell source category, and a view of each category's spells with the option to whitelist
      or blacklist each one;
    - changing tree parameters doesn't update the preview until Apply, or the preview gets much faster.
11. **Learning settings.** Clean up the learning menu settings.
12. **Roots from known spells.** When a school's root spell is chosen and the player already knows spells of that
    school that could be the root, one of them must be the root. An option in the root menu such as "Select root
    from known spells".
13. **Top of the scan menu.** Reorganise its top section (scan details, excluded spells, etc.). The advertised
    spell count shows the available tomes first, with a small mention of the total spell count, instead of the
    reverse.
14. **Presets.** Rebuild the settings preset system, combining scan and settings profiles.
