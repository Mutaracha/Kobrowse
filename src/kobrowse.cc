// kobrowse — файловый менеджер для Kobo (плагин Nickel на NickelHook)
// Этапы 1–2: просмотр папок /mnt/onboard, выбор, копирование/перенос/корзина. Окно открывается по файлу-сигналу
// /tmp/kobrowse.open, который создаёт пункт NickelMenu.
// Ограничения NickelTC: GCC 4.9, Qt 5.2.1, C++11 — без QTimer::singleShot(лямбда).

#include <QApplication>
#include <QBrush>
#include <QDesktopWidget>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QRegExp>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QGlyphRun>
#include <QImage>
#include <QPixmap>
#include <QRawFont>
#include <QGuiApplication>
#include <QInputMethod>
#include <QHash>
#include <QScreen>
#include <QSignalMapper>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QTextLayout>
#include <QPointer>
#include <QPushButton>
#include <QPolygonF>
#include <QScrollBar>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTextCodec>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTouchEvent>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <QWindow>

#include <unistd.h> // sync()
#include <dlfcn.h>  // dlsym — функции Nickel
#include <cstring>
#include <functional>

#include <NickelHook.h>

#include "path_label.h"
#include "sleep_policy.h"
#include "file_policy.h"
#include "file_mutations.h"
#include "zip_writer.h"
#include "zip_extract.h"
#include "pagination.h"
#include "home_folder.h"
#include "action_menu.h"
#include "item_style.h"
#include "file_kind.h"
#include "settings_model.h"
#include "footer_notice.h"
#include "device_status.h"
#include "setting_slider.h"
#include "brightness.h"
#include <dirent.h>
#include <cerrno>

// ---------- настройки ----------
static const char *KOBROWSE_ROOT    = "/mnt/onboard";   // выше этой папки не поднимаемся
static const char *KOBROWSE_TRIGGER = "/tmp/kobrowse.open"; // файл-сигнал от NickelMenu
static const char *KOBROWSE_DIAG    = "/tmp/kobrowse.diag"; // сигнал: диагностика шрифтов интерфейса Nickel
static double SCREEN_DPI = 300;        // плотность экрана; уточняется по устройству в loadConfig (Clara HD — 300)
static const char *KOBROWSE_HOME = "/mnt/onboard/.adds/kobrowse/home_path";
static const char *KOBROWSE_INI     = "/mnt/onboard/.adds/kobrowse/kobrowse.ini"; // настройки, читаются при каждом открытии
static const int   LOG_TAPS     = 30;  // отладка: сколько касаний записать в лог
static const int   SWIPE_PX     = 40;  // сдвиг пальца больше этого — перетаскивание, а не нажатие
static const char *KOBROWSE_TRASH   = "/mnt/onboard/.kobrowse_trash"; // корзина (папка с точкой — Nickel её не индексирует)
static const char *KOBROWSE_LOG     = "/mnt/onboard/.adds/kobrowse/logs/kobrowse.log";
static const int   LOG_EVENTS   = 0;   // сколько событий окна записать в лог (отладка)

// Функции Nickel (libnickel), загружаются при старте плагина
class MainWindowController;
static MainWindowController *(*MWC_sharedInstance)() = nullptr;
static QWidget *(*MWC_currentView)(MainWindowController *) = nullptr;

static QString u8(const char *s) { return QString::fromUtf8(s); }

// ---------- внешний вид: значения из kobrowse.ini (пересчитаны в пиксели) ----------
// Заполняются loadConfig() при каждом открытии менеджера.
static int     LIST_PX, LIST_DETAIL_PX, FONT_BTN, FONT_MSG, GAP, BTN_MARGIN, BTN_HEIGHT, ICON_PX, LONG_TAP_MS;
static qreal   BTN_RADIUS, BTN_PEN;
static QString LIST_FONT, BTN_FONT;
// Клавиатура поиска (перенесена из ksearch): 0 — кегль считается от высоты клавиши.
static int     KEY_FONT_PX = 0, FUNC_PX = 0;
static bool    KEY_FONT_SET = false, FUNC_FONT_SET = false;
static int     KEY_PX = 34;             // кегль поля запроса; уточняется в buildKeyboard()
// Список результатов поиска — свои кегль и гарнитура, чтобы не менять вид списка файлов.
static int     SEARCH_LIST_PX = 42;
static QString SEARCH_LIST_FONT;
static bool    SHOW_HIDDEN = true, TRASH_ENABLED = true, ALLOW_ADDS = false;
static QString OPEN_BOOKS;   // auto / 1 / 0 — см. open_books в kobrowse.ini

// Ключи kobrowse.ini: имя, значение по умолчанию, пояснение
struct CfgKey { const char *key, *def, *comment; };
static const CfgKey CFG_KEYS[] = {
    { "list_pt",       "8.5",           "кегль списка файлов, пт: 8 = петит, 9 = боргес, 10 = корпус (можно дробное: 8.5)" },
    { "list_font",     "Avenir",      "гарнитура списка; пусто — Kobo. По умолчанию Avenir или Avenir Next" },
    { "long_tap_ms",   "450",         "долгое нажатие (выбор строки), мс" },
    { "allow_adds_edit", "0",       "разрешить изменение содержимого .adds: 1 — да, 0 — только просмотр" },
    { "trash_enabled", "1",           "использовать корзину: 1 — да, 0 — удалять после подтверждения" },
    { "logging_enabled", "1",          "журнал отладки Kobrowse: 1 — вести, 0 — выключить" },
    { "show_hidden",   "1",           "показывать скрытые файлы и папки (с точкой): 1 — да, 0 — нет" },
    { "open_books",    "auto",        "открытие книг тапом: auto — только на проверенной прошивке 4.38; 1 — всегда (на свой риск); 0 — никогда" },
};

// Значение по умолчанию для ключа kobrowse.ini
static QString defaultListFont() {
    const QStringList available = QFontDatabase().families();
    const char *preferred[] = { "Avenir", "Avenir Next" };
    for (const char *name : preferred)
        for (const QString &family : available)
            if (family.compare(u8(name), Qt::CaseInsensitive) == 0) return family;
    return QString(); // Kobo's current UI font if neither family is installed.
}
static QString cfgDefault(const QString &key) {
    if (key == u8("list_font")) return defaultListFont();
    for (const CfgKey &k : CFG_KEYS) if (key == u8(k.key)) return u8(k.def);
    return QString();
}

static int ptToPx(double pt) { return qRound(pt * SCREEN_DPI / 72.0); }

// Kobalt scales its 300-ppi design by panel density, not screen width.
// PRODUCT is provided by Kobo; Qt's generic 96/100 dpi is not panel density.
static void detectDpi() {
    struct ModelDensity { const char *prefix; int ppi; };
    static const ModelDensity models[] = {
        {"trilogy",167}, {"pika",167}, {"pixie",200}, {"kraken",212},
        {"phoenix",212}, {"star",212}, {"luna",212}, {"dragon",265},
        {"dahlia",265}, {"snow",265}, {"europa",227}, {"condor",227},
        {"alyssum",300}, {"nova",300}, {"goldfinch",300}, {"spa",300},
        {"storm",300}, {"io",300}, {"monza",300}, {"frost",300},
        {"cadmus",300}, {"daylight",300}
    };
    const QByteArray product = qgetenv("PRODUCT");
    for (const ModelDensity &model : models) {
        if (product.startsWith(model.prefix)) { SCREEN_DPI = model.ppi; return; }
    }
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen ? screen->logicalDotsPerInch() : 0;
    SCREEN_DPI = dpi >= 150 && dpi <= 400 ? dpi : 300;
}

// Прочитать kobrowse.ini. Недостающие ключи дописываются в конец файла с пояснениями
// (то, что пользователь уже поменял, не трогаем).
static void loadConfig() {
    detectDpi();                                     // разные модели Kobo — разная плотность
    const QString ini = u8(KOBROWSE_INI);
    const QString appDir = QFileInfo(ini).absolutePath();
    if (safeLocalPath(u8(KOBROWSE_ROOT), appDir, true)) QDir().mkpath(appDir);
    QString text;
    bool canUpdate = safeLocalPath(u8(KOBROWSE_ROOT), ini, true);
    {
        QFile f(ini);
        if (canUpdate && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray bytes = f.readAll();
            text = QString::fromUtf8(bytes);
            if (f.error() != QFile::NoError || text.toUtf8() != bytes) canUpdate = false;
        } else if (QFileInfo(ini).exists() || QFileInfo(ini).isSymLink()) canUpdate = false;
    }
    QStringList lines = text.split(QLatin1Char('\n'));
    const char *obsolete[] = { "btn_pt", "btn_font", "info_pt", "gap_pt", "btn_pad_pt",
        "btn_height_pt", "icon_pt", "btn_radius_pt", "btn_line_pt", "scale_pct", "idle_min",
        "key_font_pt", "func_pt", "search_list_pt", "search_list_font", "opds_key_pt", "opds_func_pt" };
    QSet<QString> oldKeys;
    for (const char *key : obsolete) oldKeys.insert(u8(key));
    // Remove only comments generated verbatim by older Kobrowse versions.
    struct OldComment { const char *key, *text; };
    const OldComment oldComments[] = {
        { "btn_pt", "наибольший кегль подписей кнопок, пт (уменьшается, если слово не влезает)" },
        { "btn_font", "гарнитура подписей кнопок" },
        { "info_pt", "кегль сведений о файле и сообщений, пт" },
        { "gap_pt", "поля от краёв экрана и промежутки между кнопками, пт" },
        { "btn_pad_pt", "поле внутри кнопки слева и справа от подписи, пт" },
        { "btn_height_pt", "высота кнопки, пт" },
        { "icon_pt", "размер иконки на кнопке, пт" },
        { "btn_radius_pt", "скругление углов кнопки, пт (0 — прямые углы)" },
        { "btn_line_pt", "толщина рамки кнопки, пт" },
        { "scale_pct", "общий масштаб интерфейса kobrowse, %: 100 — как на Clara HD (по ширине экрана); 50–200" },
        { "idle_min", "закрыть менеджер, если не было касаний, мин" },
        { "key_font_pt", "кегль подписей клавиш поиска, пт; 0 — от высоты клавиши (около 0.52)" },
        { "func_pt", "кегль служебных клавиш поиска (Shift, Space, DelBwd), пт; 0 — от высоты клавиши (около 0.34)" },
        { "search_list_pt", "кегль списка результатов поиска, пт (8 = петит, 10 = корпус; можно дробное: 8.5)" },
        { "search_list_font", "гарнитура списка результатов поиска; пусто — шрифт виджета" },
        { "opds_key_pt", "кегль клавиш OPDS-клавиатуры, пт (уменьшите, если клавиши не влезают)" },
        { "opds_func_pt", "кегль нижнего ряда кнопок OPDS, пт" },
    };
    QHash<QString, QString> generatedComments;
    for (const OldComment &entry : oldComments) generatedComments.insert(u8(entry.key), u8("; ") + u8(entry.text));
    bool section = false, removedOld = false;
    for (int i = 0; i < lines.size();) {
        const QString line = lines.at(i).trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) section = line == u8("[kobrowse]");
        const int eq = line.indexOf(QLatin1Char('='));
        const QString key = line.left(eq).trimmed();
        if (section && eq > 0 && !line.startsWith(QLatin1Char(';')) && !line.startsWith(QLatin1Char('#'))
            && oldKeys.contains(key)) {
            lines.removeAt(i); removedOld = true;
            if (i > 0 && lines.at(i - 1).trimmed() == generatedComments.value(key)) { lines.removeAt(i - 1); --i; }
            continue;
        }
        ++i;
    }
    QStringList present;
    bool inSection = false, hasSection = false;
    int insertAt = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString l = lines[i].trimmed();
        if (l.startsWith(QLatin1Char('[')) && l.endsWith(QLatin1Char(']'))) {
            if (inSection && insertAt < 0) insertAt = i;
            inSection = l == u8("[kobrowse]");
            if (inSection) hasSection = true;
            continue;
        }
        const int eq = l.indexOf(QLatin1Char('='));
        if (inSection && eq > 0 && !l.startsWith(QLatin1Char(';')) && !l.startsWith(QLatin1Char('#')))
            present << l.left(eq).trimmed();
    }
    QStringList added;
    if (!hasSection) added << u8("[kobrowse]") << u8("; настройки Kobrowse");
    for (const CfgKey &k : CFG_KEYS)
        if (!present.contains(u8(k.key)))
            added << QString() << (u8("; ") + u8(k.comment)) << (u8(k.key) + u8("=") + cfgDefault(u8(k.key)));
    if (canUpdate && (removedOld || !added.isEmpty())) {
        if (insertAt < 0) insertAt = lines.size();
        for (const QString &line : added) lines.insert(insertAt++, line);
        QSaveFile f(ini);
        const QByteArray output = lines.join(u8("\n")).toUtf8() + "\n";
        if (f.open(QIODevice::WriteOnly | QIODevice::Text) && f.write(output) == output.size()) f.commit();
    }

    QSettings st(ini, QSettings::IniFormat);
    st.setIniCodec(QTextCodec::codecForName("UTF-8"));
    // число в пределах [lo, hi], иначе значение по умолчанию
    auto num = [&st, canUpdate](const char *key, double def, double lo, double hi) {
        if (!canUpdate) return def;
        bool ok = false;
        const double v = st.value(u8("kobrowse/") + u8(key)).toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        return (ok && v >= lo && v <= hi) ? v : def;
    };
    OPEN_BOOKS  = canUpdate ? st.value(u8("kobrowse/open_books")).toString().trimmed().toLower() : QString();
    const double listPt = qBound(7.0, num("list_pt", 8.5, 4, 24), 14.0);
    LIST_PX = ptToPx(listPt);
    // The earlier detail font was 3/4 of the list size; reduce it by 0.5 pt.
    LIST_DETAIL_PX = ptToPx(listPt * 0.75 - 0.5);
    // Fixed Kobo-proportional chrome; legacy appearance keys are preserved but ignored.
    const double uiScale = SCREEN_DPI / 300.0;
    FONT_BTN = qRound(32 * uiScale); FONT_MSG = qRound(30 * uiScale); GAP = qRound(16 * uiScale);
    BTN_MARGIN = qRound(12 * uiScale); BTN_HEIGHT = qRound(72 * uiScale); ICON_PX = qRound(32 * uiScale);
    BTN_RADIUS = 0; BTN_PEN = qMax(1.0, 2 * uiScale);
    LONG_TAP_MS = int(qBound(350.0, num("long_tap_ms", 450, 150, 3000), 700.0));
    KEY_FONT_SET = false; FUNC_FONT_SET = false;
    KEY_FONT_PX = 0; FUNC_PX = 0;
    SEARCH_LIST_PX = ptToPx(10); SEARCH_LIST_FONT.clear();
    LIST_FONT   = canUpdate ? st.value(u8("kobrowse/list_font"), cfgDefault(u8("list_font"))).toString().trimmed() : cfgDefault(u8("list_font"));
    BTN_FONT.clear();
    SHOW_HIDDEN = num("show_hidden", 1, 0, 1) != 0;
    TRASH_ENABLED = num("trash_enabled", 1, 0, 1) != 0;
    ALLOW_ADDS = num("allow_adds_edit", 0, 0, 1) != 0;
}

// Отладочный лог: файл открывается и сразу закрывается (не держим дескриптор на /mnt/onboard)
static void klog(const QString &msg) {
    const QString app = u8(KOBROWSE_ROOT) + u8("/.adds/kobrowse");
    if (!safeLocalPath(u8(KOBROWSE_ROOT), app) || !QFileInfo(app).isDir()) return;
    if (!safeLocalPath(u8(KOBROWSE_ROOT), u8(KOBROWSE_INI), true)) return;
    QSettings st(u8(KOBROWSE_INI), QSettings::IniFormat);
    if (!st.value(u8("kobrowse/logging_enabled"), 1).toBool()) return;
    const QString dir = QFileInfo(u8(KOBROWSE_LOG)).absolutePath();
    if (!safeLocalPath(u8(KOBROWSE_ROOT), dir, true) || !QDir().mkpath(dir)
        || !safeLocalPath(u8(KOBROWSE_ROOT), dir)
        || !safeLocalPath(u8(KOBROWSE_ROOT), u8(KOBROWSE_LOG), true)) return;
    QFile f(u8(KOBROWSE_LOG));
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write((QDateTime::currentDateTime().toString(u8("hh:mm:ss ")) + msg + u8("\n")).toUtf8());
        f.close();
    }
}

// Размер файла в читаемом виде
static QString humanSize(qint64 b) {
    if (b < 1024) return QString::number(b) + u8(" Б");
    if (b < 1024 * 1024) return QString::number(b / 1024.0, 'f', 1) + u8(" КБ");
    if (b < 1024LL * 1024 * 1024) return QString::number(b / (1024.0 * 1024.0), 'f', 1) + u8(" МБ");
    return QString::number(b / (1024.0 * 1024.0 * 1024.0), 'f', 1) + u8(" ГБ");
}

// ---------- файловые операции ----------

// Свободное имя в папке dir: «name», «name (1)», «name (2).epub» …
static QString uniqueName(const QString &dir, const QString &name, bool isDir) {
    QString base = name, ext;
    if (!isDir) {
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        if (dot > 0) { base = name.left(dot); ext = name.mid(dot); }
    }
    QString cand = dir + u8("/") + name;
    for (int n = 1; QFileInfo(cand).exists() || QFileInfo(cand).isSymLink(); ++n)
        cand = dir + u8("/") + base + u8(" (%1)").arg(n) + ext;
    return cand;
}

// Данные строки списка
static const int ROLE_PATH = Qt::UserRole,     // полный путь
                 ROLE_DIR  = Qt::UserRole + 1, // это папка
                 ROLE_MARK = Qt::UserRole + 2, // строка отмечена
                 ROLE_DETAIL = Qt::UserRole + 4, // дата и размер
                 ROLE_PARENT = Qt::UserRole + 5, // специальная строка «..»
                 ROLE_READONLY = Qt::UserRole + 6, // защита от операций
                 ROLE_KIND = Qt::UserRole + 7, // тип файла для значка
                 ROLE_DATE = Qt::UserRole + 8, ROLE_SIZE = Qt::UserRole + 9;

