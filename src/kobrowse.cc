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
#include <QHash>
#include <QScreen>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProcess>
#include <QPainterPath>
#include <QtMath>
#include <QTextLayout>
#include <QPointer>
#include <QPushButton>
#include <QPolygonF>
#include <QScrollBar>
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

#include <NickelHook.h>

// ---------- настройки ----------
static const char *KOBROWSE_ROOT    = "/mnt/onboard";   // выше этой папки не поднимаемся
static const char *KOBROWSE_TRIGGER = "/tmp/kobrowse.open"; // файл-сигнал от NickelMenu
static const char *KOBROWSE_DIAG    = "/tmp/kobrowse.diag"; // сигнал: диагностика шрифтов интерфейса Nickel
static double SCREEN_DPI = 300;        // плотность экрана; уточняется по устройству в loadConfig (Clara HD — 300)
static const char *KOBROWSE_INI     = "/mnt/onboard/.adds/kobrowse/kobrowse.ini"; // настройки, читаются при каждом открытии
static const int   LOG_TAPS     = 30;  // отладка: сколько касаний записать в лог
static const int   SWIPE_PX     = 40;  // сдвиг пальца больше этого — перетаскивание, а не нажатие
static const char *KOBROWSE_TRASH   = "/mnt/onboard/.kobrowse_trash"; // корзина (папка с точкой — Nickel её не индексирует)
static const char *KOBROWSE_LOG     = "/mnt/onboard/.adds/kobrowse/kobrowse.log";
static const int   LOG_EVENTS   = 0;   // сколько событий окна записать в лог (отладка)

// Функции Nickel (libnickel), загружаются при старте плагина
class MainWindowController;
static MainWindowController *(*MWC_sharedInstance)() = nullptr;
static QWidget *(*MWC_currentView)(MainWindowController *) = nullptr;

static QString u8(const char *s) { return QString::fromUtf8(s); }

// ---------- внешний вид: значения из kobrowse.ini (пересчитаны в пиксели) ----------
// Заполняются loadConfig() при каждом открытии менеджера.
static int     LIST_PX, FONT_BTN, FONT_MSG, GAP, BTN_MARGIN, BTN_HEIGHT, ICON_PX, LONG_TAP_MS, IDLE_MS;
static qreal   BTN_RADIUS, BTN_PEN;
static QString LIST_FONT, BTN_FONT;
static int OPDS_KEY_PX, OPDS_FUNC_PX;   // кегли клавиатуры OPDS (из kobrowse.ini)
// Клавиатура поиска (перенесена из ksearch): 0 — кегль считается от высоты клавиши.
static int     KEY_FONT_PX = 0, FUNC_PX = 0;
static bool    KEY_FONT_SET = false, FUNC_FONT_SET = false;
static int     KEY_PX = 34;             // кегль поля запроса; уточняется в buildKeyboard()
// Список результатов поиска — свои кегль и гарнитура, чтобы не менять вид списка файлов.
static int     SEARCH_LIST_PX = 42;
static QString SEARCH_LIST_FONT;
static bool    SHOW_HIDDEN = true;
static QString OPEN_BOOKS;   // auto / 1 / 0 — см. open_books в kobrowse.ini

// Ключи kobrowse.ini: имя, значение по умолчанию, пояснение
struct CfgKey { const char *key, *def, *comment; };
static const CfgKey CFG_KEYS[] = {
    { "list_pt",       "8",           "кегль списка файлов, пт: 8 = петит, 9 = боргес, 10 = корпус (можно дробное: 8.5)" },
    { "list_font",     "",            "гарнитура списка; пусто — как в Nickel. Примеры: Georgia, Amasis, Kobo Nickel" },
    { "btn_pt",        "6",           "наибольший кегль подписей кнопок, пт (уменьшается, если слово не влезает)" },
    { "btn_font",      "Kobo Nickel", "гарнитура подписей кнопок" },
    { "info_pt",       "6",           "кегль сведений о файле и сообщений, пт" },
    { "gap_pt",        "6",           "поля от краёв экрана и промежутки между кнопками, пт" },
    { "btn_pad_pt",    "6",           "поле внутри кнопки слева и справа от подписи, пт" },
    { "btn_height_pt", "29",          "высота кнопки, пт" },
    { "icon_pt",       "8",           "размер иконки на кнопке, пт" },
    { "btn_radius_pt", "3.4",         "скругление углов кнопки, пт (0 — прямые углы)" },
    { "btn_line_pt",   "0.6",         "толщина рамки кнопки, пт" },
    { "long_tap_ms",   "500",         "долгое нажатие (выбор строки), мс" },
    { "idle_min",      "2",           "закрыть менеджер, если не было касаний, мин" },
    { "show_hidden",   "1",           "показывать скрытые файлы и папки (с точкой): 1 — да, 0 — нет" },
    { "scale_pct",     "100",         "общий масштаб интерфейса kobrowse, %: 100 — как на Clara HD (по ширине экрана); 50–200" },
    { "open_books",    "auto",        "открытие книг тапом: auto — только на проверенной прошивке 4.38; 1 — всегда (на свой риск); 0 — никогда" },
    { "opds_key_pt",   "10",          "кегль клавиш OPDS-клавиатуры, пт (уменьшите, если клавиши не влезают)" },
    { "opds_func_pt",  "5",           "кегль нижнего ряда кнопок OPDS, пт" },
    { "key_font_pt",   "0",           "кегль подписей клавиш поиска, пт; 0 — от высоты клавиши (около 0.52)" },
    { "func_pt",       "0",           "кегль служебных клавиш поиска (Shift, Space, DelBwd), пт; 0 — от высоты клавиши (около 0.34)" },
    { "search_list_pt", "10",         "кегль списка результатов поиска, пт (8 = петит, 10 = корпус; можно дробное: 8.5)" },
    { "search_list_font", "",         "гарнитура списка результатов поиска; пусто — шрифт виджета" },
};

// Значение по умолчанию для ключа kobrowse.ini
static QString cfgDefault(const QString &key) {
    for (const CfgKey &k : CFG_KEYS) if (key == u8(k.key)) return u8(k.def);
    return QString();
}

static int ptToPx(double pt) { return qRound(pt * SCREEN_DPI / 72.0); }

// Масштаб интерфейса для любого ридера Kobo: пропорционально ширине экрана. Образец — Clara HD
// (1072 px по ширине, 300 dpi): на другом экране кнопки и шрифты занимают ту же долю ширины.
// Плотности из Qt (physicalDotsPerInch) не доверяем: у платформы Kobo она не проверена.
static void detectDpi() {
    SCREEN_DPI = 300;
    if (QScreen *scr = QGuiApplication::primaryScreen()) {
        const QSize sz = scr->size();
        const int w = qMin(sz.width(), sz.height());
        if (w >= 480 && w <= 4000) SCREEN_DPI = 300.0 * w / 1072.0;
    }
}

// Прочитать kobrowse.ini. Недостающие ключи дописываются в конец файла с пояснениями
// (то, что пользователь уже поменял, не трогаем).
static void loadConfig() {
    detectDpi();                                     // разные модели Kobo — разная плотность
    const QString ini = u8(KOBROWSE_INI);
    QDir().mkpath(QFileInfo(ini).absolutePath());
    QString text;
    {
        QFile f(ini);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) { text = QString::fromUtf8(f.readAll()); f.close(); }
    }
    QStringList present;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString l = line.trimmed();
        const int eq = l.indexOf(QLatin1Char('='));
        if (eq > 0 && !l.startsWith(QLatin1Char(';'))) present << l.left(eq).trimmed();
    }
    QString add;
    if (!text.contains(u8("[kobrowse]")))
        add += u8("[kobrowse]\n; настройки внешнего вида kobrowse; применяются при следующем открытии менеджера\n");
    for (const CfgKey &k : CFG_KEYS)
        if (!present.contains(u8(k.key)))
            add += u8("\n; ") + u8(k.comment) + u8("\n") + u8(k.key) + u8("=") + u8(k.def) + u8("\n");
    if (!add.isEmpty()) {
        QFile f(ini);
        if (f.open(QIODevice::Append | QIODevice::Text)) { f.write(add.toUtf8()); f.close(); }
    }

    QSettings st(ini, QSettings::IniFormat);
    st.setIniCodec(QTextCodec::codecForName("UTF-8"));
    // число в пределах [lo, hi], иначе значение по умолчанию
    auto num = [&st](const char *key, double def, double lo, double hi) {
        bool ok = false;
        const double v = st.value(u8("kobrowse/") + u8(key)).toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        return (ok && v >= lo && v <= hi) ? v : def;
    };
    SCREEN_DPI *= num("scale_pct", 100, 50, 200) / 100.0;   // до пересчёта пунктов в пиксели
    OPEN_BOOKS  = st.value(u8("kobrowse/open_books")).toString().trimmed().toLower();
    LIST_PX     = ptToPx(num("list_pt", 8, 4, 24));
    FONT_BTN    = ptToPx(num("btn_pt", 6, 3, 24));
    FONT_MSG    = ptToPx(num("info_pt", 6, 3, 14));
    GAP         = ptToPx(num("gap_pt", 6, 0, 24));
    BTN_MARGIN  = ptToPx(num("btn_pad_pt", 6, 0, 24));
    BTN_HEIGHT  = ptToPx(num("btn_height_pt", 29, 12, 60));
    ICON_PX     = ptToPx(num("icon_pt", 8, 3, 24));
    BTN_RADIUS  = num("btn_radius_pt", 3.4, 0, 20) * SCREEN_DPI / 72.0;
    BTN_PEN     = num("btn_line_pt", 0.6, 0.1, 3) * SCREEN_DPI / 72.0;
    LONG_TAP_MS = int(num("long_tap_ms", 500, 150, 3000));
    IDLE_MS     = int(num("idle_min", 2, 0.5, 60) * 60000);
    OPDS_KEY_PX  = ptToPx(num("opds_key_pt", 10, 5, 20));
    OPDS_FUNC_PX = ptToPx(num("opds_func_pt", 5, 3, 10));
    // Клавиатура поиска: ключ задан и больше нуля — фиксированный кегль, иначе от высоты клавиши.
    const double kf = num("key_font_pt", 0, 0, 24);
    KEY_FONT_SET = kf > 0;
    KEY_FONT_PX  = KEY_FONT_SET ? qMax(8, ptToPx(kf)) : 0;
    const double ff = num("func_pt", 0, 0, 24);
    FUNC_FONT_SET = ff > 0;
    FUNC_PX       = FUNC_FONT_SET ? qMax(8, ptToPx(ff)) : 0;
    SEARCH_LIST_PX   = ptToPx(num("search_list_pt", 10, 4, 24));
    SEARCH_LIST_FONT = st.value(u8("kobrowse/search_list_font")).toString().trimmed();
    LIST_FONT   = st.value(u8("kobrowse/list_font")).toString().trimmed();
    BTN_FONT    = st.value(u8("kobrowse/btn_font")).toString().trimmed();
    SHOW_HIDDEN = num("show_hidden", 1, 0, 1) != 0;
}

