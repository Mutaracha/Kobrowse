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
        require(w.m_deviceStatus->height() < 50, "compact status row");
        w.showSettings(true); QApplication::processEvents();
        require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "settings fit screen");
        const int font = row("list_pt"), family = row("list_font"), hold = row("long_tap_ms");
        const int hidden = row("show_hidden"), adds = row("allow_adds_edit"), books = row("open_books");
        QPushButton *yes = w.findChild<QPushButton *>("confirmYes");
        QPushButton *no = w.findChild<QPushButton *>("confirmNo");
        QPushButton *reset = w.findChild<QPushButton *>("resetTab");
        QPushButton *close = w.findChild<QPushButton *>("closeSettings");
        require(yes && no && reset && close, "settings controls exist");
        require(!w.findChild<QPushButton *>("resetAll"), "no global reset");
        require(w.m_sliders[hold] && w.m_minusBtns[hold] && w.m_plusBtns[hold], "hold time uses slider and buttons");
        require(w.m_sliders[font] && w.m_minusBtns[font] && w.m_plusBtns[font], "font slider has buttons");
        const int oldHold = LONG_TAP_MS; w.m_plusBtns[hold]->click();
        require(LONG_TAP_MS == oldHold + 50 && w.m_settings->isVisible(), "hold setting saved immediately");
        w.stepSetting(adds, 1); QApplication::processEvents();
        require(w.m_confirm->isVisible() && !ALLOW_ADDS, "adds asks confirmation");
        require(qAbs(w.m_confirm->geometry().center().x() - w.rect().center().x()) < 3
                && qAbs(w.m_confirm->geometry().center().y() - w.rect().center().y()) < 3, "confirmation centered");
        no->click(); require(!ALLOW_ADDS, "declining leaves adds protected");
        w.stepSetting(adds, 1); yes->click(); require(ALLOW_ADDS, "confirmation persists adds");
        QPushButton *choice = w.findChild<QPushButton *>(QString("settingChoice_%1").arg(books));
        require(choice, "current book choice is clickable"); choice->click();
        require(w.m_fontPicker->isVisible() && w.m_fontPicker->currentItem()
            && w.m_fontPicker->currentItem()->text(0).startsWith(QString::fromUtf8("✓")), "current choice marked");
        w.chooseFont(w.m_fontPicker->topLevelItem(3)); require(OPEN_BOOKS == "0", "book choice applies immediately");
        w.setSettingsTab(1); QApplication::processEvents();
        require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "font tab fits screen");
        choice = w.findChild<QPushButton *>(QString("settingChoice_%1").arg(family));
        require(choice, "current font choice is clickable"); choice->click();
        require(w.m_fontPicker->currentItem() && w.m_fontPicker->currentItem()->text(0).startsWith(QString::fromUtf8("✓")), "current font marked");
        w.chooseFont(w.m_fontPicker->topLevelItem(0));
        const int beforeFont = LIST_PX; w.m_plusBtns[font]->click();
        require(LIST_PX > beforeFont && w.m_list->font().pixelSize() == LIST_PX, "font applies immediately");
        reset->click(); no->click(); require(LIST_PX > beforeFont, "reset cancellation preserves setting");
        reset->click(); yes->click(); require(LIST_PX == ptToPx(8), "font reset applies immediately");
        require(QFileInfo(homeFile).exists(), "font reset preserves home");
        w.setSettingsTab(0); w.stepSetting(hidden, 1); require(!SHOW_HIDDEN, "hidden toggle applied");
        const QString path = w.m_root + "/.adds"; w.m_path = path;
        w.m_clip = QStringList() << (w.m_root + "/.adds/archive-source");
        close->click(); QApplication::processEvents();
        require(w.isVisible() && !w.m_settings->isVisible() && w.m_path == path && w.m_clip.size() == 1, "close retains folder and clipboard");
        w.showSettings(true); require(w.m_setVals[adds] == "1" && w.m_setVals[hidden] == "0", "saved values persist");
        const char *savedIni = KOBROWSE_INI; const QByteArray badIni = (w.m_root + "/.adds/no-such/config").toUtf8();
        KOBROWSE_INI = badIni.constData(); w.stepSetting(hidden, 1);
        require(!SHOW_HIDDEN && w.m_setVals[hidden] == "0" && !w.m_settingsNotice->text().isEmpty(), "write error rolls back");
        KOBROWSE_INI = savedIni;
        w.m_pendingDelete = 7; w.m_resetTabPending = 0; w.confirmDeletion();
        require(!ALLOW_ADDS && SHOW_HIDDEN && !QFileInfo(homeFile).exists(), "general reset applied");
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
    require(initial.contains("; retain comment") && initial.contains("unknown=keep") && !initial.contains("scale_pct=125") && !initial.contains("icon_pt=22") && !initial.contains("btn_radius_pt=19"), "obsolete settings removed and unknown preserved");
    require(saveConfig(QStringList() << "show_hidden", QStringList() << "1"), "save config");
    QFile afterSave(QString::fromUtf8(ini)); require(afterSave.open(QIODevice::ReadOnly), "read saved ini");
    const QByteArray saved = afterSave.readAll(); afterSave.close();
    require(saved.contains("[other]\nscale_pct=777") && saved.contains("[after]\nshow_hidden=other"), "save leaves unrelated section unchanged");
    require(saved.contains("; retain comment") && saved.contains("unknown=keep") && !saved.contains("icon_pt=22"), "save keeps unknown settings and comments");
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