// Разбивка текста на строки по ширине: по словам, а слово длиннее строки — где угодно.
// Возвращает высоту; если painter задан — ещё и рисует.
static qreal layoutText(const QString &text, const QFont &f, qreal width,
                        QPainter *p = nullptr, const QPointF &pos = QPointF()) {
    QTextLayout tl(text, f);
    QTextOption o;
    o.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    tl.setTextOption(o);
    tl.beginLayout();
    qreal y = 0;
    for (;;) {
        QTextLine line = tl.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(width);
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    tl.endLayout();
    if (p) tl.draw(p, pos);
    return y;
}

// Share exact pixel boundaries between header hit areas and painted rows.
struct BrowserColumns { int name, date, size; };
static BrowserColumns browserColumns(int width, const QFont &listFont) {
    QFont detail = listFont; detail.setPixelSize(qMax(10, LIST_DETAIL_PX));
    const QFontMetrics fm(detail);
    // Allow for the arrow, label and padding even when the column is active.
    QFont header = listFont;
    header.setPixelSize(qRound(qMax(10, qMax(12, qRound(26 * SCREEN_DPI / 300.0)) * 3 / 4) * 1.5));
    const QFontMetrics hm(header);
    const int inset = qMax(6, qRound(8 * SCREEN_DPI / 300.0));
    int date = qMax(fm.width(u8("88.88.88")) + 12, hm.width(u8("↓  Дата")) + 2 * inset + 2);
    int size = qMax(fm.width(u8("888.8 МБ")) + 12, hm.width(u8("↓  Размер")) + 2 * inset + 2);
    const int budget = qMax(2, width - qMax(qMax(32, ICON_PX * 2) + 24, width / 3));
    if (date + size > budget) {
        date = qMax(1, budget * date / (date + size));
        size = qMax(1, budget - date);
    }
    BrowserColumns result = { qMax(1, width - date - size), date, size };
    return result;
}

// Контрастные пиктограммы и двухстрочные сведения удобны на e-ink.
class RowDelegate : public QStyledItemDelegate {
public:
    RowDelegate(QWidget *view, const QFont &f) : QStyledItemDelegate(view), m_view(view), m_font(f) {}
    void setMaxRowHeight(int height) { m_maxHeight = height; }
    void setListFont(const QFont &font) { m_font = font; }
    QString displayedName(const QModelIndex &idx) const {
        QString name = idx.data().toString();
        name.replace(QLatin1Char('\n'), QLatin1Char(' '));
        name.replace(QLatin1Char('\r'), QLatin1Char(' '));
        name.replace(QLatin1Char('\t'), QLatin1Char(' '));
        // The model retains the full name for every file operation and sort.
        if (!idx.data(ROLE_DIR).toBool() && fileKind(name) != FileGeneric) {
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            if (dot > 0) name.truncate(dot);
        }
        return QFontMetrics(fontFor(idx)).elidedText(name, Qt::ElideRight, int(textWidth()));
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &idx) const override {
        const qreal h = layoutText(displayedName(idx), fontFor(idx), textWidth());
        // Parent row uses the same two-line minimum as entries with a date.
        const int detailHeight = QFontMetrics(detailFont()).lineSpacing() * 2;
        const int wanted = qMax(iconSize(), qMax(int(h), detailHeight)) + 2 * PAD_V + 1;
        return QSize(m_view->width(), m_maxHeight > 0 ? qMin(wanted, m_maxHeight) : wanted);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override {
        const bool mark = idx.data(ROLE_MARK).toBool();
        const QColor ink = mark ? Qt::white : Qt::black;
        p->save();
        p->fillRect(opt.rect, mark ? Qt::black : Qt::white);
        p->setPen(QPen(itemIconColor(idx.data(ROLE_READONLY).toBool(), mark),
                       qMax(1, ICON_PX / 16)));
        p->setBrush(Qt::NoBrush);
        const int sz = iconSize();
        const QRectF r(opt.rect.left() + PAD_H, opt.rect.top() + PAD_V, sz, sz);
        if (idx.data(ROLE_DIR).toBool()) {
            // Two overlapping folders: outlined back and filled front.
            p->setRenderHint(QPainter::Antialiasing, true);
            QPen outline = p->pen(); outline.setWidthF(qMax(1.0, sz * .035));
            outline.setJoinStyle(Qt::RoundJoin); outline.setCapStyle(Qt::RoundCap);
            p->setPen(outline);
            QPainterPath rear;
            rear.moveTo(r.left() + sz * .16, r.top() + sz * .25);
            rear.lineTo(r.left() + sz * .16, r.top() + sz * .14);
            rear.quadTo(r.left() + sz * .16, r.top() + sz * .11, r.left() + sz * .20, r.top() + sz * .11);
            rear.lineTo(r.left() + sz * .46, r.top() + sz * .11);
            rear.quadTo(r.left() + sz * .49, r.top() + sz * .11, r.left() + sz * .52, r.top() + sz * .19);
            rear.lineTo(r.left() + sz * .94, r.top() + sz * .19);
            rear.quadTo(r.left() + sz * .97, r.top() + sz * .19, r.left() + sz * .97, r.top() + sz * .22);
            rear.lineTo(r.left() + sz * .97, r.top() + sz * .80);
            rear.lineTo(r.left() + sz * .87, r.top() + sz * .80);
            p->drawPath(rear);
            QPainterPath front;
            front.moveTo(r.left() + sz * .09, r.top() + sz * .24);
            front.quadTo(r.left() + sz * .09, r.top() + sz * .20, r.left() + sz * .13, r.top() + sz * .20);
            front.lineTo(r.left() + sz * .37, r.top() + sz * .20);
            front.quadTo(r.left() + sz * .40, r.top() + sz * .20, r.left() + sz * .43, r.top() + sz * .28);
            front.lineTo(r.left() + sz * .86, r.top() + sz * .28);
            front.quadTo(r.left() + sz * .89, r.top() + sz * .28, r.left() + sz * .89, r.top() + sz * .31);
            front.lineTo(r.left() + sz * .89, r.top() + sz * .86);
            front.quadTo(r.left() + sz * .89, r.top() + sz * .89, r.left() + sz * .86, r.top() + sz * .89);
            front.lineTo(r.left() + sz * .12, r.top() + sz * .89);
            front.quadTo(r.left() + sz * .09, r.top() + sz * .89, r.left() + sz * .09, r.top() + sz * .86);
            front.closeSubpath();
            p->setBrush(outline.color()); p->drawPath(front); p->setBrush(Qt::NoBrush);
            p->setRenderHint(QPainter::Antialiasing, false);
        } else {
            const QRectF box = r.adjusted(sz * .03, sz * .07, -sz * .03, -sz * .07);
            const int kind = idx.data(ROLE_KIND).toInt();
            if (kind != FileArchive && kind != FileBook && kind != FileFb2 && kind != FilePdf && kind != FileEpub) p->drawRect(box);
            if (kind == FileBook || kind == FileFb2 || kind == FilePdf || kind == FileEpub) {
                // Book outline based on the supplied FB2 reference, with a thin spine.
                QPainterPath book;
                book.moveTo(box.right(), box.top()); book.lineTo(box.left() + sz * .18, box.top());
                book.cubicTo(box.left(), box.top(), box.left(), box.top() + sz * .13, box.left(), box.top() + sz * .2);
                book.lineTo(box.left(), box.bottom() - sz * .16);
                book.cubicTo(box.left(), box.bottom() - sz * .04, box.left() + sz * .08, box.bottom(), box.left() + sz * .18, box.bottom());
                book.lineTo(box.right(), box.bottom());
                book.moveTo(box.left(), box.bottom() - sz * .16);
                book.cubicTo(box.left(), box.bottom() - sz * .27, box.left() + sz * .08, box.bottom() - sz * .30, box.left() + sz * .18, box.bottom() - sz * .30);
                book.lineTo(box.right(), box.bottom() - sz * .30); book.lineTo(box.right(), box.top());
                QPen bookPen = p->pen(); bookPen.setWidthF(qMax(1.0, bookPen.widthF() * 1.3)); p->setPen(bookPen);
                p->drawPath(book);
                QString ext = QFileInfo(idx.data().toString()).suffix().toUpper();
                if (ext.isEmpty()) ext = u8("TXT");
                QFont badge(u8("Sans Serif")); badge.setWeight(QFont::Black);
                int badgeSize = qMax(8, qRound(sz * .30)); badge.setPixelSize(badgeSize);
                while (badgeSize > 8 && QFontMetrics(badge).width(ext) > box.width() * .82) badge.setPixelSize(--badgeSize);
                p->setFont(badge);
                p->drawText(QRectF(box.left() + sz * .08, box.top(), box.width() - sz * .13, box.height() * .70), Qt::AlignCenter, ext);
            } else if (kind == FileImage) {
                p->drawEllipse(QPointF(box.left() + sz * .20, box.top() + sz * .21), sz * .06, sz * .06);
                QPolygonF mountains;
                mountains << QPointF(box.left() + sz * .05, box.bottom() - sz * .10)
                          << QPointF(box.left() + sz * .28, box.top() + sz * .48)
                          << QPointF(box.left() + sz * .40, box.top() + sz * .60)
                          << QPointF(box.left() + sz * .52, box.top() + sz * .43)
                          << QPointF(box.right() - sz * .05, box.bottom() - sz * .10);
                p->drawPolyline(mountains);
            } else if (kind == FileArchive) {
                // The reference is a stack of three curved books, bound by a strap.
                const qreal x = box.left(), y = box.top(), w = box.width(), h = box.height();
                QPen archivePen = p->pen(); archivePen.setWidthF(qMax(1.0, archivePen.widthF() * 1.3));
                archivePen.setJoinStyle(Qt::RoundJoin); p->setPen(archivePen);
                QPainterPath cover;
                cover.moveTo(x, y); cover.lineTo(x+w*.69, y); cover.lineTo(x+w*.97, y+h*.19);
                cover.cubicTo(x+w*1.01, y+h*.24, x+w*1.01, y+h*.35, x+w*.97, y+h*.42);
                cover.cubicTo(x+w*1.01, y+h*.49, x+w*1.01, y+h*.62, x+w*.97, y+h*.68);
                cover.cubicTo(x+w*1.01, y+h*.76, x+w*1.01, y+h*.92, x+w*.97, y+h);
                cover.lineTo(x+w*.24, y+h); cover.lineTo(x, y+h*.81);
                cover.cubicTo(x+w*.03, y+h*.74, x+w*.03, y+h*.68, x, y+h*.62);
                cover.cubicTo(x+w*.03, y+h*.55, x+w*.03, y+h*.48, x, y+h*.42);
                cover.cubicTo(x+w*.03, y+h*.29, x+w*.03, y+h*.14, x, y); p->drawPath(cover);
                p->drawLine(QPointF(x, y), QPointF(x+w*.24, y+h*.19));
                p->drawLine(QPointF(x+w*.24, y+h*.19), QPointF(x+w*.97, y+h*.19));
                QPainterPath spine; spine.moveTo(x+w*.24, y+h*.19);
                spine.cubicTo(x+w*.29, y+h*.27, x+w*.29, y+h*.37, x+w*.24, y+h*.46);
                spine.cubicTo(x+w*.29, y+h*.54, x+w*.29, y+h*.64, x+w*.24, y+h*.73);
                spine.cubicTo(x+w*.29, y+h*.83, x+w*.29, y+h*.94, x+w*.24, y+h);
                p->drawPath(spine);
                for (int i = 1; i <= 2; ++i) {
                    const qreal yy = y+h*(.19 + .27*i);
                    p->drawLine(QPointF(x, yy-h*.19), QPointF(x+w*.24, yy));
                    p->drawLine(QPointF(x+w*.24, yy), QPointF(x+w*.97, yy));
                }
                p->drawLine(QPointF(x+w*.42, y), QPointF(x+w*.66, y+h*.19));
                p->drawLine(QPointF(x+w*.55, y), QPointF(x+w*.78, y+h*.19));
                p->drawLine(QPointF(x+w*.66, y+h*.19), QPointF(x+w*.66, y+h));
                p->drawLine(QPointF(x+w*.78, y+h*.19), QPointF(x+w*.78, y+h));
                p->setBrush(mark ? Qt::black : Qt::white);
                p->drawRoundedRect(QRectF(x+w*.61, y+h*.40, w*.22, h*.18), sz*.025, sz*.025);
                p->setBrush(Qt::NoBrush);
            }
        }
        p->setPen(ink);
        const QPointF pos(opt.rect.left() + PAD_H * 2 + sz, opt.rect.top() + PAD_V);
        p->setClipRect(opt.rect);
        layoutText(displayedName(idx), fontFor(idx), textWidth(), p, pos);
        const BrowserColumns columns = browserColumns(opt.rect.width(), m_font);
        const int dateLeft = opt.rect.left() + columns.name;
        const int sizeLeft = dateLeft + columns.date;
        const QRect dateRect(dateLeft + 3, opt.rect.top() + PAD_V, qMax(1, sizeLeft - dateLeft - 6), opt.rect.height() - 2 * PAD_V);
        const QRect sizeRect(sizeLeft + 3, opt.rect.top() + PAD_V, qMax(1, opt.rect.right() - sizeLeft - 6), opt.rect.height() - 2 * PAD_V);
        QFont dateFont = detailFont();
        const QString date = idx.data(ROLE_DATE).toString();
        const QStringList dateLines = date.split(QLatin1Char('\n'));
        int datePx = dateFont.pixelSize();
        while (datePx > 10 && (QFontMetrics(dateFont).width(dateLines.value(0)) > dateRect.width()
                              || QFontMetrics(dateFont).width(dateLines.value(1)) > dateRect.width()))
            dateFont.setPixelSize(--datePx);
        p->setFont(dateFont);
        p->setPen(mark ? QColor(Qt::white) : QColor(80, 80, 80));
        p->drawText(dateRect, Qt::AlignVCenter | Qt::AlignLeft, date);
        QFont sizeFont = detailFont(); int sizePx = sizeFont.pixelSize();
        const QString size = idx.data(ROLE_SIZE).toString();
        while (sizePx > 10 && QFontMetrics(sizeFont).width(size) > sizeRect.width()) sizeFont.setPixelSize(--sizePx);
        p->setFont(sizeFont);
        p->setPen(ink);
        p->drawText(sizeRect, Qt::AlignVCenter | Qt::AlignRight,
                    QFontMetrics(sizeFont).elidedText(size, Qt::ElideRight, sizeRect.width()));
        if (!mark) {
            const bool parentRow = idx.data(ROLE_PARENT).toBool();
            QPen separator(parentRow ? QColor(Qt::black) : QColor(150, 150, 150),
                           qMax(1, qRound(1.5 * SCREEN_DPI / 300.0)),
                           parentRow ? Qt::DashLine : Qt::SolidLine, Qt::FlatCap);
            p->setPen(separator);
            p->drawLine(opt.rect.bottomLeft(), opt.rect.bottomRight());
        }
        p->restore();
    }
private:
    enum { PAD_V = 10, PAD_H = 6 };
    int iconSize() const { return qMax(32, ICON_PX * 2); }
    QFont detailFont() const { QFont f = m_font; f.setPixelSize(qMax(10, LIST_DETAIL_PX)); return f; }
    QFont fontFor(const QModelIndex &idx) const {
        QFont f = m_font;
        f.setBold(idx.data(ROLE_DIR).toBool());
        return f;
    }
    qreal textWidth() const {
        const QAbstractScrollArea *area = qobject_cast<const QAbstractScrollArea *>(m_view);
        const int width = area ? area->viewport()->width() : m_view->width();
        return qMax(1, browserColumns(width, m_font).name - iconSize() - 3 * PAD_H);
    }
    QWidget *m_view;
    QFont m_font;
    int m_maxHeight = 0;
};

class BrowserColumnHeader : public QWidget {
public:
    BrowserColumnHeader(QWidget *parent, const QFont &listFont) : QWidget(parent), m_listFont(listFont) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_layout = new QHBoxLayout(this); m_layout->setContentsMargins(0, 0, 0, 0); m_layout->setSpacing(0);
    }
    void addColumn(QPushButton *button) { m_columns << button; m_layout->addWidget(button); }
    void setListFont(const QFont &font) { m_listFont = font; applyWidths(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QWidget::resizeEvent(event); applyWidths(); }
private:
    void applyWidths() {
        if (m_columns.size() != 3 || width() <= 1) return;
        const BrowserColumns c = browserColumns(width(), m_listFont);
        m_columns[0]->setFixedWidth(c.name); m_columns[1]->setFixedWidth(c.date); m_columns[2]->setFixedWidth(c.size);
    }
    QFont m_listFont;
    QHBoxLayout *m_layout;
    QVector<QPushButton *> m_columns;
};

// Рубленый шрифт Kobo для кнопок: Kobo Nickel (шрифт интерфейса), затем Avenir, иначе любой sans
static QFont sansFont() {
    const QStringList fam = QFontDatabase().families();
    if (!BTN_FONT.isEmpty())                       // гарнитура из kobrowse.ini, если она есть на ридере
        for (const QString &f : fam)
            if (f.compare(BTN_FONT, Qt::CaseInsensitive) == 0) return QFont(f);
    const char *pref[] = { "Kobo Nickel", "KoboNickel", "Avenir Next", "Avenir", "Rakuten Sans UI App" };
    for (const char *name : pref)
        for (const QString &f : fam)
            if (f.compare(u8(name), Qt::CaseInsensitive) == 0) return QFont(f);
    QFont f;
    f.setStyleHint(QFont::SansSerif);
    return f;
}

// Иконки кнопок — рисуются линиями (QPainter), без файлов и шрифтов
enum Icon { I_NONE, I_COPY, I_CUT, I_TRASH, I_INFO, I_DESELECT, I_FOLDER_UP, I_CHEV_UP, I_CHEV_DOWN,
            I_EYE, I_EYE_OFF, I_CLOSE, I_CLIPBOARD, I_CLIPBOARD_X, I_GEAR, I_BACK, I_SEARCH, I_MORE, I_HOME };

// Рисует иконку в квадрате r; координаты в сетке 24×24 (как у Tabler Icons)
static qreal chromeOutlineWidth(qreal scale = 1.0) { return 0.9 * scale; }
static void drawIcon(QPainter *p, Icon ic, const QRectF &r, const QColor &c, qreal outlineScale = 1.0) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->translate(r.topLeft());
    p->scale(r.width() / 24.0, r.height() / 24.0);
    QPen pen(c, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    switch (ic) {
    case I_BACK:
        p->setPen(QPen(c, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawLine(QPointF(20, 12), QPointF(4, 12));
        p->drawLine(QPointF(4, 12), QPointF(10, 6));
        p->drawLine(QPointF(4, 12), QPointF(10, 18));
        break;
    case I_HOME: { // Continuous roof and walls, open doorway like the reference.
        p->setPen(QPen(c, chromeOutlineWidth(outlineScale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath house; house.moveTo(1.8, 11); house.lineTo(12, 1.8); house.lineTo(22.2, 11);
        house.lineTo(20, 11); house.lineTo(20, 22); house.lineTo(14, 22);
        house.lineTo(14, 15.5); house.lineTo(10, 15.5); house.lineTo(10, 22);
        house.lineTo(4, 22); house.lineTo(4, 11); house.closeSubpath(); p->drawPath(house);
        break;
    }
    case I_SEARCH: { // Lens with a curved inner reflection and outlined grip.
        p->setPen(QPen(c, chromeOutlineWidth(outlineScale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath lens;
        lens.moveTo(16.2, 13.9);
        lens.cubicTo(18.7, 10.7, 17.8, 5.9, 14.9, 3.5);
        lens.cubicTo(11.8, .9, 7.4, 1.1, 4.6, 3.9);
        lens.cubicTo(1.7, 6.7, 1.7, 11.5, 4.6, 14.4);
        lens.cubicTo(7.1, 17, 11.4, 17.5, 14.3, 15.5);
        lens.lineTo(17.1, 18.2);
        lens.quadTo(16.8, 18.9, 17.4, 19.5);
        lens.lineTo(21.0, 23.0); lens.quadTo(21.6, 23.6, 22.2, 23.0);
        lens.lineTo(23.0, 22.2); lens.quadTo(23.6, 21.6, 23.0, 21.0);
        lens.lineTo(19.4, 17.4); lens.quadTo(18.9, 16.9, 18.2, 17.1);
        lens.closeSubpath(); p->drawPath(lens);
        QPainterPath reflection; reflection.moveTo(4.9, 7.4);
        reflection.cubicTo(4.4, 10.7, 6.8, 14.4, 11.0, 14.3);
        p->drawPath(reflection);
        break;
    }
    case I_MORE:
        p->setBrush(c);
        p->drawEllipse(QPointF(12, 5), 1.5, 1.5);
        p->drawEllipse(QPointF(12, 12), 1.5, 1.5);
        p->drawEllipse(QPointF(12, 19), 1.5, 1.5);
        break;
    case I_COPY:                       // два листа
        p->drawRoundedRect(QRectF(4, 4, 11, 11), 1.5, 1.5);
        p->setBrush(Qt::white);
        p->drawRoundedRect(QRectF(9, 9, 11, 11), 1.5, 1.5);
        break;
    case I_CUT:                        // ножницы
        p->drawEllipse(QPointF(7, 18), 3, 3);
        p->drawEllipse(QPointF(17, 18), 3, 3);
        p->drawLine(QPointF(9, 15.6), QPointF(17, 4));
        p->drawLine(QPointF(15, 15.6), QPointF(7, 4));
        break;
    case I_TRASH: { // Lid, handle and three vertical bars.
        p->setPen(QPen(c, chromeOutlineWidth(outlineScale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawRect(QRectF(2.8, 6.4, 18.4, 1.5));
        QPainterPath handle; handle.moveTo(9, 6.3); handle.lineTo(9, 4.9);
        handle.quadTo(9, 3.8, 10.2, 3.8); handle.lineTo(13.8, 3.8);
        handle.quadTo(15, 3.8, 15, 4.9); handle.lineTo(15, 6.3); p->drawPath(handle);
        QPainterPath body; body.moveTo(4.2, 8); body.lineTo(5.6, 21);
        body.lineTo(18.4, 21); body.lineTo(19.8, 8); p->drawPath(body);
        p->drawLine(QPointF(8.0, 10.1), QPointF(8.4, 19));
        p->drawLine(QPointF(12, 10.1), QPointF(12, 19));
        p->drawLine(QPointF(16.0, 10.1), QPointF(15.6, 19));
        break;
    }
    case I_INFO:                       // «i» в круге
        p->drawEllipse(QPointF(12, 12), 9, 9);
        p->drawLine(QPointF(12, 11), QPointF(12, 16.5));
        p->setBrush(c);
        p->drawEllipse(QPointF(12, 7.8), 0.6, 0.6);
        break;
    case I_DESELECT: {                 // пунктирная рамка
        QPen d = pen; d.setStyle(Qt::DashLine); d.setCapStyle(Qt::FlatCap);
        p->setPen(d);
        p->drawRect(QRectF(4, 4, 16, 16));
        break;
    }
    case I_FOLDER_UP: {                // папка со стрелкой вверх
        QPolygonF f; f << QPointF(3, 6) << QPointF(9, 6) << QPointF(11, 8) << QPointF(21, 8)
                       << QPointF(21, 19) << QPointF(3, 19) << QPointF(3, 6);
        p->drawPolyline(f);
        p->drawLine(QPointF(12, 17), QPointF(12, 11));
        QPolygonF a; a << QPointF(9.5, 13.5) << QPointF(12, 11) << QPointF(14.5, 13.5);
        p->drawPolyline(a);
        break;
    }
    case I_CHEV_UP:
    case I_CHEV_DOWN: { // Broad chevron, not a solid triangle.
        QPolygonF shape;
        shape << QPointF(1.5, 5) << QPointF(7.0, 5) << QPointF(12, 11.0)
              << QPointF(17.0, 5) << QPointF(22.5, 5) << QPointF(12, 19);
        if (ic == I_CHEV_UP) for (int i = 0; i < shape.size(); ++i) shape[i].setY(24 - shape[i].y());
        p->setPen(Qt::NoPen); p->setBrush(c); p->drawPolygon(shape);
        break;
    }
    case I_EYE:
    case I_EYE_OFF: {                  // глаз; перечёркнутый — скрытые не показаны
        QPainterPath e;
        e.moveTo(2, 12);
        e.cubicTo(6, 5, 18, 5, 22, 12);
        e.cubicTo(18, 19, 6, 19, 2, 12);
        p->drawPath(e);
        p->drawEllipse(QPointF(12, 12), 3, 3);
        if (ic == I_EYE_OFF) p->drawLine(QPointF(4, 4), QPointF(20, 20));
        break;
    }
    case I_CLOSE:
        p->drawLine(QPointF(6, 6), QPointF(18, 18));
        p->drawLine(QPointF(18, 6), QPointF(6, 18));
        break;
    case I_GEAR: { // Rounded eight-tooth outline and a single large inner circle.
        QVector<QPointF> points;
        const qreal pi = 3.141592653589793;
        for (int i = 0; i < 8; ++i) {
            const qreal center = -pi / 2 + i * pi / 4;
            const qreal offsets[] = { -.34, -.22, .22, .34 };
            const qreal radii[] = { 8.0, 10.5, 10.5, 8.0 };
            for (int j = 0; j < 4; ++j)
                points << QPointF(12 + radii[j] * qCos(center + offsets[j]), 12 + radii[j] * qSin(center + offsets[j]));
        }
        QPainterPath gear;
        for (int i = 0; i < points.size(); ++i) {
            const QPointF prev = points[(i + points.size() - 1) % points.size()];
            const QPointF cur = points[i], next = points[(i + 1) % points.size()];
            const QPointF start = cur * .7 + prev * .3, end = cur * .7 + next * .3;
            if (i == 0) gear.moveTo(start); else gear.lineTo(start);
            gear.quadTo(cur, end);
        }
        gear.closeSubpath();
        p->setPen(QPen(c, chromeOutlineWidth(outlineScale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(gear); p->drawEllipse(QPointF(12, 12), 4.5, 4.5);
        break;
    }
    case I_CLIPBOARD:
    case I_CLIPBOARD_X:                // планшет; с крестиком — очистить
        p->drawRoundedRect(QRectF(5, 5, 14, 16), 1.5, 1.5);
        p->setBrush(Qt::white);
        p->drawRoundedRect(QRectF(9, 3, 6, 4), 1, 1);
        if (ic == I_CLIPBOARD_X) {
            p->drawLine(QPointF(10, 12), QPointF(14, 16));
            p->drawLine(QPointF(14, 12), QPointF(10, 16));
        }
        break;
    case I_NONE:
        break;
    }
    p->restore();
}

// Лицевая сторона кнопки: иконка сверху, подпись под ней (по центру, перенос по словам).
// Прозрачна для касаний — tapAt поднимается от неё к QPushButton.
class ButtonFace : public QWidget {
public:
    ButtonFace(Icon ic, const QString &t, QWidget *parent) : QWidget(parent), m_icon(ic), m_text(t) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setFace(Icon ic, const QString &t) { m_icon = ic; m_text = t; updateGeometry(); update(); }
    const QString &text() const { return m_text; }
    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        return QSize(fm.width(m_text) + 2 * BTN_MARGIN + 4, fm.lineSpacing() + 8);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPushButton *button = qobject_cast<QPushButton *>(parentWidget());
        const bool on = !button || button->isEnabled();
        const bool inverted = button && (button->isDown() || button->property("selectedTab").toBool());
        const QColor c = inverted ? QColor(Qt::white) : (on ? QColor(Qt::black) : QColor(160, 160, 160));
        const qreal h = BTN_PEN / 2.0;
        p.setPen(QPen(on ? QColor(Qt::black) : QColor(160, 160, 160), BTN_PEN));
        p.setBrush(inverted ? Qt::black : Qt::white);
        p.drawRoundedRect(QRectF(rect()).adjusted(h, h, -h, -h), BTN_RADIUS, BTN_RADIUS);
        p.setPen(c);
        p.setFont(font());
        if (m_icon == I_NONE) {            // кнопка без иконки — подпись по центру
            p.drawText(rect().adjusted(BTN_MARGIN, 0, -BTN_MARGIN, 0),
                       Qt::AlignCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere, m_text);
            return;
        }
        const int top = 12;
        drawIcon(&p, m_icon, QRectF((width() - ICON_PX) / 2.0, top, ICON_PX, ICON_PX), c);
        p.setPen(c);
        const QRect tr(BTN_MARGIN, top + ICON_PX + 4, width() - 2 * BTN_MARGIN, height() - top - ICON_PX - 6);
        p.drawText(tr, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap | Qt::TextWrapAnywhere, m_text);
    }
private:
    Icon    m_icon;
    QString m_text;
};

// Компактные элементы навигации по образцу верхней панели Kobalt.
class ChromeButton : public QPushButton {
public:
    ChromeButton(Icon icon, const QString &text, QWidget *parent, bool framed = false)
        : QPushButton(parent), m_icon(icon), m_text(text), m_framed(framed) {
        setFocusPolicy(Qt::NoFocus);
        setAccessibleName(text);
    }
    void setIconScale(qreal scale) { m_iconScale = scale; update(); }
    void setLargePlainIcon(bool value) { m_largePlainIcon = value; update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const bool pressed = isEnabled() && isDown() && !m_largePlainIcon;
        const QColor ink = pressed ? QColor(Qt::white) : (isEnabled() ? QColor(Qt::black) : QColor(165, 165, 165));
        if (pressed) p.fillRect(rect(), Qt::black);
        if (m_framed) {
            p.setRenderHint(QPainter::Antialiasing, true); p.setPen(QPen(ink, qMax(1.0, BTN_PEN)));
            p.setBrush(pressed ? Qt::black : Qt::white); p.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), BTN_RADIUS, BTN_RADIUS);
        }
        const int iconSize = qRound(ICON_PX * m_iconScale * ((m_icon == I_CHEV_UP || m_icon == I_CHEV_DOWN) ? 1.6 : 1.0));
        const int size = m_largePlainIcon ? qRound(qMin(height(), width()) * 1.08 * m_iconScale)
                                          : qMin(iconSize, qMin(height() - 8, width() - 8));
        if (size <= 0) return;
        const int left = m_text.isEmpty() ? (width() - size) / 2 : (m_framed ? 12 : 4);
        // Enlarging a toolbar icon must not thicken its outline compared with
        // the search lens, which is drawn at the unscaled toolbar size.
        const qreal outlineScale = m_largePlainIcon ? qRound(qMin(height(), width()) * 1.08) / qreal(size) : 1.0;
        drawIcon(&p, m_icon, QRectF(left, (height() - size) / 2, size, size), ink, outlineScale);
        if (!m_text.isEmpty()) {
            p.setPen(ink); p.setFont(font());
            p.drawText(QRect(left + size + 8, 0, width() - left - size - 8, height()),
                       Qt::AlignLeft | Qt::AlignVCenter, m_text);
        }
    }
private:
    Icon m_icon;
    QString m_text;
    bool m_framed;
    bool m_largePlainIcon = false;
    qreal m_iconScale = 1.0;
};

// Geometric plus/minus avoids font-dependent baseline offsets on Kobo.
class SettingStepButton : public QPushButton {
public:
    SettingStepButton(const QString &text, QWidget *parent) : QPushButton(text, parent), plus(text == QLatin1String("+")) {}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing, true);
        const bool down = isEnabled() && isDown();
        const QColor ink = isEnabled() ? (down ? QColor(Qt::white) : QColor(Qt::black)) : QColor(170, 170, 170);
        p.fillRect(rect(), down ? Qt::black : Qt::white);
        p.setPen(QPen(ink, qMax(1.0, BTN_PEN))); p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(rect()).adjusted(BTN_PEN/2, BTN_PEN/2, -BTN_PEN/2, -BTN_PEN/2));
        const qreal length = qMin(width(), height()) * .48;
        const QPointF center(width() / 2.0, height() / 2.0);
        p.setPen(QPen(ink, qMax(2.0, BTN_PEN * 1.5), Qt::SolidLine, Qt::SquareCap));
        p.drawLine(center + QPointF(-length/2, 0), center + QPointF(length/2, 0));
        if (plus) p.drawLine(center + QPointF(0, -length/2), center + QPointF(0, length/2));
    }
private:
    bool plus;
};

// Рисованный переключатель: одинаково виден на Qt 5 и e-ink.
class SettingSwitch : public QPushButton {
public:
    explicit SettingSwitch(QWidget *parent) : QPushButton(parent) { setCheckable(true); setFocusPolicy(Qt::NoFocus); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing, true);
        const qreal h = qMax(12.0, qMin(height() - 12, width() - 12) * .85);
        const QRectF box((width()-h)/2, (height()-h)/2, h, h);
        const QColor ink = Qt::black;
        p.setPen(QPen(ink, qMax(1.5, h / 20.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(isChecked() ? Qt::black : Qt::white); p.drawRect(box);
        if (isChecked()) {
            p.setPen(QPen(Qt::white, qMax(1.5, h / 20.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPolygonF check; check << QPointF(box.left()+h*.18, box.top()+h*.52)
                << QPointF(box.left()+h*.42, box.top()+h*.75) << QPointF(box.left()+h*.84, box.top()+h*.24);
            p.drawPolyline(check);
        }
    }
};

// ---------- открытие книги через Nickel ----------
// Функции libnickel вызываются по адресам из dlsym. Сигнатуры восстановлены по дизассемблеру
// (прошивка 4.38): все методы — с this в r0; возврат объекта по значению — скрытый указатель в r0.
//   VolumeManager::getById(id, db)          — статический, Volume по значению
//   Device::getCurrentDevice()              — статический, Device*
//   Device::getDbName() const               — QString const&
//   AbstractReadingViewMixin::showReader(this, Volume, QVector<ContentKey>, ctrl, Bookmark)
//       — 4 аргумента; Kobalt перехватывает ВЫЗОВЫ этой функции изнутри libnickel (через PLT),
//         прямой вызов по адресу из dlsym идёт мимо перехвата → штатная читалка Nickel
//   ReadingViewMixin::showReaderOrInfo(this, Volume, Bookmark)
//       — тот же путь, что и тап в библиотеке Nickel → проходит через Kobalt
// this — «пустой» ReadingViewMixin: только указатель на его vtable (_ZTV16ReadingViewMixin + 8).
template <typename T> static T nsym(const char *n) { return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, n)); }

// Можно ли открывать книги на этой прошивке. Вызовы libnickel проверены только на 4.38: на другой версии
// функции с теми же именами могут вести себя иначе, поэтому по умолчанию (auto) там открытие выключено.
static bool bookOpenAllowedFor(const QString &mode, const QString &version) {
    if (mode == u8("1")) return true;
    if (mode == u8("0")) return false;
    if (mode != u8("auto") && !mode.isEmpty()) return false;
    const QStringList parts = version.trimmed().split(QLatin1Char(','));
    return parts.size() > 2 && parts.at(2).startsWith(u8("4.38."));
}
static bool bookOpenAllowed() {
    if (OPEN_BOOKS == u8("1") || OPEN_BOOKS == u8("0")) return bookOpenAllowedFor(OPEN_BOOKS, QString());
    QFile f(u8("/mnt/onboard/.kobo/version"));
    if (!f.open(QIODevice::ReadOnly)) return false;
    return bookOpenAllowedFor(OPEN_BOOKS, QString::fromLatin1(f.readAll()));
}
static QString bookOpenDenial() {
    if (bookOpenAllowed()) return QString();
    return OPEN_BOOKS == u8("0")
        ? u8("Открытие книг выключено (open_books=0 в kobrowse.ini)")
        : u8("Открытие книг проверено только на прошивке 4.38. Включить на свой риск: open_books=1 в .adds/kobrowse/kobrowse.ini");
}
#ifdef KOBROWSE_SETTINGS_TEST
static std::function<bool(const QString &, bool, QString &)> testBookOpener;
#endif
static bool openBook(const QString &path, bool viaKobalt, QString &err) {
    // Enforce policy at the single gateway too, before looking up any private API.
    err = bookOpenDenial();
    if (!err.isEmpty()) return false;
    if (!safeLocalPath(u8(KOBROWSE_ROOT), path) || !QFileInfo(path).isFile()) {
        err = u8("Ссылка или файл вне накопителя недоступен"); return false;
    }
#ifdef KOBROWSE_SETTINGS_TEST
    if (testBookOpener) return testBookOpener(path, viaKobalt, err);
#endif
    auto getCur  = nsym<void *(*)()>("_ZN6Device16getCurrentDeviceEv");
    auto getDb   = nsym<const QString &(*)(void *)>("_ZNK6Device9getDbNameEv");
    auto getById = nsym<void (*)(void *, const QString &, const QString &)>("_ZN13VolumeManager7getByIdERK7QStringS2_");
    auto isValid = nsym<bool (*)(void *)>("_ZNK6Volume7isValidEv");
    auto volDtor = nsym<void (*)(void *)>("_ZN6VolumeD1Ev");
    auto bmCtor  = nsym<void (*)(void *)>("_ZN8BookmarkC1Ev");
    auto bmDtor  = nsym<void (*)(void *)>("_ZN8BookmarkD1Ev");
    auto showRdr = nsym<void (*)(void *, void *, void *, void *, void *)>(
        "_ZN24AbstractReadingViewMixin10showReaderERK6VolumeRK7QVectorI10ContentKeyEP25AbstractLibraryControllerIS0_ERK8Bookmark");
    auto showOrI = nsym<void (*)(void *, void *, void *)>("_ZN16ReadingViewMixin16showReaderOrInfoERK6VolumeRK8Bookmark");
    auto vtRVM   = nsym<char *>("_ZTV16ReadingViewMixin");
    auto shNull  = nsym<void *>("_ZN10QArrayData11shared_nullE"); // пустой QVector указывает сюда
    if (!getCur || !getDb || !getById || !isValid || !volDtor || !bmCtor || !bmDtor || !vtRVM
        || (viaKobalt ? !showOrI : (!showRdr || !shNull))) {
        err = u8("Эта прошивка не поддерживает открытие книг из kobrowse");
        klog(u8("open: symbol missing"));
        return false;
    }
    static void *mixin[16];                 // «пустой» ReadingViewMixin, живёт всё время
    mixin[0] = vtRVM + 8;

    void *dev = getCur();
    if (!dev) { err = u8("Nickel: устройство не найдено"); return false; }
    const QString db = getDb(dev);
    const QString id = u8("file://") + path; // ContentID книги в базе Nickel

    alignas(8) char vol[256];               // с запасом: Volume = vptr + d-указатель
    std::memset(vol, 0, sizeof vol);
    getById(vol, id, db);
    if (!isValid(vol)) {
        volDtor(vol);
        err = u8("Книги нет в библиотеке Nickel. Обновите библиотеку: NickelMenu → «Обновить библиотеку»");
        klog(u8("open: not in db: ") + id);
        return false;
    }
    alignas(8) char bm[256];
    std::memset(bm, 0, sizeof bm);
    bmCtor(bm);                             // пустая закладка — Nickel откроет с сохранённой позиции
    klog(u8("open %1: %2").arg(viaKobalt ? u8("kobalt") : u8("nickel")).arg(id));
    if (viaKobalt) {
        showOrI(mixin, vol, bm);
    } else {
        void *vec = shNull;                 // пустой QVector<ContentKey>
        showRdr(mixin, vol, &vec, nullptr, bm);
    }
    bmDtor(bm);
    volDtor(vol);
    return true;
}

static QWidget *koboHostWindow();

// ================= ПОИСК КНИГ (встроен в kobrowse) =================
// Открывается кнопкой «Поиск» в окне kobrowse (и по сигналу /tmp/ksearch.open).
// Клавиатура в стиле Plato, поле запроса и список результатов перенесены из
// отдельного плагина ksearch (ksearch/src/ksearch.cc) — там история правок.
// layoutText() уже есть выше в этом файле, дубликат не нужен.
static const char *KSEARCH_TRIGGER = "/tmp/ksearch.open";

// ---- раскладки в стиле Plato: 2 языка × 4 уровня (обычный, Shift, Alt, Shift+Alt) ----
struct KeyDef {
    QString role;   // out, Sft, Ret, Alt, Cmb, Spc, DB, DF, MB, MF
    double stretch;
};

struct KbLayout {
    QString name;
    QString outputs[4][3];   // [уровень][ряд]
    QVector<KeyDef> keys[4];
};

static KeyDef KD(const char *role, double stretch = 1.0) {
    KeyDef d; d.role = QString::fromUtf8(role); d.stretch = stretch; return d;
}

static QVector<KbLayout> makeLayouts() {
    QVector<KbLayout> L(2);

    // Русская ЙЦУКЕН: 11 / 11 / 9 букв, нижний функциональный ряд
    L[0].name = u8("Русская · ЙЦУКЕН");
    L[0].outputs[0][0] = u8("йцукенгшщзх");
    L[0].outputs[0][1] = u8("фывапролджэ");
    L[0].outputs[0][2] = u8("ячсмитьбю");
    L[0].outputs[1][0] = u8("ЙЦУКЕНГШЩЗХ");
    L[0].outputs[1][1] = u8("ФЫВАПРОЛДЖЭ");
    L[0].outputs[1][2] = u8("ЯЧСМИТЬБЮ");
    L[0].outputs[2][0] = u8("1234567890№");
    L[0].outputs[2][1] = u8("ё\\=,'_\".*/ъ");
    L[0].outputs[2][2] = u8("₽!-():+?§");
    L[0].outputs[3][0] = u8("·“%^#$~×”°≠");
    L[0].outputs[3][1] = u8("Ё‘@[`|…]&’Ъ");
    L[0].outputs[3][2] = u8("≤–<{;}>—≥");
    for (int i = 0; i < 11; ++i) L[0].keys[0] << KD("out");
    for (int i = 0; i < 11; ++i) L[0].keys[1] << KD("out");
    L[0].keys[2] << KD("Sft", 2.0);
    for (int i = 0; i < 9; ++i) L[0].keys[2] << KD("out");
    L[0].keys[2] << KD("Ret", 2.0);
    L[0].keys[3] << KD("DB") << KD("MB", 1.53) << KD("Cmb", 1.53)
                 << KD("Spc", 3.12) << KD("Alt", 1.53) << KD("MF", 1.53) << KD("DF");

    // English QWERTY: 10 / 9 / 7 букв (как в эталонных JSON Plato)
    L[1].name = u8("English · QWERTY");
    L[1].outputs[0][0] = u8("qwertyuiop");
    L[1].outputs[0][1] = u8("asdfghjkl");
    L[1].outputs[0][2] = u8("zxcvbnm");
    L[1].outputs[1][0] = u8("QWERTYUIOP");
    L[1].outputs[1][1] = u8("ASDFGHJKL");
    L[1].outputs[1][2] = u8("ZXCVBNM");
    L[1].outputs[2][0] = u8("1234567890");
    L[1].outputs[2][1] = u8("\\=,'_\".*/");
    L[1].outputs[2][2] = u8("!-():+?");
    L[1].outputs[3][0] = u8("·“%^#$~×”°");
    L[1].outputs[3][1] = u8("'@[`|:]&'");
    L[1].outputs[3][2] = u8("-<{;}>—");
    for (int i = 0; i < 10; ++i) L[1].keys[0] << KD("out");
    L[1].keys[1] << KD("DB");
    for (int i = 0; i < 9; ++i) L[1].keys[1] << KD("out");
    L[1].keys[1] << KD("DF");
    L[1].keys[2] << KD("Sft", 2.06);
    for (int i = 0; i < 7; ++i) L[1].keys[2] << KD("out");
    L[1].keys[2] << KD("Ret", 2.06);
    L[1].keys[3] << KD("MB", 1.53) << KD("Cmb", 1.53) << KD("Spc", 4.18)
                 << KD("Alt", 1.53) << KD("MF", 1.53);
    return L;
}

static const QVector<KbLayout> KB = makeLayouts();

static QString roleLabel(const QString &role) {
    if (role == "Sft") return u8("Shift");
    if (role == "Ret") return u8("Return");
    if (role == "Alt") return u8("Alt");
    if (role == "Cmb") return u8("Cmb");
    if (role == "Spc") return u8("Space");
    if (role == "DB")  return u8("DelBwd");
    if (role == "DF")  return u8("DelFwd");
    if (role == "MB")  return u8("MoveBwd");
    if (role == "MF")  return u8("MoveFwd");
    return role;
}

static bool hasLongAction(const QString &role) {
    return role == "Spc" || role == "DB" || role == "DF" || role == "MB" || role == "MF";
}

static bool isIconRole(const QString &role) {
    return role == "DB" || role == "DF" || role == "MB" || role == "MF" ||
           role == "Sft" || role == "Ret" || role == "Cls";
}

// Полная таблица DEFAULT_COMBINATIONS из Plato (doc/MANUAL.md, keyboard.rs).
static const QHash<QString, QString> &combinations() {
    static QHash<QString, QString> m;
    if (m.isEmpty()) {
        static const char *lines[] = {
            "oe œ", "Oe Œ", "ae æ", "AE Æ", "c, ç", "C, Ç",
            "a; ą", "e; ę", "A; Ą", "E; Ę",
            "a~ ã", "o~ õ", "n~ ñ", "A~ Ã", "O~ Õ", "N~ Ñ",
            "a' á", "e' é", "i' í", "o' ó", "u' ú", "y' ý", "z' ź", "s' ś", "c' ć", "n' ń",
            "A' Á", "E' É", "I' Í", "O' Ó", "U' Ú", "Y' Ý", "Z' Ź", "S' Ś", "C' Ć", "N' Ń",
            "a` à", "e` è", "i` ì", "o` ò", "u` ù", "A` À", "E` È", "I` Ì", "O` Ò", "U` Ù",
            "a^ â", "e^ ê", "i^ î", "o^ ô", "u^ û", "w^ ŵ", "y^ ŷ",
            "A^ Â", "E^ Ê", "I^ Î", "O^ Ô", "U^ Û", "W^ Ŵ", "Y^ Ŷ",
            "a: ä", "e: ë", "i: ï", "o: ö", "u: ü", "y: ÿ",
            "A: Ä", "E: Ë", "I: Ï", "O: Ö", "U: Ü", "Y: Ÿ",
            "u\" ű", "o\" ő", "U\" Ű", "O\" Ő",
            "z. ż", "Z. Ż", "th þ", "Th Þ", "ao å", "Ao Å",
            "l/ ł", "d/ đ", "o/ ø", "L/ Ł", "D/ Đ", "O/ Ø",
            "mu µ", "l- £", "pp ¶", "so §", "|- †", "|= ‡",
            "ss ß", "Ss ẞ", "o_ º", "a_ ª", "oo °",
            "!! ¡", "?? ¿", ".- ·", ".= •", ".> ›", ".< ‹",
            "'1 ′", "'2 ″", "[[ ⟦", "]] ⟧",
            "+- ±", "-: ÷", "<= ≤", ">= ≥", "=/ ≠", "-, ¬", "~~ ≈",
            "<< «", ">> »",
            "12 ½", "13 ⅓", "23 ⅔", "14 ¼", "34 ¾", "15 ⅕", "25 ⅖", "35 ⅗", "45 ⅘",
            "16 ⅙", "56 ⅚", "18 ⅛", "38 ⅜", "58 ⅝", "78 ⅞",
            "#f ♭", "#n ♮", "#s ♯", "%o ‰",
            "e= €", "or ®", "oc ©", "op ℗", "tm ™"
        };
        for (unsigned i = 0; i < sizeof(lines) / sizeof(*lines); ++i) {
            const QString s = QString::fromUtf8(lines[i]);
            const int sp = s.indexOf(QLatin1Char(' '));
            if (sp > 0) m.insert(s.left(sp), s.mid(sp + 1));
        }
    }
    return m;
}

// Клавиша с векторной иконкой вместо длинной подписи. Фон/рамку рисует QSS,
// иконку — QPainter в том же цвете, что и текст (жёсткая инверсия при pressed/locked).
class KeyButton : public QPushButton {
public:
    explicit KeyButton(QWidget *parent = nullptr) : QPushButton(parent) {}
protected:
    void paintEvent(QPaintEvent *e) override {
        QPushButton::paintEvent(e);
        const QString role = property("role").toString();
        if (!isIconRole(role)) return;

        const bool inverted = isDown() || property("locked").toBool();
        const QColor ink = inverted ? QColor(Qt::white) : QColor(Qt::black);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(ink);
        pen.setWidth(2);
        pen.setJoinStyle(Qt::MiterJoin);
        pen.setCapStyle(Qt::SquareCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        const qreal w = qreal(width());
        const qreal h = qreal(height());
        const qreal cx = w / 2.0;
        const qreal cy = h / 2.0;

        if (role == "Cls") {
            const qreal m = qMin(w, h) * 0.28;
            p.drawLine(QPointF(m, m), QPointF(w - m, h - m));
            p.drawLine(QPointF(w - m, m), QPointF(m, h - m));
            return;
        }

        if (role == "MB" || role == "MF") {
            const bool left = (role == "MB");
            const qreal xa = w * 0.20;
            const qreal xb = w * 0.80;
            const qreal tipX = left ? xa : xb;
            const qreal tailX = left ? xb : xa;
            p.drawLine(QPointF(tailX, cy), QPointF(tipX, cy));
            const qreal wing = w * 0.15;
            const qreal dy = h * 0.17;
            const qreal bx = left ? (tipX + wing) : (tipX - wing);
            p.drawLine(QPointF(tipX, cy), QPointF(bx, cy - dy));
            p.drawLine(QPointF(tipX, cy), QPointF(bx, cy + dy));
            return;
        }

        if (role == "DB" || role == "DF") {
            const bool back = (role == "DB");
            QPolygonF poly;
            if (back) {
                poly << QPointF(w * 0.80, h * 0.24)
                     << QPointF(w * 0.42, h * 0.24)
                     << QPointF(w * 0.16, h * 0.50)
                     << QPointF(w * 0.42, h * 0.76)
                     << QPointF(w * 0.80, h * 0.76);
            } else {
                poly << QPointF(w * 0.20, h * 0.24)
                     << QPointF(w * 0.58, h * 0.24)
                     << QPointF(w * 0.84, h * 0.50)
                     << QPointF(w * 0.58, h * 0.76)
                     << QPointF(w * 0.20, h * 0.76);
            }
            p.drawPolygon(poly);
            const qreal xx = back ? w * 0.62 : w * 0.38;
            const qreal r = qMin(w * 0.085, h * 0.12);
            p.drawLine(QPointF(xx - r, cy - r), QPointF(xx + r, cy + r));
            p.drawLine(QPointF(xx + r, cy - r), QPointF(xx - r, cy + r));
            return;
        }

        if (role == "Sft") {
            QPolygonF arrow;
            arrow << QPointF(cx, h * 0.16)
                  << QPointF(w * 0.76, h * 0.48)
                  << QPointF(w * 0.62, h * 0.48)
                  << QPointF(w * 0.62, h * 0.80)
                  << QPointF(w * 0.38, h * 0.80)
                  << QPointF(w * 0.38, h * 0.48)
                  << QPointF(w * 0.24, h * 0.48);
            if (property("locked").toBool()) {
                p.setBrush(QBrush(ink));
                p.drawPolygon(arrow);
                p.setBrush(Qt::NoBrush);
            } else {
                p.drawPolygon(arrow);
            }
            return;
        }

        if (role == "Ret") {
            p.drawLine(QPointF(w * 0.78, h * 0.28), QPointF(w * 0.78, h * 0.62));
            p.drawLine(QPointF(w * 0.78, h * 0.62), QPointF(w * 0.24, h * 0.62));
            p.drawLine(QPointF(w * 0.24, h * 0.62), QPointF(w * 0.38, h * 0.48));
            p.drawLine(QPointF(w * 0.24, h * 0.62), QPointF(w * 0.38, h * 0.76));
        }
    }
};

// Строка запроса: свой виджет, курсор виден, тап ставит курсор в позицию.
class QueryField : public QWidget {
public:
    QueryField(QWidget *parent, const QFont &f) : QWidget(parent), m_f(f), m_cursor(0) {
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(u8("background-color:#FFFFFF;border:1px solid #000000;border-radius:0px;"));
        setMinimumHeight(0);
    }
    void setQueryFont(const QFont &f) { m_f = f; update(); }
    void setText(const QString &t) { m_text = t; m_cursor = qBound(0, m_cursor, m_text.length()); update(); }
    void setCursor(int c) { m_cursor = qBound(0, c, m_text.length()); update(); }
    QString text() const { return m_text; }
    int cursorAt(const QPoint &p) const {
        const int start = visibleStart(); const QFontMetrics fm(m_f);
        int x = 12;
        for (int i = start; i < m_text.length(); ++i) {
            const int w = fm.width(QString(m_text.at(i)));
            if (x + w / 2 > p.x()) return i;
            x += w;
        }
        return m_text.length();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setFont(m_f); p.setPen(Qt::black);
        const int start = visibleStart();
        const QString shown = m_text.mid(start);
        p.drawText(rect().adjusted(12, 0, -12, 0), Qt::AlignLeft | Qt::AlignVCenter, shown);
        const int cx = 12 + QFontMetrics(m_f).width(shown.left(qMax(0, m_cursor - start)));
        const int cm = qMax(4, qRound(height() * 0.18));
        p.drawLine(cx, cm, cx, height() - cm);
        p.drawLine(cx + 1, cm, cx + 1, height() - cm);
    }
private:
    int visibleStart() const {
        const QFontMetrics fm(m_f); const int avail = qMax(width() - 24, 60);
        int s = 0; while (s < m_text.length() && fm.width(m_text.mid(s)) > avail) ++s;
        return s;
    }
    QFont m_f; QString m_text; int m_cursor;
};

// ---- список результатов (тот же вид, что в отдельном плагине ksearch) ----

// Строка результата: имя целиком в 1–3+ строки, отступы сверху/снизу PAD_V,
// снизу чёрная разделительная линия. Только #FFFFFF / #000000, без выключки.
class ResultDelegate : public QStyledItemDelegate {
public:
    ResultDelegate(QWidget *view, const QFont &f) : QStyledItemDelegate(view), m_view(view), m_f(f) {}
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &idx) const override {
        const qreal h = layoutText(idx.data().toString(), m_f, nameWidth());
        return QSize(m_view->width(), int(h) + 2 * PAD_V + 1);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &idx) const override {
        p->save();
        p->fillRect(o.rect, QColor(0xFF, 0xFF, 0xFF));
        p->setPen(QColor(0x00, 0x00, 0x00));
        layoutText(idx.data().toString(), m_f, nameWidth(), p,
                   QPointF(o.rect.left() + PAD_H, o.rect.top() + PAD_V));
        p->drawLine(o.rect.bottomLeft(), o.rect.bottomRight());
        p->restore();
    }
private:
    enum { PAD_V = 10, PAD_H = 6 };
    qreal nameWidth() const { return qMax(100, m_view->width() - 3 * PAD_H); }
    QWidget *m_view; QFont m_f;
};

// Надпись, которую рисуем по символам: набор всей строки некоторыми гарнитурами добавляет
// разрядку, которой нет ни в letterSpacing, ни в ширинах отдельных букв.
// Посимвольный вывод даёт гарантированно плотный текст.
class PlainLabel : public QLabel {
public:
    explicit PlainLabel(const QString &t, QWidget *parent = nullptr) : QLabel(t, parent) {}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        const QFont f = font();
        p.setFont(f);
        const QFontMetrics fm(f);
        const QString t = text();
        const int y = (height() - fm.height()) / 2 + fm.ascent();
        int x = 0;
        for (int i = 0; i < t.size(); ++i) {
            const QString ch(t.at(i));
            p.drawText(x, y, ch);
            x += fm.width(ch);
        }
    }
};

// Гарнитура с кириллицей и наименьшей шириной фразы: «Sans Serif» на Clara берёт
// запасной шрифт с широкими глифами (wStr=sum=233 px при 25 px), отчего и выглядит
// как разрядка. Пробуем шрифты Kobalt и системные кириллические, выбираем самый узкий.
static QString tightTitleFamily(int px, const QString &text) {
    QStringList cands;
    const QString plex = u8("/mnt/onboard/.adds/kobalt/assets/fonts/IBMPlexSans-Regular.ttf");
    if (QFile::exists(plex)) {
        const int id = QFontDatabase::addApplicationFont(plex);
        cands << QFontDatabase::applicationFontFamilies(id);
    }
    cands << u8("Kobo Nickel") << u8("Avenir Next") << u8("Georgia") << u8("Amasis")
          << u8("Rakuten Sans") << u8("DejaVu Sans") << u8("Noto Sans");
    QFontDatabase db;
    const QStringList fams = db.families();
    QSet<QString> seen;
    QString best;
    int bestW = 0;
    QString report;
    for (int i = 0; i < cands.size(); ++i) {
        const QString fam = cands.at(i);
        if (fam.isEmpty() || seen.contains(fam)) continue;
        seen.insert(fam);
        if (!fams.contains(fam, Qt::CaseInsensitive)) continue;
        if (!db.writingSystems(fam).contains(QFontDatabase::Cyrillic)) continue;
        QFont f(fam);
        f.setPixelSize(px);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0);
        f.setWordSpacing(0);
        f.setStretch(QFont::Unstretched);
        const int w = QFontMetrics(f).width(text);
        report += fam + u8("=") + QString::number(w) + u8(" ");
        if (best.isEmpty() || w < bestW) { best = fam; bestW = w; }
    }
    klog(u8("search title families: ") + (report.isEmpty() ? u8("нет кириллических кандидатов") : report));
    return best.isEmpty() ? u8("Sans Serif") : best;
}

// Шрифт списка: гарнитура из search_list_font (kobrowse.ini), иначе шрифт виджета; кегль SEARCH_LIST_PX.
// Трекинг, межсловный интервал и растяжение сброшены явно: буквы и слова не раздвигаются.
static QFont makeListFont(const QFont &base) {
    QFont f = SEARCH_LIST_FONT.isEmpty() ? base : QFont(SEARCH_LIST_FONT);
    f.setPixelSize(SEARCH_LIST_PX);
    f.setBold(false);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    f.setWordSpacing(0);
    f.setStretch(QFont::Unstretched);
    return f;
}

// Одинаковая настройка QTreeWidget для списков как в kobrowse.
static void setupResultList(QTreeWidget *list, const QFont &f) {
    list->setFont(f);
    list->setColumnCount(1);
    list->setHeaderHidden(true);
    list->setRootIsDecorated(false);
    list->setIndentation(0);
    list->setUniformRowHeights(false);                  // высота строки по содержимому
    list->setItemDelegate(new ResultDelegate(list, f));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setFocusPolicy(Qt::NoFocus);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);   // для перетаскивания пальцем
    list->setStyleSheet(u8("QTreeWidget{background-color:#FFFFFF;border:none;"
                           "border-top:1px solid #000000;}"));
}

// E-Ink: строгий монохром. Прямоугольные клавиши, рамка 1px, жёсткая инверсия
// при нажатии и при зафиксированном модификаторе — как у Plato.
static QString keyQss() {
    return QString::fromUtf8(
        "QPushButton{background-color:#FFFFFF;color:#000000;border:1px solid #000000;"
        "border-radius:0px;padding:0px;}"
        "QPushButton:pressed{background-color:#000000;color:#FFFFFF;}"
        "QPushButton[locked=\"true\"]{background-color:#000000;color:#FFFFFF;}"
    );
}

// Геометрия Plato: зазор между клавишами равен 0.06 от высоты клавиши.
static const double PADDING_RATIO = 0.06;
static double maxLayoutWidth(const KbLayout &L) {
    double m = 0;
    for (int r = 0; r < 4; ++r) {
        const QVector<KeyDef> &row = L.keys[r];
        double s = (row.size() + 1) * PADDING_RATIO;
        for (int i = 0; i < row.size(); ++i) s += row.at(i).stretch;
        m = qMax(m, s);
    }
    return m;
}

class SearchWindow;
static SearchWindow *g_search = nullptr;
static void attachSleepWindow(QWidget *window);

class SearchWindow : public QWidget {
#ifdef KOBROWSE_SETTINGS_TEST
    friend class SettingsUiTest;
#endif
public:
    explicit SearchWindow(QWidget *parent) : QWidget(parent), m_layout(0), m_shift(0), m_alt(0), m_combine(false) {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_AcceptTouchEvents);
        setAutoFillBackground(true);
        setStyleSheet(u8("QWidget{background-color:#FFFFFF;color:#000000;}"));
        const QStringList fams = QFontDatabase().families();
        const QString face = fams.contains(u8("Avenir"), Qt::CaseInsensitive)
                           ? u8("Avenir") : u8("Sans Serif");
        QFont f(tightTitleFamily(FONT_MSG, u8("Поиск книг")));
        f.setPixelSize(FONT_MSG);
        // Заголовок «Поиск книг»: убрать разрядку, межсловный зазор и растяжение,
        // которые задаёт гарнитура Avenir (в списке это уже делает makeListFont()).
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0);
        f.setWordSpacing(0);
        f.setStretch(QFont::Unstretched);
        f.setBold(false);
        const QFont fList = makeListFont(font());   // как в kobrowse: LIST_FONT / LIST_PX, без трекинга
        QFont fKey(face);   fKey.setPixelSize(qMax(8, KEY_FONT_PX));   fKey.setBold(false);
        QFont fField(face); fField.setPixelSize(KEY_PX);
        QFont fFunc(face);  fFunc.setPixelSize(qMax(8, FUNC_PX));      fFunc.setBold(false);

        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->setContentsMargins(12, 10, 12, 12);
        lay->setSpacing(8);

        m_title = new PlainLabel(u8("Поиск книг"), this);
        m_title->setFont(f); lay->addWidget(m_title);
        {
            // Замер: если wStr заметно больше sum, разрядку даёт шейпинг строки,
            // а не шрифт — тогда посимвольная отрисовка PlainLabel всё равно убирает её.
            const QFontMetrics fm(f);
            const QString t = u8("Поиск книг");
            int sum = 0;
            for (int i = 0; i < t.size(); ++i) sum += fm.width(QString(t.at(i)));
            klog(u8("search title: px=%1 family=%2 wStr=%3 sum=%4")
                     .arg(f.pixelSize()).arg(f.family()).arg(fm.width(t)).arg(sum));
        }

        // Верхняя панель: поле запроса + компактная кнопка закрытия
        QHBoxLayout *top = new QHBoxLayout();
        top->setSpacing(8);
        m_field = new QueryField(this, fField);
        top->addWidget(m_field, 1);
        // Public Qt input method first; if the platform does not expose one,
        // keep the existing keyboard and QueryField as the fallback.
        m_nativeField = new QLineEdit(this);
        m_nativeField->setObjectName(u8("systemInputField"));
        m_nativeField->setFont(fField); m_nativeField->hide();
        top->addWidget(m_nativeField, 1);
        connect(m_nativeField, &QLineEdit::textChanged, [this](const QString &text) {
            m_field->setText(text); setCursorPos(m_nativeField->cursorPosition());
        });
        connect(m_nativeField, &QLineEdit::cursorPositionChanged, [this](int, int pos) { setCursorPos(pos); });
        connect(m_nativeField, &QLineEdit::returnPressed, [this]() { submitInput(); });
        m_closeBtn = new KeyButton(this);
        m_closeBtn->setText(QString());              // глиф ✕ отсутствует в шрифте Kobo
        m_closeBtn->setProperty("role", "Cls");      // крестик рисуется через QPainter
        m_closeBtn->setFont(fFunc);
        m_closeBtn->setFixedSize(40, 40);
        m_closeBtn->setFocusPolicy(Qt::NoFocus);
        m_closeBtn->setStyleSheet(keyQss());
        connect(m_closeBtn, &QPushButton::clicked, [this]() { close(); });
        top->addWidget(m_closeBtn);
        lay->addLayout(top);

        // Клавиатура в стиле Plato: 4 ряда, геометрия считается от ширины окна.
        m_fKey = fKey; m_fFunc = fFunc;
        m_kbBox = new QWidget(this);
        m_kbLay = new QVBoxLayout(m_kbBox);
        m_kbLay->setContentsMargins(0, 0, 0, 0);
        m_kbLay->setSpacing(6);
        buildKeyboard();
        // Добавляется ниже результатов и сообщений — у нижней границы экрана.

        // Меню раскладок открывается долгим нажатием на Space.
        m_menu = new QFrame(this);
        m_menu->setFrameShape(QFrame::NoFrame);
        m_menu->setStyleSheet(u8("QFrame{background-color:#FFFFFF;border:1px solid #000000;}"));
        QVBoxLayout *mv = new QVBoxLayout(m_menu);
        mv->setContentsMargins(4, 4, 4, 4);
        mv->setSpacing(4);
        for (int i = 0; i < KB.size(); ++i) {
            QPushButton *b = new QPushButton(KB[i].name, m_menu);
            b->setFont(m_fFunc);
            b->setFocusPolicy(Qt::NoFocus);
            b->setStyleSheet(keyQss());
            connect(b, &QPushButton::clicked, this, [this, i]() { setLayoutIndex(i); });
            mv->addWidget(b);
        }
        QPushButton *bCloseMenu = new QPushButton(u8("Закрыть"), m_menu);
        bCloseMenu->setFont(m_fFunc);
        bCloseMenu->setFocusPolicy(Qt::NoFocus);
        bCloseMenu->setStyleSheet(keyQss());
        connect(bCloseMenu, &QPushButton::clicked, this, [this]() { m_menu->hide(); });
        mv->addWidget(bCloseMenu);
        m_menu->hide();

        m_longTimer.setSingleShot(true);
        m_longTimer.setInterval(LONG_TAP_MS);
        connect(&m_longTimer, &QTimer::timeout, this, [this]() {
            if (m_longBtn) { m_longFired = true; onLongPress(m_longBtn); }
        });

        m_list = new QTreeWidget(this);
        setupResultList(m_list, fList);
        lay->addWidget(m_list, 1);

        m_msg = new FooterNotice(this); m_msg->setFont(f);
        m_msg->setFixedHeight(QFontMetrics(f).lineSpacing() * 2 + 4);
        lay->addWidget(m_msg);
        lay->addWidget(m_kbBox);
        m_nativeSpace = new QWidget(this); m_nativeSpace->setFixedHeight(0); m_nativeSpace->hide();
        lay->addWidget(m_nativeSpace);
        m_inputTimer.setSingleShot(true); m_inputTimer.setInterval(800);
        connect(&m_inputTimer, &QTimer::timeout, [this]() { finishInputProbe(); });
        if (QGuiApplication::inputMethod())
            connect(QGuiApplication::inputMethod(), &QInputMethod::visibleChanged, this, [this]() { updateSystemInput(); });
        m_msgTimer.setSingleShot(true); m_msgTimer.setInterval(5000);
        connect(&m_msgTimer, &QTimer::timeout, this, [this]() { m_msg->setMessage(QString()); });

        qApp->installEventFilter(this);
    }
    void startRename(const QString &name, const std::function<QString(const QString &)> &submit, const QString &title) {
        m_rename = submit; m_title->setText(title);
        m_field->setText(name); setCursorPos(name.size());
    }
    ~SearchWindow() { qApp->removeEventFilter(this); if (g_search == this) g_search = nullptr; }

protected:
    void showEvent(QShowEvent *e) override {
        QWidget::showEvent(e);
        const int w = qMax(240, this->width() - 24);
        if (m_kbBox && w != m_lastKbWidth) buildKeyboard();
        beginInputProbe();
    }
    void hideEvent(QHideEvent *e) override {
        m_inputTimer.stop();
        if ((m_inputPending || m_systemInput) && QGuiApplication::inputMethod()) QGuiApplication::inputMethod()->hide();
        m_inputPending = false; m_systemInput = false;
        QWidget::hideEvent(e);
    }
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        const int w = qMax(240, this->width() - 24);
        if (m_kbBox && w != m_lastKbWidth) buildKeyboard();
    }
    bool event(QEvent *e) override { if (handleTouch(e)) return true; return QWidget::event(e); }
    bool eventFilter(QObject *obj, QEvent *e) override {
        const QEvent::Type t = e->type();
        if (!(t == QEvent::TouchBegin || t == QEvent::TouchUpdate || t == QEvent::TouchEnd
              || t == QEvent::TouchCancel) || obj == this || !obj->isWidgetType()) return false;
        QWidget *w = static_cast<QWidget *>(obj);
        if ((m_inputPending || m_systemInput) && (w == m_nativeField || m_nativeField->isAncestorOf(w))) return false;
        if (isAncestorOf(w)) return handleTouch(e);
        if (w == window() || w->isAncestorOf(this)) {
            const QList<QTouchEvent::TouchPoint> pts = static_cast<QTouchEvent *>(e)->touchPoints();
            if (!pts.isEmpty() && rect().contains(mapFromGlobal(pts.first().screenPos().toPoint())))
                return handleTouch(e);
        }
        return false;
    }

private:
    void beginInputProbe() {
        if (!QGuiApplication::inputMethod() || m_inputPending || m_systemInput) return;
        m_inputPending = true;
        m_nativeField->setText(m_field->text()); m_nativeField->setCursorPosition(m_cur);
        m_field->hide(); m_nativeField->show(); m_nativeField->setFocus(Qt::OtherFocusReason);
        m_inputTimer.start(); QGuiApplication::inputMethod()->show();
        updateSystemInput();
    }
    void updateSystemInput() {
        QInputMethod *im = QGuiApplication::inputMethod();
        if (!im || (!m_inputPending && !m_systemInput)) return;
        if (im->isVisible() && isVisible()) {
            m_inputTimer.stop(); m_inputPending = false; m_systemInput = true;
            m_field->hide(); m_nativeField->show(); m_kbBox->hide();
            const int keyboardHeight = qBound(0, qRound(im->keyboardRectangle().height()), height() / 2);
            m_nativeSpace->setFixedHeight(keyboardHeight); m_nativeSpace->setVisible(keyboardHeight > 0);
        } else if (m_systemInput) {
            m_systemInput = false; useFallbackInput();
        }
    }
    void finishInputProbe() {
        if (!m_inputPending) return;
        if (QGuiApplication::inputMethod() && QGuiApplication::inputMethod()->isVisible()) updateSystemInput();
        else { m_inputPending = false; useFallbackInput(); }
    }
    void useFallbackInput() {
        m_field->setText(m_nativeField->text()); setCursorPos(m_nativeField->cursorPosition());
        m_nativeField->hide(); m_nativeSpace->hide(); m_field->show(); m_kbBox->show();
    }
    void submitInput() {
        releaseCombine();
        if (m_rename) { const QString error = m_rename(m_field->text()); if (error.isEmpty()) close(); else showMsg(error); }
        else doSearch();
    }
    void insertText(const QString &s) {
        if (s.isEmpty()) return;
        const int c = cursorPos();
        m_field->setText(m_field->text().left(c) + s + m_field->text().mid(c));
        setCursorPos(c + s.length());
        klog(u8("ksearch key: ") + s);
    }
    void insertChar(QChar ch) { insertText(QString(ch)); }
    void backspace() {
        const int c = cursorPos();
        if (c <= 0) return;
        const QString t = m_field->text();
        m_field->setText(t.left(c - 1) + t.mid(c)); setCursorPos(c - 1);
    }
    void deleteForward() {
        const int c = cursorPos(); const QString t = m_field->text();
        if (c >= t.length()) return;
        m_field->setText(t.left(c) + t.mid(c + 1));
    }
    void moveCursor(int d) { setCursorPos(cursorPos() + d); }
    int cursorPos() const { return m_cur; }
    void setCursorPos(int c) {
        m_cur = qBound(0, c, m_field->text().length());
        m_field->setCursor(m_cur);
    }
    static int wordStart(const QString &t, int pos) {
        int i = pos;
        while (i > 0 && t.at(i - 1).isSpace()) --i;
        while (i > 0 && !t.at(i - 1).isSpace()) --i;
        return i;
    }
    static int wordEnd(const QString &t, int pos) {
        const int n = t.length(); int i = pos;
        while (i < n && t.at(i).isSpace()) ++i;
        while (i < n && !t.at(i).isSpace()) ++i;
        return i;
    }
    void deleteWord(int dir) {
        const QString t = m_field->text(); const int c = cursorPos();
        if (dir < 0) {
            const int s = wordStart(t, c);
            if (s < c) { m_field->setText(t.left(s) + t.mid(c)); setCursorPos(s); }
        } else {
            const int e = wordEnd(t, c);
            if (e > c) m_field->setText(t.left(c) + t.mid(e));
        }
    }
    void moveWord(int dir) {
        const QString t = m_field->text();
        setCursorPos(dir < 0 ? wordStart(t, cursorPos()) : wordEnd(t, cursorPos()));
    }
    int level() const { return (m_shift > 0 ? 1 : 0) + (m_alt > 0 ? 2 : 0); }
    void releaseModifiers() {
        bool ch = false;
        if (m_shift == 1) { m_shift = 0; ch = true; }
        if (m_alt == 1) { m_alt = 0; ch = true; }
        if (ch) updateKeys();
    }
    void onOutputChar(const QString &s) {
        if (s.length() != 1) return;
        const QChar ch = s.at(0);
        if (m_combine) {
            m_combineBuf += ch;
            if (m_combineBuf.length() > 1) {
                const QHash<QString, QString> &cm = combinations();
                if (cm.contains(m_combineBuf)) insertText(cm.value(m_combineBuf));
                m_combineBuf.clear(); m_combine = false; updateKeys();
            }
        } else {
            insertChar(ch);
        }
        if (ch != QLatin1Char(' ')) releaseModifiers();
    }
    void releaseCombine() { m_combine = false; m_combineBuf.clear(); updateKeys(); }
    void onKeyClick(QPushButton *b) {
        const QString role = b->property("role").toString();
        if (role == "out") { onOutputChar(b->text()); return; }
        if (role == "Sft") { m_shift = (m_shift + 1) % 3; updateKeys(); return; }
        if (role == "Alt") { m_alt = (m_alt + 1) % 3; updateKeys(); return; }
        if (role == "Cmb") { m_combine = !m_combine; m_combineBuf.clear(); updateKeys(); return; }
        if (role == "Spc") { insertChar(QLatin1Char(' ')); return; }
        if (role == "Ret") {
            submitInput(); return;
        }
        if (role == "DB") { backspace(); return; }
        if (role == "DF") { deleteForward(); return; }
        if (role == "MB") { moveCursor(-1); return; }
        if (role == "MF") { moveCursor(1); }
    }
    void onLongPress(QPushButton *b) {
        const QString role = b->property("role").toString();
        if (role == "Spc") { showLayoutMenu(); return; }
        if (role == "DB") { deleteWord(-1); return; }
        if (role == "DF") { deleteWord(1); return; }
        if (role == "MB") { moveWord(-1); return; }
        if (role == "MF") { moveWord(1); }
    }
    QPushButton *buttonAt(const QPoint &p) const {
        for (QWidget *cur = childAt(p); cur && cur != this; cur = cur->parentWidget())
            if (QPushButton *b = qobject_cast<QPushButton *>(cur)) return b;
        return nullptr;
    }

    void buildKeyboard() {
        // Удаляем старые ряды и кнопки, чтобы не оставалось дублей.
        m_longTimer.stop(); m_longBtn = nullptr; m_longFired = false;
        while (m_kbLay->count() > 0) { QLayoutItem *it = m_kbLay->takeAt(0); delete it; }
        for (int i = 0; i < m_btns.size(); ++i) delete m_btns.at(i);
        m_btns.clear();

        const KbLayout &L = KB[m_layout];
        const int availW = qMax(240, this->width() - 24);
        const double unit = double(availW) / maxLayoutWidth(L);
        const int kh = qMax(24, qRound(unit));
        const int pad = qMax(2, qRound(unit * PADDING_RATIO));
        m_lastKbWidth = availW;
        m_kh = kh;

        // Шрифты: от высоты клавиши, если в kobrowse.ini явно не задан кегль.
        const int symPx = KEY_FONT_SET ? KEY_FONT_PX : qMax(10, qRound(kh * 0.52));
        const int funcPx = FUNC_FONT_SET ? FUNC_PX : qMax(8, qRound(kh * 0.34));
        m_fKey.setPixelSize(symPx);   m_fKey.setBold(false);
        m_fFunc.setPixelSize(funcPx); m_fFunc.setBold(false);

        // Верхняя панель: заголовок, поле ввода и кнопка закрытия одной высоты с клавишами.
        if (m_title) {
            QFont tf = m_title->font();
            tf.setPixelSize(qMax(8, qRound(kh * 0.45)));
            tf.setBold(false);
            m_title->setFont(tf);
        }
        if (m_field) {
            QFont ff = m_field->font();
            ff.setPixelSize(qMax(8, qRound(kh * 0.45)));
            ff.setBold(false);
            m_field->setFont(ff);
            m_field->setQueryFont(ff);
            m_field->setFixedHeight(kh);
            if (m_nativeField) { m_nativeField->setFont(ff); m_nativeField->setFixedHeight(kh); }
        }
        if (m_closeBtn) {
            QFont cf = m_closeBtn->font();
            cf.setPixelSize(funcPx);
            cf.setBold(false);
            m_closeBtn->setFont(cf);
            m_closeBtn->setFixedSize(kh, kh);
        }
        if (m_menu) {
            const QList<QPushButton *> mbs = m_menu->findChildren<QPushButton *>();
            for (int i = 0; i < mbs.size(); ++i) {
                QFont mf = mbs.at(i)->font();
                mf.setPixelSize(funcPx);
                mf.setBold(false);
                mbs.at(i)->setFont(mf);
            }
        }

        m_kbLay->setSpacing(pad);
        for (int r = 0; r < 4; ++r) {
            const QVector<KeyDef> &defs = L.keys[r];
            QVector<int> widths;
            int sumW = 0;
            for (int i = 0; i < defs.size(); ++i) {
                const int wi = qMax(8, qRound(defs.at(i).stretch * unit));
                widths << wi;
                sumW += wi;
            }
            // Центрируем ряд в availW: отступ = зазор + свободное место пополам.
            const int used = sumW + (defs.size() + 1) * pad;
            const int side = pad + qMax(0, (availW - used) / 2);

            QHBoxLayout *row = new QHBoxLayout();
            row->setContentsMargins(side, 0, side, 0);
            row->setSpacing(pad);
            int outIdx = 0;
            for (int i = 0; i < defs.size(); ++i) {
                const KeyDef &d = defs.at(i);
                QPushButton *b;
                if (isIconRole(d.role)) {
                    KeyButton *ib = new KeyButton(m_kbBox);
                    ib->setText(QString());
                    b = ib;
                } else {
                    b = new QPushButton(m_kbBox);
                }
                b->setFont(isIconRole(d.role) ? m_fFunc : (d.role == "out" ? m_fKey : m_fFunc));
                b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
                b->setFixedSize(widths.at(i), kh);
                b->setFocusPolicy(Qt::NoFocus);
                b->setStyleSheet(keyQss());
                b->setProperty("role", d.role);
                b->setProperty("row", r);
                b->setProperty("out", outIdx);
                if (d.role == "out") ++outIdx;
                connect(b, &QPushButton::clicked, this, [this, b]() { onKeyClick(b); });
                row->addWidget(b);
                m_btns << b;
            }
            m_kbLay->addLayout(row);
        }
        updateKeys();
    }

    void updateKeys() {
        const KbLayout &L = KB[m_layout];
        const int lv = level();
        for (int i = 0; i < m_btns.size(); ++i) {
            QPushButton *b = m_btns.at(i);
            const QString role = b->property("role").toString();
            if (role == "out") {
                const int r = b->property("row").toInt();
                const int idx = b->property("out").toInt();
                const QString s = L.outputs[lv][r];
                b->setText(idx < s.length() ? QString(s.at(idx)) : QString());
            } else if (role == "Spc" || isIconRole(role)) {
                b->setText(QString());
            } else {
                b->setText(roleLabel(role));
            }
            const bool locked = (role == "Sft" && m_shift == 2) ||
                                (role == "Alt" && m_alt == 2) ||
                                (role == "Cmb" && m_combine);
            b->setProperty("locked", locked);
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
    }
    void setLayoutIndex(int i) {
        m_layout = qBound(0, i, KB.size() - 1);
        m_shift = 0; m_alt = 0; m_combine = false; m_combineBuf.clear();
        m_menu->hide();
        buildKeyboard();
    }
    void showLayoutMenu() {
        const int h = 3 * (m_kh + 4) + 8;
        const int w = qMin(width() - 40, 700);
        int y = m_kbBox->mapTo(this, QPoint(0, 0)).y() - h - 8;
        if (y < 8) y = 8;
        m_menu->setGeometry((width() - w) / 2, y, w, h);
        m_menu->raise();
        m_menu->show();
    }
    void showMsg(const QString &s) { m_msg->setMessage(s); m_msgTimer.start(); }

    static bool isBook(const QString &path) {
        const QString s = path.toLower();
        const char *exts[] = { ".epub", ".kepub.epub", ".pdf", ".fb2", ".fb2.zip", ".mobi",
                               ".azw3", ".txt", ".djvu", ".cbz", ".cbr" };
        for (int i = 0; i < 11; ++i) if (s.endsWith(u8(exts[i]))) return true;
        return false;
    }
    void doSearch() {
        const QString q = m_field->text().trimmed();
        if (q.isEmpty()) { showMsg(u8("Введите часть названия")); return; }
        const QStringList tokens = q.split(QRegExp(u8("\\s+")), QString::SkipEmptyParts);
        m_list->clear(); m_paths.clear(); m_title->setText(u8("Результаты поиска: ") + q);
        QDirIterator it(u8(KOBROWSE_ROOT), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.contains(u8("/.")) || !isBook(path)) continue;
            const QString name = QFileInfo(path).fileName();
            bool ok = true;
            for (const QString &tok : tokens)
                if (!name.contains(tok, Qt::CaseInsensitive)) { ok = false; break; }
            if (!ok) continue;
            m_paths << path;
            QTreeWidgetItem *item = new QTreeWidgetItem(m_list);
            item->setText(0, name);
            if (m_paths.size() >= 300) break;
        }
        if (m_paths.isEmpty()) showMsg(u8("Ничего не найдено"));
        else showMsg(u8("Найдено: %1").arg(m_paths.size()));
        klog(u8("ksearch search '") + q + u8("': ") + QString::number(m_paths.size()));
    }
    void openResult(QTreeWidgetItem *it) {
        const int row = m_list->indexOfTopLevelItem(it);
        if (row < 0 || row >= m_paths.size()) return;
        if (!safeLocalPath(u8(KOBROWSE_ROOT), m_paths.at(row))) { showMsg(u8("Ссылка недоступна")); return; }
        const QString denial = bookOpenDenial();
        if (!denial.isEmpty()) { showMsg(denial); return; }
        hide();
        QString err;
        if (openBook(m_paths.at(row), true, err)) close();
        else { show(); raise(); showMsg(err); }
    }

    bool handleTouch(QEvent *e) {
        const QEvent::Type t = e->type();
        if (t != QEvent::TouchBegin && t != QEvent::TouchUpdate && t != QEvent::TouchEnd
            && t != QEvent::TouchCancel) return false;
        const QList<QTouchEvent::TouchPoint> pts = static_cast<QTouchEvent *>(e)->touchPoints();
        const QPoint p = pts.isEmpty() ? QPoint() : mapFromGlobal(pts.first().screenPos().toPoint());
        switch (t) {
        case QEvent::TouchBegin: {
            m_press.restart(); m_pressPos = p; m_moved = false; m_twoFinger = false;
            m_longBtn = buttonAt(p); m_longFired = false; m_longTimer.stop();
            if (m_longBtn && hasLongAction(m_longBtn->property("role").toString())) m_longTimer.start();
            else m_longBtn = nullptr;
            break;
        }
        case QEvent::TouchUpdate:
            if (pts.size() >= 2) {
                // Два пальца — перетаскивание списка по среднему положению (как в kobrowse)
                m_longTimer.stop(); m_longBtn = nullptr;
                const qreal y = (mapFromGlobal(pts.at(0).screenPos().toPoint()).y()
                                 + mapFromGlobal(pts.at(1).screenPos().toPoint()).y()) / 2.0;
                if (!m_twoFinger) {
                    m_twoFinger = true;
                    m_twoStartY = y;
                    m_scrollStart = m_list->verticalScrollBar()->value();
                } else if (inList(m_pressPos)) {
                    m_list->verticalScrollBar()->setValue(m_scrollStart - int(y - m_twoStartY));
                }
            } else if (!pts.isEmpty() && (p - m_pressPos).manhattanLength() > SWIPE_PX) {
                m_moved = true; m_longTimer.stop(); m_longBtn = nullptr;
            }
            break;
        case QEvent::TouchEnd:
            m_longTimer.stop();
            if (!pts.isEmpty() && !m_moved && !m_twoFinger && !m_longFired) tapAt(p);
            m_longBtn = nullptr; m_longFired = false; m_twoFinger = false;
            break;
        default:
            m_longTimer.stop(); m_longBtn = nullptr; m_longFired = false; m_twoFinger = false;
            break;
        }
        e->accept(); return true;
    }
    bool inList(const QPoint &p) const {
        QWidget *w = childAt(p);
        return w && (w == m_list->viewport() || m_list->viewport()->isAncestorOf(w));
    }
    void tapAt(const QPoint &p) {
        if (m_menu && m_menu->isVisible() && !m_menu->geometry().contains(p)) m_menu->hide();
        const QPoint fp = m_field->mapFrom(this, p);
        if (m_field->rect().contains(fp)) { setCursorPos(m_field->cursorAt(fp)); return; }
        if (m_list->isVisible()) {
            const QPoint vp = m_list->viewport()->mapFrom(this, p);
            if (m_list->viewport()->rect().contains(vp)) {
                QTreeWidgetItem *it = m_list->itemAt(vp);
                if (it) openResult(it);
                return;
            }
        }
        QWidget *w = childAt(p);
        for (QWidget *cur = w; cur && cur != this; cur = cur->parentWidget())
            if (QPushButton *b = qobject_cast<QPushButton *>(cur)) {
                if (b->isEnabled() && b->isVisible()) b->click();
                return;
            }
    }

    QLabel *m_title;
    FooterNotice *m_msg;
    QueryField *m_field;
    QLineEdit *m_nativeField = nullptr;
    QWidget *m_nativeSpace = nullptr;
    QTimer m_inputTimer;
    bool m_inputPending = false, m_systemInput = false;
    QTreeWidget *m_list;
    QPushButton *m_closeBtn = nullptr;
    QWidget *m_kbBox = nullptr;
    QVBoxLayout *m_kbLay = nullptr;
    QFrame *m_menu = nullptr;
    QVector<QPushButton *> m_btns;
    QFont m_fKey, m_fFunc;
    QStringList m_paths;
    std::function<QString(const QString &)> m_rename;
    int m_layout, m_cur = 0;
    int m_shift, m_alt;
    bool m_combine;
    QString m_combineBuf;
    QPushButton *m_longBtn = nullptr;
    bool m_longFired = false;
    QTimer m_longTimer;
    int m_lastKbWidth = 0;
    int m_kh = 40;
    QElapsedTimer m_press;
    QPoint m_pressPos;
    bool m_moved = false;
    bool m_twoFinger = false;
    qreal m_twoStartY = 0;
    int m_scrollStart = 0;
    QTimer m_msgTimer;
};

static void ksearch_open() {
    if (g_search) { g_search->raise(); return; }
    loadConfig();
    QWidget *host = koboHostWindow();
    if (!host) { klog(u8("ksearch: no host")); return; }
    g_search = new SearchWindow(host);
    attachSleepWindow(g_search);
    g_search->setGeometry(host->rect());
    g_search->show(); g_search->raise();
    klog(u8("ksearch window shown"));
}

static void krename_open(const QString &name, const std::function<QString(const QString &)> &submit,
                         const QString &title = u8("Переименовать: нажмите Enter")) {
    if (g_search) return;
    QWidget *host = koboHostWindow();
    if (!host) return;
    g_search = new SearchWindow(host);
    attachSleepWindow(g_search);
    g_search->startRename(name, submit, title);
    g_search->setGeometry(host->rect());
    g_search->show(); g_search->raise();
}


static void kobrowse_open();

// Пункты экрана «Настройки»: ключ kobrowse.ini, подпись, тип, шаг, пределы, знаков после запятой
enum SetType { S_NUM, S_FONT, S_BOOL, S_OPEN };
struct SetItem { const char *key, *label; SetType type; double step, lo, hi; int dec; };
static const SetItem SET_ITEMS[] = {
    { "list_pt",       "Размер шрифта списка",    S_NUM,  0.5, 7,   14,   1 },
    { "list_font",     "Шрифт списка файлов",    S_FONT, 0,   0,   0,    0 },
    { "long_tap_ms",   "Долгое нажатие",         S_NUM,  50,  350, 700,  0 },
    { "allow_adds_edit", "Разрешить изменение .adds", S_BOOL, 0, 0, 1, 0 },
    { "trash_enabled", "Перемещать удалённое в корзину", S_BOOL, 0, 0, 1, 0 },
    { "logging_enabled", "Записывать журнал отладки", S_BOOL, 0, 0, 1, 0 },
    { "show_hidden",   "Показывать скрытые файлы", S_BOOL, 0, 0, 1, 0 },
    { "open_books",    "Открывать книги в читалке", S_OPEN, 0, 0, 0, 0 },
};
static const int SET_COUNT = int(sizeof(SET_ITEMS) / sizeof(SET_ITEMS[0]));
static bool isFontSizeSetting(const QString &key) { return key == u8("list_pt") || key == u8("long_tap_ms"); }
// Записать значения в kobrowse.ini: строка «ключ=…» заменяется, комментарии сохраняются
static bool saveConfig(const QStringList &keys, const QStringList &vals) {
    const QString ini = u8(KOBROWSE_INI);
    if (keys.size() != vals.size() || !safeLocalPath(u8(KOBROWSE_ROOT), ini, true)) return false;
    QString text;
    {
        QFile f(ini);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray bytes = f.readAll();
            text = QString::fromUtf8(bytes);
            if (f.error() != QFile::NoError || text.toUtf8() != bytes) return false;
        } else if (QFileInfo(ini).exists() || QFileInfo(ini).isSymLink()) return false;
    }
    QStringList lines = text.split(QLatin1Char('\n'));
    bool inSection = false, hasSection = false;
    int insertAt = -1;
    QSet<QString> found;
    for (int n = 0; n < lines.size(); ++n) {
        QString &line = lines[n];
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']'))) {
            if (inSection && insertAt < 0) insertAt = n;
            inSection = t == u8("[kobrowse]");
            if (inSection) hasSection = true;
            continue;
        }
        if (!inSection || t.startsWith(QLatin1Char(';')) || t.startsWith(QLatin1Char('#'))) continue;
        const int eq = t.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = t.left(eq).trimmed();
        const int index = keys.indexOf(key);
        if (index >= 0) { line = key + u8("=") + vals[index]; found.insert(key); }
    }
    if (!hasSection) { lines << u8("[kobrowse]"); insertAt = lines.size(); }
    if (insertAt < 0) insertAt = lines.size();
    for (int i = 0; i < keys.size(); ++i)
        if (!found.contains(keys[i])) lines.insert(insertAt++, keys[i] + u8("=") + vals[i]);
    QSaveFile f(ini);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QByteArray output = lines.join(u8("\n")).toUtf8();
    if (f.write(output) != output.size() || !f.commit()) return false;
    ::sync();
    return true;
}

// Inline choices share the same black/white selection feedback as Kobo controls.
class SettingChoiceButton : public QPushButton {
public:
    SettingChoiceButton(const QString &label, const QFont &face, bool preview, QWidget *parent)
        : QPushButton(label, parent), m_face(face), m_preview(preview) {
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        const int lines = preview ? 2 : 1;
        setMinimumHeight(qMax(48, QFontMetrics(face).lineSpacing() * lines + 20));
        setAccessibleName(label);
    }
    void setSelected(bool selected) { m_selected = selected; update(); }
    bool isSelected() const { return m_selected; }
    QFont previewFont() const { return m_face; }
    static QString sampleText() { return u8("Пример русского текста: Съешь ещё этих мягких булок."); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const bool selected = m_selected || isDown();
        p.fillRect(rect(), selected ? Qt::black : Qt::white);
        p.setPen(selected ? Qt::white : Qt::black);
        p.setFont(m_face);
        const QFontMetrics fm(m_face);
        if (m_preview) {
            const int left = qMax(8, BTN_MARGIN);
            const int available = qMax(1, width() - 2 * left);
            const int lineHeight = fm.lineSpacing();
            const int top = (height() - 2 * lineHeight) / 2;
            p.drawText(QRect(left, top, available, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(text(), Qt::ElideRight, available));
            p.drawText(QRect(left, top + lineHeight, available, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(sampleText(), Qt::ElideRight, available));
        } else {
            p.drawText(rect().adjusted(4, 0, -4, 0), Qt::AlignCenter,
                       fm.elidedText(text(), Qt::ElideRight, qMax(1, width() - 8)));
        }
        p.setPen(QPen(Qt::black, qMax(1.0, BTN_PEN)));
        p.drawRect(QRectF(rect()).adjusted(BTN_PEN/2, BTN_PEN/2, -BTN_PEN/2, -BTN_PEN/2));
    }
private:
    QFont m_face;
    bool m_preview, m_selected = false;
};

// ---------- окно менеджера ----------
// Без Q_OBJECT: только сигналы Qt + лямбды, moc не нужен
class KobrowseWindow : public QWidget {
#ifdef KOBROWSE_SETTINGS_TEST
    friend class SettingsUiTest;
#endif
public:
    // parent — главное окно Nickel: менеджер лежит поверх его содержимого, а системные
    // диалоги Nickel (USB, питание) — отдельные окна и остаются сверху
    explicit KobrowseWindow(QWidget *parent)
        : QWidget(parent),
          m_root(u8(KOBROWSE_ROOT)), m_trash(u8(KOBROWSE_TRASH)), m_path(m_root), m_showHidden(SHOW_HIDDEN),
          m_nativeBrightness(this)
    {
        m_brightnessBackend = &m_nativeBrightness;
        m_path = readHomeFolder(m_root, u8(KOBROWSE_HOME));
        setAttribute(Qt::WA_DeleteOnClose);     // при закрытии окно удаляется из памяти
        setAttribute(Qt::WA_AcceptTouchEvents); // Nickel шлёт касания как QTouchEvent, не как мышь
        setAutoFillBackground(true);            // непрозрачный фон поверх Nickel
        setStyleSheet(u8("QWidget{background:white;color:black;}"
                         "QPushButton{border:none;background:transparent;padding:0;}"
                         "QTreeWidget{border:none;}"
                         ));

        QFont fBtn = sansFont(); fBtn.setPixelSize(FONT_BTN); // подписи кнопок — рубленым
        QFont fList = LIST_FONT.isEmpty() ? font() : QFont(LIST_FONT); // гарнитура и кегль из kobrowse.ini
        fList.setPixelSize(LIST_PX);
        QFont fMsg = font();  fMsg.setPixelSize(FONT_MSG);
        m_bold = fList;       m_bold.setBold(true);

        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(GAP, qMax(4, qRound(8 * SCREEN_DPI / 300.0)), GAP, GAP);
        lay->setSpacing(qMax(4, GAP / 2));

        m_deviceStatus = new DeviceStatusBar(this, u8("/sys/class/power_supply"), m_root);
        QFont statusFont(u8("Sans Serif")); statusFont.setStyleHint(QFont::SansSerif);
        const QStringList statusFamilies = QFontDatabase().families();
        const char *statusPreferred[] = { "Avenir Next", "Avenir", "Rakuten Sans UI App", "Kobo Nickel" };
        bool statusFound = false;
        for (const char *name : statusPreferred) {
            for (const QString &family : statusFamilies)
                if (family.compare(u8(name), Qt::CaseInsensitive) == 0) { statusFont = QFont(family); statusFound = true; break; }
            if (statusFound) break;
        }
        statusFont.setPixelSize(qMax(12, qRound(26 * SCREEN_DPI / 300.0)));
        m_deviceStatus->setStatusFont(statusFont);
        m_deviceStatus->setScale(SCREEN_DPI / 300.0);
        lay->addWidget(m_deviceStatus);

        m_toolbar = new QWidget(this);
        QHBoxLayout *toolbar = new QHBoxLayout(m_toolbar);
        toolbar->setContentsMargins(0, 0, GAP, 0); toolbar->setSpacing(qMax(6, qRound(GAP * 2.25)));
        ChromeButton *back = new ChromeButton(I_BACK, u8("Закрыть Kobrowse"), m_toolbar);
        QFont backFont = QFont(u8("Sans Serif")); backFont.setPixelSize(qMax(18, qRound(8 * SCREEN_DPI / 72.0)));
        back->setFont(backFont); back->setIconScale(1.5);
        back->setMinimumHeight(qMax(48, qRound(72 * SCREEN_DPI / 300.0)));
        back->setFixedWidth(QFontMetrics(backFont).width(u8("Закрыть Kobrowse")) + qRound(ICON_PX * 1.5) + 36);
        toolbar->addWidget(back); toolbar->addStretch(1);
        ChromeButton *find = new ChromeButton(I_SEARCH, QString(), m_toolbar);
        ChromeButton *settings = new ChromeButton(I_GEAR, QString(), m_toolbar);
        find->setLargePlainIcon(true); settings->setLargePlainIcon(true);
        find->setAccessibleName(u8("Поиск")); settings->setAccessibleName(u8("Настройки"));
        const int chromeHeight = qMax(48, qRound(72 * SCREEN_DPI / 300.0));
        find->setFixedSize(chromeHeight, chromeHeight); settings->setFixedSize(chromeHeight, chromeHeight);
        ChromeButton *home = new ChromeButton(I_HOME, QString(), m_toolbar);
        home->setLargePlainIcon(true); home->setAccessibleName(u8("Домашняя папка")); home->setFixedSize(chromeHeight, chromeHeight);
        toolbar->addWidget(home);
        connect(home, &QPushButton::clicked, [this]() {
            m_menuRequested = false; m_actions->hide(); m_info->hide();
            m_marked.clear(); m_selectionOrder.clear();
            m_path = readHomeFolder(m_root, u8(KOBROWSE_HOME)); load();
        });
        m_trashButton = new ChromeButton(I_TRASH, QString(), m_toolbar);
        m_trashButton->setLargePlainIcon(true); m_trashButton->setIconScale(1.18); m_trashButton->setAccessibleName(u8("Открыть корзину")); m_trashButton->setFixedSize(chromeHeight, chromeHeight);
        m_trashButton->setVisible(TRASH_ENABLED); toolbar->addWidget(m_trashButton);
        connect(m_trashButton, &QPushButton::clicked, [this]() { if (pathWithin(m_path, m_trash)) { m_path = m_trash; goUp(); } else openTrash(); });
        m_emptyTrashButton = new ChromeButton(I_TRASH, u8("Очистить корзину"), m_toolbar, true);
        m_emptyTrashButton->setObjectName(u8("emptyTrash"));
        m_emptyTrashButton->setFont(backFont); m_emptyTrashButton->setIconScale(1.5);
        m_emptyTrashButton->setMinimumHeight(chromeHeight);
        m_emptyTrashButton->setFixedWidth(QFontMetrics(backFont).width(u8("Очистить корзину")) + qRound(ICON_PX * 1.5) + 36);
        m_emptyTrashButton->hide(); toolbar->insertWidget(toolbar->indexOf(home), m_emptyTrashButton);
        connect(m_emptyTrashButton, &QPushButton::clicked, [this]() { requestDeletion(2); });
        toolbar->addWidget(find); toolbar->addWidget(settings);
        connect(back, &QPushButton::clicked, [this]() { close(); });
        connect(find, &QPushButton::clicked, [this]() { ksearch_open(); });
        connect(settings, &QPushButton::clicked, [this]() { showSettings(true); });
        lay->addWidget(m_toolbar);

        // Текущий путь остаётся видимым при прокрутке списка.
        m_pathHeader = new QWidget(this);
        QVBoxLayout *pathAndHeader = new QVBoxLayout(m_pathHeader);
        pathAndHeader->setContentsMargins(0, 0, 0, 0); pathAndHeader->setSpacing(0);
        m_pathLabel = new BrowserPathLabel(m_pathHeader);
        QFont pathFont = statusFont;
        const QString pathFamily = defaultListFont();
        if (!pathFamily.isEmpty()) pathFont.setFamily(pathFamily);
        pathFont.setPixelSize(qMax(12, qRound(FONT_MSG * 1.4) - ptToPx(1)));
        m_pathLabel->setFont(pathFont);
        m_pathLabel->setWordWrap(false);
        m_pathLabel->setTextFormat(Qt::PlainText);
        m_pathLabel->setStyleSheet(u8("border:1px solid black;padding:6px;"));
        pathAndHeader->addWidget(m_pathLabel);

        // Fully tappable labelled columns, with the active direction on the left.
        m_columnHeader = new BrowserColumnHeader(m_pathHeader, fList);
        for (int n = 0; n < 3; ++n) {
            QPushButton *button = new QPushButton(m_columnHeader);
            const char *names[] = { "Сортировать по имени", "Сортировать по дате", "Сортировать по размеру" };
            button->setAccessibleName(u8(names[n]));
            QFont headerFont = statusFont; headerFont.setPixelSize(qRound(qMax(10, statusFont.pixelSize() * 3 / 4) * 1.5));
            button->setObjectName(u8("sortColumn_%1").arg(n)); button->setFont(headerFont);
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            button->setMinimumHeight(qMax(48, QFontMetrics(statusFont).lineSpacing() + 12));
            const int inset = qMax(6, qRound(8 * SCREEN_DPI / 300.0));
            button->setStyleSheet(u8("QPushButton{border:1px solid black;background:white;color:black;text-align:left;padding-left:%1px;padding-right:%1px;} QPushButton:pressed{background:black;color:white;}").arg(inset));
            m_sortButtons << button; m_columnHeader->addColumn(button);
            connect(button, &QPushButton::clicked, [this, n]() { setSort(static_cast<BrowserSort>(n)); });
        }
        updateSortHeader(); pathAndHeader->addWidget(m_columnHeader);
        lay->addWidget(m_pathHeader);

        // Список файлов и папок
        m_list = new QTreeWidget(this);
        m_list->setFont(fList);
        m_list->setColumnCount(1);
        m_list->setHeaderHidden(true);
        m_list->setRootIsDecorated(false);
        m_list->setIndentation(0);
        m_list->setUniformRowHeights(false);   // высота строки по содержимому
        m_fileDelegate = new RowDelegate(m_list, fList);
        m_list->setItemDelegate(m_fileDelegate);
        m_list->setSelectionMode(QAbstractItemView::NoSelection);
        m_list->setFocusPolicy(Qt::NoFocus);
        // Полосы прокрутки скрыты: на e-ink листаем страницами кнопками
        m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel); // для перетаскивания пальцем
        lay->addWidget(m_list, 1);

        // Сведения о файле: скрыто, показывается кнопкой «Сведения»
        m_info = new QLabel(this);
        m_info->setFont(fMsg);
        m_info->setWordWrap(true);
        m_info->setStyleSheet(u8("border:2px solid black;padding:10px;"));
        m_info->setTextFormat(Qt::PlainText);
        m_info->hide();
        lay->addWidget(m_info);

        // Экран «Настройки» (kobrowse.ini): строки «название — значение — [−] [+]», внизу
        // Сохранить / По умолчанию / Отмена. Скрыт; на время показа прячутся список и кнопки.
        QFont settingsText = sansFont();
        const QStringList settingsFamilies = QFontDatabase().families();
        const char *avenirNames[] = { "Avenir", "Avenir Next" };
        bool avenirFound = false;
        for (const char *name : avenirNames) {
            for (const QString &family : settingsFamilies)
                if (family.compare(u8(name), Qt::CaseInsensitive) == 0) { settingsText = QFont(family); avenirFound = true; break; }
            if (avenirFound) break;
        }
        settingsText.setStyleHint(QFont::SansSerif);
        settingsText.setPixelSize(FONT_MSG + ptToPx(2));
        QFont settingsButton = settingsText; settingsButton.setPixelSize(FONT_BTN + ptToPx(2));
        buildSettings(settingsText, settingsButton);
        lay->addWidget(m_settings, 1);

        m_msgTimer.setSingleShot(true);
        m_msgTimer.setInterval(4000);
        connect(&m_msgTimer, &QTimer::timeout, this, [this]() {
            if (m_footerNotice) m_footerNotice->setMessage(QString());
            if (m_settingsNotice) { m_settingsNotice->setMessage(QString()); m_settingsNotice->hide(); }
        });

        // Диагностика настроек: геометрия строк через 0,4 с после показа
        m_setGeom.setSingleShot(true);
        m_setGeom.setInterval(400);
        connect(&m_setGeom, &QTimer::timeout, this, [this]() { logSettingsGeometry(); });

        m_ensure.setSingleShot(true);
        m_ensure.setInterval(50);
        connect(&m_ensure, &QTimer::timeout, this, [this]() {
            for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
                QTreeWidgetItem *it = m_list->topLevelItem(i);
                if (it->data(0, ROLE_PATH).toString() == m_lastMarked) {
                    for (int page = 0; page + 1 < int(m_pageStarts.size()); ++page)
                        if (i >= m_pageStarts[page] && i < m_pageStarts[page + 1]) {
                            if (m_pageIndex != page) { m_pageIndex = page; applyPage(); }
                            break;
                        }
                    break;
                }
            }
        });

        // Контекстное меню — список операций поверх файлов, без панели кнопок.
        m_actions = new QTreeWidget(this);
        m_actions->setColumnCount(1);
        m_actions->setHeaderHidden(true);
        m_actions->setRootIsDecorated(false);
        m_actions->setIndentation(0);
        QFont actionFont = fList; actionFont.setPixelSize(ptToPx(12));
        m_actions->setFont(actionFont);
        m_actions->setFocusPolicy(Qt::NoFocus);
        m_actions->setSelectionMode(QAbstractItemView::NoSelection);
        m_actions->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_actions->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_actions->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_actions->setStyleSheet(u8("QTreeWidget{background:white;color:black;border:2px solid black;}"
                                   "QTreeWidget::item{padding-left:12px;padding-right:12px;}"));
        m_actions->setItemDelegate(new ActionMenuDelegate(m_actions));
        m_actions->hide();
        m_pickerShade = new QWidget(this);
        m_pickerShade->setObjectName(u8("pickerShade"));
        m_pickerShade->setStyleSheet(u8("QWidget#pickerShade{background-color:rgba(0,0,0,40);}"));
        m_pickerShade->hide();
        connect(m_actions, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int) {
            if (it && (it->flags() & Qt::ItemIsEnabled)) runMenuAction(it->data(0, Qt::UserRole).toInt());
        });

        // Подтверждение необратимых операций — внутри окна, без системного диалога.
        m_confirm = new QWidget(this);
        QVBoxLayout *confirmLayout = new QVBoxLayout(m_confirm);
        confirmLayout->setContentsMargins(2, 2, 2, 2); confirmLayout->setSpacing(0);
        m_confirm->setObjectName(u8("confirm"));
        m_confirm->setStyleSheet(u8("QWidget#confirm{border:2px solid black;background:white;}"));
        m_confirmText = new QLabel(m_confirm);
        m_confirmText->setWordWrap(true);
        m_confirmText->setFont(settingsText);
        m_confirmText->setStyleSheet(u8("border:none;padding:16px;"));
        confirmLayout->addWidget(m_confirmText);
        QHBoxLayout *confirmButtons = new QHBoxLayout();
        confirmButtons->setSpacing(0);
        QPushButton *confirmYes = makeButton(confirmButtons, settingsButton, "Да", qRound(BTN_HEIGHT * 1.3), 1);
        QPushButton *confirmNo = makeButton(confirmButtons, settingsButton, "Отмена", qRound(BTN_HEIGHT * 1.3), 1);
        confirmYes->setObjectName(u8("confirmYes")); confirmNo->setObjectName(u8("confirmNo"));
        confirmLayout->addLayout(confirmButtons);
        connect(confirmYes, &QPushButton::clicked, [this]() { confirmDeletion(); });
        connect(confirmNo, &QPushButton::clicked, [this]() {
            if (m_pendingDelete == 1) clearSelection();
            m_confirm->hide(); m_pickerShade->hide(); m_pendingDelete = 0; m_confirmSetting = -1; m_pendingZip.clear();
        });
        m_confirm->hide();

        m_workOverlay = new QWidget(this); m_workOverlay->setObjectName(u8("workOverlay"));
        m_workOverlay->setStyleSheet(u8("QWidget#workOverlay{background-color:rgba(0,0,0,40);}"));
        QVBoxLayout *workLayout = new QVBoxLayout(m_workOverlay);
        m_workText = new QLabel(m_workOverlay); m_workText->setFont(fMsg);
        m_workText->setAlignment(Qt::AlignCenter); m_workText->setWordWrap(true);
        m_workText->setStyleSheet(u8("background:white;border:2px solid black;padding:20px;"));
        workLayout->addStretch(1); workLayout->addWidget(m_workText, 0, Qt::AlignHCenter); workLayout->addStretch(1);
        m_workOverlay->hide();
        m_workTimer.setSingleShot(true);
        connect(&m_workTimer, &QTimer::timeout, this, [this]() {
            if (property("kobrowseSleepHidden").toBool()) return;
            const std::function<void()> operation = m_work; m_work = std::function<void()>();
            if (operation) operation();
            load();
            m_busy = false; m_workOverlay->hide();
            if (!m_operationDetail.isEmpty()) {
                m_info->setText(m_operationDetail.left(800)); m_info->show(); m_pageTimer.start(0);
                m_operationDetail.clear();
            }
            update(); if (m_list->isVisible()) m_list->viewport()->update();
        });

        m_holdTimer.setSingleShot(true);
        m_holdTimer.setInterval(LONG_TAP_MS);
        connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
            if (m_moved || m_twoFinger) return;
            if (m_holdBlank) {
                m_holdFired = true; m_menuRequested = true; updateActions(); return;
            }
            if (m_holdPath.isEmpty()) return;
            for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
                QTreeWidgetItem *it = m_list->topLevelItem(i);
                if (it->data(0, ROLE_PATH).toString() == m_holdPath) {
                    m_holdFired = true;
                    m_menuRequested = true;
                    toggleMark(it);
                    break;
                }
            }
        });

        // Нижняя строка: постраничная навигация и счётчик всей папки.
        m_footerLine = new QFrame(this); m_footerLine->setFrameShape(QFrame::NoFrame);
        m_footerLine->setFixedHeight(qMax(1, qRound(1.5 * SCREEN_DPI / 300.0)));
        m_footerLine->setStyleSheet(u8("background:black;border:none;")); lay->addWidget(m_footerLine);
        m_nav = new QWidget(this);
        QHBoxLayout *footer = new QHBoxLayout(m_nav);
        footer->setContentsMargins(0, 0, 0, 0); footer->setSpacing(qMax(6, qRound(GAP * 2.25)));
        m_prev = new ChromeButton(I_CHEV_UP, QString(), m_nav);
        m_next = new ChromeButton(I_CHEV_DOWN, QString(), m_nav);
        m_prev->setAccessibleName(u8("Предыдущая страница"));
        m_next->setAccessibleName(u8("Следующая страница"));
        QFont footerFont = statusFont; footerFont.setPixelSize(FONT_MSG);
        const int footerHeight = qMax(chromeHeight, QFontMetrics(footerFont).lineSpacing() * 2);
        m_prev->setFixedSize(chromeHeight, chromeHeight); m_next->setFixedSize(chromeHeight, chromeHeight);
        m_prev->setLargePlainIcon(true); m_next->setLargePlainIcon(true);
        m_pageLabel = new QLabel(m_nav);
        m_pageLabel->setFont(footerFont);
        m_pageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        footer->addWidget(m_pageLabel);
        footer->addWidget(m_prev, 0, Qt::AlignVCenter); footer->addWidget(m_next, 0, Qt::AlignVCenter);
        m_footerNotice = new FooterNotice(m_nav); m_footerNotice->setFont(fMsg);
        m_footerNotice->setFixedHeight(footerHeight);
        footer->addWidget(m_footerNotice, 1);
        m_countLabel = new FooterCountsButton(m_nav); m_countLabel->setCounterFont(footerFont);
        m_countLabel->setMinimumHeight(footerHeight);
        m_countLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_nav->setMinimumHeight(footerHeight);
        m_countLabel->setAccessibleName(u8("Сведения"));
        connect(m_countLabel, &QPushButton::clicked, [this]() { showInfo(); });
        footer->addWidget(m_countLabel);
        lay->addWidget(m_nav);
        connect(m_prev, &QPushButton::clicked, [this]() { turnPage(-1); });
        connect(m_next, &QPushButton::clicked, [this]() { turnPage(1); });
        m_pageTimer.setSingleShot(true);
        connect(&m_pageTimer, &QTimer::timeout, this, [this]() { recalculatePages(); });

        load();

        // Касания внутри окна перехватываем на уровне приложения (см. eventFilter)
        qApp->installEventFilter(this);
    }

    ~KobrowseWindow() { klog(u8("window closed")); cancelBrightness(); qApp->removeEventFilter(this); }
    void resumeDeferredWork() { if (m_busy && m_work && !m_workTimer.isActive()) m_workTimer.start(600); }