// Отладочный лог: файл открывается и сразу закрывается (не держим дескриптор на /mnt/onboard)
static void klog(const QString &msg) {
    QDir().mkpath(QFileInfo(u8(KOBROWSE_LOG)).absolutePath());
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
    return QString::number(b / (1024.0 * 1024.0), 'f', 1) + u8(" МБ");
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

// Рекурсивное копирование. Файл пишется во временный «*.kobrowse_tmp» и переименовывается
// в конце — при сбое не остаётся «наполовину скопированной» книги под настоящим именем.
static bool copyRecursive(const QString &src, const QString &dst, QString &err) {
    const QFileInfo fi(src);
    if (fi.isDir() && !fi.isSymLink()) {
        if (!QDir().mkpath(dst)) { err = u8("Не создать папку ") + QFileInfo(dst).fileName(); return false; }
        const QFileInfoList kids = QDir(src).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &k : kids)
            if (!copyRecursive(k.absoluteFilePath(), dst + u8("/") + k.fileName(), err)) return false;
        return true;
    }
    const QString tmp = dst + u8(".kobrowse_tmp");
    QFile::remove(tmp);
    if (!QFile::copy(src, tmp)) {
        QFile::remove(tmp);
        err = u8("Ошибка копирования ") + fi.fileName();
        return false;
    }
    if (!QFile::rename(tmp, dst)) {
        QFile::remove(tmp);
        err = u8("Ошибка записи ") + QFileInfo(dst).fileName();
        return false;
    }
    return true;
}

// Данные строки списка
static const int ROLE_PATH = Qt::UserRole,     // полный путь
                 ROLE_DIR  = Qt::UserRole + 1, // это папка
                 ROLE_MARK = Qt::UserRole + 2, // строка отмечена
                 ROLE_DETAIL = Qt::UserRole + 4, // дата и размер
                 ROLE_PARENT = Qt::UserRole + 5; // специальная строка «..»

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

