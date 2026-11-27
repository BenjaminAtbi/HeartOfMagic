/**
 * Dev Harness - the spell card's data: what C++'s GetSpellInfo / GetSpellInfoBatch send
 * (SpellScannerSpellInfo.cpp), from the scan's own record of each spell
 * (window._mockSpellData: the game's spell_scan_output.json under the dev renderer, or the
 * test spells).
 *
 * - effects: the scan's effects (the same BuildEffectJson fields the game sends)
 * - description: the first described effect's that is not Hide in UI (one that is, as a last
 *   resort), with <mag>/<dur>/<area> filled in from that effect and other <> marks dropped
 *   (ResolveDescriptionTags)
 * - chips: the scan's traits without the value.* ones, the card's first MAX_CHIPS: the scan
 *   builds traits with the chips' rules and the catalog's elements, without the chips' limit
 *   (BuildSpellTraits, MergeCatalogElements)
 * No icon keys: the icon packs are the game's files.
 *
 * Loaded before dev-harness-bridge.js, which answers the calls with mockSpellInfo.
 */

var MOCK_SPELL_INFO_MAX_CHIPS = 6;   // Librarian::MAX_CARD_CHIPS

function mockSpellInfoDescription(effects) {
    var shown = '', hidden = '';
    for (var i = 0; i < effects.length; i++) {
        var e = effects[i];
        if (!e || typeof e.description !== 'string' || !e.description) continue;
        var isHidden = !!(e.flags && e.flags.hideInUI);
        if (isHidden ? hidden : shown) continue;
        var text = e.description;
        if (text.indexOf('<') >= 0) {
            text = text.replace(/<mag>/gi, String(Math.round(+e.magnitude || 0)))
                .replace(/<dur>/gi, String(Math.trunc(+e.duration || 0)))
                .replace(/<area>/gi, String(Math.trunc(+e.area || 0)))
                .replace(/<([^<>]*)>/g, '$1');
        }
        if (isHidden) hidden = text; else shown = text;
    }
    return shown || hidden;
}

function mockSpellInfo(formId) {
    var spell = (window._mockSpellData || []).find(function(s) { return s.formId === formId; });
    if (!spell) return { formId: formId, notFound: true };
    var level = spell.skillLevel || 'Novice';
    var effects = Array.isArray(spell.effects) ? spell.effects : [];
    var type = spell.castingType || 'Fire and Forget';
    return {
        formId: spell.formId, name: spell.name, school: spell.school,
        editorId: spell.editorId || '',
        level: level, skillLevel: level, minimumSkill: spell.minimumSkill || 0,
        cost: spell.magickaCost || 0, magickaCost: spell.magickaCost || 0,
        type: type, castingType: type, delivery: spell.delivery || '',
        persistentId: spell.persistentId, plugin: spell.plugin || (spell.persistentId || '').split('|')[0],
        effects: effects,
        effectNames: effects.map(function(e) { return e.name; }),
        description: mockSpellInfoDescription(effects),
        chips: (spell.traits || []).filter(function(t) { return typeof t === 'string' && t.indexOf('value.') !== 0; })
            .slice(0, MOCK_SPELL_INFO_MAX_CHIPS),
        isWeakened: false, effectiveness: 100
    };
}