protected:
    // Касания обрабатываем сами: TouchBegin нужно принять, иначе TouchEnd не придёт.
    // Касание с дочернего виджета (кнопка, список) поднимается сюда. Таймер
    // отмечает строку при удержании; TouchEnd обрабатывает только короткое нажатие.
    bool event(QEvent *e) override {
        const QEvent::Type t = e->type();
        if (m_logLeft > 0 && t != QEvent::Paint && t != QEvent::UpdateRequest) {
            --m_logLeft;
            klog(u8("event %1").arg(int(t)));
        }
        if (handleTouch(e)) return true;
        return QWidget::event(e);
    }

    // Перехват касаний на уровне приложения: что бы ни случилось с виджетами внутри
    // (скрытие ряда кнопок, перестройка списка), касание внутри окна попадёт сюда.
    bool eventFilter(QObject *obj, QEvent *e) override {
        const QEvent::Type t = e->type();
        if (t == QEvent::ApplicationDeactivate) { setTouchButton(nullptr); cancelBrightness(); }
        if (m_list && obj == m_list->viewport() && t == QEvent::Resize) m_pageTimer.start(0);
        if (m_settingsScroll && obj == m_settingsScroll->viewport() && t == QEvent::Resize) updateResetGeometry();
        if (!(t == QEvent::TouchBegin || t == QEvent::TouchUpdate || t == QEvent::TouchEnd
              || t == QEvent::TouchCancel) || obj == this || !obj->isWidgetType())
            return false;
        QWidget *w = static_cast<QWidget *>(obj);
        if (isAncestorOf(w)) return handleTouch(e);          // дочерний виджет менеджера
        // Событие пришло окну Nickel (под нами), но палец внутри менеджера — забираем
        if (w == window() || w->isAncestorOf(this)) {
            const QList<QTouchEvent::TouchPoint> pts = static_cast<QTouchEvent *>(e)->touchPoints();
            if (!pts.isEmpty() && rect().contains(mapFromGlobal(pts.first().screenPos().toPoint())))
                return handleTouch(e);
        }
        return false;
    }

    // Единая обработка касаний. Координаты берём экранные (screenPos) и переводим в окно —
    // не зависят от того, какому виджету Qt доставил событие.
    bool handleTouch(QEvent *e) {
        const QEvent::Type t = e->type();
        if (m_busy && (t == QEvent::TouchBegin || t == QEvent::TouchUpdate || t == QEvent::TouchEnd || t == QEvent::TouchCancel)) {
            e->accept(); return true;
        }
        if (t != QEvent::TouchBegin && t != QEvent::TouchUpdate && t != QEvent::TouchEnd
            && t != QEvent::TouchCancel)
            return false;
        const QList<QTouchEvent::TouchPoint> pts = static_cast<QTouchEvent *>(e)->touchPoints();
        const QPoint p = pts.isEmpty() ? QPoint() : mapFromGlobal(pts.first().screenPos().toPoint());
        switch (t) {
        case QEvent::TouchBegin:
            setTouchButton(nullptr);
            cancelBrightness();
            m_brightnessConsumed = false;
            // Include the margin between the viewport and the physical edge.
            // The vertical bounds keep toolbar and footer controls untouched.
            {
                const QRect listRect(m_list->viewport()->mapTo(this, QPoint(0, 0)), m_list->viewport()->size());
                m_brightnessGesture.begin(p, size(), pts.size() == 1
                    && p.y() >= listRect.top() && p.y() <= listRect.bottom()
                    && !m_settings->isVisible() && !m_actions->isVisible()
                    && !m_confirm->isVisible());
            }
            m_press.start();
            m_pressPos = p;
            m_buttonTouchCancelled = false;
            m_moved = false;
            m_twoFinger = false;
            m_holdFired = false;
            m_holdPath.clear(); m_holdBlank = false;
            m_swipeList = false;
            m_holdTimer.stop();
            m_menuTouch = m_actions->isVisible()
                && m_actions->viewport()->rect().contains(m_actions->viewport()->mapFrom(this, p));
            m_menuScrollStart = m_actions->verticalScrollBar()->value();
            m_settingsTouch = !m_confirm->isVisible() && m_settings->isVisible() && m_settingsScroll
                && m_settingsScroll->viewport()->rect().contains(m_settingsScroll->viewport()->mapFrom(this, p));
            m_activeSlider = nullptr;
            if (m_settingsTouch && !m_confirm->isVisible() && pts.size() == 1) {
                QWidget *hit = childAt(p);
                for (SettingSlider *slider : m_sliders) {
                    if (slider && slider->isVisible() && (hit == slider || slider->isAncestorOf(hit))) {
                        m_activeSlider = slider; slider->begin(slider->mapFrom(this, p).x()); break;
                    }
                }
            }
            if (m_settingsTouch) m_settingsScrollStart = m_settingsScroll->verticalScrollBar()->value();
            if (pts.size() == 1 && !m_activeSlider && !m_actions->isVisible()) {
                for (QWidget *hit = childAt(p); hit && hit != this; hit = hit->parentWidget()) {
                    QPushButton *button = qobject_cast<QPushButton *>(hit);
                    if (button) {
                        if (button->isEnabled() && (!m_confirm->isVisible() || m_confirm->isAncestorOf(button)))
                            setTouchButton(button);
                        break;
                    }
                }
            }
            if (pts.size() >= 2) {
                m_twoFinger = true;
                m_twoStartY = (mapFromGlobal(pts.at(0).screenPos().toPoint()).y()
                               + mapFromGlobal(pts.at(1).screenPos().toPoint()).y()) / 2.0;
                m_twoCurrentY = m_twoStartY;
                m_scrollStart = m_list->verticalScrollBar()->value();
            }
            if (pts.size() == 1 && !m_menuTouch && !m_confirm->isVisible() && inList(p)) {
                m_swipeList = !m_actions->isVisible();
                QTreeWidgetItem *item = m_list->itemAt(m_list->viewport()->mapFrom(this, p));
                if (item && !item->data(0, ROLE_PARENT).toBool()) {
                    m_holdPath = item->data(0, ROLE_PATH).toString();
                    if (!m_holdPath.isEmpty()) m_holdTimer.start();
                } else {
                    m_holdBlank = true; m_holdTimer.start();
                }
            }
            break;
        case QEvent::TouchUpdate:
            if (m_touchButton && (pts.size() != 1 || (p - m_pressPos).manhattanLength() > SWIPE_PX
                || !m_touchButton->rect().contains(m_touchButton->mapFrom(this, p)))) {
                m_buttonTouchCancelled = true; setTouchButton(nullptr);
            }
            if (m_brightnessConsumed) {
                if (pts.size() != 1) cancelBrightness();
                else if (m_brightnessTracking) updateBrightness(p);
                break;
            }
            if (pts.size() != 1 || m_holdFired) m_brightnessGesture.cancel();
            else if (m_brightnessGesture.move(p)) {
                m_brightnessConsumed = true; m_moved = true;
                m_holdTimer.stop(); m_holdPath.clear(); m_swipeList = false;
                if (m_brightnessBackend->begin()) {
                    m_brightnessTracking = true; updateBrightness(p);
                } else {
                    m_brightnessGesture.cancel();
                    klog(u8("brightness unavailable: ") + m_brightnessBackend->errorDetail());
                    showMessage(u8("Яркость недоступна; подробности в журнале, если он включён"));
                }
                break;
            }
            if (m_activeSlider) {
                if (pts.size() != 1) { m_activeSlider->cancel(); m_activeSlider = nullptr; m_moved = true; }
                else m_activeSlider->move(m_activeSlider->mapFrom(this, p).x());
                break;
            }
            if (m_settingsTouch && !pts.isEmpty()) {
                if ((p - m_pressPos).manhattanLength() > SWIPE_PX) m_moved = true;
                if (pts.size() >= 2) m_moved = true;
                if (m_moved) m_settingsScroll->verticalScrollBar()->setValue(m_settingsScrollStart - (p.y() - m_pressPos.y()));
                break;
            }
            if (m_menuTouch && !pts.isEmpty()) {
                if ((p - m_pressPos).manhattanLength() > SWIPE_PX) m_moved = true;
                if (m_moved) m_actions->verticalScrollBar()->setValue(m_menuScrollStart - (p.y() - m_pressPos.y()));
                if (pts.size() >= 2) m_moved = true;
                break;
            }
            if (pts.size() >= 2) {
                m_holdTimer.stop(); m_holdPath.clear();
                // Два пальца — перетаскивание списка (по среднему положению пальцев)
                const qreal y = (mapFromGlobal(pts.at(0).screenPos().toPoint()).y()
                                 + mapFromGlobal(pts.at(1).screenPos().toPoint()).y()) / 2.0;
                if (!m_twoFinger) {
                    m_twoFinger = true;
                    m_twoStartY = y; m_twoCurrentY = y;
                    m_scrollStart = m_list->verticalScrollBar()->value();
                } else if (inList(m_pressPos)) {
                    m_twoCurrentY = y;
                }
            } else if (!pts.isEmpty() && (p - m_pressPos).manhattanLength() > SWIPE_PX) {
                m_moved = true;   // один палец сдвинулся — это не нажатие
                m_holdTimer.stop(); m_holdPath.clear();
            }
            break;
        case QEvent::TouchEnd: {
            if (m_touchButton && (pts.size() != 1 || !m_touchButton->rect().contains(m_touchButton->mapFrom(this, p))))
                m_buttonTouchCancelled = true;
            setTouchButton(nullptr);
            if (m_brightnessConsumed) {
                if (m_brightnessTracking && !pts.isEmpty()) updateBrightness(p);
                if (m_brightnessTracking) {
                    const bool saved = m_brightnessBackend->finish(true);
                    klog(saved ? u8("brightness gesture completed") : u8("brightness gesture finish failed"));
                    if (!saved) showMessage(u8("Не удалось завершить изменение яркости"));
                    m_brightnessTracking = false;
                    m_deviceStatus->hideBrightnessSoon();
                }
                m_brightnessGesture.cancel(); m_brightnessConsumed = false;
                m_holdTimer.stop(); m_holdPath.clear(); m_twoFinger = false;
                break;
            }
            m_brightnessGesture.cancel();
            if (m_activeSlider) {
                if (!pts.isEmpty()) m_activeSlider->move(m_activeSlider->mapFrom(this, p).x());
                m_activeSlider->finish(); m_activeSlider = nullptr;
                break;
            }
            m_holdTimer.stop(); m_holdPath.clear();
            if (m_twoFinger && m_swipeList) {
                const int direction = pageSwipeDirection(0, int(m_twoCurrentY - m_twoStartY), SWIPE_PX);
                if (direction) turnPage(direction);
            }
            if (!pts.isEmpty() && !m_twoFinger && m_moved && !m_holdFired && m_swipeList) {
                const QPoint delta = p - m_pressPos;
                const int direction = pageSwipeDirection(delta.x(), delta.y(), SWIPE_PX);
                if (direction) turnPage(direction);
            } else if (!pts.isEmpty() && !m_twoFinger && !m_moved && !m_holdFired && !m_buttonTouchCancelled) tapAt(p, false);
            m_twoFinger = false;
            break;
        }
        default:              // TouchCancel
            setTouchButton(nullptr);
            cancelBrightness(); m_brightnessConsumed = false;
            if (m_activeSlider) { m_activeSlider->cancel(); m_activeSlider = nullptr; }
            m_holdTimer.stop(); m_holdPath.clear();
            m_twoFinger = false;
            break;
        }
        e->accept();
        return true;
    }


    // Ширина окна изменилась — пересчитать высоты строк (перенос имён)
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        if (m_list) m_list->doItemsLayout();
        if (m_workOverlay) m_workOverlay->setGeometry(rect());
        placeActionMenu();
        if (m_confirm && m_confirm->isVisible()) { m_pickerShade->setGeometry(rect()); placeConfirmation(); }
        m_pageTimer.start(0);
    }
    void showEvent(QShowEvent *e) override {
        QWidget::showEvent(e);
        m_pageTimer.start(0);
    }
    void hideEvent(QHideEvent *e) override {
        setTouchButton(nullptr);
        cancelBrightness();
        QWidget::hideEvent(e);
    }