// Контрастные пиктограммы и двухстрочные сведения удобны на e-ink.
// Делегат также используется старым OPDS-списком, где сведений может не быть.
class RowDelegate : public QStyledItemDelegate {
public:
    RowDelegate(QWidget *view, const QFont &f) : QStyledItemDelegate(view), m_view(view), m_font(f) {}
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &idx) const override {
        const qreal h = layoutText(idx.data().toString(), fontFor(idx), textWidth());
        const QString detail = idx.data(ROLE_DETAIL).toString();
        const qreal d = detail.isEmpty() ? 0 : layoutText(detail, detailFont(), textWidth());
        return QSize(m_view->width(), qMax(iconSize(), int(h + d)) + 2 * PAD_V + 1);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override {
        const bool mark = idx.data(ROLE_MARK).toBool();
        const QColor ink = mark ? Qt::white : Qt::black;
        p->save();
        p->fillRect(opt.rect, mark ? Qt::black : Qt::white);
        p->setPen(QPen(ink, qMax(1, m_font.pixelSize() / 16)));
        p->setBrush(Qt::NoBrush);
        const int sz = iconSize();
        const QRectF r(opt.rect.left() + PAD_H, opt.rect.top() + PAD_V, sz, sz);
        if (idx.data(ROLE_DIR).toBool()) {
            QPainterPath folder;
            folder.moveTo(r.left(), r.top() + sz * .25);
            folder.lineTo(r.left(), r.top() + sz * .12);
            folder.lineTo(r.left() + sz * .38, r.top() + sz * .12);
            folder.lineTo(r.left() + sz * .48, r.top() + sz * .25);
            folder.lineTo(r.right(), r.top() + sz * .25);
            folder.lineTo(r.right(), r.bottom() - sz * .1);
            folder.lineTo(r.left(), r.bottom() - sz * .1);
            folder.closeSubpath();
            p->drawPath(folder);
        } else {
            p->drawRect(r.adjusted(sz * .16, sz * .06, -sz * .16, -sz * .06));
        }
        p->setPen(ink);
        const QPointF pos(opt.rect.left() + PAD_H * 2 + sz, opt.rect.top() + PAD_V);
        const qreal h = layoutText(idx.data().toString(), fontFor(idx), textWidth(), p, pos);
        const QString detail = idx.data(ROLE_DETAIL).toString();
        if (!detail.isEmpty())
            layoutText(detail, detailFont(), textWidth(), p, QPointF(pos.x(), pos.y() + h));
        if (!mark) {
            p->setPen(QColor(150, 150, 150));
            p->drawLine(opt.rect.bottomLeft(), opt.rect.bottomRight());
        }
        p->restore();
    }
private:
    enum { PAD_V = 10, PAD_H = 6 };
    int iconSize() const { return qMax(20, m_font.pixelSize()); }
    QFont detailFont() const { QFont f = m_font; f.setPixelSize(qMax(10, m_font.pixelSize() * 3 / 4)); return f; }
    QFont fontFor(const QModelIndex &idx) const {
        QFont f = m_font;
        f.setBold(idx.data(ROLE_DIR).toBool());
        return f;
    }
    qreal textWidth() const { return qMax(40, m_view->width() - iconSize() - 3 * PAD_H); }
    QWidget *m_view;
    QFont m_font;
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

// Узкий (narrow) шрифт для всего интерфейса OPDS
static QFont narrow(QFont f) { f.setStretch(QFont::Condensed); return f; }

// Иконки кнопок — рисуются линиями (QPainter), без файлов и шрифтов
enum Icon { I_NONE, I_COPY, I_CUT, I_TRASH, I_INFO, I_DESELECT, I_FOLDER_UP, I_CHEV_UP, I_CHEV_DOWN,
            I_EYE, I_EYE_OFF, I_CLOSE, I_CLIPBOARD, I_CLIPBOARD_X, I_GEAR };

// Рисует иконку в квадрате r; координаты в сетке 24×24 (как у Tabler Icons)
static void drawIcon(QPainter *p, Icon ic, const QRectF &r, const QColor &c) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->translate(r.topLeft());
    p->scale(r.width() / 24.0, r.height() / 24.0);
    QPen pen(c, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    switch (ic) {
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
    case I_TRASH: {                    // корзина
        p->drawLine(QPointF(4, 7), QPointF(20, 7));
        QPolygonF lid;  lid << QPointF(9, 7) << QPointF(9, 4) << QPointF(15, 4) << QPointF(15, 7);
        p->drawPolyline(lid);
        QPolygonF body; body << QPointF(6, 7) << QPointF(7, 20) << QPointF(17, 20) << QPointF(18, 7);
        p->drawPolyline(body);
        p->drawLine(QPointF(10, 11), QPointF(10, 16));
        p->drawLine(QPointF(14, 11), QPointF(14, 16));
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
    case I_CHEV_UP: {
        QPolygonF a; a << QPointF(6, 15) << QPointF(12, 9) << QPointF(18, 15);
        p->drawPolyline(a);
        break;
    }
    case I_CHEV_DOWN: {
        QPolygonF a; a << QPointF(6, 9) << QPointF(12, 15) << QPointF(18, 9);
        p->drawPolyline(a);
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
    case I_GEAR:                       // шестерёнка: кольцо + 8 зубцов
        p->drawEllipse(QPointF(12, 12), 3, 3);
        p->drawEllipse(QPointF(12, 12), 6.5, 6.5);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * 3.14159265 / 4;
            p->drawLine(QPointF(12 + 6.5 * qCos(a), 12 + 6.5 * qSin(a)),
                        QPointF(12 + 9.5 * qCos(a), 12 + 9.5 * qSin(a)));
        }
        break;
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
    void setFace(Icon ic, const QString &t) { m_icon = ic; m_text = t; update(); }
    const QString &text() const { return m_text; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const bool on = parentWidget() ? parentWidget()->isEnabled() : true;
        const QColor c = on ? Qt::black : QColor(160, 160, 160);
        // Рамка со сглаженными скруглёнными углами (вместо рамки из стилей Qt)
        const qreal h = BTN_PEN / 2.0;
        p.setPen(QPen(c, BTN_PEN));
        p.setBrush(Qt::white);
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
static bool bookOpenAllowed() {
    if (OPEN_BOOKS == u8("1")) return true;
    if (OPEN_BOOKS == u8("0")) return false;
    QFile f(u8("/mnt/onboard/.kobo/version"));   // «серийный,3.0.35+,4.38.23852,…»
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QStringList parts = QString::fromLatin1(f.readAll()).trimmed().split(QLatin1Char(','));
    f.close();
    return parts.size() > 2 && parts.at(2).startsWith(u8("4.38."));
}

static bool openBook(const QString &path, bool viaKobalt, QString &err) {
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

// ================= OPDS: диалог каталога книг =================
// Открывается по файлу-сигнали /tmp/kobrowse.opds из NickelMenu. Вся сетевая работа
// вынесена в скрипт /mnt/onboard/.adds/opds/opds_get.sh (curl/wget + awk); окно
// только показывает список, выбирает каталог/книгу и вводит адрес своей клавиатурой.
static const char *OPDS_TRIGGER = "/tmp/kobrowse.opds";
static const char *OPDS_SCRIPT  = "/mnt/onboard/.adds/opds/opds_get.sh";
static const char *OPDS_CONF    = "/mnt/onboard/.adds/opds/opds.conf";
static const char *OPDS_LIST    = "/tmp/opds.list";
static const char *OPDS_NEXT    = "/tmp/opds.next";
static const char *OPDS_META    = "/tmp/opds.meta";
static const char *OPDS_HISTORY = "/mnt/onboard/.adds/opds/history.txt";
static const char *OPDS_HISTORY_DEFAULT = "/mnt/onboard/.adds/opds/history.default.txt";
static const QString OPDS_SEP(1, QChar(31));   // разделитель полей в машинном списке

// Раскладки клавиатуры: латиница, заглавные+символы, цифры/знаки. Кириллица в адресах не нужна.
// По 10 символов в строке — как у Plato, но без внешних JSON-файлов.
static const char *KB_LAYOUT[3][4] = {
    { "1234567890",  "qwertyuiop",  "asdfghjkl-",  "zxcvbnm./?" },
    { "!@#$%^&*()",  "QWERTYUIOP",  "ASDFGHJKL_",  "ZXCVBNM,;:" },
    { "\"'~<>[]{}|", "~|=+\\-/*%_", "0123456789",  ".,:;?!()@#" }
};

struct OpdsItem {
    QString kind, title, href, ext, type;
};

// Поле набора: рисуем текст сами, чтобы курсор был виден на e-ink (у QLineEdit
// read-only каретка часто не рисуется). Тап по полю ставит курсор в позицию.
class UrlField : public QWidget {
public:
    UrlField(QWidget *parent, const QFont &f) : QWidget(parent), m_f(f), m_cursor(0) {
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(u8("background:white;border:2px solid black;border-radius:6px;"));
        setMinimumHeight(qMax(FONT_MSG + 40, 90));
    }
    void setText(const QString &t) { m_text = t; m_cursor = qBound(0, m_cursor, m_text.length()); update(); }
    void setCursor(int c) { m_cursor = qBound(0, c, m_text.length()); update(); }
    QString text() const { return m_text; }
    int cursorAt(const QPoint &p) const {
        const int start = visibleStart();
        const QFontMetrics fm(m_f);
        int x = 6;
        for (int i = start; i < m_text.length(); ++i) {
            const int w = fm.width(QString(m_text.at(i)));
            if (x + w / 2 > p.x()) return i;
            x += w;
        }
        return m_text.length();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setFont(m_f);
        p.setPen(Qt::black);
        const int start = visibleStart();
        const QString shown = m_text.mid(start);
        p.drawText(rect().adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, shown);
        const QFontMetrics fm(m_f);
        const int cx = 6 + fm.width(shown.left(qMax(0, m_cursor - start)));
        p.drawLine(cx, 8, cx, height() - 8);
        p.drawLine(cx + 1, 8, cx + 1, height() - 8);
    }
private:
    int visibleStart() const {
        const QFontMetrics fm(m_f);
        const int avail = qMax(width() - 16, 60);
        int s = 0;
        while (s < m_text.length() && fm.width(m_text.mid(s)) > avail) ++s;
        return s;
    }
    QFont m_f;
    QString m_text;
    int m_cursor;
};

class OpdsWindow;
static OpdsWindow *g_opds = nullptr;
static QWidget *koboHostWindow();   // определено ниже, используется и kobrowse, и OPDS

class OpdsWindow : public QWidget {
public:
    explicit OpdsWindow(QWidget *parent)
        : QWidget(parent), m_bShift(nullptr), m_cursor(0), m_level(0), m_shift(false),
          m_proc(nullptr), m_op(0), m_pendingPush(false)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_AcceptTouchEvents);
        setAutoFillBackground(true);
        setStyleSheet(u8("QWidget{background:white;color:black;}"
                         "QPushButton{border:2px solid black;background:white;color:black;padding:0;}"));

        QFont fBtn = narrow(font()); fBtn.setPixelSize(FONT_BTN);
        QFont fMsg = narrow(font()); fMsg.setPixelSize(FONT_MSG);
        QFont fList = LIST_FONT.isEmpty() ? font() : QFont(LIST_FONT);
        fList.setPixelSize(LIST_PX);
        fList = narrow(fList);

        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->setContentsMargins(GAP, GAP, GAP, GAP);
        lay->setSpacing(GAP);

        m_title = new QLabel(this);
        m_title->setFont(fMsg);
        m_title->setWordWrap(true);
        lay->addWidget(m_title);

        m_list = new QTreeWidget(this);
        m_list->setFont(fList);
        m_list->setColumnCount(1);
        m_list->setHeaderHidden(true);
        m_list->setRootIsDecorated(false);
        m_list->setIndentation(0);
        m_list->setUniformRowHeights(false);
        m_list->setItemDelegate(new RowDelegate(m_list, fList)); // как в kobrowse
        m_list->setSelectionMode(QAbstractItemView::NoSelection);
        m_list->setFocusPolicy(Qt::NoFocus);
        m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        lay->addWidget(m_list, 1);

        // Поле адреса и клавиатура в стиле Plato: 3 уровня (только латиница и знаки), курсор,
        // стрелки, бэкспейс, широкая кнопка пробела. Конфиг править не нужно.
        m_keyboard = new QWidget(this);
        QVBoxLayout *kl = new QVBoxLayout(m_keyboard);
        kl->setContentsMargins(0, 0, 0, 0);
        kl->setSpacing(GAP / 2);
        // Строка набора: высокая, крупным полужирным; тап по ней ставит курсор
        QFont fUrl = font();
        fUrl.setPixelSize(qMax(FONT_MSG + 12, 38));
        fUrl.setBold(true);
        m_urlEdit = new UrlField(m_keyboard, fUrl);
        kl->addWidget(m_urlEdit);
        showUrl();

        QFont fKey = narrow(sansFont());
        fKey.setPixelSize(OPDS_KEY_PX);   // кегль из kobrowse.ini (opds_key_pt)
        const int KEY_H = qRound(BTN_HEIGHT * 0.8);
        QGridLayout *grid = new QGridLayout();
        grid->setSpacing(ptToPx(2));   // между клавишами 2 пт
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 10; ++c) {
                QPushButton *b = new QPushButton(m_keyboard);
                b->setFont(fKey);
                b->setMinimumHeight(KEY_H);
                b->setFocusPolicy(Qt::NoFocus);
                b->setStyleSheet(u8("QPushButton{border:2px solid black;border-radius:8px;"
                                    "background:white;color:black;}"));
                connect(b, &QPushButton::clicked, [this, r, c]() {
                    QString s = u8(KB_LAYOUT[m_level][r]);
                    if (c < s.size()) {
                        QChar ch = s.at(c);
                        if (m_shift) { ch = ch.toUpper(); m_shift = false; updateKeys(); }
                        insertChar(ch);
                    }
                });
                m_keys[r][c] = b;
                grid->addWidget(b, r, c);
            }
        }

        // Нижний ряд: поменьше и рубленым полужир. Кириллица в URL не нужна.
        QFont fFunc = fKey;                 // тот же рубленый кегль, что у клавиш
        fFunc.setBold(true);
        const int FUNC_H = KEY_H;           // последний ряд — такого же размера, как кнопки
        struct FuncBtn { const char *label; int col, span; };
        static const FuncBtn FUNCS[] = {
            { "Shift", 0, 1 }, { "Back", 1, 1 }, { "<", 2, 1 }, { ">", 3, 1 },
            { "Пробел", 4, 2 }, { "Ab", 6, 1 }, { "123", 7, 1 }, { "OK", 8, 2 }
        };
        for (int i = 0; i < 8; ++i) {
            QPushButton *b = new QPushButton(u8(FUNCS[i].label), m_keyboard);
            b->setFont(fFunc);
            b->setMinimumHeight(FUNC_H);
            b->setFocusPolicy(Qt::NoFocus);
            b->setStyleSheet(u8("QPushButton{border:2px solid black;border-radius:8px;"
                                "background:white;color:black;}"));
            grid->addWidget(b, 4, FUNCS[i].col, 1, FUNCS[i].span);
            switch (i) {
            case 0: m_bShift = b;
                    connect(b, &QPushButton::clicked, [this]() { m_shift = !m_shift; updateKeys(); });
                    break;
            case 1: connect(b, &QPushButton::clicked, [this]() { backspace(); }); break;
            case 2: connect(b, &QPushButton::clicked, [this]() { moveCursor(-1); }); break;
            case 3: connect(b, &QPushButton::clicked, [this]() { moveCursor(1); }); break;
            case 4: connect(b, &QPushButton::clicked, [this]() { insertChar(QLatin1Char(' ')); }); break;
            case 5: connect(b, &QPushButton::clicked, [this]() { setKbLevel(0); }); break;
            case 6: connect(b, &QPushButton::clicked, [this]() { setKbLevel(2); }); break;
            case 7: connect(b, &QPushButton::clicked, [this]() { keyboardDone(); }); break;
            }
        }
        updateKeys();   // m_bShift уже создан — вызывать ТОЛЬКО после нижнего ряда
        kl->addLayout(grid);
        m_keyboard->hide();
        lay->addWidget(m_keyboard);

        // История адресов: короткий тап — подставить в строку, долгий — удалить
        QFont fHist = narrow(fMsg);
        m_histLabel = new QLabel(u8("История адресов (долгое нажатие — удалить)"), this);
        m_histLabel->setFont(fHist);
        m_histLabel->hide();
        lay->addWidget(m_histLabel);
        m_histList = new QTreeWidget(this);
        m_histList->setFont(fHist);
        m_histList->setColumnCount(1);
        m_histList->setHeaderHidden(true);
        m_histList->setRootIsDecorated(false);
        m_histList->setIndentation(0);
        m_histList->setSelectionMode(QAbstractItemView::NoSelection);
        m_histList->setFocusPolicy(Qt::NoFocus);
        m_histList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_histList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_histList->setItemDelegate(new RowDelegate(m_histList, fHist));
        m_histList->hide();
        lay->addWidget(m_histList, 1);
        loadHistory();

        m_msg = new QLabel(this);
        m_msg->setFont(fMsg);
        m_msg->setWordWrap(true);
        m_msg->setAlignment(Qt::AlignCenter);
        m_msg->hide();
        lay->addWidget(m_msg);
        m_msgTimer.setSingleShot(true);
        m_msgTimer.setInterval(5000);
        connect(&m_msgTimer, &QTimer::timeout, this, [this]() { m_msg->hide(); });

        // Ряд кнопок: открыть / назад / далее / адрес / закрыть
        QWidget *bar = new QWidget(this);
        QHBoxLayout *row = new QHBoxLayout(bar);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(GAP);
        QPushButton *bOpen = new QPushButton(u8("Открыть"), bar);
        QPushButton *bPrev = new QPushButton(u8("Назад"), bar);
        QPushButton *bNext = new QPushButton(u8("Далее"), bar);
        QPushButton *bAddr = new QPushButton(u8("Адрес"), bar);
        QPushButton *bClose = new QPushButton(u8("Закрыть"), bar);
        QPushButton *btns[] = { bOpen, bPrev, bNext, bAddr, bClose };
        for (int i = 0; i < 5; ++i) {
            btns[i]->setFont(fBtn);
            btns[i]->setFocusPolicy(Qt::NoFocus);
            btns[i]->setMinimumHeight(BTN_HEIGHT);
            row->addWidget(btns[i], 1);
        }
        lay->addWidget(bar);
        connect(bOpen,  &QPushButton::clicked, [this]() { openCurrent(); });
        connect(bPrev,  &QPushButton::clicked, [this]() { goBack(); });
        connect(bNext,  &QPushButton::clicked, [this]() { goNext(); });
        connect(bAddr,  &QPushButton::clicked, [this]() {
            m_cursor = m_url.length(); showUrl();
            m_list->hide(); m_keyboard->show(); m_histLabel->show(); m_histList->show();
            m_urlEdit->setFocus();
        });
        connect(bClose, &QPushButton::clicked, [this]() { close(); });

        m_idle.setSingleShot(true);
        m_idle.setInterval(IDLE_MS);
        connect(&m_idle, &QTimer::timeout, this, [this]() { klog(u8("opds idle close")); close(); });
        m_idle.start();
        QGuiApplication *ga = static_cast<QGuiApplication *>(QCoreApplication::instance());
        connect(ga, &QGuiApplication::focusWindowChanged, this, [this](QWindow *w) {
            if (w && window() && w != window()->windowHandle()) close();
        });

        qApp->installEventFilter(this);
        m_url = opdsReadUrl();
        if (m_url.isEmpty() || m_url.contains(u8("ВСТАВЬ"))) {
            m_url = u8("http://");
            m_cursor = m_url.length();
            m_title->setText(u8("Введите адрес OPDS-каталога"));
            showUrl();
            m_list->hide(); m_keyboard->show(); m_histLabel->show(); m_histList->show();
            m_urlEdit->setFocus();
        } else {
            loadFeed(m_url, false);
        }
    }

    ~OpdsWindow() {
        qApp->removeEventFilter(this);
        if (g_opds == this) g_opds = nullptr;
    }

protected:
    bool event(QEvent *e) override {
        if (handleTouch(e)) return true;
        return QWidget::event(e);
    }
    bool eventFilter(QObject *obj, QEvent *e) override {
        const QEvent::Type t = e->type();
        if (!(t == QEvent::TouchBegin || t == QEvent::TouchUpdate || t == QEvent::TouchEnd
              || t == QEvent::TouchCancel) || obj == this || !obj->isWidgetType())
            return false;
        QWidget *w = static_cast<QWidget *>(obj);
        if (isAncestorOf(w)) return handleTouch(e);
        if (w == window() || w->isAncestorOf(this)) {
            const QList<QTouchEvent::TouchPoint> pts = static_cast<QTouchEvent *>(e)->touchPoints();
            if (!pts.isEmpty() && rect().contains(mapFromGlobal(pts.first().screenPos().toPoint())))
                return handleTouch(e);
        }
        return false;
    }

private:
    bool handleTouch(QEvent *e) {
        const QEvent::Type t = e->type();
        if (t != QEvent::TouchBegin && t != QEvent::TouchUpdate && t != QEvent::TouchEnd
            && t != QEvent::TouchCancel) return false;
        const QList<QTouchEvent::TouchPoint> pts = static_cast<QTouchEvent *>(e)->touchPoints();
        const QPoint p = pts.isEmpty() ? QPoint() : mapFromGlobal(pts.first().screenPos().toPoint());
        switch (t) {
        case QEvent::TouchBegin:
            m_idle.start(); m_press.restart(); m_pressPos = p;
            m_moved = false; m_twoFinger = false;
            break;
        case QEvent::TouchUpdate:
            if (pts.size() >= 2) {
                const qreal y = (mapFromGlobal(pts.at(0).screenPos().toPoint()).y()
                                 + mapFromGlobal(pts.at(1).screenPos().toPoint()).y()) / 2.0;
                if (!m_twoFinger) {
                    m_twoFinger = true; m_twoStartY = y;
                    m_scrollStart = m_list->verticalScrollBar()->value();
                } else if (m_list->isVisible() && inList(m_pressPos)) {
                    m_list->verticalScrollBar()->setValue(m_scrollStart - int(y - m_twoStartY));
                }
            } else if (!pts.isEmpty() && (p - m_pressPos).manhattanLength() > SWIPE_PX) {
                m_moved = true;
            }
            break;
        case QEvent::TouchEnd: {
            const bool longTap = m_press.isValid() && m_press.elapsed() >= LONG_TAP_MS;
            if (!pts.isEmpty() && !m_twoFinger && !m_moved) tapAt(p, longTap);
            m_twoFinger = false;
            break;
        }
        default:
            m_twoFinger = false;
            break;
        }
        e->accept();
        return true;
    }
    bool inList(const QPoint &p) const {
        QWidget *w = childAt(p);
        return w && (w == m_list->viewport() || m_list->viewport()->isAncestorOf(w));
    }
    void tapAt(const QPoint &p, bool longTap = false) {
        // История адресов: короткий тап — подставить, долгий — удалить из списка
        if (m_histList->isVisible()) {
            const QPoint hp = m_histList->viewport()->mapFrom(this, p);
            if (m_histList->viewport()->rect().contains(hp)) {
                QTreeWidgetItem *it = m_histList->itemAt(hp);
                if (it) {
                    if (longTap) {
                        delete it;
                        saveHistory();
                        showMsg(u8("Адрес удалён из истории"));
                    } else {
                        m_url = it->text(0);
                        m_cursor = m_url.length();
                        showUrl();
                    }
                }
                return;
            }
        }
        // Тап по строке набора — курсор в эту позицию
        if (m_keyboard->isVisible() && m_urlEdit->isVisible()) {
            const QPoint up = m_urlEdit->mapFrom(this, p);
            if (m_urlEdit->rect().contains(up)) {
                m_cursor = m_urlEdit->cursorAt(up);
                showUrl();
                return;
            }
        }
        if (m_list->isVisible()) {
            const QPoint vp = m_list->viewport()->mapFrom(this, p);
            if (m_list->viewport()->rect().contains(vp)) {
                QTreeWidgetItem *it = m_list->itemAt(vp);
                if (it) { m_selected = it->text(0); openItem(it); }
                return;
            }
        }
        QWidget *w = childAt(p);
        for (QWidget *cur = w; cur && cur != this; cur = cur->parentWidget()) {
            if (QPushButton *b = qobject_cast<QPushButton *>(cur)) {
                if (b->isEnabled() && b->isVisible()) b->click();
                return;
            }
        }
    }
    void showMsg(const QString &s) { m_msg->setText(s); m_msg->show(); m_msgTimer.start(); }

    // --- клавиатура в стиле Plato ---
    void updateKeys() {
        for (int r = 0; r < 4; ++r) {
            const QString row = u8(KB_LAYOUT[m_level][r]);
            for (int c = 0; c < 10; ++c)
                m_keys[r][c]->setText(c < row.size() ? QString(row.at(c)) : QString());
        }
        if (m_bShift) m_bShift->setText(m_shift ? u8("SHIFT+") : u8("Shift"));
    }
    void setKbLevel(int n) { m_level = n; m_shift = false; updateKeys(); }
    void insertChar(QChar ch) {
        m_url.insert(qBound(0, m_cursor, m_url.length()), ch);
        m_cursor = qMin(m_cursor + 1, m_url.length());
        klog(u8("opds key: ") + QString(ch));
        showUrl();
    }
    void backspace() {
        if (m_cursor <= 0) return;
        m_url.remove(m_cursor - 1, 1);
        --m_cursor;
        showUrl();
    }
    void moveCursor(int d) { m_cursor = qBound(0, m_cursor + d, m_url.length()); showUrl(); }

    void showUrl() {
        m_urlEdit->setText(m_url);
        m_urlEdit->setCursor(m_cursor);
    }

    // --- работа со скриптом ---
    static QString opdsReadUrl() {
        QFile f(u8(OPDS_CONF));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
        const QString text = QString::fromUtf8(f.readAll());
        f.close();
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QString l = line.trimmed();
            if (l.startsWith(u8("OPDS_URL="))) return l.mid(9).trimmed();
        }
        return QString();
    }
    void loadHistory() {
        m_histList->clear();
        QString text;
        QFile f(u8(OPDS_HISTORY));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text = QString::fromUtf8(f.readAll());
            f.close();
        }
        if (text.trimmed().isEmpty()) {          // первый запуск — взять готовый список из пакета
            QFile d(u8(OPDS_HISTORY_DEFAULT));
            if (d.open(QIODevice::ReadOnly | QIODevice::Text)) {
                text = QString::fromUtf8(d.readAll());
                d.close();
            }
        }
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            if (m_histList->topLevelItemCount() >= 12) break;   // в истории не больше 12 адресов
            const QString u = line.trimmed();
            if (u.isEmpty()) continue;
            QTreeWidgetItem *it = new QTreeWidgetItem(m_histList);
            it->setText(0, u);
        }
        if (m_histList->topLevelItemCount() > 0 && !QFile::exists(u8(OPDS_HISTORY)))
            saveHistory();     // создать history.txt из готового списка
    }
    void saveHistory() {
        QDir().mkpath(QFileInfo(u8(OPDS_HISTORY)).absolutePath());
        QFile f(u8(OPDS_HISTORY));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;
        for (int i = 0; i < m_histList->topLevelItemCount(); ++i)
            f.write((m_histList->topLevelItem(i)->text(0) + u8("\n")).toUtf8());
        f.close(); ::sync();
    }
    void addHistory(const QString &url) {
        for (int i = 0; i < m_histList->topLevelItemCount(); ++i)
            if (m_histList->topLevelItem(i)->text(0) == url) { delete m_histList->takeTopLevelItem(i); break; }
        QTreeWidgetItem *it = new QTreeWidgetItem(m_histList);
        it->setText(0, url);
        while (m_histList->topLevelItemCount() > 12)
            delete m_histList->takeTopLevelItem(m_histList->topLevelItemCount() - 1);
        saveHistory();
    }
    void opdsSaveUrl(const QString &url) {
        QDir().mkpath(QFileInfo(u8(OPDS_CONF)).absolutePath());
        QFile f(u8(OPDS_CONF));
        QString text;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text = QString::fromUtf8(f.readAll()); f.close();
        }
        QStringList out;
        for (const QString &line : text.split(QLatin1Char('\n')))
            if (!line.trimmed().startsWith(u8("OPDS_URL="))) out << line;
        out << u8("OPDS_URL=") + url;
        if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            f.write(out.join(u8("\n")).toUtf8()); f.close(); ::sync();
        }
    }
    // ВАЖНО: нельзя ждать скрипт в event loop Nickel — main thread блокируется, через 8 с
    // ридер гасит экран и watchdog перезапускает интерфейс. Только асинхронный QProcess.
    bool runScriptAsync(const QStringList &args, int op, const QString &url,
                        const QString &title, bool push) {
        if (m_proc && m_proc->state() != QProcess::NotRunning) {
            showMsg(u8("Ещё загружается…"));
            return false;
        }
        if (!m_proc) {
            m_proc = new QProcess(this);
            connect(m_proc,
                    static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                    this, [this](int code, QProcess::ExitStatus st) { onScriptDone(code, st); });
        }
        m_op = op; m_pendingUrl = url; m_dlTitle = title;
        m_prevUrl = m_url; m_pendingPush = push;
        m_proc->start(u8("/bin/sh"), QStringList() << u8(OPDS_SCRIPT) << args);
        return m_proc->waitForStarted(3000);
    }
    void onScriptDone(int code, QProcess::ExitStatus st) {
        const bool ok = (st == QProcess::NormalExit && code == 0);
        const QString err = QString::fromUtf8(m_proc->readAllStandardError());
        if (!err.isEmpty()) klog(u8("opds script: ") + err.trimmed());
        if (m_op == 1) {                      // загрузка фида
            if (!ok) {
                m_title->setText(u8("OPDS — ошибка подключения"));
                showMsg(u8("Не удалось получить каталог. Подробности: /tmp/opds.log"));
                m_op = 0; return;
            }
            if (m_pendingPush) m_history << m_prevUrl;
            m_url = m_pendingUrl;
            opdsSaveUrl(m_url);
            parseList();
            QString feedTitle;
            QFile mf(u8(OPDS_META));
            if (mf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QStringList mp = QString::fromUtf8(mf.readAll()).trimmed().split(OPDS_SEP);
                if (mp.size() > 1) feedTitle = mp.at(1);
                mf.close();
            }
            m_title->setText(feedTitle.isEmpty() ? m_url : feedTitle);
            m_next.clear();
            QFile nf(u8(OPDS_NEXT));
            if (nf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString ls = QString::fromUtf8(nf.readAll());
                nf.close();
                for (const QString &line : ls.split(QLatin1Char('\n')))
                    if (!line.trimmed().isEmpty()) { m_next = line.trimmed(); break; }
            }
            if (m_entries.isEmpty()) showMsg(u8("Каталог пуст или ссылки не OPDS"));
        } else if (m_op == 2) {               // скачивание книги
            m_title->setText(m_dlTitle);
            if (!ok) showMsg(u8("Ошибка скачивания. Подробности: /tmp/opds.log"));
            else showMsg(u8("Скачано. Нажмите «Обновить библиотеку» в меню"));
        }
        m_op = 0;
    }
    void parseList() {
        m_entries.clear();
        m_list->clear();
        QFile f(u8(OPDS_LIST));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        const QString text = QString::fromUtf8(f.readAll());
        f.close();
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            if (line.isEmpty()) continue;
            const QStringList p = line.split(OPDS_SEP);
            if (p.size() < 4) continue;
            OpdsItem it;
            it.kind = p.at(1); it.title = p.at(2); it.href = p.at(3);
            it.ext  = p.size() > 4 ? p.at(4) : QString();
            it.type = p.size() > 5 ? p.at(5) : QString();
            const QString label = (it.kind == u8("CAT") ? u8("[КАТ] ") : u8("[КНИГА] "))
                                + it.title
                                + (!it.ext.isEmpty() && it.ext != u8("-") ? u8(".") + it.ext : QString());
            m_entries << it;
            QTreeWidgetItem *w = new QTreeWidgetItem(m_list);
            w->setText(0, label);
            w->setData(0, ROLE_DIR, it.kind == u8("CAT"));  // каталоги полужирным, как в kobrowse
        }
    }
    void loadFeed(const QString &url, bool pushHistory) {
        m_title->setText(u8("Загрузка каталога…"));
        if (!runScriptAsync(QStringList() << u8("url") << url, 1, url, QString(), pushHistory))
            showMsg(u8("Не удалось запустить загрузку"));
    }
    void openItem(QTreeWidgetItem *it) {
        const int row = m_list->indexOfTopLevelItem(it);
        if (row < 0 || row >= m_entries.size()) return;
        const OpdsItem e = m_entries.at(row);
        if (e.kind == u8("CAT")) { loadFeed(e.href, true); return; }
        m_title->setText(u8("Скачивание…"));
        if (!runScriptAsync(QStringList() << u8("dl") << e.href << e.ext << e.title,
                            2, QString(), e.title, false))
            showMsg(u8("Не удалось запустить скачивание"));
    }
    void openCurrent() {
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            if (m_list->topLevelItem(i)->text(0) == m_selected) { openItem(m_list->topLevelItem(i)); return; }
        showMsg(u8("Коснитесь строки каталога или книги"));
    }
    void goBack() {
        if (m_history.isEmpty()) { showMsg(u8("Назад некуда")); return; }
        const QString u = m_history.takeLast();
        loadFeed(u, false);
    }
    void goNext() {
        if (m_next.isEmpty()) { showMsg(u8("Следующей страницы нет")); return; }
        loadFeed(m_next, true);
    }
    void keyboardDone() {
        m_url = m_url.trimmed();
        if (m_url.isEmpty() || !m_url.contains(u8("://"))) {
            showMsg(u8("Адрес должен начинаться с http:// или https://"));
            return;
        }
        addHistory(m_url);
        m_keyboard->hide(); m_histLabel->hide(); m_histList->hide(); m_list->show();
        loadFeed(m_url, false);
    }

    QLabel *m_title, *m_msg, *m_histLabel;
    UrlField *m_urlEdit;
    QTreeWidget *m_list, *m_histList;
    QWidget *m_keyboard;
    QPushButton *m_bShift;
    QPushButton *m_keys[4][10];
    int m_cursor, m_level;
    bool m_shift;
    QProcess *m_proc;
    int m_op;                 // 0 — нет, 1 — фид, 2 — скачивание
    bool m_pendingPush;
    QString m_prevUrl, m_pendingUrl, m_dlTitle;
    QTimer m_msgTimer, m_idle;
    QElapsedTimer m_press;
    QPoint m_pressPos;
    qreal m_twoStartY = 0;
    int m_scrollStart = 0;
    bool m_moved = false, m_twoFinger = false;
    QString m_url, m_next, m_selected;
    QStringList m_history;
    QList<OpdsItem> m_entries;
};

