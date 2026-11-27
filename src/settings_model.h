#ifndef KOBROWSE_SETTINGS_MODEL_H
#define KOBROWSE_SETTINGS_MODEL_H
#include <QString>
#include <QStringList>
#include <QtGlobal>

enum SettingGroup {
    GroupFiles, GroupTrash, GroupBehavior, GroupBooks, GroupLogging, GroupListFont,
    GroupCount, GroupUnknown = -1
};
inline SettingGroup settingGroup(const QString &key) {
    struct Mapping { const char *key; SettingGroup group; };
    static const Mapping map[] = {
        {"show_hidden", GroupFiles}, {"allow_adds_edit", GroupFiles}, {"trash_enabled", GroupTrash},
        {"long_tap_ms", GroupBehavior},
        {"open_books", GroupBooks}, {"logging_enabled", GroupLogging},
        {"list_pt", GroupListFont}, {"list_font", GroupListFont}
    };
    for (const Mapping &m : map) if (key == QLatin1String(m.key)) return m.group;
    return GroupUnknown;
}
inline int settingGroupTab(SettingGroup group) {
    if (group >= GroupFiles && group <= GroupLogging) return 0;
    if (group == GroupListFont) return 1;
    return -1;
}
inline bool settingGroupAdvanced(SettingGroup) { return false; }
inline QString settingNumberDisplay(const QString &value, double lo, double hi, bool autoZero) {
    bool ok = false;
    const double number = QString(value).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    const QString shown = autoZero && ok && number == 0 ? QString::fromUtf8("Авто") : value;
    return shown + QLatin1Char('\n') + QString::number(lo, 'g', 6)
        + QChar(0x2013) + QString::number(hi, 'g', 6);
}
inline void resetSettingsDraft(const QStringList &keys, const QStringList &defaults,
                               QStringList &values, int tab) {
    const int count = qMin(keys.size(), qMin(defaults.size(), values.size()));
    for (int i = 0; i < count; ++i) {
        const int itemTab = settingGroupTab(settingGroup(keys[i]));
        if (itemTab >= 0 && (tab == -1 || itemTab == tab)) values[i] = defaults[i];
    }
}
#endif