private:
    void setTouchButton(QPushButton *button) {
        if (m_touchButton == button) return;
        if (m_touchButton) {
            QPushButton *old = m_touchButton.data();
            old->setDown(false); old->update();
            for (QWidget *child : old->findChildren<QWidget *>()) child->update();
            if (old->property("settingsChoice").toBool())
                for (QLabel *label : old->findChildren<QLabel *>())
                    label->setStyleSheet(u8("background:transparent;color:black;"));
        }
        m_touchButton = button;
        if (button) {
            button->setDown(true); button->update();
            for (QWidget *child : button->findChildren<QWidget *>()) child->update();
            if (button->property("settingsChoice").toBool())
                for (QLabel *label : button->findChildren<QLabel *>())
                    label->setStyleSheet(u8("background:transparent;color:white;"));
        }
    }
    void cancelBrightness() {
        if (m_brightnessTracking) {
            const bool restored = m_brightnessBackend->finish(false);
            m_brightnessTracking = false;
            klog(restored ? u8("brightness gesture cancelled") : u8("brightness: could not restore cancelled gesture"));
        }
        m_brightnessGesture.cancel();
        if (m_deviceStatus) m_deviceStatus->clearBrightness();
    }
    void updateBrightness(const QPoint &point) {
        const int desired = qBound(0, m_brightnessBackend->startLevel() + m_brightnessGesture.change(point), 100);
        const int actual = m_brightnessBackend->setLevel(desired);
        if (actual < 0) {
            cancelBrightness();
            showMessage(u8("Не удалось изменить яркость"));
            return;
        }
        m_deviceStatus->showBrightness(actual);
    }
    bool inList(const QPoint &p) const {
        QWidget *w = childAt(p);
        return w && (w == m_list->viewport() || m_list->viewport()->isAncestorOf(w));
    }

    void placeConfirmation() {
        if (!m_confirm) return;
        const int margin = qMax(12, GAP);
        const int availableW = qMax(1, width() - 2 * margin);
        const int w = qMin(availableW, qMax(240, qRound(width() * 0.8)));
        m_confirm->setFixedWidth(w);
        m_confirm->adjustSize();
        const int h = qMin(qMax(1, height() - 2 * margin), m_confirm->layout()->hasHeightForWidth()
                            ? m_confirm->layout()->heightForWidth(w) : m_confirm->sizeHint().height());
        m_confirm->setGeometry((width() - w) / 2, (height() - h) / 2, w, h);
    }
    void showConfirmation() {
        placeConfirmation(); m_pickerShade->setGeometry(rect()); m_pickerShade->show(); m_pickerShade->raise();
        m_confirm->show(); m_confirm->raise();
    }

    // Показать отмеченную строку после того, как раскладка обновится
    // (ряд действий и сведения сжимают список и могли её закрыть)
    void ensureMarkedVisible() { m_ensure.start(); }

    // Касание в точке p (координаты окна)
    void tapAt(const QPoint &p, bool longTap) {
        if (m_actions->isVisible()) {
            const QPoint menuPoint = m_actions->viewport()->mapFrom(this, p);
            if (m_actions->viewport()->rect().contains(menuPoint)) {
                QTreeWidgetItem *item = m_actions->itemAt(menuPoint);
                if (item && (item->flags() & Qt::ItemIsEnabled)) runMenuAction(item->data(0, Qt::UserRole).toInt());
                return;
            }
            const QPoint listPoint = m_list->viewport()->mapFrom(this, p);
            if ((m_list->isVisible() && m_list->viewport()->rect().contains(listPoint) && !m_list->itemAt(listPoint))
                || (m_nav->isVisible() && emptyFooterTap(m_nav, m_nav->mapFrom(this, p)))) {
                runMenuAction(ActClose);
                return;
            }
        }
        if (m_confirm->isVisible()) {
            QWidget *target = childAt(p);
            for (QWidget *cur = target; cur && cur != this; cur = cur->parentWidget()) {
                if (QPushButton *button = qobject_cast<QPushButton *>(cur)) {
                    if (m_confirm->isAncestorOf(button)) button->click();
                    return;
                }
            }
            return;
        }
        // Список проверяем по геометрии, не через childAt (надёжнее)
        const QPoint vp = m_list->viewport()->mapFrom(this, p);
        if (m_list->isVisible() && m_list->viewport()->rect().contains(vp)) {
            QTreeWidgetItem *it = m_list->itemAt(vp);
            if (m_tapLog > 0) {
                --m_tapLog;
                klog(u8("tap list %1,%2 item=%3 long=%4")
                         .arg(vp.x()).arg(vp.y()).arg(it ? it->text(0) : u8("-")).arg(longTap));
            }
            // Долгое нажатие — отметить; если уже что-то отмечено, короткое тоже отмечает
            if (longTap || !m_marked.isEmpty()) toggleMark(it);
            else onItem(it);
            return;
        }
        QWidget *w = childAt(p);
        if (m_tapLog > 0) {
            --m_tapLog;
            klog(u8("tap %1,%2 child=%3").arg(p.x()).arg(p.y())
                     .arg(w ? QString::fromLatin1(w->metaObject()->className()) : u8("-")));
        }
        for (QWidget *cur = w; cur && cur != this; cur = cur->parentWidget()) {
            if (QPushButton *b = qobject_cast<QPushButton *>(cur)) {
                const QVariant stepRow = b->property("set_row");
                if (stepRow.isValid()) {          // «−»/«+» на экране настроек
                    const int dir = b->property("set_dir").toInt();
                    klog(u8("settings tap row %1 dir %2 (en=%3 vis=%4)")
                             .arg(stepRow.toInt()).arg(dir)
                             .arg(b->isEnabled() ? 1 : 0).arg(b->isVisible() ? 1 : 0));
                    if (b->isEnabled() && b->isVisible()) stepSetting(stepRow.toInt(), dir);
                    return;
                }
                if (b->isEnabled() && b->isVisible()) b->click();
                return;
            }
            if (cur == m_list->viewport()) {
                QTreeWidgetItem *it = m_list->itemAt(m_list->viewport()->mapFrom(this, p));
                // Долгое нажатие — отметить; если уже что-то отмечено, короткое тоже отмечает
                if (longTap || !m_marked.isEmpty()) toggleMark(it);
                else onItem(it);
                return;
            }
        }
    }

    static void setButtonFace(QPushButton *b, Icon ic, const QString &text) {
        if (ButtonFace *f = b->findChild<ButtonFace *>()) f->setFace(ic, text);
    }

    void showMessage(const QString &text) {
        if (m_settings && m_settings->isVisible() && m_settingsNotice) { m_settingsNotice->setMessage(text); m_settingsNotice->show(); }
        else if (m_footerNotice) m_footerNotice->setMessage(text);
        m_msgTimer.start();
    }

    // Служебные каталоги видимы, но изменения в них запрещены.
    bool isProtected(const QString &path) const { return protectedLocalPath(m_root, path, ALLOW_ADDS); }

    // Отметка строки: флаг для MarkDelegate (он рисует инверсию)
    void setMarkedLook(QTreeWidgetItem *it, bool on) {
        if (it->data(0, ROLE_MARK).toBool() != on) it->setData(0, ROLE_MARK, on);
        if (on) m_lastMarked = it->data(0, ROLE_PATH).toString();
    }

    // Сведения об одном отмеченном файле/папке
    void showInfo() {
        if (m_info->isVisible()) { m_info->hide(); return; }
        if (m_marked.size() > 1) {
            m_info->setText(u8("Выбрано элементов: %1").arg(m_marked.size()));
            m_info->show(); return;
        }
        const QFileInfo fi(m_marked.isEmpty() ? m_path : *m_marked.begin());
        QString t = fi.fileName() + u8("\n\n");
        if (fi.isSymLink()) {
            t += u8("Символическая ссылка: ") + fi.symLinkTarget() + u8("\n");
        } else if (fi.isDir()) {
            const int n = QDir(fi.absoluteFilePath()).entryList(
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).size();
            t += u8("Папка, элементов: %1\n").arg(n);
        } else {
            t += u8("Размер: %1 (%2 байт)\n").arg(humanSize(fi.size())).arg(fi.size());
        }
        t += u8("Изменён: ") + fi.lastModified().toString(u8("dd.MM.yyyy hh:mm")) + u8("\n");
        t += u8("Где: ") + fi.absolutePath();
        m_info->setText(t);
        m_info->show();
        ensureMarkedVisible();
    }

    void toggleMark(QTreeWidgetItem *it) {
        if (!it || it->data(0, ROLE_PARENT).toBool()) return;
        const QString path = it->data(0, ROLE_PATH).toString();
        if (path.isEmpty()) return;
        if (readOnlyLocalEntry(m_root, m_trash, path, ALLOW_ADDS)) {
            if (m_marked.isEmpty()) m_menuRequested = false;
            showMessage(u8("Защищённые элементы доступны только для просмотра"));
            return;
        }
        const bool on = !m_marked.contains(path);
        if (on) { m_marked.insert(path); m_selectionOrder << path; }
        else { m_marked.remove(path); m_selectionOrder.removeAll(path); }
        if (m_marked.isEmpty()) m_menuRequested = false;
        setMarkedLook(it, on);
        updateActions();
        // The tapped item is already on this page; a delayed relayout here
        // would repaint the entire file list after the menu appears.
    }

    void clearSelection() {
        m_marked.clear(); m_selectionOrder.clear();
        m_menuRequested = false;
        if (m_deviceStatus) m_deviceStatus->setSelectionCount(0);
        if (m_actions) m_actions->hide();
        if (m_info) m_info->hide();
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) setMarkedLook(m_list->topLevelItem(i), false);
    }

    enum MenuAction { ActCopy, ActCut, ActDelete, ActZip, ActHome, ActRename, ActPaste, ActClose, ActMkdir };

    void placeActionMenu() {
        if (!m_actions) return;
        const QFontMetrics fm(m_actions->font());
        int widthNeeded = 0, heightNeeded = 6;
        const char *allLabels[] = { "Сделать домашней", "Вырезать", "Копировать", "Вставить",
                                    "Архивировать", "Удалить", "Переименовать", "Создать папку", "Закрыть меню" };
        for (const char *label : allLabels) widthNeeded = qMax(widthNeeded, fm.width(u8(label)));
        for (int i = 0; i < m_actions->topLevelItemCount(); ++i)
            heightNeeded += m_actions->topLevelItem(i)->sizeHint(0).height();
        const int w = qBound(1, widthNeeded + qMax(48, GAP * 2), qMax(1, width() - 2 * GAP));
        const int h = qMin(height() - 2 * GAP, heightNeeded);
        const QRect geometry(width() - GAP - w, GAP, w, qMax(1, h));
        if (m_actions->geometry() != geometry) m_actions->setGeometry(geometry);
    }
    void updateActions() {
        m_deviceStatus->setSelectionCount(m_marked.size());
        m_info->hide();
        const bool show = m_menuRequested && !m_settings->isVisible() && !m_confirm->isVisible();
        if (!show) { if (m_actions->isVisible()) m_actions->hide(); return; }
        const int row = qMax(BTN_HEIGHT, QFontMetrics(m_actions->font()).height() + 24);
        const int separator = qMax(5, GAP / 2);
        QList<ActionMenuEntry> entries;
        const bool selected = !m_marked.isEmpty();
        if (safeLocalPath(m_root, m_path) && writableLocalDirectory(m_root, m_path, ALLOW_ADDS))
            entries << ActionMenuEntry(u8("Создать папку"), ActMkdir, row)
                    << ActionMenuEntry(QString(), 0, separator, false);
        if (selected) {
            if (m_marked.size() == 1 && validHomeFolder(m_root, *m_marked.begin()))
                entries << ActionMenuEntry(u8("Сделать домашней"), ActHome, row)
                        << ActionMenuEntry(QString(), 0, separator, false);
            entries << ActionMenuEntry(u8("Вырезать"), ActCut, row)
                    << ActionMenuEntry(u8("Копировать"), ActCopy, row);
        }
        if (!m_clip.isEmpty()) entries << ActionMenuEntry(u8("Вставить"), ActPaste, row);
        if (selected) {
            entries << ActionMenuEntry(QString(), 0, separator, false)
                    << ActionMenuEntry(u8("Архивировать"), ActZip, row)
                    << ActionMenuEntry(u8("Удалить"), ActDelete, row);
            if (m_marked.size() == 1) entries << ActionMenuEntry(u8("Переименовать"), ActRename, row);
        }
        if (!entries.isEmpty() && entries.last().enabled) entries << ActionMenuEntry(QString(), 0, separator, false);
        entries << ActionMenuEntry(u8("Закрыть меню"), ActClose, row);
        const bool changed = syncActionMenu(m_actions, entries);
        if (changed || !m_actions->isVisible()) placeActionMenu();
        if (!m_actions->isVisible()) { m_actions->show(); m_actions->raise(); }
    }
    void runMenuAction(int action) {
        m_menuRequested = false;
        m_actions->hide();
        switch (action) {
        case ActCopy: toClipboard(false); break;
        case ActCut: toClipboard(true); break;
        case ActDelete:
            if (TRASH_ENABLED && !pathWithin(m_path, m_trash)) moveToTrash();
            else requestDeletion(1);
            break;
        case ActZip: zipSelected(); break;
        case ActHome:
            if (m_marked.size() == 1 && writeHomeFolder(m_root, u8(KOBROWSE_HOME), *m_marked.begin())) {
                m_marked.clear(); m_selectionOrder.clear(); load();
                showMessage(u8("Домашняя папка сохранена"));
            } else { clearSelection(); showMessage(u8("Не удалось сохранить домашнюю папку")); }
            break;
        case ActRename: startRename(); break;
        case ActMkdir: startCreateFolder(); break;
        case ActPaste: paste(); break;
        case ActClose: clearSelection(); break;
        default: break;
        }
    }
    void startRename() {
        if (m_marked.size() != 1) return;
        const QString source = *m_marked.begin();
        if (!safeLocalPath(m_root, source) || isProtected(source)) {
            clearSelection(); showMessage(u8("Защищённый путь нельзя переименовать")); return;
        }
        QPointer<KobrowseWindow> self(this);
        krename_open(QFileInfo(source).fileName(), [self, source](const QString &name) -> QString {
            if (!self) return u8("Окно закрыто");
            QString error;
            if (!renameLocalEntry(self->m_root, source, name, error, ALLOW_ADDS)) {
                klog(u8("rename failed: ") + source + u8("; ") + error);
                self->clearSelection(); return error;
            }
            const QString dest = QFileInfo(source).absolutePath() + u8("/") + name;
            klog(u8("rename complete: ") + source + u8(" -> ") + dest);
            for (int i = 0; i < self->m_clip.size(); ++i)
                if (pathWithin(self->m_clip[i], source)) self->m_clip[i] = dest + self->m_clip[i].mid(source.size());
            self->m_marked.clear(); self->m_selectionOrder.clear(); self->load();
            self->showMessage(u8("Переименовано: ") + name);
            return QString();
        });
    }

    void startCreateFolder() {
        const QString parent = m_path;
        if (!safeLocalPath(m_root, parent) || !writableLocalDirectory(m_root, parent, ALLOW_ADDS)) {
            clearSelection(); showMessage(u8("Папка недоступна для изменений")); return;
        }
        clearSelection();
        QPointer<KobrowseWindow> self(this);
        krename_open(QString(), [self, parent](const QString &name) -> QString {
            if (!self) return u8("Окно закрыто");
            QString error;
            if (!createLocalDirectory(self->m_root, parent, name, error, ALLOW_ADDS)) return error;
            self->load(); self->showMessage(u8("Папка создана: ") + name);
            return QString();
        }, u8("Создать папку: нажмите Enter"));
    }

    // Копировать / Вырезать: запомнить отмеченное, снять выбор
    void toClipboard(bool cut) {
        QStringList list;
        for (const QString &p : m_marked) {
            if (!safeLocalPath(m_root, p) || (cut ? isProtected(p) : readOnlyLocalEntry(m_root, m_trash, p, ALLOW_ADDS))) continue;
            list << p;
        }
        const bool skipped = list.size() < m_marked.size();
        m_clip = list;
        m_clipCut = cut;
        m_marked.clear(); m_selectionOrder.clear();
        load();
        if (skipped) showMessage(u8("Ссылки и защищённые пути пропущены"));
    }

    void startWork(const QString &text, const std::function<void()> &work) {
        if (m_busy) return;
        m_busy = true; m_work = work; m_operationDetail.clear(); m_holdTimer.stop();
        m_workText->setText(text); m_workOverlay->setGeometry(rect());
        m_workOverlay->show(); m_workOverlay->raise(); m_workOverlay->repaint();
        // Allow the e-ink compositor to present the overlay before synchronous I/O.
        m_workTimer.start(600);
    }

    // Вставить буфер в текущую папку
    void paste() { startWork(u8("Выполняется вставка…"), [this]() { pasteWork(); }); }
    void pasteWork() {
        if (!safeLocalPath(m_root, m_path) || !writableLocalDirectory(m_root, m_path, ALLOW_ADDS)) {
            clearSelection(); showMessage(u8("Сюда вставлять нельзя")); return;
        }
        QString err;
        QStringList remaining;
        int done = 0;
        for (const QString &src : m_clip) {
            const QFileInfo fi(src);
            if (!safeLocalPath(m_root, src) || (m_clipCut ? isProtected(src) : readOnlyLocalEntry(m_root, m_trash, src, ALLOW_ADDS))) {
                err = u8("Недоступный или защищённый путь: ") + fi.fileName();
                remaining << src; continue;
            }
            const bool isDir = fi.isDir();
            if (isDir && pathWithin(m_path, src)) {
                err = u8("Папку нельзя вставить в саму себя");
                remaining << src; continue;
            }
            if (m_clipCut && fi.absolutePath() == m_path) { ++done; continue; }
            if (!m_clipCut && !safeCopyTree(m_root, src)) {
                err = u8("Не удалось копировать: ссылка или недоступный файл в ") + fi.fileName();
                remaining << src; continue;
            }
            const QString dst = uniqueName(m_path, fi.fileName(), isDir);
            bool ok;
            if (m_clipCut) {
                ok = moveLocalEntry(m_root, src, dst, err, false, ALLOW_ADDS);
            } else {
                ok = copyStagedTree(m_root, src, dst, err, nullptr, ALLOW_ADDS);
            }
            if (ok) ++done; else remaining << src;
        }
        ::sync();
        if (m_clipCut) m_clip = remaining;
        klog(u8("paste %1: completed=%2 failed=%3 destination=%4 error=%5")
             .arg(m_clipCut ? u8("move") : u8("copy")).arg(done).arg(remaining.size()).arg(m_path).arg(err));
        load();
        if (!err.isEmpty()) { clearSelection(); showMessage(err); }
    }

    // Архив создаётся рядом с выбранными элементами, исходники не меняются.
    void zipSelected() { startWork(u8("Создание архива…"), [this]() { zipSelectedWork(); }); }
    void zipSelectedWork() {
        QStringList sources;
        for (const QString &path : m_selectionOrder)
            if (m_marked.contains(path)) sources << path;
        if (sources.isEmpty()) return;
        const QString dest = archiveDestination(m_path, QFileInfo(sources.first()).fileName());
        QString error;
        klog(u8("zip create start: count=%1 destination=%2").arg(sources.size()).arg(dest));
        if (!createZipArchive(m_root, dest, sources, error, ALLOW_ADDS)) {
            klog(u8("zip create failed: ") + error); clearSelection(); showMessage(error); return;
        }
        klog(u8("zip create complete: ") + dest);
        ::sync();
        m_marked.clear(); m_selectionOrder.clear();
        load();
        showMessage(u8("Создан архив: ") + QFileInfo(dest).fileName());
    }

    void openTrash() {
        if (!safeLocalPath(m_root, m_trash, true) || !QDir().mkpath(m_trash)
            || !safeLocalPath(m_root, m_trash)) {
            showMessage(u8("Корзина недоступна")); return;
        }
        if (!pathWithin(m_path, m_trash)) m_trashReturn = m_path;
        m_menuRequested = false; m_actions->hide();
        m_path = m_trash;
        m_marked.clear(); m_selectionOrder.clear();
        load();
    }

    void leaveSettingsForTrash(bool empty) {
        if (m_setVals != m_initialSetVals || m_resetHomeDraft) {
            m_pendingDelete = empty ? 6 : 5;
            m_confirmText->setText(u8("Несохранённые настройки будут потеряны. Продолжить?"));
            showConfirmation();
            return;
        }
        showSettings(false);
        if (empty) requestDeletion(2); else openTrash();
    }

    void requestDeletion(int action) {
        if (action == 1 && m_marked.isEmpty()) return;
        m_pendingDelete = action;
        m_deleteSnapshot = m_selectionOrder;
        m_confirmText->setText(action == 2
            ? u8("Очистить корзину? Все её элементы будут удалены безвозвратно.")
            : u8("Удалить выбранные элементы безвозвратно: %1?").arg(m_deleteSnapshot.size()));
        showConfirmation();
    }

    void confirmDeletion() {
        const int action = m_pendingDelete;
        m_pendingDelete = 0;
        m_confirm->hide(); m_pickerShade->hide();
        if ((action == 1 || action == 2) && !m_busy) {
            m_pendingDelete = action;
            startWork(action == 2 ? u8("Очистка корзины…") : u8("Удаление файлов…"), [this]() { confirmDeletion(); });
            return;
        }
        if (action == 9) {
            const QString path = m_pendingZip; m_pendingZip.clear();
            if (!path.isEmpty()) extractSelectedZip(path);
            return;
        }
        if (action == 5 || action == 6) {
            showSettings(false);
            if (action == 6) requestDeletion(2); else openTrash();
            return;
        }
        if (action == 8) {
            if (m_confirmSetting >= 0 && m_confirmSetting < m_setVals.size()) {
                m_setVals[m_confirmSetting] = u8("1");
                refreshSettings(); saveSettings();
            }
            m_confirmSetting = -1;
            return;
        }
        if (action == 4 || action == 7) {
            QStringList keys, defaults;
            for (int i = 0; i < SET_COUNT; ++i) {
                keys << u8(SET_ITEMS[i].key);
                defaults << cfgDefault(u8(SET_ITEMS[i].key));
            }
            const int tab = action == 4 ? -1 : m_resetTabPending;
            resetSettingsDraft(keys, defaults, m_setVals, tab);
            if (tab == -1 || tab == 0) {
                m_resetHomeDraft = true;
                updateHomeDraft();
            }
            refreshSettings(); saveSettings();
            return;
        }
        if (action == 3) {
            const QString logs = QFileInfo(u8(KOBROWSE_LOG)).absolutePath();
            const QString legacy = u8(KOBROWSE_ROOT) + u8("/.adds/kobrowse/kobrowse.log");
            bool ok = true;
            if (QFileInfo(logs).exists() || QFileInfo(logs).isSymLink())
                ok = safeLocalPath(m_root, logs) && removeTreeNoFollow(m_root, logs);
            if (QFileInfo(legacy).exists() || QFileInfo(legacy).isSymLink())
                ok = safeLocalPath(m_root, legacy) && QFile::remove(legacy) && ok;
            showMessage(ok ? u8("Журнал очищен") : u8("Не удалось очистить журнал"));
            return;
        }
        if (action != 1 && action != 2) return;
        QStringList paths;
        if (action == 2) {
            if (!safeLocalPath(m_root, m_trash) || !QFileInfo(m_trash).isDir()) {
                showMessage(u8("Корзина недоступна или пуста")); return;
            }
            QFileInfoList entries;
            if (!operationEntries(m_trash, entries)) {
                showMessage(u8("Не удалось прочитать содержимое корзины")); return;
            }
            for (const QFileInfo &item : entries) paths << item.absoluteFilePath();
        } else paths = m_deleteSnapshot;
        QSet<QString> failed;
        for (const QString &path : paths) {
            if (!mayDeleteLocalEntry(m_root, m_trash, path, ALLOW_ADDS) || !removeTreeNoFollow(m_root, path)) {
                failed.insert(path); continue;
            }
            m_clip.removeAll(path);
            m_marked.remove(path);
            m_selectionOrder.removeAll(path);
        }
        ::sync();
        klog(u8("delete action=%1 total=%2 failed=%3").arg(action).arg(paths.size()).arg(failed.size()));
        if (action == 1) clearSelection();
        load();
        showMessage(failed.isEmpty() ? u8("Удалено: %1").arg(paths.size())
                                   : u8("Не удалено: %1").arg(failed.size()));
    }

    // В корзину: перенос в /mnt/onboard/.kobrowse_trash с датой в начале имени
    void moveToTrash() { startWork(u8("Перемещение в корзину…"), [this]() { moveToTrashWork(); }); }
    void moveToTrashWork() {
        if (!safeLocalPath(m_root, m_trash, true) || !QDir().mkpath(m_trash)
            || !safeLocalPath(m_root, m_trash)) {
            clearSelection(); showMessage(u8("Корзина недоступна")); return;
        }
        const QString stamp = QDateTime::currentDateTime().toString(u8("yyyyMMdd-hhmmss_"));
        QSet<QString> remaining;
        for (const QString &src : m_marked) {
            if (isProtected(src) || !safeLocalPath(m_root, src)) { remaining.insert(src); continue; }
            const QFileInfo fi(src);
            const QString dst = uniqueName(m_trash, stamp + fi.fileName(), fi.isDir());
            QString error;
            if (!moveLocalEntry(m_root, src, dst, error, true, ALLOW_ADDS)) remaining.insert(src);
            else m_clip.removeAll(src);
        }
        ::sync();
        klog(u8("trash move total=%1 failed=%2").arg(m_marked.size()).arg(remaining.size()));
        clearSelection();
        load();
        if (!remaining.isEmpty()) showMessage(u8("Не перенесено в корзину: %1").arg(remaining.size()));
    }

    void updateSortHeader() {
        const char *labels[] = { "Название", "Дата", "Размер" };
        for (int n = 0; n < m_sortButtons.size(); ++n)
            m_sortButtons[n]->setText((n == int(m_sort) ? (m_sortAscending ? u8("↑  ") : u8("↓  ")) : QString()) + u8(labels[n]));
    }
    void setSort(BrowserSort column) {
        if (column == m_sort) m_sortAscending = !m_sortAscending;
        else { m_sort = column; m_sortAscending = true; }
        updateSortHeader(); load();
    }

    // Прочитать текущую папку и заполнить список
    void load() {
        m_emptyTrashButton->setVisible(m_path == m_trash);
        m_deviceStatus->refresh();
        m_pathLabel->setPath(m_root, m_path);
        m_fileCount = 0; m_dirCount = 0; m_pageIndex = 0; m_pageStarts.clear();
        m_list->clear();
        updateActions();

        QDir dir(m_path);
        if (!dir.isReadable()) {
            new QTreeWidgetItem(m_list, QStringList() << u8("Нет доступа к папке"));
            m_pageTimer.start(0);
            return;
        }

        // Папки сверху, по имени без учёта регистра; список читается разом,
        // дескрипторы на /mnt/onboard не удерживаются (важно для USB-режима)
        const QFileInfoList qtEntries = browserQtEntries(dir, m_showHidden);
        QFileInfoList entries = completeBrowserEntries(dir, qtEntries, m_showHidden);
        sortBrowserEntries(entries, m_sort, m_sortAscending);
        if (m_path == m_root && !m_rootDiagLogged) {
            m_rootDiagLogged = true;
            bool qtAdds = false, qtDisabled = false, shownAdds = false, shownDisabled = false;
            bool fsAdds = false, fsDisabled = false;
            for (const QFileInfo &fi : qtEntries) {
                if (fi.fileName() == u8(".adds")) qtAdds = true;
                if (fi.fileName() == u8(".disabled")) qtDisabled = true;
            }
            for (const QFileInfo &fi : entries) {
                if (fi.fileName() == u8(".adds")) shownAdds = true;
                if (fi.fileName() == u8(".disabled")) shownDisabled = true;
            }
            int dirError = 0;
            DIR *raw = ::opendir(QFile::encodeName(m_root).constData());
            if (raw) {
                errno = 0;
                while (struct dirent *entry = ::readdir(raw)) {
                    if (strcmp(entry->d_name, ".adds") == 0) fsAdds = true;
                    if (strcmp(entry->d_name, ".disabled") == 0) fsDisabled = true;
                }
                dirError = errno;
                ::closedir(raw);
            } else dirError = errno;
            klog(u8("root listing hidden=%1 qt_adds=%2 fs_adds=%3 exists_adds=%4 "
                    "qt_disabled=%5 fs_disabled=%6 exists_disabled=%7 dir_errno=%8 shown_adds=%9 shown_disabled=%10")
                 .arg(m_showHidden).arg(qtAdds).arg(fsAdds).arg(QFileInfo(m_root + u8("/.adds")).exists())
                 .arg(qtDisabled).arg(fsDisabled).arg(QFileInfo(m_root + u8("/.disabled")).exists()).arg(dirError)
                 .arg(shownAdds).arg(shownDisabled));
        }

        if (m_path != m_root) {
            QTreeWidgetItem *parent = new QTreeWidgetItem(m_list);
            parent->setText(0, m_path == m_trash ? u8("Закрыть корзину") : u8(".."));
            parent->setData(0, ROLE_DIR, true);
            parent->setData(0, ROLE_PARENT, true);
            parent->setData(0, ROLE_DETAIL, m_path == m_trash ? u8("Вернуться к файлам") : u8("Родительская папка"));
        }
        for (const QFileInfo &fi : entries) {
            if (fi.fileName().endsWith(u8(".kobrowse_tmp"))) continue; // недокопированные файлы не показываем
            auto *it = new QTreeWidgetItem(m_list);
            const bool isDir = fi.isDir();
            if (isDir) ++m_dirCount; else ++m_fileCount;
            const QString path = fi.absoluteFilePath();
            it->setText(0, fi.fileName()); // имя целиком, с расширением; папки — полужирным
            QString detail = fi.lastModified().toString(u8("dd.MM.yyyy hh:mm"));
            if (!isDir && !fi.isSymLink()) detail += u8("  ·  ") + humanSize(fi.size());
            if (fi.isSymLink()) detail += u8("  ·  → ") + fi.symLinkTarget();
            const bool readOnly = readOnlyLocalEntry(m_root, m_trash, path, ALLOW_ADDS);
            if (readOnly) detail += u8("  ·  защищено");
            it->setData(0, ROLE_DETAIL, detail);
            it->setData(0, ROLE_DATE, fi.lastModified().toString(u8("dd.MM.yy\nhh:mm")));
            it->setData(0, ROLE_SIZE, isDir ? QString() : fi.isSymLink() ? u8("—") : humanSize(fi.size()));
            it->setData(0, ROLE_PATH, path);
            it->setData(0, ROLE_DIR, isDir);
            it->setData(0, ROLE_READONLY, readOnly);
            it->setData(0, ROLE_KIND, int(isDir ? FileGeneric : fileKind(fi.fileName())));
            if (m_marked.contains(path)) setMarkedLook(it, true);
        }
        m_list->scrollToTop();
        m_list->doItemsLayout();
        // Set the first page before Qt paints the newly loaded model, avoiding
        // an extra intermediate full-list repaint during navigation.
        m_pageTimer.stop();
        if (m_list->isVisible() && m_list->viewport()->height() > 1) recalculatePages();
        else m_pageTimer.start(0);
    }

    void recalculatePages() {
        if (!m_list || !m_list->isVisible() || m_list->viewport()->height() <= 1) return;
        const int anchor = m_pageStarts.empty() ? 0 : m_pageStarts[std::min(m_pageIndex, int(m_pageStarts.size()) - 2)];
        const int available = m_list->viewport()->height();
        m_fileDelegate->setMaxRowHeight(available);
        std::vector<int> heights;
        const QStyleOptionViewItem option;
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            heights.push_back(m_fileDelegate->sizeHint(option, m_list->model()->index(i, 0)).height());
        m_pageStarts = pageBoundaries(heights, available);
        m_pageIndex = 0;
        for (int i = 0; i + 1 < int(m_pageStarts.size()); ++i)
            if (anchor >= m_pageStarts[i] && anchor < m_pageStarts[i + 1]) { m_pageIndex = i; break; }
        applyPage();
    }
    void applyPage() {
        if (m_pageStarts.size() < 2) return;
        m_pageIndex = qBound(0, m_pageIndex, int(m_pageStarts.size()) - 2);
        const int begin = m_pageStarts[m_pageIndex], end = m_pageStarts[m_pageIndex + 1];
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = m_list->topLevelItem(i);
            const bool hidden = i < begin || i >= end;
            if (item->isHidden() != hidden) item->setHidden(hidden);
        }
        m_list->doItemsLayout(); m_list->scrollToTop();
        updatePageInfo();
    }
    void turnPage(int direction) {
        if (m_pageStarts.size() < 2) return;
        const int next = qBound(0, m_pageIndex + direction, int(m_pageStarts.size()) - 2);
        if (next != m_pageIndex) { m_pageIndex = next; applyPage(); }
    }
    void updatePageInfo() {
        if (!m_pageLabel || !m_countLabel || !m_prev || !m_next) return;
        const int total = qMax(1, int(m_pageStarts.size()) - 1);
        m_prev->setEnabled(m_pageIndex > 0); m_next->setEnabled(m_pageIndex + 1 < total);
        const QFontMetrics pageFont(m_pageLabel->font());
        int digitWidth = 0;
        for (char digit = '0'; digit <= '9'; ++digit) digitWidth = qMax(digitWidth, pageFont.width(QLatin1Char(digit)));
        const int digits = QString::number(total).size();
        m_pageLabel->setFixedWidth(2 * digits * digitWidth + pageFont.width(u8(" / ")) + 2);
        m_pageLabel->setText(u8("%1 / %2").arg(m_pageIndex + 1).arg(total));
        m_countLabel->setCounts(m_fileCount, m_dirCount);
    }

    // Нажатие на строку: папка — войти; файлы — открытие на следующем этапе
    void onItem(QTreeWidgetItem *it) {
        if (!it) return;
        if (it->data(0, ROLE_PARENT).toBool()) { goUp(); return; }
        const QString path = it->data(0, ROLE_PATH).toString();
        if (path.isEmpty()) return;
        if (!safeLocalPath(m_root, path)) { showMessage(u8("Ссылка или путь вне накопителя недоступен")); return; }
        if (it->data(0, ROLE_DIR).toBool()) {
            if (path == m_trash && !pathWithin(m_path, m_trash)) m_trashReturn = m_path;
            m_path = path;
            m_marked.clear(); m_selectionOrder.clear();
            load();
            return;
        }
        // The internal reader stages and verifies the entire archive; the ZIP
        // itself is always kept, whether extraction succeeds or fails.
        if (path.endsWith(u8(".zip"), Qt::CaseInsensitive)) {
            m_pendingZip = path; m_pendingDelete = 9;
            m_confirmText->setText(u8("Распаковать архив «%1»?").arg(QFileInfo(path).fileName()));
            showConfirmation(); return;
        }
        // Файл — открыть читалкой, выбранной в Kobalt («Автоматически открывать книги в Kobalt»)
        openFile(path);
    }

    void extractSelectedZip(const QString &path) {
        startWork(u8("Распаковка архива…"), [this, path]() { extractSelectedZipWork(path); });
    }
    void extractSelectedZipWork(const QString &path) {
        QString output, error;
        klog(u8("zip extract start: ") + path);
        if (!extractZipArchive(m_root, path, output, error, ALLOW_ADDS)) {
            klog(u8("zip extract failed: ") + error);
            m_operationDetail = u8("Не удалось распаковать: ") + error;
            if (m_operationDetail.size() > 800) m_operationDetail = m_operationDetail.left(760) + u8("… (подробности в журнале)");
            load(); showMessage(u8("Ошибка распаковки; подробности выше"));
        } else {
            ::sync(); klog(u8("zip extract complete: ") + output);
            load(); showMessage(u8("Архив распакован в текущую папку"));
        }
    }

    // Открыть книгу. Окно менеджера сначала прячется (иначе останется поверх читалки);
    // при ошибке показывается снова с сообщением, при успехе — закрывается.
    void openFile(const QString &path) {
        const QString denial = bookOpenDenial();
        if (!denial.isEmpty()) { showMessage(denial); return; }
        hide();
        QString err;
        if (openBook(path, true, err)) {
            close();
        } else {
            show();
            raise();
            showMessage(err);
        }
    }

    // ----- экран «Настройки» -----
    QPushButton *makeButton(QHBoxLayout *row, const QFont &f, const char *text, int minH, int stretch) {
        auto *b = new QPushButton(this);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(minH);
        auto *bl = new QHBoxLayout(b);
        bl->setContentsMargins(0, 0, 0, 0);
        auto *face = new ButtonFace(I_NONE, u8(text), b);
        face->setFont(f);
        connect(b, &QPushButton::pressed, face, [face]() { face->update(); });
        connect(b, &QPushButton::released, face, [face]() { face->update(); });
        bl->addWidget(face);
        row->addWidget(b, stretch);
        return b;
    }

    // Кнопка «−»/«+» строки настроек. Номер строки и направление хранятся в свойствах:
    // их читает tapAt и вызывает stepSetting напрямую (сигнал clicked на e-ink не всегда доходит).
    QPushButton *makeStepButton(QHBoxLayout *row, const QFont &f, const char *text,
                                int h, int w, int idx, int dir) {
        auto *b = new SettingStepButton(u8(text), row->parentWidget());
        QFont sf = f;
        sf.setPixelSize(qMax(16, qMin(f.pixelSize() > 0 ? f.pixelSize() : 24, int(h * 0.55))));
        b->setFont(sf);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(h);
        b->setFixedWidth(w);
        b->setProperty("set_row", idx);
        b->setProperty("set_dir", dir);
        connect(b, &QPushButton::clicked, this, [this, idx, dir]() { stepSetting(idx, dir); });
        row->addWidget(b, 0);
        m_stepBtns << b;
        return b;
    }

    void buildSettings(const QFont &fText, const QFont &fBtn) {
        m_settings = new QWidget(this);
        QVBoxLayout *v = new QVBoxLayout(m_settings);
        v->setContentsMargins(0, 0, 0, 0); v->setSpacing(GAP);
        QHBoxLayout *header = new QHBoxLayout();
        QLabel *title = new QLabel(u8("Настройки"), m_settings);
        QFont titleFont = fText; titleFont.setBold(true); title->setFont(titleFont);
        header->addWidget(title); header->addStretch(1);
        ChromeButton *closeSettings = new ChromeButton(I_CLOSE, QString(), m_settings);
        closeSettings->setIconScale(1.5);
        closeSettings->setObjectName(u8("closeSettings")); closeSettings->setAccessibleName(u8("Закрыть настройки"));
        const int headSize = qMax(48, qRound(72 * SCREEN_DPI / 300.0));
        closeSettings->setFixedSize(headSize, headSize); header->addWidget(closeSettings);
        connect(closeSettings, &QPushButton::clicked, [this]() { showSettings(false); });
        v->addLayout(header);
        QHBoxLayout *tabs = new QHBoxLayout();
        m_generalTab = makeButton(tabs, fBtn, "Общие", qMax(40, BTN_HEIGHT / 2), 1);
        m_fontTab = makeButton(tabs, fBtn, "Шрифты", qMax(40, BTN_HEIGHT / 2), 1);
        v->addLayout(tabs);
        connect(m_generalTab, &QPushButton::clicked, [this]() { setSettingsTab(0); });
        connect(m_fontTab, &QPushButton::clicked, [this]() { setSettingsTab(1); });

        m_settingsScroll = new QScrollArea(m_settings);
        m_settingsScroll->setWidgetResizable(true);
        m_settingsScroll->setFrameShape(QFrame::NoFrame);
        m_settingsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QWidget *content = new QWidget(m_settingsScroll);
        QVBoxLayout *rows = new QVBoxLayout(content);
        const double sc = SCREEN_DPI / 300.0;
        const int controlInset = qMax(8, qRound(16 * sc));
        rows->setContentsMargins(controlInset, 0, controlInset, 0); rows->setSpacing(qMax(16, GAP * 2));
        const int rowH = qMax(48, qRound(80 * sc));
        m_fonts << QString() << QFontDatabase().families();
        for (int i = 0; i < SET_COUNT; ++i) {
            m_setLabels << static_cast<QLabel *>(nullptr);
            m_switches << static_cast<SettingSwitch *>(nullptr);
            m_sliders << static_cast<SettingSlider *>(nullptr);
            m_settingRows << static_cast<QWidget *>(nullptr);
            m_settingLines << static_cast<QWidget *>(nullptr);
            m_minusBtns << static_cast<QPushButton *>(nullptr);
            m_plusBtns << static_cast<QPushButton *>(nullptr);
        }
        const char *headings[] = { "Файлы", "Корзина", "Поведение", "Открытие книг", "Отладка", "Список файлов" };
        for (int g = 0; g < GroupCount; ++g) {
            const SettingGroup group = static_cast<SettingGroup>(g);
            QWidget *block = new QWidget(content);
            QVBoxLayout *groupLayout = new QVBoxLayout(block);
            groupLayout->setContentsMargins(0, 0, 0, 0); groupLayout->setSpacing(qMax(2, GAP / 3));
            QWidget *body = new QWidget(block);
            QVBoxLayout *items = new QVBoxLayout(body);
            items->setContentsMargins(0, 0, 0, 0); items->setSpacing(qMax(4, qRound(6 * sc)));
            if (settingGroupAdvanced(group)) {
                QHBoxLayout *headingRow = new QHBoxLayout();
                QPushButton *expand = makeButton(headingRow, fBtn, headings[g], rowH, 1);
                m_advancedButtons[g] = expand;
                connect(expand, &QPushButton::clicked, [this, g]() {
                    m_groupExpanded[g] = !m_groupExpanded[g];
                    updateAdvancedGroup(static_cast<SettingGroup>(g));
                });
                groupLayout->addLayout(headingRow);
            } else {
                QLabel *heading = new QLabel(u8(headings[g]), block);
                heading->setWordWrap(true); heading->setFont(titleFont);
                heading->setMinimumHeight(rowH / 2);
                groupLayout->addWidget(heading);
            }
            for (int i = 0; i < SET_COUNT; ++i) {
                if (settingGroup(u8(SET_ITEMS[i].key)) != group) continue;
                QWidget *line = new QWidget(body);
                line->setMinimumHeight(rowH);
                QHBoxLayout *r = new QHBoxLayout(line);
                r->setContentsMargins(0, 0, 0, 0); r->setSpacing(qMax(4, qRound(10 * sc)));
                QLabel *name = new QLabel(u8(SET_ITEMS[i].label), line);
                name->setFont(fText); name->setWordWrap(true); name->setMinimumWidth(0);
                name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                if (isFontSizeSetting(u8(SET_ITEMS[i].key))) {
                    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                    r->addWidget(name, 1);
                } else {
                    QVBoxLayout *textColumn = new QVBoxLayout(); textColumn->setContentsMargins(0, 0, 0, 0); textColumn->setSpacing(2);
                    textColumn->addWidget(name);
                    const QString key = u8(SET_ITEMS[i].key);
                    const QString hint = key == u8("allow_adds_edit") ? u8("Изменение файлов приложений может нарушить их работу.")
                        : key == u8("show_hidden") ? u8("Показывать имена, начинающиеся с точки.")
                        : key == u8("trash_enabled") ? u8("При выключенной корзине файлы удаляются безвозвратно после подтверждения.")
                        : key == u8("open_books") ? u8("Авто: только прошивки 4.38.x; Всегда: на свой риск; Никогда: не открывать.")
                        : key == u8("logging_enabled") ? u8("Журнал может содержать пути и имена файлов.") : QString();
                    if (!hint.isEmpty()) {
                        QLabel *description = new QLabel(hint, line); description->setWordWrap(true);
                        description->setMinimumWidth(0); description->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                        QFont descriptionFont = fText; descriptionFont.setPixelSize(qMax(12, FONT_MSG * 3 / 4 + ptToPx(2)));
                        description->setFont(descriptionFont); textColumn->addWidget(description);
                    }
                    r->addLayout(textColumn, 1);
                }
                QLabel *val = nullptr;
                SettingSwitch *toggle = nullptr;
                if (SET_ITEMS[i].type == S_BOOL) {
                    toggle = new SettingSwitch(line);
                    toggle->setFixedSize(qMax(55, qRound(120 * sc)), rowH);
                    toggle->setAccessibleName(u8(SET_ITEMS[i].label));
                    r->addWidget(toggle);
                    connect(toggle, &QPushButton::clicked, this, [this, i]() { stepSetting(i, 1); });
                } else if (isFontSizeSetting(u8(SET_ITEMS[i].key))) {
                    QHBoxLayout *controls = new QHBoxLayout();
                    controls->setContentsMargins(0, 0, 0, 0); controls->setSpacing(qMax(4, qRound(10 * sc)));
                    r->addLayout(controls, 2);
                    const int bw = qMax(48, qRound(72 * sc));
                    m_minusBtns[i] = makeStepButton(controls, fBtn, "−", rowH, bw, i, -1);
                    SettingSlider *slider = new SettingSlider(SET_ITEMS[i].lo, SET_ITEMS[i].hi, SET_ITEMS[i].step, line, sc);
                    slider->setAccessibleName(u8(SET_ITEMS[i].label)); m_sliders[i] = slider;
                    slider->changed = [this, i](double value) {
                        m_setVals[i] = QString::number(value, 'f', SET_ITEMS[i].dec); refreshSettings();
                    };
                    slider->finished = [this]() { saveSettings(); };
                    controls->addWidget(slider, 1);
                    m_plusBtns[i] = makeStepButton(controls, fBtn, "+", rowH, bw, i, +1);
                    val = name;
                } else if (SET_ITEMS[i].type == S_OPEN) {
                    QHBoxLayout *segments = new QHBoxLayout();
                    segments->setContentsMargins(0, 0, 0, 0); segments->setSpacing(0);
                    const char *labels[] = { "Авто", "Всегда", "Никогда" };
                    const char *values[] = { "auto", "1", "0" };
                    for (int n = 0; n < 3; ++n) {
                        SettingChoiceButton *button = new SettingChoiceButton(u8(labels[n]), fBtn, false, line);
                        button->setObjectName(u8("bookMode_%1").arg(n));
                        button->setMinimumHeight(rowH);
                        m_bookModes << button;
                        segments->addWidget(button, 1);
                        const QString value = u8(values[n]);
                        connect(button, &QPushButton::clicked, [this, i, value]() {
                            if (m_setVals[i] == value) return;
                            m_setVals[i] = value; refreshSettings(); saveSettings();
                        });
                    }
                    r->addLayout(segments, 2);
                }
                m_setLabels[i] = val; m_switches[i] = toggle;
                items->addWidget(line);
                if (SET_ITEMS[i].type == S_FONT) {
                    for (int n = 0; n < m_fonts.size(); ++n) {
                        const QString family = m_fonts[n];
                        QFont preview = family.isEmpty() ? font() : QFont(family);
                        preview.setPixelSize(fText.pixelSize());
                        SettingChoiceButton *button = new SettingChoiceButton(
                            family.isEmpty() ? u8("Kobo") : family, preview, true, body);
                        button->setObjectName(u8("fontChoice_%1").arg(n));
                        m_fontChoices << button;
                        items->addWidget(button);
                        connect(button, &QPushButton::clicked, [this, i, family]() {
                            if (m_setVals[i].compare(family, Qt::CaseInsensitive) == 0) return;
                            m_setVals[i] = family; refreshSettings(); saveSettings();
                        });
                    }
                }
                QFrame *separator = new QFrame(body); separator->setFrameShape(QFrame::HLine);
                separator->setFrameShape(QFrame::NoFrame);
                separator->setFixedHeight(qMax(1, qRound(1.5 * sc)));
                separator->setStyleSheet(u8("background:black;")); items->addWidget(separator);
                m_settingRows[i] = line; m_settingLines[i] = separator;
            }
            if (group == GroupFiles) {
                m_homeStatus = new QLabel(body);
                m_homeStatus->setFont(fText); m_homeStatus->setWordWrap(true);
                m_homeStatus->setTextFormat(Qt::PlainText); m_homeStatus->setMinimumWidth(0);
                m_homeStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                QHBoxLayout *actions = new QHBoxLayout(); actions->addWidget(m_homeStatus, 1);
                m_resetHomeButton = makeButton(actions, fBtn, "Сбросить домашнюю папку", rowH, 0);
                m_resetHomeButton->setMinimumWidth(QFontMetrics(fBtn).width(u8("Сбросить домашнюю папку")) + 2 * BTN_MARGIN + 8);
                connect(m_resetHomeButton, &QPushButton::clicked, [this]() {
                    m_resetHomeDraft = true; saveSettings(); updateHomeDraft();
                });
                items->addLayout(actions);
            } else if (group == GroupLogging) {
                QHBoxLayout *actions = new QHBoxLayout();
                QPushButton *clearLogs = makeButton(actions, fBtn, "Очистить журнал", rowH, 0);
                clearLogs->setObjectName(u8("settingsClearLog"));
                clearLogs->setFixedWidth(QFontMetrics(fBtn).width(u8("Очистить журнал")) + 2 * BTN_MARGIN + 8); actions->addStretch(1);
                connect(clearLogs, &QPushButton::clicked, [this]() {
                    m_pendingDelete = 3;
                    m_confirmText->setText(u8("Очистить журнал Kobrowse? Это действие нельзя отменить."));
                    showConfirmation();
                });
                items->addLayout(actions);
            }
            groupLayout->addWidget(body);
            m_groupBlocks[g] = block; m_groupBodies[g] = body;
            rows->addWidget(block);
            if (settingGroupAdvanced(group)) updateAdvancedGroup(group);
        }
        rows->addStretch(1);
        QHBoxLayout *resets = new QHBoxLayout();
        resets->setContentsMargins(controlInset, 0, controlInset, 0);
        m_resetTabButton = makeButton(resets, fBtn, "Сбросить общие настройки", rowH, 0);
        m_resetTabButton->setObjectName(u8("resetTab"));
        m_resetTabButton->setProperty("baseRowHeight", rowH);
        resets->addStretch(1);
        connect(m_resetTabButton, &QPushButton::clicked, [this]() {
            m_pendingDelete = 7; m_resetTabPending = m_currentSettingsTab;
            m_confirmText->setText(m_currentSettingsTab == 0
                ? u8("Сбросить общие настройки? Редактирование .adds будет выключено.")
                : u8("Сбросить настройки шрифтов?"));
            showConfirmation();
        });
        m_settingsScroll->setWidget(content); v->addWidget(m_settingsScroll, 1);
        m_settingsNotice = new FooterNotice(m_settings); m_settingsNotice->setFont(fText);
        m_settingsNotice->setFixedHeight(QFontMetrics(fText).lineSpacing() * 2 + 4);
        v->addWidget(m_settingsNotice); m_settingsNotice->hide();
        v->addLayout(resets);
        setSettingsTab(0);
        m_settings->hide();
    }

    void updateHomeDraft() {
        setButtonFace(m_resetHomeButton, I_NONE,
            m_resetHomeDraft ? u8("Сброс домашней: да") : u8("Сбросить домашнюю папку"));
        if (m_homeStatus) {
            const QString path = m_resetHomeDraft ? m_root : readHomeFolder(m_root, u8(KOBROWSE_HOME));
            m_homeStatus->setText((m_resetHomeDraft ? u8("После сохранения: ") : u8("Домашняя: ")) + path);
        }
    }

    void updateAdvancedGroup(SettingGroup group) {
        const int g = int(group);
        if (!settingGroupAdvanced(group) || !m_advancedButtons[g] || !m_groupBodies[g]) return;
        m_groupBodies[g]->setVisible(m_groupExpanded[g]);
        const QString title = u8("Дополнительно");
        setButtonFace(m_advancedButtons[g], I_NONE, (m_groupExpanded[g] ? u8("− ") : u8("+ ")) + title);
    }

    void updateResetGeometry() {
        if (!m_resetTabButton || !m_settingsScroll) return;
        ButtonFace *face = m_resetTabButton->findChild<ButtonFace *>();
        if (!face) return;
        const QFontMetrics fm(face->font());
        const int desired = qMax(fm.width(u8("Сбросить общие настройки")),
                                 fm.width(u8("Сбросить настройки шрифтов"))) + 2 * BTN_MARGIN + 8;
        const QMargins margins = m_settingsScroll->widget() && m_settingsScroll->widget()->layout()
            ? m_settingsScroll->widget()->layout()->contentsMargins() : QMargins();
        const int available = qMax(1, m_settingsScroll->viewport()->width() - margins.left() - margins.right());
        const int buttonWidth = qMin(desired, available);
        m_resetTabButton->setFixedWidth(buttonWidth);
        const int textHeight = fm.boundingRect(0, 0, qMax(1, buttonWidth - 2 * BTN_MARGIN), 10000,
                                               Qt::TextWordWrap | Qt::TextWrapAnywhere, face->text()).height();
        m_resetTabButton->setMinimumHeight(qMax(m_resetTabButton->property("baseRowHeight").toInt(), textHeight + 8));
    }

    void setSettingsTab(int tab) {
        m_currentSettingsTab = tab;
        if (m_resetTabButton) setButtonFace(m_resetTabButton, I_NONE, tab == 0
            ? u8("Сбросить общие настройки") : u8("Сбросить настройки шрифтов"));
        updateResetGeometry();
        for (int g = 0; g < GroupCount; ++g)
            m_groupBlocks[g]->setVisible(settingGroupTab(static_cast<SettingGroup>(g)) == tab);
        m_generalTab->setProperty("selectedTab", tab == 0); m_fontTab->setProperty("selectedTab", tab == 1);
        for (QWidget *face : m_generalTab->findChildren<QWidget *>()) face->update();
        for (QWidget *face : m_fontTab->findChildren<QWidget *>()) face->update();
        m_settingsScroll->verticalScrollBar()->setValue(0);
    }

    void showSettings(bool on) {
        if (on) {
            m_resetHomeDraft = false;
            updateHomeDraft();
            const bool configSafe = safeLocalPath(m_root, u8(KOBROWSE_INI), true);
            QSettings st(u8(KOBROWSE_INI), QSettings::IniFormat);
            st.setIniCodec(QTextCodec::codecForName("UTF-8"));
            m_setVals.clear();
            for (int i = 0; i < SET_COUNT; ++i) {
                QString v = configSafe ? st.value(u8("kobrowse/") + u8(SET_ITEMS[i].key),
                                                  cfgDefault(u8(SET_ITEMS[i].key))).toString().trimmed()
                                       : cfgDefault(u8(SET_ITEMS[i].key));
                // Числовое значение из старого ini могло выйти за новые пределы
                if (SET_ITEMS[i].type == S_NUM) {
                    bool ok = false;
                    double x = QString(v).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
                    if (ok) {
                        const double c = qBound(SET_ITEMS[i].lo, x, SET_ITEMS[i].hi);
                        if (c != x) v = QString::number(c, 'f', SET_ITEMS[i].dec);
                    }
                }
                m_setVals << v;
            }
            m_initialSetVals = m_setVals;
            refreshSettings();
            setSettingsTab(false);
        }
        m_list->setVisible(!on);
        m_pathHeader->setVisible(!on);
        m_nav->setVisible(!on);
        m_toolbar->setVisible(!on);
        m_footerLine->setVisible(!on);
        m_info->hide();
        if (m_footerNotice) m_footerNotice->setMessage(QString());
        if (m_settingsNotice) { m_settingsNotice->setMessage(QString()); m_settingsNotice->hide(); }
        m_settings->setVisible(on);
        if (on) {
            if (m_menuRequested) {
                m_marked.clear(); m_selectionOrder.clear();
                m_deviceStatus->setSelectionCount(0);
                for (int i = 0; i < m_list->topLevelItemCount(); ++i)
                    setMarkedLook(m_list->topLevelItem(i), false);
                m_list->viewport()->update();
            }
            m_menuRequested = false; m_actions->hide(); m_setGeom.start();
        } else { updateActions(); m_pageTimer.start(0); }
    }

    // Диагностика: геометрия строк настроек после раскладки (пишется в kobrowse.log).
    void logSettingsGeometry() {
        if (!m_settings || !m_settings->isVisible() || m_stepBtns.isEmpty()) return;
        klog(u8("settings geom: box %1x%2 at %3,%4, rows %5")
                 .arg(m_settings->width()).arg(m_settings->height())
                 .arg(m_settings->x()).arg(m_settings->y())
                 .arg(m_stepBtns.size() / 2));
        for (int i = 0; i + 1 < m_stepBtns.size(); i += 2) {
            const QRect a(m_stepBtns.at(i)->mapTo(this, QPoint(0, 0)), m_stepBtns.at(i)->size());
            const QRect b(m_stepBtns.at(i + 1)->mapTo(this, QPoint(0, 0)), m_stepBtns.at(i + 1)->size());
            klog(u8("settings row %1: - %2,%3 %4x%5 ; + %6,%7 %8x%9")
                     .arg(i / 2)
                     .arg(a.x()).arg(a.y()).arg(a.width()).arg(a.height())
                     .arg(b.x()).arg(b.y()).arg(b.width()).arg(b.height()));
        }
    }

    // Показать значения в читаемом виде
    void refreshSettings() {
        for (int i = 0; i < SET_COUNT; ++i) {
            const QString &v = m_setVals[i];
            QString shown = v;
            if (SET_ITEMS[i].type == S_BOOL) {
                const bool enabled = v.toDouble() != 0;
                shown = QString::fromLatin1(SET_ITEMS[i].key) == u8("trash_enabled")
                    ? (enabled ? u8("включена") : u8("отключена"))
                    : (enabled ? u8("показывать") : u8("скрывать"));
            }
            if (SET_ITEMS[i].type == S_FONT && v.isEmpty()) shown = u8("Kobo");
            if (SET_ITEMS[i].type == S_OPEN)
                shown = v == u8("1") ? u8("Всегда") : v == u8("0") ? u8("Никогда") : u8("Авто");
            if (SET_ITEMS[i].type == S_NUM) {
                const QString key = u8(SET_ITEMS[i].key);
                const bool autoZero = key == u8("key_font_pt") || key == u8("func_pt");
                shown = settingNumberDisplay(v, SET_ITEMS[i].lo, SET_ITEMS[i].hi, autoZero,
                                             key == u8("list_pt") ? u8(" пт") : QString());
                bool valid = false;
                const double number = QString(v).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&valid);
                if (m_sliders[i]) m_sliders[i]->setValue(valid ? number : SET_ITEMS[i].lo);
                if (m_minusBtns[i]) m_minusBtns[i]->setEnabled(!valid || number > SET_ITEMS[i].lo);
                if (m_plusBtns[i]) m_plusBtns[i]->setEnabled(!valid || number < SET_ITEMS[i].hi);
            }
            if (m_setLabels[i]) {
                if (SET_ITEMS[i].type == S_NUM) {
                    const QString suffix = QString::fromLatin1(SET_ITEMS[i].key) == u8("list_pt") ? u8(" пт") : u8(" мс");
                    m_setLabels[i]->setText(u8(SET_ITEMS[i].label) + u8(": ") + v + suffix);
                } else m_setLabels[i]->setText(shown);

            }
            if (SET_ITEMS[i].type == S_FONT) {
                for (int n = 0; n < m_fontChoices.size(); ++n)
                    m_fontChoices[n]->setSelected(m_fonts[n].compare(v, Qt::CaseInsensitive) == 0);
            } else if (SET_ITEMS[i].type == S_OPEN) {
                const QString mode = v.toLower();
                const int selected = mode == u8("1") ? 1 : mode == u8("0") ? 2 : 0;
                for (int n = 0; n < m_bookModes.size(); ++n) m_bookModes[n]->setSelected(n == selected);
            }
            if (m_switches[i]) m_switches[i]->setChecked(v.toDouble() != 0);
        }
    }

    // [−]/[+]: число на шаг в пределах; переключатель меняет логическое значение.
    void stepSetting(int i, int dir) {
        if (i < 0 || i >= SET_COUNT || i >= m_setVals.size()) return;
        const SetItem &it = SET_ITEMS[i];
        QString &v = m_setVals[i];
        const QString before = v;
        if (it.type == S_BOOL) {
            if (QString::fromLatin1(it.key) == u8("allow_adds_edit") && v.toDouble() == 0) {
                m_confirmSetting = i; m_pendingDelete = 8;
                refreshSettings(); // кнопка уже переключилась до сигнала clicked
                m_confirmText->setText(u8("Разрешить изменение содержимого .adds? Это может повредить установленные приложения. Настройка применится сразу."));
                showConfirmation();
                return;
            }
            v = v.toDouble() != 0 ? u8("0") : u8("1");
        } else if (it.type == S_NUM) {
            bool ok = false;
            double x = QString(v).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
            if (!ok) x = cfgDefault(u8(it.key)).toDouble();
            x = qBound(it.lo, x + dir * it.step, it.hi);
            v = QString::number(x, 'f', it.dec);
        }
        klog(u8("settings step ") + u8(it.key) + u8(": ") + before + u8(" -> ") + v);
        refreshSettings();
        if (v != before) saveSettings();
    }

    // Apply the saved draft in place, keeping navigation and clipboard.
    void saveSettings() {
        QStringList keys, values;
        for (int i = 0; i < SET_COUNT; ++i) {
            if (i >= m_initialSetVals.size() || m_setVals[i] != m_initialSetVals[i]) {
                keys << u8(SET_ITEMS[i].key); values << m_setVals[i];
            }
        }
        if (!keys.isEmpty() && !saveConfig(keys, values)) {
            m_setVals = m_initialSetVals; m_resetHomeDraft = false;
            refreshSettings(); updateHomeDraft(); showMessage(u8("Не удалось записать kobrowse.ini")); return;
        }
        m_initialSetVals = m_setVals;
        const bool homeResetFailed = m_resetHomeDraft && !resetHomeFolder(m_root, u8(KOBROWSE_HOME));
        loadConfig();
        m_showHidden = SHOW_HIDDEN;
        m_holdTimer.setInterval(LONG_TAP_MS);
        m_trashButton->setVisible(TRASH_ENABLED);
        QFont listFont = LIST_FONT.isEmpty() ? font() : QFont(LIST_FONT);
        listFont.setPixelSize(LIST_PX);
        m_list->setFont(listFont); m_fileDelegate->setListFont(listFont);
        m_columnHeader->setListFont(listFont);
        QFont actionFont = listFont; actionFont.setPixelSize(ptToPx(12));
        m_actions->setFont(actionFont);
        m_resetHomeDraft = false; updateHomeDraft();
        clearSelection(); load();
        klog(u8("settings applied"));
        if (homeResetFailed) showMessage(u8("Настройки сохранены, но домашнюю папку сбросить не удалось"));
    }

    void goUp() {
        if (m_path == m_root) return;
        QString parent = m_path == m_trash && safeLocalPath(m_root, m_trashReturn)
            && QFileInfo(m_trashReturn).isDir() ? m_trashReturn : QFileInfo(m_path).absolutePath();
        if (!parent.startsWith(m_root)) parent = m_root; // не выходим за корень
        m_path = parent;
        m_marked.clear(); m_selectionOrder.clear();
        load();
    }

    QString       m_root, m_trash, m_path, m_trashReturn, m_pendingZip;
    std::vector<int> m_pageStarts;
    int m_pageIndex = 0;
    RowDelegate *m_fileDelegate = nullptr;
    QFrame *m_footerLine = nullptr;
    bool          m_showHidden;
    NativeBrightness m_nativeBrightness;
    BrightnessBackend *m_brightnessBackend = nullptr;
    BrightnessGesture m_brightnessGesture;
    bool m_brightnessTracking = false, m_brightnessConsumed = false;
    bool          m_rootDiagLogged = false;
    int           m_logLeft = LOG_EVENTS;
    QTimer        m_msgTimer, m_ensure, m_setGeom, m_holdTimer, m_pageTimer, m_workTimer;
    QWidget *m_workOverlay = nullptr;
    QLabel *m_workText = nullptr;
    bool m_busy = false;
    std::function<void()> m_work;
    QString       m_holdPath, m_operationDetail;
    QStringList   m_deleteSnapshot;
    bool          m_menuTouch = false;
    int           m_menuScrollStart = 0;
    QWidget *m_pickerShade = nullptr; // shared shade for confirmation dialogs
    bool          m_holdFired = false, m_holdBlank = false, m_menuRequested = false, m_swipeList = false;
    int           m_pendingDelete = 0, m_confirmSetting = -1;
    QPoint        m_pressPos;
    int           m_scrollStart = 0;
    bool          m_twoFinger = false, m_moved = false;
    QPointer<QPushButton> m_touchButton;
    bool m_buttonTouchCancelled = false;
    qreal         m_twoStartY = 0, m_twoCurrentY = 0;
    int           m_tapLog = LOG_TAPS;
    QString       m_lastMarked;
    QElapsedTimer m_press;
    QFont         m_bold;
    QSet<QString> m_marked;          // отмеченные пути (в текущей папке)
    QStringList m_selectionOrder;    // порядок выбора для имени архива
    QStringList   m_clip;            // буфер: пути для вставки
    bool          m_clipCut = false; // буфер «вырезан» (перенос) или «скопирован»
    QTreeWidget  *m_list = nullptr;
    QWidget *m_pathHeader = nullptr;
    BrowserColumnHeader *m_columnHeader = nullptr;
    QVector<QPushButton *> m_sortButtons;
    BrowserSort m_sort = SortName;
    bool m_sortAscending = true;
    QLabel       *m_info = nullptr;
    BrowserPathLabel *m_pathLabel = nullptr;
    FooterNotice *m_footerNotice = nullptr, *m_settingsNotice = nullptr;
    DeviceStatusBar *m_deviceStatus = nullptr;
    QWidget      *m_settings = nullptr, *m_nav = nullptr, *m_toolbar = nullptr; // экран настроек; ряд навигации
    QList<QLabel *> m_setLabels;        // значения на экране настроек
    QList<QPushButton *> m_stepBtns;    // кнопки «−»/«+» по строкам: 0,1 — строка 0; 2,3 — строка 1 …
    QStringList   m_setVals, m_initialSetVals; // черновик и значения при открытии
    QStringList   m_fonts;              // гарнитуры для выбора
    QVector<SettingChoiceButton *> m_fontChoices, m_bookModes;
    QTreeWidget  *m_actions = nullptr;
    QWidget      *m_confirm = nullptr;
    QLabel       *m_confirmText = nullptr;
    ChromeButton *m_prev = nullptr, *m_next = nullptr;
    QLabel *m_pageLabel = nullptr;
    FooterCountsButton *m_countLabel = nullptr;
    int m_fileCount = 0, m_dirCount = 0;
    QScrollArea *m_settingsScroll = nullptr;
    QWidget *m_groupBlocks[GroupCount] = {}, *m_groupBodies[GroupCount] = {};
    QPushButton *m_advancedButtons[GroupCount] = {};
    bool m_groupExpanded[GroupCount] = {};
    int m_currentSettingsTab = 0, m_resetTabPending = 0;
    QPushButton *m_generalTab = nullptr, *m_fontTab = nullptr;
    ChromeButton *m_trashButton = nullptr, *m_emptyTrashButton = nullptr;
    QPushButton *m_resetHomeButton = nullptr, *m_resetTabButton = nullptr;
    QLabel *m_homeStatus = nullptr;
    bool m_resetHomeDraft = false;
    QVector<QWidget *> m_settingRows, m_settingLines;
    QVector<QPushButton *> m_minusBtns, m_plusBtns;
    QVector<SettingSwitch *> m_switches;
    QVector<SettingSlider *> m_sliders;
    SettingSlider *m_activeSlider = nullptr;
    bool m_settingsTouch = false;
    int m_settingsScrollStart = 0;
};