static void opds_open() {
    if (g_opds) { g_opds->raise(); return; }
    loadConfig();     // без этого IDLE_MS=0 и размеры кнопок=0, пока не откроют «Файлы»
    QWidget *host = koboHostWindow();
    if (!host) { klog(u8("opds: no host window")); return; }
    g_opds = new OpdsWindow(host);
    g_opds->setGeometry(host->rect());
    g_opds->show();
    g_opds->raise();
    klog(u8("opds dialog shown"));
}

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

class SearchWindow : public QWidget {
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
        lay->addWidget(m_kbBox);

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

        m_msg = new QLabel(this); m_msg->setFont(f); m_msg->setWordWrap(true); m_msg->hide();
        lay->addWidget(m_msg);
        m_msgTimer.setSingleShot(true); m_msgTimer.setInterval(5000);
        connect(&m_msgTimer, &QTimer::timeout, this, [this]() { m_msg->hide(); });

        m_idle.setSingleShot(true); m_idle.setInterval(IDLE_MS);
        connect(&m_idle, &QTimer::timeout, this, [this]() { close(); });
        m_idle.start();
        connect(static_cast<QGuiApplication *>(QCoreApplication::instance()),
                &QGuiApplication::focusWindowChanged, this, [this](QWindow *w) {
                    if (w && window() && w != window()->windowHandle()) close();
                });
        qApp->installEventFilter(this);
    }
    ~SearchWindow() { qApp->removeEventFilter(this); if (g_search == this) g_search = nullptr; }

