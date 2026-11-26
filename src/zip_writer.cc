#include "zip_writer.h"
#include "file_policy.h"
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QSet>
#include <QTemporaryFile>
#include <QVector>
#include <zlib.h>

namespace {
const qint64 ZIP_LIMIT = 0xffffffffLL;
struct Entry {
    QByteArray name;
    quint32 offset, crc, size, compressed;
    quint16 time, date, method;
    bool directory;
};

bool writeBytes(QFile &file, const QByteArray &bytes) {
    return file.write(bytes.constData(), bytes.size()) == bytes.size();
}
QByteArray localHeader(const Entry &e) {
    QByteArray bytes; QDataStream d(&bytes, QIODevice::WriteOnly); d.setByteOrder(QDataStream::LittleEndian);
    d << quint32(0x04034b50) << quint16(20) << quint16(0x0800) << e.method << e.time << e.date
      << e.crc << e.compressed << e.size << quint16(e.name.size()) << quint16(0);
    bytes += e.name; return bytes;
}
QByteArray centralHeader(const Entry &e) {
    QByteArray bytes; QDataStream d(&bytes, QIODevice::WriteOnly); d.setByteOrder(QDataStream::LittleEndian);
    d << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0x0800) << e.method << e.time << e.date
      << e.crc << e.compressed << e.size << quint16(e.name.size()) << quint16(0) << quint16(0)
      << quint16(0) << quint16(0) << quint32(e.directory ? 0x10 : 0) << e.offset;
    bytes += e.name; return bytes;
}
void setDate(Entry &e, const QDateTime &dt) {
    QDate date = dt.date(); QTime time = dt.time();
    if (!date.isValid() || date.year() < 1980) date = QDate(1980, 1, 1);
    if (date.year() > 2107) date = QDate(2107, 12, 31);
    if (!time.isValid()) time = QTime(0, 0, 0);
    e.date = quint16(((date.year() - 1980) << 9) | (date.month() << 5) | date.day());
    e.time = quint16((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
}

class Writer {
public:
    Writer(const QString &root, QFile &out, QString &error) : root(root), out(out), error(error) {}
    bool add(const QString &path, const QString &name) {
        if (!safeLocalPath(root, path)) return fail("Недоступный путь или ссылка");
        QFileInfo info(path);
        if (!info.isDir() && !info.isFile()) return fail("Неподдерживаемый тип файла");
        if (info.isFile() && info.size() >= ZIP_LIMIT) return fail("Файл превышает предел ZIP32");
        if (protectedLocalPath(root, path)) return fail("Служебные данные нельзя архивировать");
        if (entries.size() >= 65535 || out.pos() >= ZIP_LIMIT) return fail("Архив превышает предел ZIP32");
        Entry e = {};
        e.directory = info.isDir();
        e.name = (name + (e.directory ? QLatin1String("/") : QLatin1String(""))).toUtf8();
        if (e.name.isEmpty() || e.name.size() > 65535) return fail("Слишком длинное имя в архиве");
        e.offset = quint32(out.pos()); e.method = e.directory ? 0 : 8;
        setDate(e, info.lastModified());
        if (!writeBytes(out, localHeader(e))) return fail("Ошибка записи архива");
        if (!e.directory && !writeFile(path, e)) return false;
        const qint64 end = out.pos();
        if (end >= ZIP_LIMIT || !out.seek(e.offset) || !writeBytes(out, localHeader(e)) || !out.seek(end))
            return fail("Ошибка записи или превышен предел ZIP32");
        entries.append(e);
        if (e.directory) {
            if (!QDir(path).isReadable()) return fail("Папка недоступна");
            const QFileInfoList children = browserEntries(QDir(path), true);
            for (const QFileInfo &child : children)
                if (!add(child.absoluteFilePath(), name + QLatin1Char('/') + child.fileName())) return false;
        }
        return true;
    }
    bool finish() {
        if (out.pos() >= ZIP_LIMIT) return fail("Архив превышает предел ZIP32");
        const quint32 offset = quint32(out.pos());
        for (const Entry &e : entries)
            if (!writeBytes(out, centralHeader(e)) || out.pos() >= ZIP_LIMIT)
                return fail("Ошибка записи или превышен предел ZIP32");
        const quint32 size = quint32(out.pos() - offset);
        QByteArray end; QDataStream d(&end, QIODevice::WriteOnly); d.setByteOrder(QDataStream::LittleEndian);
        d << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size())
          << quint16(entries.size()) << size << offset << quint16(0);
        if (out.pos() + end.size() >= ZIP_LIMIT || !writeBytes(out, end) || !out.flush())
            return fail("Ошибка завершения архива");
        return true;
    }
private:
    bool fail(const char *message) { error = QString::fromUtf8(message); return false; }
    bool writeFile(const QString &path, Entry &e) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) return fail("Не удалось прочитать файл");
        z_stream z = {};
        if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return fail("Не удалось начать сжатие");
        unsigned char in[32768], compressed[32768];
        quint64 inputSize = 0, outputSize = 0;
        uLong crc = crc32(0L, Z_NULL, 0);
        bool ok = true, done = false;
        while (ok && !done) {
            const qint64 n = input.read(reinterpret_cast<char *>(in), sizeof(in));
            if (n < 0) { ok = false; break; }
            inputSize += quint64(n);
            if (inputSize >= quint64(ZIP_LIMIT)) { ok = false; break; }
            crc = crc32(crc, in, uInt(n));
            z.next_in = in; z.avail_in = uInt(n);
            const int flush = n == 0 ? Z_FINISH : Z_NO_FLUSH;
            do {
                z.next_out = compressed; z.avail_out = sizeof(compressed);
                const int result = deflate(&z, flush);
                if (result != Z_OK && result != Z_STREAM_END) { ok = false; break; }
                const int count = int(sizeof(compressed) - z.avail_out);
                outputSize += quint64(count);
                if (outputSize >= quint64(ZIP_LIMIT) || out.pos() + count >= ZIP_LIMIT
                    || out.write(reinterpret_cast<const char *>(compressed), count) != count) { ok = false; break; }
                done = result == Z_STREAM_END;
            } while (z.avail_in || (flush == Z_FINISH && !done));
        }
        deflateEnd(&z);
        if (!ok || !done) return fail("Ошибка чтения, сжатия или записи архива");
        e.crc = quint32(crc); e.size = quint32(inputSize); e.compressed = quint32(outputSize);
        return true;
    }
    QString root; QFile &out; QString &error; QVector<Entry> entries;
};
} // namespace