// ---------- запуск окна ----------
static QPointer<KobrowseWindow> g_win;          // одно окно на всё время работы
static QFileSystemWatcher  *g_watch = nullptr;
static QTimer              *g_poll = nullptr;  // страховка: inotify на /tmp иногда молчит

// Kobalt listens to PlugManager::plugged and N3PowerWorkflowManager::aboutToSleep.
// Do not hook the same power/USB PLT entries: NickelHook replaces, rather than chains,
// another plugin's hook (notably Kobalt's shutdown save/drain).
static QTimer *g_lifecycleClose = nullptr;
static QTimer *g_sleepRetry = nullptr;
static QPointer<QObject> g_sleepManager;
static QPointer<QObject> g_plugManager;

// The same optional queries used by Kobalt: do not raise an overlay while
// Nickel's sleep, PIN, or USB view is still visible. Missing APIs fail closed.
static bool nativeWakeReady() {
    typedef const void *(*Manager)();
    typedef bool (*Query)(const void *);
    const Manager power = nsym<Manager>("_ZN22N3PowerWorkflowManager14sharedInstanceEv");
    const Query sleeping = nsym<Query>("_ZNK22N3PowerWorkflowManager8sleepingEv");
    const Query visible = nsym<Query>("_ZNK22N3PowerWorkflowManager16sleepViewVisibleEv");
    const Query pin = nsym<Query>("_ZNK22N3PowerWorkflowManager25pinCodeInputDialogVisibleEv");
    const Manager usb = nsym<Manager>("_ZN19PlugWorkflowManager14sharedInstanceEv");
    const Query usbVisible = nsym<Query>("_ZN19PlugWorkflowManager20pluggedInViewVisibleEv");
    if (!power || !sleeping || !visible || !pin || !usb || !usbVisible) return false;
    const void *p = power(), *u = usb();
    return p && u && sleepRecoveryAllowed(true, sleeping(p), visible(p), pin(p), usbVisible(u));
}
static void restoreAfterSleep() {
    if (!g_win && !g_search) { g_sleepRetry->stop(); return; }
    if (!nativeWakeReady()) return;
    g_sleepRetry->stop();
    const bool restoreSearch = g_search && g_search->property("kobrowseSleepHidden").toBool();
    if (restoreSearch) {
        g_search->setProperty("kobrowseSleepHidden", false); g_search->show(); g_search->raise();
    }
    if (g_win && g_win->property("kobrowseSleepHidden").toBool()) {
        g_win->setProperty("kobrowseSleepHidden", false); g_win->show();
        if (restoreSearch && g_win->parentWidget() == g_search->parentWidget()) g_win->stackUnder(g_search);
        else g_win->raise();
        g_win->resumeDeferredWork();
    }
    klog(u8("sleep: restored after native views closed"));
}
static void attachSleepWindow(QWidget *window) {
    typedef QObject *(*Manager)();
    const Manager power = nsym<Manager>("_ZN22N3PowerWorkflowManager14sharedInstanceEv");
    QObject *manager = power ? power() : nullptr;
    // If the wake queries are missing, do not hide the only visible window.
    if (!manager || !g_sleepRetry || !nsym<void *>("_ZNK22N3PowerWorkflowManager8sleepingEv")
        || !nsym<void *>("_ZNK22N3PowerWorkflowManager16sleepViewVisibleEv")
        || !nsym<void *>("_ZNK22N3PowerWorkflowManager25pinCodeInputDialogVisibleEv")
        || !nsym<void *>("_ZN19PlugWorkflowManager14sharedInstanceEv")
        || !nsym<void *>("_ZN19PlugWorkflowManager20pluggedInViewVisibleEv")) {
        klog(u8("sleep: native queries unavailable")); return;
    }
    if (g_sleepManager != manager) {
        if (g_sleepManager) QObject::disconnect(g_sleepManager, nullptr, g_sleepRetry, nullptr);
        g_sleepManager = manager;
        const bool resumed = QObject::connect(manager, SIGNAL(resumedFromSleep()), g_sleepRetry, SLOT(start()), Qt::DirectConnection);
        QObject::connect(manager, SIGNAL(resumingFromSleep()), g_sleepRetry, SLOT(start()), Qt::DirectConnection);
        QObject::connect(manager, SIGNAL(aboutToSleep()), g_sleepRetry, SLOT(stop()), Qt::DirectConnection);
        if (!resumed) { g_sleepManager.clear(); klog(u8("sleep: resume signal unavailable")); return; }
    }
    // Map the old-style runtime signal to a lambda without adding a moc build.
    // Track only windows which were visible when sleep actually started.
    QSignalMapper *mapper = new QSignalMapper(window);
    mapper->setMapping(manager, window);
    QObject::connect(mapper, static_cast<void (QSignalMapper::*)(QWidget *)>(&QSignalMapper::mapped),
                     window, [window](QWidget *) {
        if (window->isVisible()) { window->setProperty("kobrowseSleepHidden", true); window->hide(); }
    });
    const bool ok = QObject::connect(manager, SIGNAL(aboutToSleep()), mapper, SLOT(map()), Qt::DirectConnection);
    klog(ok ? u8("sleep: hide and restore connected") : u8("sleep: signal unavailable"));
}
static void closeForLifecycle() {
    if (g_search || g_win) klog(u8("USB lifecycle: closing windows"));
    if (g_search) g_search->close();
    if (g_win) g_win->close();
}
static void setupLifecycleSignals() {
    typedef QObject *(*Manager)();
    Manager plug = nsym<Manager>("_ZN11PlugManager14sharedInstanceEv");
    QObject *p = plug ? plug() : nullptr;
    if (p && p != g_plugManager) {
        g_plugManager = p;
        QObject::connect(p, SIGNAL(plugged()), g_lifecycleClose, SLOT(start()), Qt::AutoConnection);
    }
    // Fallback if a signal was missed, using the same optional Nickel queries as Kobalt.
    typedef const void *(*GetManager)();
    typedef bool (*Query)(const void *);
    GetManager usbWorkflow = nsym<GetManager>("_ZN19PlugWorkflowManager14sharedInstanceEv");
    Query usbVisible = nsym<Query>("_ZN19PlugWorkflowManager20pluggedInViewVisibleEv");
    const void *usb = usbWorkflow ? usbWorkflow() : nullptr;
    if (usb && usbVisible && usbVisible(usb)) closeForLifecycle();
}