protected:
    void showEvent(QShowEvent *e) override {
        QWidget::showEvent(e);
        const int w = qMax(240, this->width() - 24);
        if (m_kbBox && w != m_lastKbWidth) buildKeyboard();
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
        if (isAncestorOf(w)) return handleTouch(e);
        if (w == window() || w->isAncestorOf(this)) {
            const QList<QTouchEvent::TouchPoint> pts = static_cast<QTouchEvent *>(e)->touchPoints();
            if (!pts.isEmpty() && rect().contains(mapFromGlobal(pts.first().screenPos().toPoint())))
                return handleTouch(e);
        }
        return false;
    }

private:
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
        if (role == "Ret") { releaseCombine(); doSearch(); return; }
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
    void showMsg(const QString &s) { m_msg->setText(s); m_msg->show(); m_msgTimer.start(); }

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
            m_idle.start(); m_press.restart(); m_pressPos = p; m_moved = false; m_twoFinger = false;
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

    QLabel *m_title, *m_msg;
    QueryField *m_field;
    QTreeWidget *m_list;
    QPushButton *m_closeBtn = nullptr;
    QWidget *m_kbBox = nullptr;
    QVBoxLayout *m_kbLay = nullptr;
    QFrame *m_menu = nullptr;
    QVector<QPushButton *> m_btns;
    QFont m_fKey, m_fFunc;
    QStringList m_paths;
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
    QTimer m_idle, m_msgTimer;
};

static void ksearch_open() {
    if (g_search) { g_search->raise(); return; }
    loadConfig();
    QWidget *host = koboHostWindow();
    if (!host) { klog(u8("ksearch: no host")); return; }
    g_search = new SearchWindow(host);
    g_search->setGeometry(host->rect());
    g_search->show(); g_search->raise();
    klog(u8("ksearch window shown"));
}


static void kobrowse_open();
static QTimer *g_reopen = nullptr;   // переоткрыть окно после сохранения настроек

// Пункты экрана «Настройки»: ключ kobrowse.ini, подпись, тип, шаг, пределы, знаков после запятой
enum SetType { S_NUM, S_FONT, S_BOOL, S_OPEN };
struct SetItem { const char *key, *label; SetType type; double step, lo, hi; int dec; };
static const SetItem SET_ITEMS[] = {
    { "list_pt",       "Кегль списка, пт",       S_NUM,  0.5, 4,   24,   1 },
    { "list_font",     "Гарнитура списка",       S_FONT, 0,   0,   0,    0 },
    { "btn_pt",        "Кегль кнопок, пт",       S_NUM,  0.5, 3,  24,    1 },
    { "btn_font",      "Гарнитура кнопок",       S_FONT, 0,   0,   0,    0 },
    { "info_pt",       "Кегль сведений, пт",     S_NUM,  0.5, 3,   14,   1 },
    { "gap_pt",        "Поля и промежутки, пт",  S_NUM,  1,   0,   24,   0 },
    { "btn_pad_pt",    "Поле в кнопке, пт",      S_NUM,  1,   0,   24,   0 },
    { "btn_height_pt", "Высота кнопки, пт",      S_NUM,  1,   12,  60,   0 },
    { "icon_pt",       "Иконка, пт",             S_NUM,  0.5, 3,   24,   1 },
    { "btn_radius_pt", "Скругление углов, пт",   S_NUM,  0.2, 0,   20,   1 },
    { "btn_line_pt",   "Толщина рамки, пт",      S_NUM,  0.1, 0.1, 3,    1 },
    { "long_tap_ms",   "Долгое нажатие, мс",     S_NUM,  50,  150, 3000, 0 },
    { "idle_min",      "Автозакрытие, мин",      S_NUM,  0.5, 0.5, 60,   1 },
    { "show_hidden",   "Скрытые файлы",          S_BOOL, 0,   0,   1,    0 },
    { "scale_pct",     "Масштаб, %",             S_NUM,  5,   50,  200,  0 },
    { "open_books",    "Открытие книг",          S_OPEN, 0,   0,   0,    0 },
};
static const int SET_COUNT = int(sizeof(SET_ITEMS) / sizeof(SET_ITEMS[0]));

// Записать значения в kobrowse.ini: строка «ключ=…» заменяется, комментарии сохраняются
static bool saveConfig(const QStringList &keys, const QStringList &vals) {
    const QString ini = u8(KOBROWSE_INI);
    QString text;
    {
        QFile f(ini);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) { text = QString::fromUtf8(f.readAll()); f.close(); }
    }
    QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < keys.size(); ++i) {
        bool found = false;
        for (QString &l : lines) {
            const QString t = l.trimmed();
            const int eq = t.indexOf(QLatin1Char('='));
            if (eq > 0 && !t.startsWith(QLatin1Char(';')) && t.left(eq).trimmed() == keys[i]) {
                l = keys[i] + u8("=") + vals[i];
                found = true;
            }
        }
        if (!found) lines << keys[i] + u8("=") + vals[i];
    }
    QFile f(ini);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    f.write(lines.join(u8("\n")).toUtf8());
    f.close();
    ::sync();
    return true;
}

