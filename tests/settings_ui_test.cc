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
        w.resize(768, 1024);
        w.show(); QApplication::processEvents();
        w.showMessage(QString::fromUtf8("Очень длинное сообщение об ошибке, которое должно поместиться не более чем в две строки внизу экрана и не сдвигать список файлов"));
        require(w.m_footerNotice->text().size() > 0, "file notice goes to footer");
        require(footerNoticeLines(w.m_footerNotice->text(), w.m_footerNotice->font(), 80).size() <= 2, "footer notice bounded to two lines");
        w.showSettings(true); QApplication::processEvents();
        w.showMessage(QString::fromUtf8("Ошибка настроек"));
        require(!w.m_settingsNotice->text().isEmpty(), "settings notice in settings footer");
        require(w.width() == 768, "settings fit a 768 pixel window");
        require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "general tab fits without horizontal scrolling");
        for (int i = 0; i < SET_COUNT; ++i) {
            require(settingGroup(u8(SET_ITEMS[i].key)) != GroupUnknown, "every setting assigned to a group");
            require(w.m_settingRows[i] && w.m_settingLines[i], "every setting has a widget");
        }
        require(w.m_groupBlocks[GroupFiles]->isVisible() && w.m_groupBlocks[GroupTrash]->isVisible(), "general groups visible");
        require(!w.m_groupBlocks[GroupAppearance]->isVisible(), "view group hidden on general tab");
        const int hidden = row("show_hidden"), scale = row("scale_pct"), font = row("list_pt");
        w.m_setVals[hidden] = "0"; w.m_setVals[scale] = "150"; w.m_setVals[font] = "15";
        w.m_resetHomeDraft = false;
        w.setSettingsTab(1); QApplication::processEvents();
        require(w.m_groupBlocks[GroupAppearance]->isVisible(), "view group visible");
        require(!w.m_groupBodies[GroupAppearanceAdvanced]->isVisible(), "advanced appearance collapsed");
        w.m_advancedButtons[GroupAppearanceAdvanced]->click(); QApplication::processEvents();
        require(w.m_groupBodies[GroupAppearanceAdvanced]->isVisible(), "advanced appearance expands");
        require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "expanded view tab fits without horizontal scrolling");
        w.m_advancedButtons[GroupAppearanceAdvanced]->click(); QApplication::processEvents();
        require(!w.m_groupBodies[GroupAppearanceAdvanced]->isVisible(), "advanced appearance collapses");
        QPushButton *resetTab = w.findChild<QPushButton *>("resetTab");
        QPushButton *yes = w.findChild<QPushButton *>("confirmYes");
        QPushButton *no = w.findChild<QPushButton *>("confirmNo");
        require(resetTab && yes && no, "reset and confirmation controls exist");
        resetTab->click();
        require(w.m_confirm->isVisible() && w.m_setVals[scale] == "150", "reset requests confirmation before changing draft");
        no->click();
        require(!w.m_confirm->isVisible() && w.m_setVals[scale] == "150", "reset confirmation cancel preserves draft");
        resetTab->click(); yes->click();
        require(w.m_setVals[scale] == cfgDefault("scale_pct"), "tab reset sets view default");
        require(w.m_setVals[hidden] == "0" && w.m_setVals[font] == "15", "tab reset preserves other tabs");
        require(!w.m_resetHomeDraft && QFileInfo(homeFile).exists(), "view reset leaves home unchanged");
        w.setSettingsTab(2); QApplication::processEvents();
        require(!w.m_groupBodies[GroupKeyboard]->isVisible(), "keyboard group starts collapsed");
        w.m_advancedButtons[GroupKeyboard]->click(); QApplication::processEvents();
        require(w.m_groupBodies[GroupKeyboard]->isVisible(), "keyboard group expands");
        require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "expanded fonts tab fits without horizontal scrolling");
        const int key = row("key_font_pt");
        w.m_setVals[key] = "0"; w.refreshSettings();
        require(w.m_setLabels[key]->text().startsWith(QString::fromUtf8("Авто\n")), "zero displays auto");
        require(w.m_sliders[key] && w.m_sliders[key]->value() == 0, "font slider starts at minimum");
        w.m_setVals[key] = "24"; w.refreshSettings();
        require(w.m_sliders[key]->value() == 24, "font slider reaches maximum");
        w.m_sliders[key]->begin(0); w.m_sliders[key]->finish();
        require(w.m_setVals[key].toDouble() == 0, "slider updates draft");
        w.setSettingsTab(0); QApplication::processEvents();
        const int adds = row("allow_adds_edit");
        require(w.m_setVals[adds] == "0" && !ALLOW_ADDS, "adds editing disabled by default");
        w.stepSetting(adds, 1);
        require(w.m_confirm->isVisible() && w.m_setVals[adds] == "0", "enabling requires confirmation");
        no->click();
        require(w.m_setVals[adds] == "0" && !w.m_switches[adds]->isChecked(), "declining preserves disabled draft");
        w.stepSetting(adds, 1); yes->click();
        require(w.m_setVals[adds] == "1", "confirmation enables adds editing in draft");
        w.stepSetting(adds, 1);
        require(w.m_setVals[adds] == "0" && !w.m_confirm->isVisible(), "disabling requires no confirmation");
        w.stepSetting(adds, 1); yes->click();
        const int books = row("open_books");
        const QString before = w.m_setVals[books];
        w.openBookPicker(books); w.chooseFont(w.m_fontPicker->topLevelItem(0));
        require(w.m_setVals[books] == before, "book picker cancel retains draft");
        w.openBookPicker(books); w.chooseFont(w.m_fontPicker->topLevelItem(2));
        require(w.m_setVals[books] == "1", "book picker explicitly selects always");
        w.openBookPicker(books); w.chooseFont(w.m_fontPicker->topLevelItem(3));
        require(w.m_setVals[books] == "0", "book picker selects never");
        w.openBookPicker(books); w.chooseFont(w.m_fontPicker->topLevelItem(1));
        require(w.m_setVals[books] == "auto", "book picker selects auto");
        w.m_pendingDelete = 7; w.m_resetTabPending = 0; w.confirmDeletion();
        require(w.m_resetHomeDraft && QFileInfo(homeFile).exists(), "general reset stages home reset only");
        require(w.m_setVals[adds] == "0", "general reset disables adds editing");
        w.m_setVals[font] = "15"; w.m_pendingDelete = 4; w.confirmDeletion();
        require(w.m_setVals[font] == cfgDefault("list_pt"), "all reset covers fonts");
        require(QFileInfo(homeFile).exists(), "reset does not write home before save");
        w.showSettings(false); w.showSettings(true);
        require(!w.m_resetHomeDraft && QFileInfo(homeFile).exists(), "cancel discards home draft");
        require(w.m_setVals[scale] == "125", "cancel discards values");
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
    file.write("[other]\nscale_pct=777\n[kobrowse]\n; retain comment\nscale_pct=125\nunknown=keep\n[after]\nshow_hidden=other\n"); file.close();
    QFile homeFile(QString::fromUtf8(home)); require(homeFile.open(QIODevice::WriteOnly), "create home");
    homeFile.write(root); homeFile.close();
    loadConfig();
    QFile afterLoad(QString::fromUtf8(ini)); require(afterLoad.open(QIODevice::ReadOnly), "read ini");
    const QByteArray initial = afterLoad.readAll(); afterLoad.close();
    require(initial.contains("[other]\nscale_pct=777") && initial.contains("[after]\nshow_hidden=other"), "other sections survive defaults");
    require(initial.contains("; retain comment") && initial.contains("unknown=keep"), "unknown values and comments survive defaults");
    require(saveConfig(QStringList() << "scale_pct" << "show_hidden", QStringList() << "125" << "1"), "save config");
    QFile afterSave(QString::fromUtf8(ini)); require(afterSave.open(QIODevice::ReadOnly), "read saved ini");
    const QByteArray saved = afterSave.readAll(); afterSave.close();
    require(saved.contains("[other]\nscale_pct=777") && saved.contains("[after]\nshow_hidden=other"), "save leaves unrelated section unchanged");
    require(saved.contains("; retain comment") && saved.contains("unknown=keep"), "save keeps unknown keys and comments");
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