// Главное окно Nickel. MWC_currentView на главном экране может вернуть nullptr —
// тогда activeWindow() и, на крайний случай, любой видимый виджет верхнего уровня.
static QWidget *koboHostWindow() {
    QWidget *host = nullptr;
    if (MWC_sharedInstance && MWC_currentView) {
        if (MainWindowController *mwc = MWC_sharedInstance())
            if (QWidget *cv = MWC_currentView(mwc)) host = cv->window();
    }
    if (!host) host = QApplication::activeWindow();
    if (!host) {
        const QList<QWidget *> tops = QApplication::topLevelWidgets();
        for (int i = 0; i < tops.size(); ++i) {
            QWidget *w = tops.at(i);
            if (w->isVisible() && w->width() > 400 && w->height() > 300) { host = w; break; }
        }
    }
    return host;
}

static void kobrowse_open() {
    if (g_win) { g_win->raise(); return; }

    // Главное окно Nickel: MWC → activeWindow → любой видимый виджет верхнего уровня
    QWidget *host = koboHostWindow();
    if (!host) { klog(u8("no host window")); return; }

    loadConfig();                     // kobrowse.ini читается при каждом открытии
    g_win = new KobrowseWindow(host);
    // Hide without destroying the window; restore only after Nickel's sleep,
    // PIN and USB views are gone. Never replace Kobalt's lifecycle hooks.
    attachSleepWindow(g_win.data());
    g_win->setGeometry(host->rect()); // всё главное окно
    g_win->show();
    g_win->raise();
    klog(u8("window shown, host %1x%2, dpi %3").arg(host->width()).arg(host->height()).arg(SCREEN_DPI));
}