// ---------- окно менеджера ----------
// Без Q_OBJECT: только сигналы Qt + лямбды, moc не нужен
class KobrowseWindow : public QWidget {
public:
    // parent — главное окно Nickel: менеджер лежит поверх его содержимого, а системные
    // диалоги Nickel (USB, питание) — отдельные окна и остаются сверху
    explicit KobrowseWindow(QWidget *parent)
        : QWidget(parent),
          m_root(u8(KOBROWSE_ROOT)), m_trash(u8(KOBROWSE_TRASH)), m_path(m_root), m_showHidden(SHOW_HIDDEN)
    {
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
        lay->setContentsMargins(GAP, GAP, GAP, GAP);
        lay->setSpacing(GAP);

        // Текущий путь остаётся видимым при прокрутке списка.
        m_pathLabel = new QLabel(this);
        m_pathLabel->setFont(fMsg);
        m_pathLabel->setWordWrap(true);
        m_pathLabel->setTextFormat(Qt::PlainText);
        lay->addWidget(m_pathLabel);

        // Список файлов и папок
        m_list = new QTreeWidget(this);
        m_list->setFont(fList);
        m_list->setColumnCount(1);
        m_list->setHeaderHidden(true);
        m_list->setRootIsDecorated(false);
        m_list->setIndentation(0);
        m_list->setUniformRowHeights(false);   // высота строки по содержимому
        m_list->setItemDelegate(new RowDelegate(m_list, fList)); // строки рисует RowDelegate
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
        buildSettings(fMsg, fBtn);
        lay->addWidget(m_settings, 1);

        // Панель «Распаковать архив?»: короткий тап по .zip
        m_unzip = new QWidget(this);
        m_unzip->setObjectName(u8("unzip"));
        m_unzip->setStyleSheet(u8("QWidget#unzip{border:2px solid black;}"));
        m_unzip->setAttribute(Qt::WA_StyledBackground, true);
        auto *ul = new QVBoxLayout(m_unzip);
        ul->setContentsMargins(GAP, GAP, GAP, GAP);
        ul->setSpacing(GAP);
        m_unzipTitle = new QLabel(m_unzip);
        m_unzipTitle->setFont(fMsg);
        m_unzipTitle->setWordWrap(true);
        m_unzipTitle->setTextFormat(Qt::PlainText);
        ul->addWidget(m_unzipTitle);
        auto *urow = new QHBoxLayout();
        urow->setSpacing(GAP);
        auto *bUz    = makeButton(urow, fBtn, "Распаковать", BTN_HEIGHT, 1);
        auto *bUzDel = makeButton(urow, fBtn, "Распаковать, архив в корзину", BTN_HEIGHT, 1);
        auto *bUzNo  = makeButton(urow, fBtn, "Отмена", BTN_HEIGHT, 1);
        ul->addLayout(urow);
        m_unzip->hide();
        lay->addWidget(m_unzip);
        connect(bUz,    &QPushButton::clicked, [this]() { unzipFile(m_zipPath, false); });
        connect(bUzDel, &QPushButton::clicked, [this]() { unzipFile(m_zipPath, true); });
        connect(bUzNo,  &QPushButton::clicked, [this]() { m_unzip->hide(); });

        // Сообщение об ошибке: скрыто, показывается на 4 с
        m_msg = new QLabel(this);
        m_msg->setFont(fMsg);
        m_msg->setWordWrap(true);
        m_msg->setAlignment(Qt::AlignCenter);
        m_msg->hide();
        lay->addWidget(m_msg);
        m_msgTimer.setSingleShot(true);
        m_msgTimer.setInterval(4000);
        connect(&m_msgTimer, &QTimer::timeout, this, [this]() { m_msg->hide(); });

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
                    m_list->scrollToItem(it, QAbstractItemView::EnsureVisible);
                    break;
                }
            }
        });

        // Ряд 2 — действия: появляется, когда есть выбор или что-то в буфере.
        // 4 кнопки в ряд: при 6–7 подписи упирались в ~16 px и «Кегль кнопок» не влиял.
        m_actions = new QWidget(this);
        auto *agrid = new QGridLayout(m_actions);
        agrid->setContentsMargins(0, 0, 0, 0);
        agrid->setSpacing(GAP);
        for (int c = 0; c < 12; ++c) agrid->setColumnStretch(c, 1);
        m_bCopy  = addButtonAt(agrid, 0, 0, 3, fBtn, "Копир.", I_COPY);
        m_bCut   = addButtonAt(agrid, 0, 3, 3, fBtn, "Вырезать", I_CUT);
        m_bTrash = addButtonAt(agrid, 0, 6, 3, fBtn, "В корзину", I_TRASH);
        m_bInfo  = addButtonAt(agrid, 0, 9, 3, fBtn, "Сведения", I_INFO);
        m_bClear = addButtonAt(agrid, 1, 0, 4, fBtn, "Снять выбор", I_DESELECT);
        m_bPaste = addButtonAt(agrid, 1, 4, 4, fBtn, "Вставить", I_CLIPBOARD);
        m_bDrop  = addButtonAt(agrid, 1, 8, 4, fBtn, "Очистить буфер", I_CLIPBOARD_X);
        lay->addWidget(m_actions);

        // Ряд 1 — навигация: 4 кнопки + 2 широкие под ними (крупные подписи)
        m_nav = new QWidget(this);
        auto *ngrid = new QGridLayout(m_nav);
        ngrid->setContentsMargins(0, 0, 0, 0);
        ngrid->setSpacing(GAP);
        for (int c = 0; c < 12; ++c) ngrid->setColumnStretch(c, 1);
        m_btnUp     = addButtonAt(ngrid, 0, 0, 3, fBtn, "Вверх", I_FOLDER_UP);
        auto *bPrev = addButtonAt(ngrid, 0, 3, 3, fBtn, "Пред.", I_CHEV_UP);
        auto *bNext = addButtonAt(ngrid, 0, 6, 3, fBtn, "След.", I_CHEV_DOWN);
        auto *bFind = addButtonAt(ngrid, 0, 9, 3, fBtn, "Поиск", I_INFO);
        auto *bSet  = addButtonAt(ngrid, 1, 0, 6, fBtn, "Настройки", I_GEAR);
        auto *bClose= addButtonAt(ngrid, 1, 6, 6, fBtn, "Закрыть", I_CLOSE);
        lay->addWidget(m_nav);

        connect(bFind, &QPushButton::clicked, [this]() { ksearch_open(); });
        connect(m_btnUp, &QPushButton::clicked, [this]() { goUp(); });
        connect(bPrev, &QPushButton::clicked, [this]() {
            m_list->verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepSub); });
        connect(bNext, &QPushButton::clicked, [this]() {
            m_list->verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd); });
        connect(bSet, &QPushButton::clicked, [this]() { showSettings(true); });
        connect(bClose, &QPushButton::clicked, [this]() { close(); });

        connect(m_bCopy,  &QPushButton::clicked, [this]() { toClipboard(false); });
        connect(m_bCut,   &QPushButton::clicked, [this]() { toClipboard(true); });
        connect(m_bTrash, &QPushButton::clicked, [this]() { moveToTrash(); });
        connect(m_bInfo,  &QPushButton::clicked, [this]() { showInfo(); });
        connect(m_bClear, &QPushButton::clicked, [this]() { m_marked.clear(); load(); });
        connect(m_bPaste, &QPushButton::clicked, [this]() { paste(); });
        connect(m_bDrop,  &QPushButton::clicked, [this]() { m_clip.clear(); updateActions(); });

        // Страховка 1: закрыть после IDLE_MS без касаний
        m_idle.setSingleShot(true);
        m_idle.setInterval(IDLE_MS);
        connect(&m_idle, &QTimer::timeout, this, [this]() { klog(u8("idle close")); close(); });
        m_idle.start();

        // Страховка 2: закрыться, если Nickel показал другое окно (диалог USB и т.п.)
        QGuiApplication *ga = static_cast<QGuiApplication *>(QCoreApplication::instance());
        connect(ga, &QGuiApplication::focusWindowChanged, this, [this](QWindow *w) {
            if (w && window() && w != window()->windowHandle()) {
                klog(u8("focus changed -> close"));
                close();
            }
        });

        load();

        // Касания внутри окна перехватываем на уровне приложения (см. eventFilter)
        qApp->installEventFilter(this);
    }

    ~KobrowseWindow() { qApp->removeEventFilter(this); }

protected:
    // Касания обрабатываем сами: TouchBegin нужно принять, иначе TouchEnd не придёт.
    // Касание с дочернего виджета (кнопка, список) поднимается сюда. По TouchEnd смотрим
    // длительность: короткое — нажатие, от LONG_TAP_MS — долгое (выбор строки).
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
        if (t != QEvent::TouchBegin && t != QEvent::TouchUpdate && t != QEvent::TouchEnd
            && t != QEvent::TouchCancel)
            return false;
        const QList<QTouchEvent::TouchPoint> pts = static_cast<QTouchEvent *>(e)->touchPoints();
        const QPoint p = pts.isEmpty() ? QPoint() : mapFromGlobal(pts.first().screenPos().toPoint());
        switch (t) {
        case QEvent::TouchBegin:
            m_idle.start();       // касание продлевает таймер простоя
            m_press.start();
            m_pressPos = p;
            m_moved = false;
            m_twoFinger = false;
            break;
        case QEvent::TouchUpdate:
            if (pts.size() >= 2) {
                // Два пальца — перетаскивание списка (по среднему положению пальцев)
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
                m_moved = true;   // один палец сдвинулся — это не нажатие
            }
            break;
        case QEvent::TouchEnd: {
            const bool longTap = m_press.isValid() && m_press.elapsed() >= LONG_TAP_MS;
            if (m_twoFinger && m_tapLog > 0) { --m_tapLog; klog(u8("two-finger scroll")); }
            if (!pts.isEmpty() && !m_twoFinger && !m_moved) tapAt(p, longTap);
            m_twoFinger = false;
            break;
        }
        default:              // TouchCancel
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
        fitButtonFont();
    }

