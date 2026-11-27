#include "zip_validator.h"
#include "zip_path_manifest.h"
#include <QByteArray>
#include <QFile>
#include <QtEndian>
#include <QVector>
#include <algorithm>
#include <sys/stat.h>
#include <zlib.h>

// ZIP32 bounds and record checks adapted from Kobalt's MIT-licensed
// ZipResource (see res/KOBALT-LICENSE.txt). This validator never writes files.
namespace {
const quint64 maxDirectory = 4 * 1024 * 1024;
const quint64 maxFile = 256 * 1024 * 1024;
const quint64 maxTotal = 1024ULL * 1024 * 1024;
const int maxEntries = 10000;
const int block = 32768;

quint16 u16(const QByteArray &b, int p) {
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(b.constData() + p));
}
quint32 u32(const QByteArray &b, int p) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + p));
}
bool fail(QString &error, const char *message) { error = QString::fromUtf8(message); return false; }
bool validExtra(const QByteArray &bytes) {
    int p = 0;
    while (p < bytes.size()) {
        if (p + 4 > bytes.size()) return false;
        const quint16 kind = u16(bytes, p), length = u16(bytes, p + 2);
        if (kind == 1 || p + 4 + length > bytes.size()) return false; // ZIP64 not supported
        p += 4 + length;
    }
    return true;
}
QByteArray readAt(QIODevice &in, qint64 offset, qint64 size) {
    if (offset < 0 || size < 0 || size > 4 * 1024 * 1024 || !in.seek(offset)) return QByteArray();
    return in.read(size);
}
struct Record {
    QByteArray name;
    quint16 flags, method;
    quint32 crc, packed, size, local;
    quint64 start, end;
    bool directory;
};
struct Identity {
    dev_t device; ino_t inode; off_t size; timespec modified, changed;
};
bool identity(QIODevice &in, Identity &out) {
    QFile *file = qobject_cast<QFile *>(&in);
    struct stat st;
    if (!file || file->handle() < 0 || ::fstat(file->handle(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    out.device = st.st_dev; out.inode = st.st_ino; out.size = st.st_size;
    out.modified = st.st_mtim; out.changed = st.st_ctim;
    return true;
}
bool sameIdentity(const Identity &a, const Identity &b) {
    return a.device == b.device && a.inode == b.inode && a.size == b.size
        && a.modified.tv_sec == b.modified.tv_sec && a.modified.tv_nsec == b.modified.tv_nsec
        && a.changed.tv_sec == b.changed.tv_sec && a.changed.tv_nsec == b.changed.tv_nsec;
}
bool checkPayload(QIODevice &in, const Record &entry) {
    if (!in.seek(qint64(entry.start))) return false;
    quint64 remaining = entry.packed, emitted = 0;
    uLong crc = crc32(0L, Z_NULL, 0);
    char input[block], output[block];
    if (entry.method == 0) {
        if (entry.packed != entry.size) return false;
        while (remaining) {
            const int n = int(qMin<quint64>(remaining, block));
            if (in.read(input, n) != n) return false;
            crc = crc32(crc, reinterpret_cast<const Bytef *>(input), n);
            remaining -= n; emitted += n;
        }
    } else {
        z_stream z = {};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
        bool valid = true, ended = false;
        while (valid && !ended) {
            if (!z.avail_in && remaining) {
                const int n = int(qMin<quint64>(remaining, block));
                if (in.read(input, n) != n) { valid = false; break; }
                remaining -= n;
                z.next_in = reinterpret_cast<Bytef *>(input); z.avail_in = uInt(n);
            }
            z.next_out = reinterpret_cast<Bytef *>(output); z.avail_out = block;
            const uInt before = z.avail_in;
            const int code = inflate(&z, Z_NO_FLUSH);
            const int n = block - int(z.avail_out);
            emitted += quint64(n);
            if (emitted > entry.size) { valid = false; break; }
            crc = crc32(crc, reinterpret_cast<const Bytef *>(output), uInt(n));
            if (code == Z_STREAM_END) ended = true;
            else if (code != Z_OK || (n == 0 && before == z.avail_in)) valid = false;
        }
        if (!ended || remaining || z.avail_in) valid = false;
        inflateEnd(&z);
        if (!valid) return false;
    }
    return emitted == entry.size && quint32(crc) == entry.crc;
}
}

bool validateZipArchive(QIODevice &source, QString &error) {
    error.clear();
    if (!source.isOpen() || !source.isReadable() || source.isSequential())
        return fail(error, "Архив недоступен для чтения");
    Identity before, after;
    if (!identity(source, before)) return fail(error, "Архив должен быть обычным файлом");
    const qint64 length = source.size();
    if (length < 22 || quint64(length) >= 0xffffffffULL) return fail(error, "Недопустимый размер ZIP32");
    const qint64 tailStart = qMax<qint64>(0, length - 65557);
    const QByteArray tail = readAt(source, tailStart, length - tailStart);
    if (tail.size() != length - tailStart) return fail(error, "Не удалось прочитать ZIP");
    int end = -1;
    for (int p = tail.size() - 22; p >= 0; --p) {
        if (u32(tail, p) == 0x06054b50U && p + 22 + u16(tail, p + 20) == tail.size()) { end = p; break; }
    }
    if (end < 0 || u16(tail, end + 4) || u16(tail, end + 6)
        || u16(tail, end + 8) != u16(tail, end + 10)) return fail(error, "Повреждённый или многотомный ZIP");
    const int count = u16(tail, end + 10);
    const quint32 dirSize = u32(tail, end + 12), dirOffset = u32(tail, end + 16);
    if (count > maxEntries || dirSize > maxDirectory || quint64(dirOffset) + dirSize != quint64(tailStart + end))
        return fail(error, "ZIP превышает ограничения или повреждён");
    const QByteArray directory = readAt(source, dirOffset, dirSize);
    if (directory.size() != int(dirSize)) return fail(error, "Не удалось прочитать каталог ZIP");
    QVector<Record> entries;
    ZipPathManifest manifest;
    quint64 total = 0;
    int at = 0;
    for (int i = 0; i < count; ++i) {
        if (at + 46 > directory.size() || u32(directory, at) != 0x02014b50U)
            return fail(error, "Повреждён каталог ZIP");
        const int n = u16(directory, at + 28), extra = u16(directory, at + 30), comment = u16(directory, at + 32);
        const int next = at + 46 + n + extra + comment;
        if (next > directory.size() || n == 0 || u16(directory, at + 34)
            || !validExtra(directory.mid(at + 46 + n, extra))) return fail(error, "Повреждён каталог ZIP");
        Record e = {};
        e.name = directory.mid(at + 46, n);
        e.flags = u16(directory, at + 8); e.method = u16(directory, at + 10);
        e.crc = u32(directory, at + 16); e.packed = u32(directory, at + 20);
        e.size = u32(directory, at + 24); e.local = u32(directory, at + 42);
        e.directory = e.name.endsWith('/');
        const quint32 attributes = u32(directory, at + 38);
        const quint32 type = (attributes >> 16) & S_IFMT;
        if ((type && type != (e.directory ? S_IFDIR : S_IFREG)) || (attributes & 0x08U)
            || ((attributes & 0x10U) && !e.directory)) return fail(error, "Ссылка или неподдерживаемый тип в ZIP");
        if ((e.flags & ~quint16(0x080e)) || (e.method != 0 && e.method != 8)
            || (e.method == 0 && (e.flags & 0x0006)) || e.size > maxFile || e.packed > maxFile
            || (e.directory && e.size != 0)) return fail(error, "Неподдерживаемая или слишком большая запись ZIP");
        total += e.size;
        if (total > maxTotal || !manifest.add(e.name, (e.flags & 0x0800) != 0, e.directory, error))
            return fail(error, "Недопустимые имена или размер ZIP");
        entries.append(e); at = next;
    }
    if (at != directory.size()) return fail(error, "Некорректный размер каталога ZIP");
    for (int i = 0; i < entries.size(); ++i) {
        Record &e = entries[i];
        if (quint64(e.local) + 30 > dirOffset) return fail(error, "Повреждён локальный заголовок ZIP");
        const QByteArray h = readAt(source, e.local, 30);
        if (h.size() != 30 || u32(h, 0) != 0x04034b50U || u16(h, 6) != e.flags || u16(h, 8) != e.method
            || u16(h, 26) != e.name.size()) return fail(error, "Несовпадение заголовков ZIP");
        const bool descriptor = e.flags & 8;
        if ((!descriptor && (u32(h, 14) != e.crc || u32(h, 18) != e.packed || u32(h, 22) != e.size))
            || (descriptor && ((u32(h, 14) && u32(h, 14) != e.crc)
                || (u32(h, 18) && u32(h, 18) != e.packed) || (u32(h, 22) && u32(h, 22) != e.size))))
            return fail(error, "Несовпадение размеров или CRC ZIP");
        e.start = quint64(e.local) + 30 + u16(h, 26) + u16(h, 28);
        e.end = e.start + e.packed;
        const int extraLength = u16(h, 28);
        if (e.end > dirOffset || readAt(source, qint64(e.local) + 30, e.name.size()) != e.name)
            return fail(error, "Несовпадение имени или границ записи ZIP");
        const QByteArray localExtra = readAt(source, qint64(e.local) + 30 + e.name.size(), extraLength);
        if (localExtra.size() != extraLength || !validExtra(localExtra))
            return fail(error, "Повреждены дополнительные поля ZIP");
        if (descriptor) {
            const quint64 left = quint64(dirOffset) - e.end;
            const QByteArray d = readAt(source, qint64(e.end), qMin<quint64>(16, left));
            int shift = -1;
            if (d.size() >= 16 && u32(d, 0) == 0x08074b50U && u32(d, 4) == e.crc
                && u32(d, 8) == e.packed && u32(d, 12) == e.size) shift = 4;
            else if (d.size() >= 12 && u32(d, 0) == e.crc && u32(d, 4) == e.packed && u32(d, 8) == e.size) shift = 0;
            if (shift < 0) return fail(error, "Повреждён дескриптор ZIP");
            e.end += 12 + shift;
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Record &a, const Record &b) { return a.local < b.local; });
    for (int i = 1; i < entries.size(); ++i)
        if (entries[i - 1].end > entries[i].local) return fail(error, "Перекрывающиеся записи ZIP");
    for (int i = 0; i < entries.size(); ++i)
        if (!checkPayload(source, entries[i])) return fail(error, "Повреждённые данные или CRC ZIP");
    if (!identity(source, after) || !sameIdentity(before, after)) return fail(error, "Архив изменился во время проверки");
    return true;
}