// Диагностика: раскладка строки по глифам — каким шрифтом на деле набран каждый кусок,
// позиция глифа (x), его шаг (a) и ширина рисунка (w). Шаг намного больше рисунка — «разрядка» в самом шрифте.
static QString diagRuns(const QString &text, const QFont &f) {
    QTextLayout tl(text, f);
    tl.beginLayout();
    QTextLine ln = tl.createLine();
    if (ln.isValid()) ln.setLineWidth(100000);
    tl.endLayout();
    QString s;
    const QList<QGlyphRun> runs = tl.glyphRuns();
    for (const QGlyphRun &r : runs) {
        const QRawFont rf = r.rawFont();
        if (!rf.isValid()) { s += u8("    run: (нет шрифта)\n"); continue; }
        const QVector<quint32> gi = r.glyphIndexes();
        const QVector<QPointF> pos = r.positions();
        const QVector<QPointF> adv = rf.advancesForGlyphIndexes(gi);
        s += u8("    run: ") + rf.familyName() + u8(" / ") + rf.styleName()
           + u8(" px=") + QString::number(rf.pixelSize()) + u8(" n=") + QString::number(gi.size()) + u8(" :");
        for (int i = 0; i < gi.size() && i < 24; ++i) {
            s += u8(" g") + QString::number(gi[i])
               + u8(" x") + QString::number(i < pos.size() ? qRound(pos[i].x()) : -1)
               + u8(" a") + QString::number(i < adv.size() ? qRound(adv[i].x()) : -1)
               + u8(" w") + QString::number(qRound(rf.boundingRect(gi[i]).width())) + u8(";");
        }
        s += u8("\n");
    }
    return s;
}

