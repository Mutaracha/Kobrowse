// Exercise the real settings window against an isolated temporary onboard tree.
#define KOBROWSE_SETTINGS_TEST
#include "../src/kobrowse.cc"
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdlib>

extern "C" void nh_log(const char *, ...) {}
extern "C" void nh_dump_log() {}

static void require(bool ok, const char *message) {
    if (!ok) { QTextStream(stderr) << "FAIL: " << message << '\n'; std::exit(1); }
}
static int row(const char *key) {
    for (int i = 0; i < SET_COUNT; ++i)
        if (QString::fromLatin1(SET_ITEMS[i].key) == QString::fromLatin1(key)) return i;
    require(false, "setting exists"); return -1;
}
class SettingsUiTest {
public:
    static void run(KobrowseWindow &w, const QString &homeFile) {
        w.resize(768, 1024); w.show(); QApplication::processEvents();
        w.showMessage(QString::fromUtf8("Длинное сообщение для нижнего колонтитула"));
        require(!w.m_footerNotice->text().isEmpty(), "file notice goes to footer");
        require(footerNoticeLines(w.m_footerNotice->text(), w.m_footerNotice->font(), 80).size() <= 2, "footer notice bounded");
        require(w.m_deviceStatus->font().pixelSize() <= FONT_MSG, "compact status font");
        require(w.m_deviceStatus->height() < 50, "compact status row");
        w.showSettings(true); QApplication::processEvents();
        require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "general tab fits 768 pixels");
        for (int i = 0; i < SET_COUNT; ++i) {
            require(settingGroup(u8(SET_ITEMS[i].key)) != GroupUnknown, "every visible setting grouped");
            require(w.m_settingRows[i], "every setting has a widget");
        }
        require(settingGroup("scale_pct") == GroupUnknown && settingGroup("icon_pt") == GroupUnknown
                && settingGroup("btn_font") == GroupUnknown, "appearance controls removed");
        const int hidden = row("show_hidden"), font = row("list_pt"), adds = row("allow_adds_edit");
        QPushButton *resetTab = w.findChild<QPushButton *>("resetTab");
        QPushButton *yes = w.findChild<QPushButton *>("confirmYes");
        QPushButton *no = w.findChild<QPushButton *>("confirmNo");
        require(resetTab && yes && no, "confirmation controls exist");
        require(w.m_setVals[adds] == "0" && !ALLOW_ADDS, "adds disabled by default");
        w.stepSetting(adds, 1);
        require(w.m_confirm->isVisible() && w.m_setVals[adds] == "0", "enabling asks confirmation");
        no->click();
        require(w.m_setVals[adds] == "0" && !w.m_switches[adds]->isChecked(), "declining preserves draft");
        w.stepSetting(adds, 1); yes->click();
        require(w.m_setVals[adds] == "1", "confirmation enables adds draft");
        w.setSettingsTab(1); QApplication::processEvents();
        require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "font tab fits 768 pixels");
        require(w.m_sliders[font], "list font size has slider");
        w.m_setVals[font] = "15"; w.refreshSettings();
        resetTab->click(); no->click();
        require(w.m_setVals[font] == "15", "cancel reset preserves draft");
        resetTab->click(); yes->click();
        require(w.m_setVals[font] == cfgDefault("list_pt") && w.m_setVals[adds] == "1", "font reset is scoped");
        require(QFileInfo(homeFile).exists() && !w.m_resetHomeDraft, "font reset preserves home");
        w.m_setVals[font] = "12"; w.m_setVals[hidden] = "0";
        const QString path = w.m_root + "/.adds";
        w.m_path = path;
        w.m_clip = QStringList() << (w.m_root + "/.adds/archive-source");
        w.saveSettings(); QApplication::processEvents();
        require(w.isVisible() && !w.m_settings->isVisible(), "save keeps the window open");
        require(w.m_path == path && w.m_clip.size() == 1, "save retains folder and clipboard");
        require(!w.m_showHidden && ALLOW_ADDS && w.m_list->font().pixelSize() == LIST_PX, "draft applied immediately");
        require(LIST_PX == ptToPx(12), "new list font size applied");
        w.showSettings(true);
        require(w.m_setVals[font].toDouble() == 12 && w.m_setVals[adds] == "1", "saved draft persists");
        w.m_pendingDelete = 7; w.m_resetTabPending = 0; w.confirmDeletion();
        require(w.m_resetHomeDraft && w.m_setVals[adds] == "0", "general reset disables adds");
        w.showSettings(false); w.showSettings(true);
        require(!w.m_resetHomeDraft && w.m_setVals[adds] == "1", "cancel discards reset draft");
        w.close();
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir tmp; require(tmp.isValid(), "temporary root");
    const QString power = tmp.path() + "/power";
    require(QDir().mkpath(power + "/battery"), "battery fixture directory");
    QFile type(power + "/battery/type"); require(type.open(QIODevice::WriteOnly), "battery type"); type.write("Battery\n"); type.close();
    QFile capacity(power + "/battery/capacity"); require(capacity.open(QIODevice::WriteOnly), "battery capacity"); capacity.write("72\n"); capacity.close();
    QFile status(power + "/battery/status"); require(status.open(QIODevice::WriteOnly), "battery status"); status.write("Charging\n"); status.close();
    BatteryStatus battery = readBatteryStatus(power);
    require(battery.percent == 72 && battery.charging, "battery fixture read");
    require(readBatteryStatus(power + "/missing").percent == -1, "missing battery is unknown");
    const QByteArray root = (tmp.path() + "/onboard").toUtf8();
    const QByteArray ini = root + "/.adds/kobrowse/kobrowse.ini";
    const QByteArray home = root + "/.adds/kobrowse/home_path";
    const QByteArray log = root + "/.adds/kobrowse/logs/kobrowse.log";
    const QByteArray trash = root + "/.kobrowse_trash";
    KOBROWSE_ROOT = root.constData(); KOBROWSE_INI = ini.constData();
    KOBROWSE_HOME = home.constData(); KOBROWSE_LOG = log.constData(); KOBROWSE_TRASH = trash.constData();
    require(QDir().mkpath(QFileInfo(QString::fromUtf8(ini)).absolutePath()), "create application directory");
    const QString archiveSource = QString::fromUtf8(root) + "/.adds/archive-source";
    QFile archiveFile(archiveSource); require(archiveFile.open(QIODevice::WriteOnly), "archive fixture"); archiveFile.write("data"); archiveFile.close();
    QString archiveError;
    const QString archive = QString::fromUtf8(root) + "/.adds/fixture.zip";
    require(!createZipArchive(QString::fromUtf8(root), archive, QStringList() << archiveSource, archiveError), "adds archive denied when disabled");
    require(createZipArchive(QString::fromUtf8(root), archive, QStringList() << archiveSource, archiveError, true), "adds archive allowed when enabled");
    QFile file(QString::fromUtf8(ini));
    require(file.open(QIODevice::WriteOnly), "create ini");
    file.write("[other]\nscale_pct=777\n[kobrowse]\n; retain comment\nscale_pct=125\nicon_pt=22\nbtn_radius_pt=19\nunknown=keep\n[after]\nshow_hidden=other\n"); file.close();
    QFile homeFile(QString::fromUtf8(home)); require(homeFile.open(QIODevice::WriteOnly), "create home");
    homeFile.write(root); homeFile.close();
    loadConfig();
    require(qAbs(SCREEN_DPI - 300) < 0.01, "legacy scale does not change density");
    require(FONT_BTN == 32 && ICON_PX == 32 && BTN_RADIUS == 0, "fixed Kobalt-style metrics ignore legacy values");
    QFile afterLoad(QString::fromUtf8(ini)); require(afterLoad.open(QIODevice::ReadOnly), "read ini");
    const QByteArray initial = afterLoad.readAll(); afterLoad.close();
    require(initial.contains("[other]\nscale_pct=777") && initial.contains("[after]\nshow_hidden=other"), "other sections survive defaults");
    require(initial.contains("; retain comment") && initial.contains("unknown=keep"), "unknown values and comments survive defaults");
    require(saveConfig(QStringList() << "scale_pct" << "show_hidden", QStringList() << "125" << "1"), "save config");
    QFile afterSave(QString::fromUtf8(ini)); require(afterSave.open(QIODevice::ReadOnly), "read saved ini");
    const QByteArray saved = afterSave.readAll(); afterSave.close();
    require(saved.contains("[other]\nscale_pct=777") && saved.contains("[after]\nshow_hidden=other"), "save leaves unrelated section unchanged");
    require(saved.contains("; retain comment") && saved.contains("unknown=keep") && saved.contains("icon_pt=22") && saved.contains("btn_radius_pt=19"), "save keeps legacy settings and comments");
    const QByteArray outside = (tmp.path() + "/outside.ini").toUtf8();
    QFile external(QString::fromUtf8(outside)); require(external.open(QIODevice::WriteOnly), "create external fixture");
    external.write("untouched"); external.close();
    const QByteArray link = root + "/.adds/kobrowse/config-link";
    require(::symlink(outside.constData(), link.constData()) == 0, "create config symlink");
    KOBROWSE_INI = link.constData();
    require(!saveConfig(QStringList() << "scale_pct", QStringList() << "100"), "symlink config save rejected");
    loadConfig();
    require(external.open(QIODevice::ReadOnly) && external.readAll() == "untouched", "default update does not follow config symlink");
    external.close(); KOBROWSE_INI = ini.constData();
    loadConfig();
    KobrowseWindow *window = new KobrowseWindow(nullptr);
    SettingsUiTest::run(*window, QString::fromUtf8(home));
    QTextStream(stdout) << "Settings UI tests passed\n";
}
