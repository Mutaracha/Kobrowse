// Exercise the real settings window against an isolated temporary onboard tree.
#define KOBROWSE_SETTINGS_TEST
#include "../src/kobrowse.cc"
#include <QTemporaryDir>
#include <QTextStream>
#include <QTouchDevice>
#include <QStandardItemModel>
#include <cstdlib>

class FakeBrightness : public BrightnessBackend {
public:
    int level = 40, original = -1, begins = 0, saves = 0, cancels = 0;
    bool available = true, fail = false;
    bool begin() override { ++begins; if (!available) return false; original = level; return true; }
    int startLevel() const override { return original; }
    int setLevel(int value) override { if (fail) return -1; return level = qBound(0, value, 100); }
    bool finish(bool save) override {
        if (save) ++saves; else { ++cancels; level = original; }
        original = -1; return true;
    }
};

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
    static void testFolderIcons(KobrowseWindow &w) {
        QStandardItemModel model(1, 1);
        const QModelIndex index = model.index(0, 0);
        model.setData(index, QString::fromUtf8("folder"), Qt::DisplayRole);
        model.setData(index, true, ROLE_DIR);
        QStyleOptionViewItem option; option.rect = QRect(0, 0, 400, 90);
        const QPoint inside(6 + ICON_PX, 10 + ICON_PX);
        for (int mode = 0; mode < 3; ++mode) {
            model.setData(index, mode == 1, ROLE_READONLY);
            model.setData(index, mode == 2, ROLE_MARK);
            QImage image(400, 90, QImage::Format_RGB32); image.fill(Qt::white);
            QPainter painter(&image); w.m_fileDelegate->paint(&painter, option, index); painter.end();
            require(QColor(image.pixel(inside)) == itemIconColor(mode == 1, mode == 2), "folder icon has filled correct ink");
        }
    }
    static void testNames(KobrowseWindow &w) {
        QStandardItemModel model(1, 1);
        const QModelIndex index = model.index(0, 0);
        const QString name = QString::fromUtf8("A very long book title with a lot of words and a recognizable ending.epub");
        model.setData(index, name, Qt::DisplayRole);
        for (int width : {320, 600, 768}) {
            w.resize(width, 1024); QApplication::processEvents();
            const QString shown = w.m_fileDelegate->displayedName(index);
            require(!shown.contains(QLatin1Char('\n')), "file name remains a single line");
            const int available = w.m_list->viewport()->width() - qMax(32, 2 * ICON_PX) - 18;
            require(QFontMetrics(w.m_list->font()).width(shown) <= available, "elided name fits viewport");
            if (shown != name) require(shown.endsWith(QString::fromUtf8(".epub")), "elision preserves extension");
        }
        w.resize(768, 1024); QApplication::processEvents();
    }
    static void testFooter(KobrowseWindow &w) {
        const std::vector<int> originalPages = w.m_pageStarts;
        const int originalIndex = w.m_pageIndex, oldFiles = w.m_fileCount, oldDirs = w.m_dirCount;
        w.m_pageStarts.assign(13, 0); w.m_pageIndex = 0; w.m_fileCount = 1; w.m_dirCount = 9;
        w.updatePageInfo(); QApplication::processEvents();
        require(w.m_countLabel->height() >= QFontMetrics(w.m_countLabel->font()).lineSpacing() * 2,
                "footer counters have room for both lines");
        require(w.m_prev->geometry().center().y() == w.m_next->geometry().center().y(), "page controls aligned");
        const int pageX = w.m_pageLabel->x(), pageWidth = w.m_pageLabel->width();
        const int countX = w.m_countLabel->x(), countWidth = w.m_countLabel->width();
        w.m_pageIndex = 9; w.m_fileCount = 999; w.m_dirCount = 99;
        w.updatePageInfo(); QApplication::processEvents();
        require(w.m_pageLabel->x() == pageX && w.m_pageLabel->width() == pageWidth, "page indicator stays fixed");
        require(w.m_countLabel->x() == countX && w.m_countLabel->width() == countWidth, "three-digit count table stays fixed");
        w.m_fileCount = 999999; w.updatePageInfo(); QApplication::processEvents();
        require(w.m_countLabel->width() > countWidth, "larger counts expand without clipping");
        w.m_pageStarts = originalPages; w.m_pageIndex = originalIndex;
        w.m_fileCount = oldFiles; w.m_dirCount = oldDirs; w.updatePageInfo();
    }
    static void touch(KobrowseWindow &w, QEvent::Type type, const QPoint &point, int count = 1) {
        QTouchDevice device; device.setType(QTouchDevice::TouchScreen);
        Qt::TouchPointState state = type == QEvent::TouchBegin ? Qt::TouchPointPressed
            : type == QEvent::TouchEnd ? Qt::TouchPointReleased : Qt::TouchPointMoved;
        QList<QTouchEvent::TouchPoint> points;
        for (int i = 0; i < count; ++i) {
            QTouchEvent::TouchPoint pt(i); pt.setState(state);
            pt.setScreenPos(w.mapToGlobal(point + QPoint(i * 20, 0)));
            points << pt;
        }
        QTouchEvent event(type, &device, Qt::NoModifier, state, points);
        w.handleTouch(&event);
    }
    static void testTouchFeedback(KobrowseWindow &w) {
        QPushButton *button = w.m_countLabel;
        if (w.m_next->isEnabled()) button = w.m_next;
        const QPoint point = button->mapTo(&w, button->rect().center());
        touch(w, QEvent::TouchBegin, point);
        require(button->isDown(), "touch starts pressed feedback");
        touch(w, QEvent::TouchCancel, point, 0);
        require(!button->isDown(), "cancel clears pressed feedback");
        touch(w, QEvent::TouchBegin, point);
        touch(w, QEvent::TouchUpdate, point + QPoint(SWIPE_PX + 10, 0));
        require(!button->isDown(), "drag clears pressed feedback");
        touch(w, QEvent::TouchCancel, point, 0);
        touch(w, QEvent::TouchBegin, point);
        touch(w, QEvent::TouchUpdate, point, 2);
        require(!button->isDown(), "second finger clears pressed feedback");
        touch(w, QEvent::TouchCancel, point, 0);
    }
    static void testExtractAndLogging(KobrowseWindow &w) {
        const QString source = w.m_root + "/fixture-book.txt";
        QFile file(source); require(file.open(QIODevice::WriteOnly) && file.write("fixture") == 7, "extract source fixture"); file.close();
        const QString archive = w.m_root + "/fixture-book.zip";
        QString error;
        require(createZipArchive(w.m_root, archive, QStringList() << source, error), "create UI extract fixture");
        QTreeWidgetItem item; item.setData(0, ROLE_PATH, archive); item.setData(0, ROLE_DIR, false);
        w.onItem(&item);
        require(w.m_confirm->isVisible(), "extract asks before writing");
        require(!QFileInfo(w.m_root + QString::fromUtf8("/Распаковано-fixture-book")).exists(), "no extraction before confirmation");
        w.findChild<QPushButton *>("confirmNo")->click();
        require(!QFileInfo(w.m_root + QString::fromUtf8("/Распаковано-fixture-book")).exists(), "cancel leaves archive untouched");
        w.onItem(&item);
        w.findChild<QPushButton *>("confirmYes")->click();
        require(w.m_busy && w.m_workOverlay->isVisible(), "extract shows busy message");
        touch(w, QEvent::TouchBegin, QPoint(1, 1)); touch(w, QEvent::TouchEnd, QPoint(1, 1));
        require(w.m_busy && w.m_workOverlay->isVisible(), "busy overlay blocks touch");
        w.m_workTimer.stop(); QMetaObject::invokeMethod(&w.m_workTimer, "timeout", Qt::DirectConnection);
        require(!w.m_busy && !w.m_workOverlay->isVisible(), "extract closes busy message");
        require(QFileInfo(archive).exists(), "UI extraction preserves archive");
        require(QFileInfo(w.m_root + QString::fromUtf8("/Распаковано-fixture-book/fixture-book.txt")).exists(),
                "UI extraction publishes contents");
        QFile log(QString::fromUtf8(KOBROWSE_LOG));
        require(log.open(QIODevice::ReadOnly), "diagnostic log created");
        const QByteArray before = log.readAll(); log.close();
        require(before.contains("zip extract start:") && before.contains("zip extract complete:"), "extraction start and result logged");
        require(saveConfig(QStringList() << "logging_enabled", QStringList() << "0"), "disable log fixture");
        klog(QString::fromUtf8("not-logged-marker"));
        require(log.open(QIODevice::ReadOnly) && log.readAll() == before, "disabled logging does not write");
        log.close();
        require(saveConfig(QStringList() << "logging_enabled", QStringList() << "1"), "restore log fixture");
    }
    static void testBrightness(KobrowseWindow &w) {
        FakeBrightness fake; w.m_brightnessBackend = &fake;
        QPoint start = w.m_list->viewport()->mapTo(&w, QPoint(5, w.m_list->viewport()->height() / 2));
        start.setX(1);
        require(start.x() < w.width() * .13, "test starts at physical left edge");
        const int page = w.m_pageIndex;
        touch(w, QEvent::TouchBegin, start);
        touch(w, QEvent::TouchUpdate, start - QPoint(0, 100));
        require(fake.begins == 1 && fake.level > 40 && w.m_brightnessTracking, "edge drag adjusts brightness");
        touch(w, QEvent::TouchEnd, start - QPoint(0, 100));
        require(fake.saves == 1 && w.m_pageIndex == page && !w.m_brightnessTracking, "release saves without turning page");
        const int saved = fake.level;
        touch(w, QEvent::TouchBegin, start);
        touch(w, QEvent::TouchUpdate, start + QPoint(0, 100));
        touch(w, QEvent::TouchCancel, start, 0);
        require(fake.cancels == 1 && fake.level == saved, "touch cancel restores brightness");
        touch(w, QEvent::TouchBegin, start);
        touch(w, QEvent::TouchUpdate, start - QPoint(0, 100));
        touch(w, QEvent::TouchUpdate, start - QPoint(0, 100), 2);
        touch(w, QEvent::TouchEnd, start - QPoint(0, 100));
        require(fake.cancels == 2 && fake.level == saved && fake.saves == 1 && w.m_pageIndex == page,
                "second finger cancels without page turn or save");
        const int begins = fake.begins;
        touch(w, QEvent::TouchBegin, start);
        touch(w, QEvent::TouchUpdate, start + QPoint(80, 0));
        touch(w, QEvent::TouchUpdate, start - QPoint(0, 120));
        touch(w, QEvent::TouchCancel, start, 0);
        require(fake.begins == begins, "horizontal intent does not alter brightness");
        fake.available = false;
        touch(w, QEvent::TouchBegin, start);
        touch(w, QEvent::TouchUpdate, start - QPoint(0, 100));
        touch(w, QEvent::TouchEnd, start - QPoint(0, 100));
        require(fake.saves == 1 && w.m_pageIndex == page, "unavailable light does not turn page");
        fake.available = true; fake.fail = true;
        touch(w, QEvent::TouchBegin, start);
        touch(w, QEvent::TouchUpdate, start - QPoint(0, 100));
        touch(w, QEvent::TouchEnd, start - QPoint(0, 100));
        require(fake.cancels == 3 && fake.saves == 1 && fake.level == saved, "write failure cancels");
        fake.fail = false;
        const int beforeToolbar = fake.begins;
        const QPoint toolbarPoint(1, w.m_toolbar->mapTo(&w, QPoint(0, 0)).y() + 5);
        touch(w, QEvent::TouchBegin, toolbarPoint);
        touch(w, QEvent::TouchUpdate, toolbarPoint + QPoint(0, 100));
        touch(w, QEvent::TouchCancel, toolbarPoint, 0);
        require(fake.begins == beforeToolbar, "toolbar drag does not control brightness");
        touch(w, QEvent::TouchBegin, start);
        touch(w, QEvent::TouchUpdate, start - QPoint(0, 100));
        QEvent deactivate(QEvent::ApplicationDeactivate); w.eventFilter(qApp, &deactivate);
        touch(w, QEvent::TouchEnd, start - QPoint(0, 100));
        require(fake.cancels == 4 && fake.saves == 1 && fake.level == saved, "deactivation cancels");
        w.m_brightnessBackend = &w.m_nativeBrightness;
    }