// Диагностика: та же строка, нарисованная обычным QPainter::drawText заданным шрифтом, — в PNG для сравнения
static void diagRender(const QString &path, const QString &text, const QFont &f) {
    const QFontMetrics fm(f);
    const int w = qMax(fm.width(text), fm.width(text.toUpper())) + 40;
    QImage img(qMax(w, 60), fm.height() + 20, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setFont(f);
    p.setPen(Qt::black);
    p.drawText(20, 10 + fm.ascent(), text);
    p.end();
    img.save(path, "PNG");
}

// Диагностика шрифтов: какие гарнитуры знает Qt (и есть ли в них кириллица) и каким шрифтом
// на самом деле нарисованы видимые надписи Nickel с русским текстом.
// Запускается через DIAG_DELAY_MS после пункта меню — за это время можно открыть нужный экран
// (диалог, экран подключения USB). Результат — в /tmp/kobrowse_diag (на экране USB /mnt/onboard отключён);
// на диск его переносит пункт NickelMenu «Диагностика: сохранить».
static const int DIAG_DELAY_MS = 25000;
static QTimer *g_diagTimer = nullptr;
static void kobrowse_font_diag() {
    QString out;
    QFontDatabase db;
    out += u8("=== Гарнитуры (кириллица: + / -)\n");
    for (const QString &f : db.families()) {
        out += (db.writingSystems(f).contains(QFontDatabase::Cyrillic) ? u8("+ ") : u8("- ")) + f + u8("  [")
             + db.styles(f).join(u8(", ")) + u8("]\n");
    }
    out += u8("Подстановки для Avenir: ") + QFont::substitutes(u8("Avenir")).join(u8(", ")) + u8("\n");
    out += u8("\n=== Видимые надписи с кириллицей: класс | объект | запрошенный шрифт | фактический | px | разрядка | регистр | текст\n");
    QSet<QString> seen;
    // снимки надписей: .adds/kobrowse/diag/dNN_widget.png (как рисует Nickel), dNN_same.png (тот же шрифт, обычный
    // drawText), dNN_an.png («Avenir Next» напрямую)
    const QString shotDir = u8("/tmp/kobrowse_diag");
    QDir().mkpath(shotDir);
    for (const QString &old : QDir(shotDir).entryList(QDir::Files)) QFile::remove(shotDir + u8("/") + old);
    int shots = 0;
    for (QWidget *w : QApplication::allWidgets()) {
        if (!w->isVisible()) continue;
        QString text;
        const QVariant tv = w->property("text");
        if (tv.isValid()) text = tv.toString();
        if (text.isEmpty()) continue;
        bool cyr = false;
        for (const QChar &ch : text) if (ch.unicode() >= 0x400 && ch.unicode() < 0x500) { cyr = true; break; }
        if (!cyr) continue;
        const QFont f = w->font();
        const QFontInfo fi(f);
        const QString line = QString::fromLatin1(w->metaObject()->className()) + u8(" | ") + w->objectName()
            + u8(" | ") + f.family() + u8(" | ") + fi.family()
            + u8(" | ") + QString::number(f.pixelSize() > 0 ? f.pixelSize() : fi.pixelSize())
            + u8(" | ") + QString::number(f.letterSpacing()) + (f.letterSpacingType() == QFont::PercentageSpacing ? u8("%") : u8("px"))
            + u8(" | ") + QString::number(int(f.capitalization()))
            // каким шрифтом на деле набирается кириллица и латиница, ширины «Д» и «D»
            + u8(" | кир:") + QRawFont::fromFont(f, QFontDatabase::Cyrillic).familyName()
            + u8(" лат:") + QRawFont::fromFont(f, QFontDatabase::Latin).familyName()
            + u8(" wД=") + QString::number(QFontMetrics(f).width(QChar(0x0414)))
            + u8(" wD=") + QString::number(QFontMetrics(f).width(QLatin1Char('D')))
            // ширина всей строки (с шейпингом) против суммы ширин букв: если строка намного шире —
            // разрядку даёт набор строки, а не шрифт; плюс полное описание шрифта
            + [&]() -> QString {
                const QFontMetrics fm(f);
                const QString t = text.left(20);
                int sum = 0;
                for (const QChar &ch : t) sum += fm.width(ch);
                QFont an(u8("Avenir Next")); an.setPixelSize(f.pixelSize() > 0 ? f.pixelSize() : fi.pixelSize());
                return u8(" wStr=") + QString::number(fm.width(t)) + u8(" sum=") + QString::number(sum)
                     + u8(" wStr(AvenirNext)=") + QString::number(QFontMetrics(an).width(t))
                     + u8(" inFont(Д)=") + QString::number(fm.inFont(QChar(0x0414)))
                     + u8(" font=") + f.toString()
                     + u8(" strategy=") + QString::number(int(f.styleStrategy()))
                     + u8(" hinting=") + QString::number(int(f.hintingPreference()));
            }()
            + u8(" | ") + text.left(40).replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (seen.contains(line)) continue;
        seen.insert(line);
        out += line + u8("\n");
        out += u8("    size=") + QString::number(w->width()) + u8("x") + QString::number(w->height())
             + u8(" kerning=") + QString::number(int(f.kerning())) + u8(" stretch=") + QString::number(f.stretch())
             + u8(" wordSp=") + QString::number(f.wordSpacing()) + u8(" exact=") + QString::number(int(fi.exactMatch()))
             + u8(" textFormat=") + w->property("textFormat").toString()
             + u8(" window=") + QString::fromLatin1(w->window()->metaObject()->className());
        if (text.size() > 40) out += u8("\n    text: ") + text.left(300).replace(QLatin1Char('\n'), QLatin1Char(' '));
        {
            QStringList props;
            for (const QByteArray &pn : w->dynamicPropertyNames())
                props << QString::fromLatin1(pn) + u8("=") + w->property(pn.constData()).toString();
            out += u8(" props: ") + props.join(u8(", ")) + u8("\n");
        }
        out += diagRuns(text.left(24), f);
        if (shots < 40) {
            const QString base = shotDir + u8("/d") + QString::number(shots).rightJustified(2, QLatin1Char('0'));
            w->grab().save(base + u8("_widget.png"), "PNG");
            diagRender(base + u8("_same.png"), text.left(40), f);
            out += u8("    png: ") + base + u8("_*.png\n");
            ++shots;
        }
        if (!w->styleSheet().isEmpty()) out += u8("    qss: ") + w->styleSheet().left(300).replace(QLatin1Char('\n'), QLatin1Char(' ')) + u8("\n");
    }
    const QString path = shotDir + u8("/fonts_diag.txt");
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        file.write(out.toUtf8());
        file.close();
    }
    ::sync();
    klog(u8("font diag written"));
}

// Проверка файлов-сигналов. Вызывается и из inotify, и по таймеру-страховке:
// на Clara inotify на /tmp иногда не приходит после долгого сна — пункт молчал.
static void checkKobrowseTriggers() {
    setupLifecycleSignals();
    if (QFile::exists(u8(KOBROWSE_TRIGGER))) { QFile::remove(u8(KOBROWSE_TRIGGER)); kobrowse_open(); }
    if (QFile::exists(u8(KSEARCH_TRIGGER))) { QFile::remove(u8(KSEARCH_TRIGGER)); ksearch_open(); }
    if (QFile::exists(u8(KOBROWSE_DIAG))) {
        QFile::remove(u8(KOBROWSE_DIAG));
        if (g_diagTimer) g_diagTimer->start();
    }
}

// Delay removal until NickelHook has restored its .failsafe library. Never
// treat an unavailable onboard directory as the user's uninstall request.
static QTimer *g_uninstallTimer = nullptr;
static void checkUninstallRequest() {
    const QString root = u8(KOBROWSE_ROOT);
    const QString plugin = u8("/usr/local/Kobo/imageformats/libkobrowse.so");
    struct stat onboard, parent;
    if (::stat(QFile::encodeName(root).constData(), &onboard) != 0
        || ::stat("/mnt", &parent) != 0 || onboard.st_dev == parent.st_dev
        || !uninstallRequested(root)) return;
    if (QFileInfo(plugin + u8(".failsafe")).exists() || !QFileInfo(plugin).exists()) return;
    g_uninstallTimer->stop();
    QString error;
    if (!cleanupAppInstall(root, plugin, error)) {
        nh_log("uninstall failed: %s", QFile::encodeName(error).constData());
        nh_dump_log();
    } else {
        ::sync();
        nh_log("uninstall complete; application data moved to trash");
    }
}

// Вызывается один раз при загрузке плагина в Nickel
static int kobrowse_init() {
    QFile::remove(u8(KOBROWSE_TRIGGER)); // убрать старый сигнал, если остался
    QFile::remove(u8(KSEARCH_TRIGGER));
    QFile::remove(u8(KOBROWSE_DIAG));
    if (!QCoreApplication::instance()) {
        nh_log("no QApplication at init, trigger disabled");
        return 0;
    }
    if (uninstallRequested(u8(KOBROWSE_ROOT))) {
        g_uninstallTimer = new QTimer(QCoreApplication::instance());
        g_uninstallTimer->setInterval(15000);
        QObject::connect(g_uninstallTimer, &QTimer::timeout, []() { checkUninstallRequest(); });
        g_uninstallTimer->start();
        nh_log("uninstall requested; waiting for library failsafe");
        return 0;
    }
    g_sleepRetry = new QTimer(QCoreApplication::instance());
    g_sleepRetry->setInterval(25);
    QObject::connect(g_sleepRetry, &QTimer::timeout, []() { restoreAfterSleep(); });
    g_lifecycleClose = new QTimer();
    g_lifecycleClose->setSingleShot(true); g_lifecycleClose->setInterval(0);
    QObject::connect(g_lifecycleClose, &QTimer::timeout, []() { closeForLifecycle(); });
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, []() { closeForLifecycle(); });
    setupLifecycleSignals();
    // inotify на /tmp — основной путь; таймер раз в 700 мс — страховка от пропущенного сигнала
    g_watch = new QFileSystemWatcher(QStringList() << u8("/tmp"));
    QObject::connect(g_watch, &QFileSystemWatcher::directoryChanged,
                     [](const QString &) { checkKobrowseTriggers(); });
    g_poll = new QTimer();
    g_poll->setInterval(700);
    QObject::connect(g_poll, &QTimer::timeout, []() { checkKobrowseTriggers(); });
    g_poll->start();
    g_diagTimer = new QTimer();
    g_diagTimer->setSingleShot(true);
    g_diagTimer->setInterval(DIAG_DELAY_MS);
    QObject::connect(g_diagTimer, &QTimer::timeout, []() { kobrowse_font_diag(); });
    // Таймер переоткрытия окна после «Сохранить» в настройках (старое окно успевает удалиться)
    nh_log("watching %s", KOBROWSE_TRIGGER);
    return 0;
}

// Удаление инициируется отсутствием установленного маркера в kobrowse_init.
// NickelHook xflag не используется: он удаляет библиотеку до проверки данных.
// failsafe: если Nickel упадёт в первые 10 с после старта, плагин отключится сам.
static struct nh_info kobrowse_info = {
    .name           = "kobrowse",
    .desc           = "File manager for Kobo",
    .uninstall_flag = nullptr,
    .uninstall_xflag = nullptr,
    .failsafe_delay = 10,
};

static struct nh_hook  kobrowse_hook[]  = { {0} };
static struct nh_dlsym kobrowse_dlsym[] = {
    { .name = "_ZN20MainWindowController14sharedInstanceEv",
      .out = nh_symoutptr(MWC_sharedInstance), .desc = "", .optional = true },
    { .name = "_ZNK20MainWindowController11currentViewEv",
      .out = nh_symoutptr(MWC_currentView), .desc = "", .optional = true },
    {0},
};

NickelHook(
    .init  = &kobrowse_init,
    .info  = &kobrowse_info,
    .hook  = kobrowse_hook,
    .dlsym = kobrowse_dlsym,
    .uninstall = nullptr,
)
