/**
 * SourceListTabs - the right half of Spell Inclusion: two tabs, Spells (the Blacklisted and
 * Whitelisted previews, written by scanStage.js) and Plugins (the plugin list, once a window:
 * settingsPanel.js renderWhitelistEntries and its helpers fill the same ids).
 *
 * A change on the Plugins page saves at once (pluginListChanged: autoSaveSettings, updatePrimedCount) - there
 * is no Done button any more. The page is filled when it opens and again after a scan while it shows.
 *
 * Depends on: renderWhitelistEntries, setupWhitelistListeners, autoSaveSettings (settingsPanel.js),
 *   updatePrimedCount (optional)
 */

var SourceListTabs = {

    PAGES: { spells: 'sourceListSpells', plugins: 'pluginListPanel' },

    current: 'spells',

    _doc: function() { return document; },

    init: function() {
        var self = this;
        var tabs = this._doc().querySelectorAll('#sourceListTabs [data-source-tab]');
        for (var i = 0; i < tabs.length; i++) {
            (function(tab) {
                tab.addEventListener('click', function() { self.show(tab.getAttribute('data-source-tab')); });
            })(tabs[i]);
        }
    },

    /** Show one tab's page; Plugins is filled each time it opens */
    show: function(tab) {
        if (!this.PAGES.hasOwnProperty(tab)) return;
        var doc = this._doc();
        this.current = tab;
        var tabs = doc.querySelectorAll('#sourceListTabs [data-source-tab]');
        for (var i = 0; i < tabs.length; i++) tabs[i].classList.toggle('active', tabs[i].getAttribute('data-source-tab') === tab);
        for (var key in this.PAGES) {
            var page = doc.getElementById(this.PAGES[key]);
            if (page) page.classList.toggle('source-list-hidden', key !== tab);
        }
        if (tab === 'plugins') this._fillPlugins();
    },

    /** After a scan: the plugin list again when it shows (new plugins, new counts) */
    refresh: function() {
        if (this.current === 'plugins') this._fillPlugins();
    },

    _fillPlugins: function() {
        var search = this._doc().getElementById('whitelist-search');
        if (search) search.value = '';
        if (typeof renderWhitelistEntries === 'function') renderWhitelistEntries();
        if (typeof setupWhitelistListeners === 'function') setupWhitelistListeners();
    }
};

/** A change the player made on the Plugins page: saved (debounced) and counted at once */
function pluginListChanged() {
    if (typeof autoSaveSettings === 'function') autoSaveSettings();
    if (typeof updatePrimedCount === 'function') updatePrimedCount();
}

if (typeof document !== 'undefined' && document.addEventListener) {
    document.addEventListener('DOMContentLoaded', function() { SourceListTabs.init(); });
}