private:
    bool inList(const QPoint &p) const {
        QWidget *w = childAt(p);
        return w && (w == m_list->viewport() || m_list->viewport()->isAncestorOf(w));
    }

    // Показать отмеченную строку после того, как раскладка обновится
    // (ряд действий и сведения сжимают список и могли её закрыть)
    void ensureMarkedVisible() { m_ensure.start(); }

    // Касание в точке p (координаты окна)
    void tapAt(const QPoint &p, bool longTap) {
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
                    stepSetting(stepRow.toInt(), dir);
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

    // Кнопка с подписью-QLabel: выключка по центру, перенос в две строки, если не помещается.
    // Касание по подписи поднимается в tapAt до QPushButton.
    QPushButton *addButtonAt(QGridLayout *g, int r, int c, int span, const QFont &f,
                             const char *text, Icon ic) {
        auto *b = new QPushButton(this);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(BTN_HEIGHT);
        auto *bl = new QHBoxLayout(b);
        bl->setContentsMargins(0, 0, 0, 0);
        auto *face = new ButtonFace(ic, u8(text), b);
        face->setFont(f);
        bl->addWidget(face);
        m_faces << face;
        g->addWidget(b, r, c, 1, span);
        return b;
    }

    static void setButtonFace(QPushButton *b, Icon ic, const QString &text) {
        if (ButtonFace *f = b->findChild<ButtonFace *>()) f->setFace(ic, text);
    }

    // Сколько строк займёт подпись при переносе по словам; длинное слово рвётся по символам.
    static int wrappedLines(const QString &text, const QFontMetrics &fm, int inner) {
        if (inner <= 0) return 1;
        const QStringList words = text.split(QLatin1Char(' '), QString::SkipEmptyParts);
        int lines = 1, cur = 0;
        for (int i = 0; i < words.size(); ++i) {
            const QString w = words.at(i);
            const int ww = fm.width(w);
            const int sp = (cur > 0) ? fm.width(QLatin1Char(' ')) : 0;
            if (cur > 0 && cur + sp + ww <= inner) { cur += sp + ww; continue; }
            if (ww > inner) {
                int cw = 0;
                for (int j = 0; j < w.size(); ++j) {
                    const int ch = fm.width(w.mid(j, 1));
                    if (cw > 0 && cw + ch > inner) { ++lines; cw = ch; }
                    else cw += ch;
                }
                cur = cw;
            } else {
                if (cur > 0) ++lines;
                cur = ww;
            }
        }
        return lines;
    }

    // Высота кнопки под подпись при текущем кегле: столько строк, сколько реально нужно
    // с учётом переноса (в том числе и длинных слов), но не меньше BTN_HEIGHT.
    static void fitFaceHeight(ButtonFace *face, int inner) {
        QWidget *b = face->parentWidget();
        if (!b || inner <= 0) return;
        const QFontMetrics fm(face->font());
        const int lines = wrappedLines(face->text(), fm, inner);
        const int need = 12 + ICON_PX + 4 + lines * fm.lineSpacing() + 6;
        const int h = qMax(BTN_HEIGHT, need);
        if (b->minimumHeight() != h) b->setMinimumHeight(h);
    }

    // Кегль подписей = btn_pt из настроек. Раньше здесь кегль ужимался до ширины
    // самой длинной подписи (~28 px) и изменение btn_pt визуально ничего не меняло.
    // Теперь подписи переносятся по словам/символам, а высота кнопок подстраивается.
    void fitButtonFont() {
        const int btnW  = (width() - 2 * GAP - 3 * GAP) / 4;   // 4 кнопки в ряд
        const int wideW = (width() - 2 * GAP - 2 * GAP) / 3;   // широкие ряды (настройки, распаковка)
        const int inner = btnW - 2 * BTN_MARGIN - 4;
        if (inner <= 0 || m_faces.isEmpty()) return;
        QFont f = m_faces.first()->font();
        const int px = FONT_BTN;
        f.setPixelSize(px);
        static int lastPx = -1;
        if (px != lastPx) {
            lastPx = px;
            klog(u8("button font: %1 px (btn_pt=%2 px, btn %3, inner %4)")
                     .arg(px).arg(FONT_BTN).arg(btnW).arg(inner));
        }
        for (ButtonFace *face : m_faces) { face->setFont(f); fitFaceHeight(face, inner); }
        const int wideInner = wideW - 2 * BTN_MARGIN - 4;
        for (ButtonFace *face : m_wideFaces) { face->setFont(f); fitFaceHeight(face, wideInner); }
    }

    void showMessage(const QString &text) {
        m_msg->setText(text);
        m_msg->show();
        m_msgTimer.start();
    }

    // Защищённые пути: служебные данные Nickel и сама корзина — не переносим и не удаляем
    bool isProtected(const QString &path) const {
        const QString kobo = m_root + u8("/.kobo");
        return path == m_root || path == kobo || path.startsWith(kobo + u8("/"))
            || path == m_root + u8("/.adds") || path == m_trash;
    }

    // Отметка строки: флаг для MarkDelegate (он рисует инверсию)
    void setMarkedLook(QTreeWidgetItem *it, bool on) {
        it->setData(0, ROLE_MARK, on);
        if (on) m_lastMarked = it->data(0, ROLE_PATH).toString();
    }

    // Сведения об одном отмеченном файле/папке
    void showInfo() {
        if (m_marked.size() != 1) return;
        const QFileInfo fi(*m_marked.begin());
        QString t = fi.fileName() + u8("\n\n");
        if (fi.isDir()) {
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
        const bool on = !m_marked.contains(path);
        if (on) m_marked.insert(path); else m_marked.remove(path);
        setMarkedLook(it, on);
        updateActions();
        ensureMarkedVisible();
    }

    // Состав ряда действий: есть выбор → операции над выбором; иначе есть буфер → вставка
    void updateActions() {
        m_info->hide(); // любое изменение выбора убирает сведения
        const bool sel = !m_marked.isEmpty();
        const bool clip = !m_clip.isEmpty();
        m_bCopy->setVisible(sel);
        m_bCut->setVisible(sel);
        m_bTrash->setVisible(sel);
        m_bInfo->setVisible(m_marked.size() == 1);
        m_bClear->setVisible(sel);
        m_bPaste->setVisible(!sel && clip);
        m_bDrop->setVisible(!sel && clip);
        if (clip)
            setButtonFace(m_bPaste, I_CLIPBOARD,
                          (m_clipCut ? u8("Переместить сюда (%1)") : u8("Вставить (%1)")).arg(m_clip.size()));
        m_actions->setVisible(sel || clip);
    }

    // Копировать / Вырезать: запомнить отмеченное, снять выбор
    void toClipboard(bool cut) {
        QStringList list;
        for (const QString &p : m_marked) {
            if (cut && isProtected(p)) continue;
            list << p;
        }
        if (cut && list.size() < m_marked.size()) showMessage(u8("Служебные файлы Kobo не перемещаются"));
        m_clip = list;
        m_clipCut = cut;
        m_marked.clear();
        load();
    }

    // Вставить буфер в текущую папку
    void paste() {
        if (isProtected(m_path) && m_path != m_root) { showMessage(u8("Сюда вставлять нельзя")); return; }
        QString err;
        int done = 0;
        for (const QString &src : m_clip) {
            const QFileInfo fi(src);
            if (!fi.exists()) continue;
            const bool isDir = fi.isDir() && !fi.isSymLink();
            // Нельзя вставить папку в саму себя или в её подпапку
            if (isDir && (m_path == src || m_path.startsWith(src + u8("/")))) {
                err = u8("Папку нельзя вставить в саму себя");
                continue;
            }
            // Перемещение в ту же папку — ничего не делаем
            if (m_clipCut && fi.absolutePath() == m_path) { ++done; continue; }
            const QString dst = uniqueName(m_path, fi.fileName(), isDir);
            bool ok;
            if (m_clipCut) {
                ok = QDir().rename(src, dst); // одна ФС (/mnt/onboard) — мгновенно
                if (!ok) err = u8("Не удалось переместить ") + fi.fileName();
            } else {
                ok = copyRecursive(src, dst, err);
            }
            if (ok) ++done;
        }
        ::sync(); // сбросить записанное на накопитель
        if (m_clipCut) m_clip.clear(); // после переноса буфер пуст; копию можно вставлять ещё раз
        if (!err.isEmpty()) showMessage(err);
        klog(u8("paste %1 item(s) to %2").arg(done).arg(m_path));
        load();
    }

    // В корзину: перенос в /mnt/onboard/.kobrowse_trash с датой в начале имени
    void moveToTrash() {
        QDir().mkpath(m_trash);
        const QString stamp = QDateTime::currentDateTime().toString(u8("yyyyMMdd-hhmmss_"));
        int skipped = 0;
        for (const QString &src : m_marked) {
            if (isProtected(src)) { ++skipped; continue; }
            const QFileInfo fi(src);
            const QString dst = uniqueName(m_trash, stamp + fi.fileName(), fi.isDir());
            if (!QDir().rename(src, dst)) ++skipped;
            m_clip.removeAll(src);
        }
        ::sync();
        if (skipped) showMessage(u8("Не перенесено в корзину: %1").arg(skipped));
        m_marked.clear();
        load();
    }

    // Прочитать текущую папку и заполнить список
    void load() {
        if (m_unzip) m_unzip->hide();
        m_pathLabel->setText(m_path);
        m_btnUp->setEnabled(m_path != m_root);
        m_btnUp->update();
        m_list->clear();
        updateActions();

        QDir dir(m_path);
        if (!dir.isReadable()) {
            new QTreeWidgetItem(m_list, QStringList() << u8("Нет доступа к папке"));
            return;
        }

        QDir::Filters flt = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
        if (m_showHidden) flt |= QDir::Hidden;
        // Папки сверху, по имени без учёта регистра; список читается разом,
        // дескрипторы на /mnt/onboard не удерживаются (важно для USB-режима)
        const QFileInfoList entries =
            dir.entryInfoList(flt, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

        if (m_path != m_root) {
            QTreeWidgetItem *parent = new QTreeWidgetItem(m_list);
            parent->setText(0, u8(".."));
            parent->setData(0, ROLE_DIR, true);
            parent->setData(0, ROLE_PARENT, true);
            parent->setData(0, ROLE_DETAIL, u8("Родительская папка"));
        }
        for (const QFileInfo &fi : entries) {
            if (fi.fileName().endsWith(u8(".kobrowse_tmp"))) continue; // недокопированные файлы не показываем
            auto *it = new QTreeWidgetItem(m_list);
            const bool isDir = fi.isDir();
            const QString path = fi.absoluteFilePath();
            it->setText(0, fi.fileName()); // имя целиком, с расширением; папки — полужирным
            QString detail = fi.lastModified().toString(u8("dd.MM.yyyy hh:mm"));
            if (!isDir && !fi.isSymLink()) detail += u8("  ·  ") + humanSize(fi.size());
            if (fi.isSymLink()) detail += u8("  ·  → ") + fi.symLinkTarget();
            it->setData(0, ROLE_DETAIL, detail);
            it->setData(0, ROLE_PATH, path);
            it->setData(0, ROLE_DIR, isDir);
            if (m_marked.contains(path)) setMarkedLook(it, true);
        }
        m_list->scrollToTop();
    }

    // Нажатие на строку: папка — войти; файлы — открытие на следующем этапе
    void onItem(QTreeWidgetItem *it) {
        if (!it) return;
        if (it->data(0, ROLE_PARENT).toBool()) { goUp(); return; }
        const QString path = it->data(0, ROLE_PATH).toString();
        if (path.isEmpty()) return;
        if (it->data(0, ROLE_DIR).toBool()) {
            m_path = path;
            m_marked.clear();
            load();
            return;
        }
        // Архив .zip — предложить распаковать
        if (path.endsWith(u8(".zip"), Qt::CaseInsensitive)) {
            m_zipPath = path;
            m_unzipTitle->setText(u8("Распаковать «") + QFileInfo(path).fileName() + u8("» в эту папку?"));
            m_info->hide();
            m_unzip->show();
            return;
        }
        // Файл — открыть читалкой, выбранной в Kobalt («Автоматически открывать книги в Kobalt»)
        openFile(path);
    }

    // Распаковка .zip в текущую папку: busybox unzip во временную папку (с точкой — Nickel её
    // не индексирует), затем перенос содержимого рядом с архивом; при совпадении имён — «имя (1)».
    // Архив по желанию — в корзину kobrowse.
    void unzipFile(const QString &zip, bool trashZip) {
        m_unzip->hide();
        const QFileInfo zi(zip);
        const QString dir = zi.absolutePath();
        const QString tmp = dir + u8("/.kobrowse_unzip_")
                          + QDateTime::currentDateTime().toString(u8("yyyyMMdd-hhmmss"));
        QDir().mkpath(tmp);
        QProcess pr;
        pr.start(u8("/usr/bin/unzip"), QStringList() << u8("-o") << u8("-q") << zip << u8("-d") << tmp);
        const bool ok = pr.waitForFinished(180000) && pr.exitStatus() == QProcess::NormalExit
                        && pr.exitCode() == 0;
        if (!ok) {
            klog(u8("unzip failed: ") + QString::fromLocal8Bit(pr.readAllStandardError()));
            QDir(tmp).removeRecursively();
            showMessage(u8("Не удалось распаковать архив"));
            return;
        }
        int n = 0;
        QString first;
        const QFileInfoList items = QDir(tmp).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &fi : items) {
            const QString dst = uniqueName(dir, fi.fileName(), fi.isDir());
            if (QDir().rename(fi.absoluteFilePath(), dst)) { ++n; if (first.isEmpty()) first = dst; }
        }
        QDir(tmp).removeRecursively();
        if (trashZip && n > 0) {
            QDir().mkpath(m_trash);
            const QString stamp = QDateTime::currentDateTime().toString(u8("yyyyMMdd-hhmmss_"));
            QDir().rename(zip, uniqueName(m_trash, stamp + zi.fileName(), false));
            m_clip.removeAll(zip);
        }
        ::sync();
        klog(u8("unzip %1: %2 item(s)").arg(zi.fileName()).arg(n));
        load();
        showMessage(n ? u8("Распаковано: %1").arg(n) : u8("Архив пуст"));
        if (n == 1) {                      // показать распакованный файл
            m_lastMarked = first;
            ensureMarkedVisible();
        }
    }

    // Открыть книгу. Окно менеджера сначала прячется (иначе останется поверх читалки);
    // при ошибке показывается снова с сообщением, при успехе — закрывается.
    void openFile(const QString &path) {
        if (!bookOpenAllowed()) {
            showMessage(OPEN_BOOKS == u8("0")
                ? u8("Открытие книг выключено (open_books=0 в kobrowse.ini)")
                : u8("Открытие книг проверено только на прошивке 4.38. Включить на свой риск: open_books=1 в .adds/kobrowse/kobrowse.ini"));
            return;
        }
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
        bl->addWidget(face);
        m_wideFaces << face;               // кегль общий, но в подборе кегля не участвует (кнопки шире)
        row->addWidget(b, stretch);
        return b;
    }

    // Кнопка «−»/«+» строки настроек. Номер строки и направление хранятся в свойствах:
    // их читает tapAt и вызывает stepSetting напрямую (сигнал clicked на e-ink не всегда доходит).
    QPushButton *makeStepButton(QHBoxLayout *row, const QFont &f, const char *text,
                                int h, int w, int idx, int dir) {
        auto *b = new QPushButton(u8(text), this);
        QFont sf = f;
        sf.setPixelSize(qMax(16, qMin(f.pixelSize() > 0 ? f.pixelSize() : 24, int(h * 0.55))));
        b->setFont(sf);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(h);
        b->setFixedWidth(w);
        b->setStyleSheet(u8("QPushButton{border:1px solid #000000;border-radius:%1px;"
                            "background-color:#FFFFFF;color:#000000;padding:0;}"
                            "QPushButton:pressed{background-color:#000000;color:#FFFFFF;}")
                             .arg(qRound(BTN_RADIUS)));
        b->setProperty("set_row", idx);
        b->setProperty("set_dir", dir);
        row->addWidget(b, 0);
        m_stepBtns << b;
        return b;
    }

    void buildSettings(const QFont &fText, const QFont &fBtn) {
        m_settings = new QWidget(this);
        auto *v = new QVBoxLayout(m_settings);
        v->setContentsMargins(0, 0, 0, 0);
        // Размеры — в долях от экрана Clara HD (SCREEN_DPI = 300 при ширине 1072), чтобы 16 строк
        // помещались на любом ридере
        const double sc = SCREEN_DPI / 300.0;
        v->setSpacing(qRound(6 * sc));
        const int rowH = qRound(62 * sc);
        for (int i = 0; i < SET_COUNT; ++i) {
            auto *r = new QHBoxLayout();
            r->setSpacing(qRound(10 * sc));
            auto *name = new QLabel(u8(SET_ITEMS[i].label), m_settings);
            name->setFont(fText);
            r->addWidget(name, 1);
            auto *val = new QLabel(m_settings);
            val->setFont(fText);
            val->setAlignment(Qt::AlignCenter);
            val->setMinimumWidth(qRound(230 * sc));
            r->addWidget(val);
            m_setLabels << val;
            // «−»/«+» — обычные кнопки с текстом (без ButtonFace). Номер строки и направление
            // лежат в свойствах set_row/set_dir: tapAt вызывает stepSetting прямо по касанию,
            // не полагаясь на сигнал clicked.
            const int bw = qRound(100 * sc);
            makeStepButton(r, fBtn, "−", rowH, bw, i, -1);
            makeStepButton(r, fBtn, "+", rowH, bw, i, +1);
            v->addLayout(r);
        }
        v->addStretch(1);
        auto *bottom = new QHBoxLayout();
        bottom->setSpacing(GAP);
        auto *bSave = makeButton(bottom, fBtn, "Сохранить", BTN_HEIGHT, 1);
        auto *bDef  = makeButton(bottom, fBtn, "По умолчанию", BTN_HEIGHT, 1);
        auto *bCanc = makeButton(bottom, fBtn, "Отмена", BTN_HEIGHT, 1);
        v->addLayout(bottom);
        connect(bSave, &QPushButton::clicked, [this]() { saveSettings(); });
        connect(bDef,  &QPushButton::clicked, [this]() {
            for (int i = 0; i < SET_COUNT; ++i) m_setVals[i] = cfgDefault(u8(SET_ITEMS[i].key));
            refreshSettings();
        });
        connect(bCanc, &QPushButton::clicked, [this]() { showSettings(false); });
        m_settings->hide();

        // Гарнитуры для выбора: пусто (как в Nickel) + все шрифты, известные Qt на ридере
        m_fonts << QString() << QFontDatabase().families();
    }

    void showSettings(bool on) {
        if (on) {
            QSettings st(u8(KOBROWSE_INI), QSettings::IniFormat);
            st.setIniCodec(QTextCodec::codecForName("UTF-8"));
            m_setVals.clear();
            for (int i = 0; i < SET_COUNT; ++i) {
                QString v = st.value(u8("kobrowse/") + u8(SET_ITEMS[i].key),
                                     cfgDefault(u8(SET_ITEMS[i].key))).toString().trimmed();
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
            refreshSettings();
        }
        m_list->setVisible(!on);
        m_pathLabel->setVisible(!on);
        m_nav->setVisible(!on);
        m_info->hide();
        m_msg->hide();
        m_settings->setVisible(on);
        if (on) { m_actions->hide(); m_setGeom.start(); } else updateActions();
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
            if (SET_ITEMS[i].type == S_BOOL) shown = v.toDouble() != 0 ? u8("показывать") : u8("скрывать");
            if (SET_ITEMS[i].type == S_FONT && v.isEmpty()) shown = u8("как в Nickel");
            if (SET_ITEMS[i].type == S_OPEN)
                shown = v == u8("1") ? u8("всегда") : v == u8("0") ? u8("никогда") : u8("авто (4.38)");
            m_setLabels[i]->setText(shown);
        }
    }

    // [−]/[+]: число — на шаг в пределах; гарнитура — предыдущая/следующая; да/нет — переключить
    void stepSetting(int i, int dir) {
        if (i < 0 || i >= SET_COUNT || i >= m_setVals.size()) return;
        const SetItem &it = SET_ITEMS[i];
        QString &v = m_setVals[i];
        const QString before = v;
        if (it.type == S_BOOL) {
            v = v.toDouble() != 0 ? u8("0") : u8("1");
        } else if (it.type == S_OPEN) {            // по кругу: авто → всегда → никогда
            const char *order[] = { "auto", "1", "0" };
            int k = v == u8("1") ? 1 : v == u8("0") ? 2 : 0;
            k = (k + dir + 3) % 3;
            v = u8(order[k]);
        } else if (it.type == S_FONT) {
            int k = -1;
            for (int j = 0; j < m_fonts.size(); ++j)
                if (m_fonts[j].compare(v, Qt::CaseInsensitive) == 0) { k = j; break; }
            k = (k + dir + m_fonts.size()) % m_fonts.size();
            v = m_fonts[k];
        } else {
            bool ok = false;
            double x = QString(v).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
            if (!ok) x = cfgDefault(u8(it.key)).toDouble();
            x = qBound(it.lo, x + dir * it.step, it.hi);
            v = QString::number(x, 'f', it.dec);
        }
        klog(u8("settings step ") + u8(it.key) + u8(": ") + before + u8(" -> ") + v);
        refreshSettings();
    }

    // Сохранить в kobrowse.ini и переоткрыть менеджер с новыми настройками
    void saveSettings() {
        QStringList keys;
        for (int i = 0; i < SET_COUNT; ++i) keys << u8(SET_ITEMS[i].key);
        if (!saveConfig(keys, m_setVals)) { showMessage(u8("Не удалось записать kobrowse.ini")); return; }
        klog(u8("settings saved"));
        close();
        if (g_reopen) g_reopen->start();
    }

    void goUp() {
        if (m_path == m_root) return;
        QString parent = QFileInfo(m_path).absolutePath();
        if (!parent.startsWith(m_root)) parent = m_root; // не выходим за корень
        m_path = parent;
        m_marked.clear();
        load();
    }

    QString       m_root, m_trash, m_path;
    bool          m_showHidden;
    int           m_logLeft = LOG_EVENTS;
    QTimer        m_idle, m_msgTimer, m_ensure, m_setGeom;
    QPoint        m_pressPos;
    int           m_scrollStart = 0;
    bool          m_twoFinger = false, m_moved = false;
    qreal         m_twoStartY = 0;
    int           m_tapLog = LOG_TAPS;
    QString       m_lastMarked;
    QElapsedTimer m_press;
    QFont         m_bold;
    QSet<QString> m_marked;          // отмеченные пути (в текущей папке)
    QStringList   m_clip;            // буфер: пути для вставки
    bool          m_clipCut = false; // буфер «вырезан» (перенос) или «скопирован»
    QTreeWidget  *m_list = nullptr;
    QLabel       *m_msg = nullptr, *m_info = nullptr, *m_pathLabel = nullptr;
    QWidget      *m_settings = nullptr, *m_nav = nullptr; // экран настроек; ряд навигации
    QList<QLabel *> m_setLabels;        // значения на экране настроек
    QList<QPushButton *> m_stepBtns;    // кнопки «−»/«+» по строкам: 0,1 — строка 0; 2,3 — строка 1 …
    QStringList   m_setVals;            // редактируемые значения (как в kobrowse.ini)
    QStringList   m_fonts;              // гарнитуры для выбора
    QWidget      *m_unzip = nullptr;    // панель «Распаковать архив?»
    QLabel       *m_unzipTitle = nullptr;
    QString       m_zipPath;
    QWidget      *m_actions = nullptr;
    QPushButton  *m_btnUp = nullptr;
    QPushButton  *m_bCopy = nullptr, *m_bCut = nullptr, *m_bTrash = nullptr, *m_bClear = nullptr, *m_bInfo = nullptr;
    QPushButton  *m_bPaste = nullptr, *m_bDrop = nullptr;
    QList<ButtonFace *> m_faces, m_wideFaces;    // подписи всех кнопок — для общего кегля
};

// ---------- запуск окна ----------
static QPointer<KobrowseWindow> g_win;          // одно окно на всё время работы
static QFileSystemWatcher  *g_watch = nullptr;
static QTimer              *g_poll = nullptr;  // страховка: inotify на /tmp иногда молчит

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
    if (QFile::exists(u8(KOBROWSE_TRIGGER))) { QFile::remove(u8(KOBROWSE_TRIGGER)); kobrowse_open(); }
    if (QFile::exists(u8(OPDS_TRIGGER))) { QFile::remove(u8(OPDS_TRIGGER)); opds_open(); }
    if (QFile::exists(u8(KSEARCH_TRIGGER))) { QFile::remove(u8(KSEARCH_TRIGGER)); ksearch_open(); }
    if (QFile::exists(u8(KOBROWSE_DIAG))) {
        QFile::remove(u8(KOBROWSE_DIAG));
        if (g_diagTimer) g_diagTimer->start();
    }
}

// Вызывается один раз при загрузке плагина в Nickel
static int kobrowse_init() {
    QFile::remove(u8(KOBROWSE_TRIGGER)); // убрать старый сигнал, если остался
    QFile::remove(u8(OPDS_TRIGGER));
    QFile::remove(u8(KSEARCH_TRIGGER));
    QFile::remove(u8(KOBROWSE_DIAG));
    if (!QCoreApplication::instance()) {
        nh_log("no QApplication at init, trigger disabled");
        return 0;
    }
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
    g_reopen = new QTimer();
    g_reopen->setSingleShot(true);
    g_reopen->setInterval(300);
    QObject::connect(g_reopen, &QTimer::timeout, []() { kobrowse_open(); });
    nh_log("watching %s", KOBROWSE_TRIGGER);
    return 0;
}

// Описание плагина; файл uninstall_flag на ридере удаляет плагин при загрузке.
// failsafe: если Nickel упадёт в первые 10 с после старта, плагин отключится сам.
static struct nh_info kobrowse_info = {
    .name           = "kobrowse",
    .desc           = "File manager for Kobo",
    .uninstall_flag = "/mnt/onboard/.adds/kobrowse/uninstall",
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
)