bool createZipArchive(const QString &root, const QString &destination,
                      const QStringList &sources, QString &error) {
    error.clear();
    const QFileInfo target(destination);
    const QString dir = target.absolutePath();
    if (sources.isEmpty() || !safeLocalPath(root, dir)
        || (protectedLocalPath(root, dir) && dir != root)
        || !safeLocalPath(root, destination, true) || target.exists() || target.isSymLink()) {
        error = QString::fromUtf8("Недопустимое место или имя архива"); return false;
    }
    QSet<QString> names;
    for (const QString &src : sources) {
        const QFileInfo info(src);
        if (info.absolutePath() != dir || names.contains(info.fileName())
            || protectedLocalPath(root, src) || !safeCopyTree(root, src)) {
            error = QString::fromUtf8("Недоступный, защищённый путь или ссылка"); return false;
        }
        names.insert(info.fileName());
    }
    QTemporaryFile temporary(dir + QLatin1String("/.kobrowse_zip_XXXXXX"));
    if (!temporary.open()) { error = QString::fromUtf8("Не удалось создать временный архив"); return false; }
    Writer writer(root, temporary, error);
    for (const QString &src : sources)
        if (!writer.add(src, QFileInfo(src).fileName())) return false;
    if (!writer.finish()) return false;
    temporary.close();
    if (!temporary.rename(destination)) { error = QString::fromUtf8("Не удалось сохранить архив"); return false; }
    temporary.setAutoRemove(false);
    return true;
}
