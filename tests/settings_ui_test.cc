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
    static void testRowSeparators(KobrowseWindow &w) {
        QStandardItemModel model(1, 1); const QModelIndex index = model.index(0, 0);
        model.setData(index, QString::fromUtf8("Файл"), Qt::DisplayRole);
        QStyleOptionViewItem option; option.rect = QRect(0, 0, 400, 90);
        QImage normal(400, 90, QImage::Format_RGB32); normal.fill(Qt::white);
        QPainter normalPainter(&normal); w.m_fileDelegate->paint(&normalPainter, option, index); normalPainter.end();
        require(QColor(normal.pixel(200, 89)) == QColor(150, 150, 150), "ordinary row separator is gray");
        model.setData(index, true, ROLE_DIR); model.setData(index, true, ROLE_PARENT);
        QImage parent(400, 90, QImage::Format_RGB32); parent.fill(Qt::white);
        QPainter parentPainter(&parent); w.m_fileDelegate->paint(&parentPainter, option, index); parentPainter.end();
        int black = 0, white = 0;
        for (int x = 100; x < 390; ++x) {
            const QColor pixel(parent.pixel(x, 89));
            if (pixel == QColor(Qt::black)) ++black;
            if (pixel == QColor(Qt::white)) ++white;
        }
        require(black > 0 && white > 0, "parent row separator is dashed");
        model.setData(index, false, ROLE_PARENT); model.setData(index, false, ROLE_DIR);
        model.setData(index, QString::fromUtf8("01.01.26\n12:34"), ROLE_DATE);
        QImage date(400, 90, QImage::Format_RGB32); date.fill(Qt::white);
        QPainter datePainter(&date); w.m_fileDelegate->paint(&datePainter, option, index); datePainter.end();
        const BrowserColumns cols = browserColumns(400, w.m_list->font());
        bool darkGrayText = false;
        for (int y = 10; y < 80; ++y) for (int x = cols.name + 3; x < cols.name + cols.date - 3; ++x)
            if (QColor(date.pixel(x, y)) == QColor(80, 80, 80)) darkGrayText = true;
        require(darkGrayText, "date and time text is dark gray");
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
            const int available = browserColumns(w.m_list->viewport()->width(), w.m_list->font()).name - qMax(32, 2 * ICON_PX) - 18;
            require(QFontMetrics(w.m_list->font()).width(shown) <= available, "elided name fits viewport");
            require(shown.startsWith(QLatin1Char('A')) && !shown.contains(QString::fromUtf8(".epub")), "known extensions hidden and beginning retained");
            if (shown.endsWith(QString::fromUtf8("…"))) require(!shown.contains(QString::fromUtf8("ending")), "name elides only at the end");
            require(index.data().toString() == name, "model keeps complete filename");
        }
        w.resize(768, 1024); QApplication::processEvents();
        const char *known[] = {"book.FB2", "book.epub", "image.png", "archive.zip", "book.mobi"};
        for (const char *n : known) {
            model.setData(index, QString::fromUtf8(n), Qt::DisplayRole);
            require(!w.m_fileDelegate->displayedName(index).contains(QLatin1Char('.')), "known file type hides extension");
        }
        model.setData(index, QString::fromUtf8("book.unknown"), Qt::DisplayRole);
        require(w.m_fileDelegate->displayedName(index) == QString::fromUtf8("book.unknown"), "unknown extension remains visible");
        model.setData(index, QString::fromUtf8("book.epub"), Qt::DisplayRole);
        model.setData(index, true, ROLE_DIR);
        require(w.m_fileDelegate->displayedName(index) == QString::fromUtf8("book.epub"), "directory extension-like suffix remains visible");
        model.setData(index, false, ROLE_DIR); model.setData(index, QString::fromUtf8(".pdf"), Qt::DisplayRole);
        require(w.m_fileDelegate->displayedName(index) == QString::fromUtf8(".pdf"), "dot-only basename not made empty");
        model.setData(index, QString::fromUtf8(".."), Qt::DisplayRole); model.setData(index, true, ROLE_DIR);
        model.setData(index, true, ROLE_PARENT);
        QStyleOptionViewItem option;
        const int parentHeight = w.m_fileDelegate->sizeHint(option, index).height();
        model.setData(index, false, ROLE_PARENT); model.setData(index, QString::fromUtf8("folder"), Qt::DisplayRole);
        model.setData(index, QString::fromUtf8("01.01.26\n12:34"), ROLE_DATE);
        require(w.m_fileDelegate->sizeHint(option, index).height() == parentHeight, "parent and ordinary folder row heights match");
    }
    static void testPathLabel(KobrowseWindow &w) {
        const QFont font = w.m_pathLabel->font(); const QFontMetrics fm(font);
        require(displayBrowserPath("/mnt/onboard", "/mnt/onboard", font, 500) == "/", "root shown as slash");
        require(displayBrowserPath("/mnt/onboard", "/mnt/onboard/a/b", font, 500) == "/a/b/", "root prefix hidden and trailing slash shown");
        require(displayBrowserPath("/mnt/onboard", "/mnt/onboard/a/b/", font, 500) == "/a/b/", "trailing slash not doubled");
        const int width = fm.width(QString::fromUtf8("…/middle/last/")) + 1;
        const QString shortened = displayBrowserPath("/mnt/onboard", "/mnt/onboard/first/middle/last", font, width);
        require(shortened == QString::fromUtf8("…/middle/last/"), "path removes whole leading components and retains slash");
        const QString longLeaf = displayBrowserPath("/mnt/onboard", "/mnt/onboard/veryveryverylongleaf", font, fm.width("short"));
        require(fm.width(longLeaf) <= fm.width("short") && !longLeaf.contains('\n') && longLeaf.endsWith('/'), "long leaf fits one line and retains slash");
        require(displayBrowserPath("/mnt/onboard", "/mnt/onboard/leaf", font, fm.width("/")) == "/", "tiny width retains slash when possible");
        require(!w.m_pathLabel->wordWrap(), "address is single line");
        if (!defaultListFont().isEmpty()) require(font.family() == defaultListFont(), "address uses Avenir when available");
        QImage address(w.m_pathLabel->size(), QImage::Format_RGB32); address.fill(Qt::white); w.m_pathLabel->render(&address);
        require(QColor(address.pixel(0, address.height() / 2)) == QColor(Qt::black)
                && QColor(address.pixel(3, address.height() / 2)) == QColor(80, 80, 80), "address has black outer and dark gray inner frame");
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
        require(!QFileInfo(w.m_root + "/fixture-book_1.txt").exists(), "no extraction before confirmation");
        w.findChild<QPushButton *>("confirmNo")->click();
        require(!QFileInfo(w.m_root + "/fixture-book_1.txt").exists(), "cancel leaves archive untouched");
        w.onItem(&item);
        w.findChild<QPushButton *>("confirmYes")->click();
        require(w.m_busy && w.m_workOverlay->isVisible(), "extract shows busy message");
        require(w.m_workTimer.isActive() && w.m_workTimer.interval() >= 500
                && !QFileInfo(w.m_root + "/fixture-book_1.txt").exists(),
                "overlay is shown before deferred extraction starts");
        touch(w, QEvent::TouchBegin, QPoint(1, 1)); touch(w, QEvent::TouchEnd, QPoint(1, 1));
        require(w.m_busy && w.m_workOverlay->isVisible(), "busy overlay blocks touch");
        w.m_workTimer.stop(); QMetaObject::invokeMethod(&w.m_workTimer, "timeout", Qt::DirectConnection);
        require(!w.m_busy && !w.m_workOverlay->isVisible(), "extract closes busy message");
        require(QFileInfo(archive).exists(), "UI extraction preserves archive");
        require(QFileInfo(w.m_root + "/fixture-book_1.txt").exists(),
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
        require(w.m_deviceStatus->findChild<QLabel *>("statusBuild")->text().contains(QString::fromUtf8("Яркость:")),
                "brightness appears in top status instead of build");
        touch(w, QEvent::TouchEnd, start - QPoint(0, 100));
        require(fake.saves == 1 && w.m_pageIndex == page && !w.m_brightnessTracking, "release saves without turning page");
        const int saved = fake.level;
        touch(w, QEvent::TouchBegin, start);
        touch(w, QEvent::TouchUpdate, start + QPoint(0, 100));
        touch(w, QEvent::TouchCancel, start, 0);
        require(fake.cancels == 1 && fake.level == saved, "touch cancel restores brightness");
        require(w.m_deviceStatus->findChild<QLabel *>("statusBuild")->text().startsWith("#"),
                "cancel restores build status");
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
        QLabel *free = w.m_deviceStatus->findChild<QLabel *>("statusFree");
        require(free && !free->text().contains("--") && free->font().pixelSize() == clock->font().pixelSize(), "available storage in status row");
        DeviceStatusBar missing(nullptr, w.m_root + "/missing-power", w.m_root + "/missing-storage");
        require(missing.findChild<QLabel *>("statusFree")->text().contains("--"), "unavailable storage reported without bogus value");
        require(clock->font().family() == batteryLabel->font().family() && build->font().family() == clock->font().family() && !clock->styleSheet().isEmpty(),
                "status labels use the same explicit sans font and override inherited stylesheet");
        require(w.m_actions->font().pixelSize() == ptToPx(12)
                && w.m_actions->font().family() == w.m_list->font().family(), "context menu uses fixed 12pt size");
        require(w.m_deviceStatus->layout()->contentsMargins().left() == 32
                && w.m_deviceStatus->layout()->contentsMargins().right() == 32, "status edges have extra margins");
        require(w.m_pathLabel->geometry().bottom() + 1 == w.m_columnHeader->geometry().top(), "path and columns touch");
        require(w.m_sortButtons[0]->text() == QString::fromUtf8("↑  Название")
                && w.m_sortButtons[1]->text() == QString::fromUtf8("Дата")
                && w.m_sortButtons[2]->text() == QString::fromUtf8("Размер"), "headers show labels and left-aligned active arrow");
        require(w.m_sortButtons[0]->font().pixelSize() == qRound(qMax(10, clock->font().pixelSize() * 3 / 4) * 1.5)
                && w.m_sortButtons[0]->styleSheet().contains("text-align:left")
                && w.m_sortButtons[0]->styleSheet().contains("padding-right:8px"), "header font enlarged 1.5 times with larger side padding");
        require(LIST_DETAIL_PX == ptToPx(8.0), "date and size default to half a point below list font");
        require(!w.m_pageStarts.empty() && !w.m_pageTimer.isActive(), "visible load paginates before a deferred repaint");
        for (int n = 1; n < 3; ++n) {
            const QString widest = QString::fromUtf8("↓  ") + (n == 1 ? QString::fromUtf8("Дата") : QString::fromUtf8("Размер"));
            require(QFontMetrics(w.m_sortButtons[n]->font()).width(widest) + 18 <= w.m_sortButtons[n]->width(),
                    "active column label and arrow fit with padding");
        }
        require(w.m_footerLine->styleSheet().contains("background:black"), "footer separator is black");
        require(w.m_pageLabel->font().family() == clock->font().family()
                && w.m_countLabel->font().family() == clock->font().family(), "footer uses status Avenir family");
        const QStringList iconNames = QStringList() << QString::fromUtf8("Домашняя папка")
            << QString::fromUtf8("Открыть корзину") << QString::fromUtf8("Поиск") << QString::fromUtf8("Настройки");
        for (const QString &name : iconNames) {
            QPushButton *control = nullptr;
            for (QPushButton *candidate : w.m_toolbar->findChildren<QPushButton *>())
                if (candidate->accessibleName() == name) { control = candidate; break; }
            require(control, "toolbar icon exists");
            QImage before(control->size(), QImage::Format_RGB32); before.fill(Qt::white); control->render(&before);
            control->setDown(true);
            QImage pressed(control->size(), QImage::Format_RGB32); pressed.fill(Qt::white); control->render(&pressed);
            control->setDown(false);
            require(before == pressed, "toolbar icon does not invert background on press");
        }
        QPushButton *homeButton = nullptr;
        for (QPushButton *candidate : w.m_toolbar->findChildren<QPushButton *>())
            if (candidate->accessibleName() == QString::fromUtf8("Домашняя папка")) homeButton = candidate;
        require(homeButton, "home button exists");
        QImage homeIcon(homeButton->size(), QImage::Format_RGB32); homeIcon.fill(Qt::white); homeButton->render(&homeIcon);
        QImage trashIcon(w.m_trashButton->size(), QImage::Format_RGB32); trashIcon.fill(Qt::white); w.m_trashButton->render(&trashIcon);
        int homeTop = homeIcon.height(), homeBottom = -1, trashTop = trashIcon.height(), trashBottom = -1;
        for (int y = 0; y < homeIcon.height(); ++y) for (int x = 0; x < homeIcon.width(); ++x) {
            if (QColor(homeIcon.pixel(x, y)).lightness() < 100) { homeTop = qMin(homeTop, y); homeBottom = qMax(homeBottom, y); }
            if (QColor(trashIcon.pixel(x, y)).lightness() < 100) { trashTop = qMin(trashTop, y); trashBottom = qMax(trashBottom, y); }
        }
        require(trashBottom - trashTop >= (homeBottom - homeTop) * 9 / 10, "trash icon height matches other toolbar icons");
        require(qAbs((trashTop + trashBottom) - (homeTop + homeBottom)) <= 6, "trash icon vertically aligned");
        require(sleepRecoveryAllowed(true, false, false, false, false), "restore only when native views clear");
        require(!sleepRecoveryAllowed(false, false, false, false, false)
            && !sleepRecoveryAllowed(true, true, false, false, false)
            && !sleepRecoveryAllowed(true, false, true, false, false)
            && !sleepRecoveryAllowed(true, false, false, true, false)
            && !sleepRecoveryAllowed(true, false, false, false, true), "sleep, PIN, USB and missing APIs block restore");
        const QString pathBeforeSleep = w.m_path; w.hide(); w.show();
        require(w.m_path == pathBeforeSleep, "hide and restore preserves browsing state");
        // The public input-method probe must retain the built-in keyboard when
        // offscreen Qt has no visible input method, without losing text/cursor.
        SearchWindow *input = new SearchWindow(&w);
        int submits = 0;
        input->startRename(QString::fromUtf8("Имя"), [&submits](const QString &value) -> QString {
            ++submits; return value == QString::fromUtf8("Тест") ? QString::fromUtf8("Проверка") : QString::fromUtf8("Ошибка");
        }, QString::fromUtf8("Создать папку"));
        input->resize(768, 1024); input->show(); QApplication::processEvents();
        require(input->m_inputPending && !input->m_systemInput, "input method probe starts safely");
        input->m_nativeField->setText(QString::fromUtf8("Тест")); input->m_nativeField->setCursorPosition(2);
        input->finishInputProbe(); QApplication::processEvents();
        require(!input->m_inputPending && !input->m_systemInput && input->m_kbBox->isVisible()
                && input->m_field->text() == QString::fromUtf8("Тест") && input->cursorPos() == 2,
                "input method fallback preserves text, cursor and built-in keyboard");
        input->submitInput(); require(submits == 1 && input->isVisible(), "failed submit occurs once and keeps editor open");
        delete input;
        testFolderIcons(w); testRowSeparators(w); testNames(w); testPathLabel(w); testFooter(w); testTouchFeedback(w);
        require(w.m_sortButtons.size() == 3 && w.m_sortButtons[0]->text().contains(QString::fromUtf8("↑")), "default sort header");
        const QPoint sortTap = w.m_sortButtons[1]->mapTo(&w, w.m_sortButtons[1]->rect().center());
        touch(w, QEvent::TouchBegin, sortTap); touch(w, QEvent::TouchEnd, sortTap); QApplication::processEvents();
        require(w.m_sort == SortDate && w.m_sortAscending && w.m_sortButtons[1]->text().contains(QString::fromUtf8("↑")), "touch sorts by date");
        w.m_sortButtons[1]->click(); require(!w.m_sortAscending && w.m_sortButtons[1]->text().contains(QString::fromUtf8("↓")), "sort direction reversed");
        QStringList previousDirs;
        for (int i = 0; i < w.m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = w.m_list->topLevelItem(i);
            if (item->data(0, ROLE_DIR).toBool() && !item->data(0, ROLE_PARENT).toBool())
                previousDirs << item->data(0, ROLE_PATH).toString();
        }
        std::sort(previousDirs.begin(), previousDirs.end(), [](const QString &a, const QString &b) {
            return browserEntryLess(QFileInfo(a), QFileInfo(b));
        });
        w.m_sortButtons[2]->click(); require(w.m_sort == SortSize && w.m_sortAscending, "size column sorts");
        for (int direction = 0; direction < 2; ++direction) {
            QStringList dirs;
            for (int i = 0; i < w.m_list->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item = w.m_list->topLevelItem(i);
                if (item->data(0, ROLE_DIR).toBool() && !item->data(0, ROLE_PARENT).toBool()) {
                    dirs << item->data(0, ROLE_PATH).toString();
                    require(item->data(0, ROLE_SIZE).toString().isEmpty(), "directory size blank");
                }
            }
            require(dirs == previousDirs, "size sort always uses ascending directory names");
            w.m_sortButtons[2]->click();
        }
        w.m_sortButtons[0]->click(); require(w.m_sort == SortName && w.m_sortAscending, "name column sorts");
        w.m_menuRequested = true; w.updateActions();
        bool mkdirShown = false;
        for (int n = 0; n < w.m_actions->topLevelItemCount(); ++n)
            if (w.m_actions->topLevelItem(n)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActMkdir
                && (w.m_actions->topLevelItem(n)->flags() & Qt::ItemIsEnabled)) mkdirShown = true;
        require(mkdirShown, "empty selection menu offers create folder"); w.clearSelection();
        const QPoint blank = w.m_list->viewport()->mapTo(&w, QPoint(w.m_list->viewport()->width() - 8, w.m_list->viewport()->height() - 8));
        if (!w.m_list->itemAt(w.m_list->viewport()->mapFrom(&w, blank))) {
            touch(w, QEvent::TouchBegin, blank);
            require(w.m_holdBlank && w.m_holdTimer.isActive(), "long press on blank works without clipboard");
            w.m_holdTimer.stop(); QMetaObject::invokeMethod(&w.m_holdTimer, "timeout", Qt::DirectConnection);
            require(w.m_actions->isVisible(), "blank long press opens action menu");
            touch(w, QEvent::TouchEnd, blank); w.clearSelection();
        }
        const QString folder = w.m_root + "/.adds";
        w.m_marked.insert(folder); w.m_menuRequested = true; w.updateActions();
        require(w.m_actions->topLevelItemCount() >= 9, "folder actions shown");
        require(w.m_actions->topLevelItem(0)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActMkdir
            && !(w.m_actions->topLevelItem(1)->flags() & Qt::ItemIsEnabled)
            && w.m_actions->topLevelItem(2)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActHome
            && !(w.m_actions->topLevelItem(3)->flags() & Qt::ItemIsEnabled)
            && w.m_actions->topLevelItem(4)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActCut
            && w.m_actions->topLevelItem(5)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActCopy
            && !(w.m_actions->topLevelItem(6)->flags() & Qt::ItemIsEnabled)
            && w.m_actions->topLevelItem(7)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActZip,
            "create folder first, then home and selected actions");
        w.m_clip = QStringList() << (w.m_root + "/.adds/archive-source"); w.updateActions();
        require(w.m_actions->topLevelItem(0)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActMkdir
            && w.m_actions->topLevelItem(6)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActPaste
            && !(w.m_actions->topLevelItem(7)->flags() & Qt::ItemIsEnabled),
            "create folder remains first with clipboard");
        w.m_marked.insert(w.m_root + "/.adds/archive-source"); w.updateActions();
        require(w.m_actions->topLevelItem(0)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActMkdir
            && w.m_actions->topLevelItem(2)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActCut,
            "create folder remains first for multiple selections");
        w.m_path = w.m_root + "/.adds"; w.updateActions();
        require(w.m_actions->topLevelItem(0)->data(0, Qt::UserRole).toInt() == KobrowseWindow::ActCut,
            "protected current directory does not offer create folder");
        w.m_path = w.m_root;
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
        w.m_trashButton->click(); QApplication::processEvents();
        require(w.m_path == w.m_root && !w.m_emptyTrashButton->isVisible(), "trash icon closes trash and returns to folder");
        require(saveConfig(QStringList() << "list_pt" << "long_tap_ms", QStringList() << "20" << "1200"), "legacy settings fixture");
        loadConfig(); require(LIST_PX == ptToPx(14) && LONG_TAP_MS == 700, "legacy settings bounded at runtime");
        w.showSettings(true); QApplication::processEvents();
        w.stepSetting(row("show_hidden"), 1);
        QSettings preserved(QString::fromUtf8(KOBROWSE_INI), QSettings::IniFormat);
        require(preserved.value("kobrowse/list_pt").toString() == "20"
                && preserved.value("kobrowse/long_tap_ms").toString() == "1200", "unrelated edit preserves legacy values");
        w.stepSetting(row("show_hidden"), 1); w.showSettings(false);
        require(saveConfig(QStringList() << "list_pt" << "long_tap_ms", QStringList() << "8.5" << "450"), "restore default fixture");
        loadConfig();
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
        require(SET_ITEMS[hold].lo == 350 && SET_ITEMS[hold].hi == 700 && SET_ITEMS[hold].step == 50
                && cfgDefault("long_tap_ms") == "450", "hold limits and default");
        require(SET_ITEMS[font].lo == 7 && SET_ITEMS[font].hi == 14 && SET_ITEMS[font].step == .5
                && cfgDefault("list_pt") == "8.5", "font limits and default");
        require(!w.m_settingsNotice->isVisible(), "empty notice does not cover settings");
        require(!w.findChild<QPushButton *>("settingsEmptyTrash"), "no empty trash action in settings");
        SettingSwitch *box = w.m_switches[hidden]; box->setChecked(true);
        QImage checkImage(box->size(), QImage::Format_RGB32); checkImage.fill(Qt::white); box->render(&checkImage);
        const int boxSize = qRound(qMin(box->height() - 12, box->width() - 12) * .85);
        const int left = (box->width() - boxSize) / 2, top = (box->height() - boxSize) / 2;
        require(qGray(checkImage.pixel(left + boxSize / 2, top + boxSize / 5)) < 80,
                "checked box has black interior");
        box->setDown(true); QImage pressed(box->size(), QImage::Format_RGB32); pressed.fill(Qt::white); box->render(&pressed); box->setDown(false);
        require(qGray(pressed.pixel(0, 0)) > 200, "checkbox press does not blacken surrounding area");
        w.refreshSettings();
        require(w.m_homeStatus->parentWidget() == w.m_resetHomeButton->parentWidget(), "home path and reset share a row");
        require(w.m_resetHomeButton->width() >= QFontMetrics(w.m_resetHomeButton->findChild<ButtonFace *>()->font()).width(QString::fromUtf8("Сбросить домашнюю папку")),
                "home reset text is not clipped");
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
            require(!w.findChild<QLabel *>(QString("rangeLow_%1").arg(hold))
                    && !w.findChild<QLabel *>(QString("rangeHigh_%1").arg(hold)), "no endpoint labels");
            require(w.m_setLabels[hold]->mapTo(&w, QPoint()).x() < w.m_sliders[hold]->mapTo(&w, QPoint()).x(),
                    "slider title is left of controls");
            require(w.m_setLabels[hold]->text().contains(QString::number(LONG_TAP_MS) + QString::fromUtf8(" мс")), "slider value appears next to title");
            const ButtonFace *face = reset->findChild<ButtonFace *>();
            require(face && face->width() >= QFontMetrics(face->font()).width(face->text()) + 2 * BTN_MARGIN,
                    "reset button label fits without clipping");
        }
        for (int screenWidth : {600, 768, 1180}) {
            w.resize(screenWidth, 1024);
            for (int tab = 0; tab < 2; ++tab) {
                w.setSettingsTab(tab); QApplication::processEvents();
                const int i = tab == 0 ? hold : font;
                QWidget *widgets[] = { w.m_setLabels[i], w.m_minusBtns[i], w.m_sliders[i], w.m_plusBtns[i] };
                for (int a = 0; a < 4; ++a) {
                    require(widgets[a]->parentWidget() == w.m_settingRows[i], "slider controls share row parent");
                    const QRect first(widgets[a]->mapTo(&w, QPoint()), widgets[a]->size());
                    for (int b = a + 1; b < 4; ++b) {
                        const QRect second(widgets[b]->mapTo(&w, QPoint()), widgets[b]->size());
                        require(!first.intersects(second), "slider label and controls never overlap");
                    }
                }
                QLabel *label = w.m_setLabels[i];
                require(label->heightForWidth(label->width()) <= label->height(), "slider caption is fully visible");
                require(qAbs(label->width() * 3 - w.m_settingRows[i]->width()) < 30, "slider caption occupies a third of row");
                require(label->text().contains(": ") && !label->text().contains(QString::fromUtf8("Сейчас")), "slider caption uses colon");
            }
        }
        w.setSettingsTab(0); w.resize(768, 1024); QApplication::processEvents();
        const int oldHold = LONG_TAP_MS; w.m_plusBtns[hold]->click();
        require(LONG_TAP_MS == oldHold + 50 && w.m_settings->isVisible(), "hold setting saved immediately");
        w.stepSetting(adds, 1); QApplication::processEvents();
        require(w.m_confirm->isVisible() && !ALLOW_ADDS, "adds asks confirmation");
        require(w.m_pickerShade->isVisible() && yes->mapTo(&w, QPoint()).x() < no->mapTo(&w, QPoint()).x(),
                "confirmation is shaded and cancel is on right");
        require(qAbs(w.m_confirm->geometry().center().x() - w.rect().center().x()) < 3
                && qAbs(w.m_confirm->geometry().center().y() - w.rect().center().y()) < 3, "confirmation centered");
        no->click(); require(!ALLOW_ADDS, "declining leaves adds protected");
        w.stepSetting(adds, 1); yes->click(); require(ALLOW_ADDS, "confirmation persists adds");
        require(!w.findChild<QWidget *>("pickerDialog"), "settings have no choice popup");
        require(w.m_bookModes.size() == 3 && w.m_bookModes[0]->isSelected(), "auto book mode selected");
        for (int n = 0; n < 3; ++n) {
            require(w.m_bookModes[n]->parentWidget() == w.m_settingRows[books], "book modes share setting row");
            if (n) require(w.m_bookModes[n-1]->geometry().right() + 1 == w.m_bookModes[n]->geometry().left(),
                           "book modes form adjacent segments");
        }
        const char *modes[] = {"auto", "1", "0"};
        for (int n = 1; n < 3; ++n) {
            w.m_bookModes[n]->click();
            require(OPEN_BOOKS == modes[n] && w.m_bookModes[n]->isSelected(), "book mode applies immediately");
            require(!w.m_pickerShade->isVisible() && !w.m_confirm->isVisible(), "book choice opens no popup");
        }
        w.setSettingsTab(1); QApplication::processEvents();
        require(w.m_settingsScroll->horizontalScrollBar()->maximum() == 0, "font tab fits screen");
        require(w.m_fontChoices.size() == w.m_fonts.size() && !w.m_fontChoices.isEmpty(), "inline font list contains all choices");
        require(w.m_fontChoices[0]->text() == "Kobo", "empty font labelled Kobo");
        require(w.m_fontChoices[0]->mapTo(&w, QPoint()).y() > w.m_settingRows[font]->mapTo(&w, QPoint()).y(),
                "font choices follow size control");
        require(!SettingChoiceButton::sampleText().isEmpty() && SettingChoiceButton::sampleText().contains(QString::fromUtf8("Пример")),
                "font choices have Russian sample");
        for (int n = 1; n < w.m_fontChoices.size(); ++n)
            require(w.m_fontChoices[n]->previewFont().family() == w.m_fonts[n], "each sample uses its own font");
        ButtonFace *fontResetFace = reset->findChild<ButtonFace *>();
        require(fontResetFace && fontResetFace->width() >= QFontMetrics(fontResetFace->font()).width(fontResetFace->text()) + 2 * BTN_MARGIN,
                "font reset label fits");
        // A drag over an inline choice scrolls the settings, not a selection.
        SettingChoiceButton *first = w.m_fontChoices[0];
        w.m_settingsScroll->ensureWidgetVisible(first); QApplication::processEvents();
        const QPoint firstPoint = first->mapTo(&w, first->rect().center());
        const QString oldFamily = w.m_setVals[family];
        touch(w, QEvent::TouchBegin, firstPoint);
        touch(w, QEvent::TouchUpdate, firstPoint - QPoint(0, SWIPE_PX + 20));
        touch(w, QEvent::TouchEnd, firstPoint - QPoint(0, SWIPE_PX + 20));
        require(w.m_setVals[family] == oldFamily && w.m_settingsScroll->verticalScrollBar()->value() > 0,
                "drag scrolls font list without changing font");
        if (w.m_fonts.size() > 1) {
            SettingChoiceButton *second = w.m_fontChoices[1];
            w.m_settingsScroll->ensureWidgetVisible(second); QApplication::processEvents();
            const QPoint point = second->mapTo(&w, second->rect().center());
            touch(w, QEvent::TouchBegin, point); touch(w, QEvent::TouchEnd, point);
            require(w.m_setVals[family] == w.m_fonts[1] && second->isSelected(), "touch selects font immediately");
            require(w.m_actions->font().family() == w.m_list->font().family()
                    && w.m_actions->font().pixelSize() == ptToPx(12), "font family applies without changing menu size");
            require(!w.m_pickerShade->isVisible() && !w.m_confirm->isVisible(), "font choice opens no popup");
        }
        require(w.m_setLabels[font]->text().contains(QString::fromUtf8(" пт")), "font size includes points");
        first->click(); require(w.m_setVals[family].isEmpty() && first->isSelected(), "Kobo font applies immediately");
        const int beforeFont = LIST_PX; w.m_plusBtns[font]->click();
        require(LIST_PX > beforeFont && w.m_list->font().pixelSize() == LIST_PX, "font applies immediately");
        require(LIST_DETAIL_PX == ptToPx(8.5), "detail font follows list setting minus half a point");
        require(w.m_actions->font().pixelSize() == ptToPx(12) && w.m_actions->font().family() == w.m_list->font().family(),
                "context menu size remains fixed as list size changes");
        reset->click(); no->click(); require(LIST_PX > beforeFont, "reset cancellation preserves setting");
        reset->click(); yes->click(); require(LIST_PX == ptToPx(8.5), "font reset applies immediately");
        require(QFileInfo(homeFile).exists(), "font reset preserves home");
        w.setSettingsTab(0); w.stepSetting(hidden, 1); require(!SHOW_HIDDEN, "hidden toggle applied");
        const QString path = w.m_root + "/.adds"; w.m_path = path;
        w.m_clip = QStringList() << (w.m_root + "/.adds/archive-source");
        close->click(); QApplication::processEvents();
        require(w.isVisible() && !w.m_settings->isVisible() && w.m_path == path && w.m_clip.size() == 1, "close retains folder and clipboard");
        w.showSettings(true); require(w.m_setVals[adds] == "1" && w.m_setVals[hidden] == "0" && w.m_setVals[books] == "0"
                && w.m_bookModes[2]->isSelected(), "saved values and selected book mode persist");
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