public:
    static void run(KobrowseWindow &w, const QString &homeFile) {
        w.resize(768, 1024); w.show(); QApplication::processEvents();
        require(w.m_deviceStatus->height() < 50, "compact status row");
        QLabel *clock = w.m_deviceStatus->findChild<QLabel *>("statusClock");
        QLabel *batteryLabel = w.m_deviceStatus->findChild<QLabel *>("statusBattery");
        require(clock && batteryLabel && clock->font().pixelSize() == 26 && batteryLabel->font().pixelSize() == 26,
                "status labels use explicit compact font");
        QLabel *build = w.m_deviceStatus->findChild<QLabel *>("statusBuild");
        require(build && build->text().contains("local") && build->font().pixelSize() == clock->font().pixelSize(), "build identity in status row");
        require(clock->font().family() == batteryLabel->font().family() && build->font().family() == clock->font().family() && !clock->styleSheet().isEmpty(),
                "status labels use the same explicit sans font and override inherited stylesheet");
        require(w.m_actions->font().pixelSize() == ptToPx(8.5), "context menu uses 8.5 points");
        testFolderIcons(w); testNames(w); testFooter(w); testTouchFeedback(w);
        const QString folder = w.m_root + "/.adds";
        w.m_marked.insert(folder); w.m_menuRequested = true; w.updateActions();
        require(w.m_actions->topLevelItemCount() >= 7, "folder actions shown");
        require(w.m_actions->topLevelItem(0)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActHome
            && !(w.m_actions->topLevelItem(1)->flags() & Qt::ItemIsEnabled)
            && w.m_actions->topLevelItem(2)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActCut
            && w.m_actions->topLevelItem(3)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActCopy
            && !(w.m_actions->topLevelItem(4)->flags() & Qt::ItemIsEnabled)
            && w.m_actions->topLevelItem(5)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActZip,
            "home action is first and separated from cut");
        w.m_clip = QStringList() << (w.m_root + "/.adds/archive-source"); w.updateActions();
        require(w.m_actions->topLevelItem(0)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActHome
            && !(w.m_actions->topLevelItem(1)->flags() & Qt::ItemIsEnabled)
            && w.m_actions->topLevelItem(4)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActPaste
            && !(w.m_actions->topLevelItem(5)->flags() & Qt::ItemIsEnabled),
            "home remains first with clipboard");
        w.m_marked.insert(w.m_root + "/.adds/archive-source"); w.updateActions();
        require(w.m_actions->topLevelItem(0)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActCut,
            "no home or extra leading separator for multiple selections");
        w.m_clip.clear(); w.clearSelection();
        testBrightness(w);
        testExtractAndLogging(w);
        w.openTrash(); QApplication::processEvents();
        require(w.m_emptyTrashButton->isVisible(), "empty trash button appears in trash");
        const QString trashFixture = w.m_trash + "/only-fixture";
        QFile trashFile(trashFixture); require(trashFile.open(QIODevice::WriteOnly), "trash fixture"); trashFile.close();
        w.m_emptyTrashButton->click(); require(w.m_confirm->isVisible(), "empty trash asks for confirmation");
        w.findChild<QPushButton *>("confirmNo")->click(); require(QFileInfo(trashFixture).exists(), "cancel keeps trash");
        w.m_emptyTrashButton->click(); w.findChild<QPushButton *>("confirmYes")->click();
        require(w.m_busy && w.m_workOverlay->isVisible(), "empty trash shows busy message");
        w.m_workTimer.stop(); QMetaObject::invokeMethod(&w.m_workTimer, "timeout", Qt::DirectConnection);
        require(!QFileInfo(trashFixture).exists(), "confirmed empty trash removes fixture");
        w.goUp(); QApplication::processEvents(); require(!w.m_emptyTrashButton->isVisible(), "empty trash button hidden elsewhere");
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
        for (int screenWidth : {600, 768, 1180}) {
            w.resize(screenWidth, 1024); QApplication::processEvents();
            if (w.m_settingsScroll->horizontalScrollBar()->maximum() != 0) {
                QTextStream(stderr) << "FAIL: settings overflow screen=" << screenWidth << " viewport="
                    << w.m_settingsScroll->viewport()->width() << " content=" << w.m_settingsScroll->widget()->width()
                    << " minimum=" << w.m_settingsScroll->widget()->minimumSizeHint().width() << '\n';
                for (int g = 0; g < GroupCount; ++g)
                    if (w.m_groupBlocks[g]->isVisible()) QTextStream(stderr) << "FAIL: group=" << g << " minimum="
                        << w.m_groupBlocks[g]->minimumSizeHint().width() << '\n';
            }
            require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "settings never overflow horizontally");
            const QLabel *rangeLow = w.findChild<QLabel *>(QString("rangeLow_%1").arg(hold));
            const QLabel *rangeHigh = w.findChild<QLabel *>(QString("rangeHigh_%1").arg(hold));
            require(rangeLow && rangeHigh && rangeLow->text() == "150" && rangeHigh->text() == "3000", "slider endpoints labelled");
            require(w.m_setLabels[hold]->text().contains(QString::number(LONG_TAP_MS) + QString::fromUtf8(" мс")), "slider value appears next to title");
            const ButtonFace *face = reset->findChild<ButtonFace *>();
            require(face && face->width() >= QFontMetrics(face->font()).width(face->text()) + 2 * BTN_MARGIN,
                    "reset button label fits without clipping");
        }
        w.resize(768, 1024); QApplication::processEvents();
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
        require(w.m_pickerShade->isVisible(), "picker shades background");
        require(w.m_fontPicker->isVisible() && w.m_fontPicker->currentItem()
            && w.m_fontPicker->currentItem()->isSelected() && w.m_fontPicker->topLevelItemCount() == 3,
            "current choice selected and no cancel item");
        w.chooseFont(w.m_fontPicker->topLevelItem(2)); require(OPEN_BOOKS == "0", "book choice applies immediately");
        require(!w.m_pickerShade->isVisible(), "picker shade hides after selection");
        w.setSettingsTab(1); QApplication::processEvents();
        require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "font tab fits screen");
        choice = w.findChild<QPushButton *>(QString("settingChoice_%1").arg(family));
        require(choice, "current font choice is clickable");
        const QString originalFont = w.m_setVals[family];
        w.m_setVals[family] = QString::fromUtf8("IBM Plex Sans"); w.refreshSettings(); QApplication::processEvents();
        require(w.m_setLabels[family]->text() == QString::fromUtf8("IBM Plex Sans")
                && w.m_setLabels[family]->width() >= QFontMetrics(w.m_setLabels[family]->font()).width(w.m_setLabels[family]->text()),
                "font choice displays the complete name");
        w.m_setVals[family] = originalFont; w.refreshSettings();
        ButtonFace *fontResetFace = reset->findChild<ButtonFace *>();
        require(fontResetFace && fontResetFace->width() >= QFontMetrics(fontResetFace->font()).width(fontResetFace->text()) + 2 * BTN_MARGIN,
                "font reset label fits");
        choice->click();
        require(w.m_fontPicker->currentItem() && w.m_fontPicker->currentItem()->isSelected(), "current font selected");
        require(w.m_fontPicker->topLevelItemCount() == w.m_fonts.size(), "font picker has no cancel item");
        if (w.m_fonts.size() > 1)
            require(w.m_fontPicker->topLevelItem(1)->font(0).family() == w.m_fonts.at(1), "font choice uses its own family");
        require(w.m_setLabels[font]->text().contains(QString::fromUtf8(" пт")), "font size includes points");
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
    for (qreal scale : {167.0 / 300, 212.0 / 300, 1.0}) {
        SettingSlider slider(0, 100, 1, nullptr, scale);
        require(slider.minimumHeight() >= 48 && slider.minimumWidth() >= 90, "slider touch target at different densities");
        slider.resize(240, slider.minimumHeight()); slider.setValue(20);
        slider.begin(220); require(slider.value() > 20, "slider geometry follows touch");
        slider.cancel(); require(slider.value() == 20, "slider cancel restores value");
    }
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
    const QByteArray outsideData("[kobrowse]\nallow_adds_edit=1\nshow_hidden=0\n");
    external.write(outsideData); external.close();
    const QByteArray link = root + "/.adds/kobrowse/config-link";
    require(::symlink(outside.constData(), link.constData()) == 0, "create config symlink");
    KOBROWSE_INI = link.constData();
    require(!saveConfig(QStringList() << "scale_pct", QStringList() << "100"), "symlink config save rejected");
    loadConfig();
    require(!ALLOW_ADDS && SHOW_HIDDEN, "unsafe config link cannot override defaults");
    require(external.open(QIODevice::ReadOnly) && external.readAll() == outsideData, "default update does not follow config symlink");
    external.close();
    const QByteArray invalidPath = root + "/.adds/kobrowse/invalid-utf8.ini";
    const QByteArray invalidData = QByteArray("[other]\nunknown=") + QByteArray("\xff", 1) + "\n[kobrowse]\nshow_hidden=0\n";
    QFile invalid(QString::fromUtf8(invalidPath));
    require(invalid.open(QIODevice::WriteOnly) && invalid.write(invalidData) == invalidData.size(), "invalid config fixture");
    invalid.close(); KOBROWSE_INI = invalidPath.constData();
    loadConfig();
    require(SHOW_HIDDEN && !ALLOW_ADDS, "invalid config uses safe defaults");
    require(!saveConfig(QStringList() << "show_hidden", QStringList() << "1"), "invalid UTF-8 config not overwritten");
    require(invalid.open(QIODevice::ReadOnly) && invalid.readAll() == invalidData, "invalid config bytes preserved");
    invalid.close(); KOBROWSE_INI = ini.constData();
    loadConfig();
    KobrowseWindow *window = new KobrowseWindow(nullptr);
    SettingsUiTest::run(*window, QString::fromUtf8(home));
    QTextStream(stdout) << "Settings UI tests passed\n";
}
