#ifndef KOBROWSE_SETTINGS_MODEL_H
#define KOBROWSE_SETTINGS_MODEL_H
#include <QString>
#include <QStringList>
#include <QtGlobal>

// Stable grouping of existing ini keys; group order is also display order.
enum SettingGroup {
    GroupFiles, GroupTrash, GroupBehavior, GroupBooks, GroupLogging,
    GroupAppearance, GroupAppearanceAdvanced,
    GroupListFont, GroupButtonFont, GroupInfoFont, GroupSearchFont, GroupKeyboard,
    GroupCount, GroupUnknown = -1
};

inline SettingGroup settingGroup(const QString &key) {
    struct Mapping { const char *key; SettingGroup group; };
    static const Mapping map[] = {
        {"show_hidden", GroupFiles}, {"trash_enabled", GroupTrash},
        {"long_tap_ms", GroupBehavior}, {"idle_min", GroupBehavior},
        {"open_books", GroupBooks}, {"logging_enabled", GroupLogging},
        {"scale_pct", GroupAppearance}, {"btn_height_pt", GroupAppearance},
        {"icon_pt", GroupAppearance}, {"gap_pt", GroupAppearance},
        {"btn_pad_pt", GroupAppearanceAdvanced}, {"btn_radius_pt", GroupAppearanceAdvanced},
        {"btn_line_pt", GroupAppearanceAdvanced},
        {"list_pt", GroupListFont}, {"list_font", GroupListFont},
        {"btn_pt", GroupButtonFont}, {"btn_font", GroupButtonFont},
        {"info_pt", GroupInfoFont}, {"search_list_pt", GroupSearchFont},
        {"search_list_font", GroupSearchFont},
        {"opds_key_pt", GroupKeyboard}, {"opds_func_pt", GroupKeyboard},
        {"key_font_pt", GroupKeyboard}, {"func_pt", GroupKeyboard}
    };
    for (const Mapping &m : map) if (key == QLatin1String(m.key)) return m.group;
    return GroupUnknown;
}
inline int settingGroupTab(SettingGroup group) {
    if (group >= GroupFiles && group <= GroupLogging) return 0;
    if (group >= GroupAppearance && group <= GroupAppearanceAdvanced) return 1;
    if (group >= GroupListFont && group <= GroupKeyboard) return 2;
    return -1;
}
inline bool settingGroupAdvanced(SettingGroup group) {
    return group == GroupAppearanceAdvanced || group == GroupKeyboard;
}
inline QString settingNumberDisplay(const QString &value, double lo, double hi, bool autoZero) {
    bool ok = false;
    const double number = QString(value).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    const QString shown = autoZero && ok && number == 0 ? QString::fromUtf8("Авто") : value;
    return shown + QLatin1Char('\n') + QString::number(lo, 'g', 6)
        + QChar(0x2013) + QString::number(hi, 'g', 6);
}
// -1 means all tabs. Invalid metadata must never be reset accidentally.
inline void resetSettingsDraft(const QStringList &keys, const QStringList &defaults,
                               QStringList &values, int tab) {
    const int count = qMin(keys.size(), qMin(defaults.size(), values.size()));
    for (int i = 0; i < count; ++i) {
        const int itemTab = settingGroupTab(settingGroup(keys[i]));
        if (itemTab >= 0 && (tab == -1 || itemTab == tab)) values[i] = defaults[i];
    }
}
#endif
