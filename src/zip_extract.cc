#include "zip_extract.h"
#include "zip_validator.h"
#include "file_mutations.h"
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
class StageSink : public ZipSink {
public:
    StageSink(const QString &path, const ZipExtractHooks *testHooks) : base(path), hooks(testHooks) { dirs.insert(QString()); }
    ~StageSink() { if (fd >= 0) ::close(fd); }
    bool beginArchive(quint64 total, int count, QString &error) override {
        struct statvfs info;
        if (::statvfs(QFile::encodeName(base).constData(), &info) != 0)
            return fail(error, "Не удалось определить свободное место");
        const quint64 unit = info.f_frsize ? info.f_frsize : info.f_bsize;
        if (!unit || unit > 4 * 1024 * 1024) return fail(error, "Не удалось определить свободное место");
        const quint64 avail = quint64(info.f_bavail) > std::numeric_limits<quint64>::max() / unit
            ? std::numeric_limits<quint64>::max() : quint64(info.f_bavail) * unit;
        // Old kernels need a second copy during safe no-overwrite publication.
        const quint64 needed = 2 * total + 2 * quint64(count) * unit + 1024 * 1024;
        if (avail < needed) return fail(error, "Недостаточно свободного места для распаковки");
        return true;
    }
    bool begin(const QString &name, bool directory, QString &error) override {
        if (fd >= 0) return fail(error, "Не закрыт предыдущий файл");
        QString path = name;
        if (directory) path.chop(1);
        const QStringList parts = path.split(QLatin1Char('/'));
        QString relative;
        const int parentCount = directory ? parts.size() : parts.size() - 1;
        for (int i = 0; i < parentCount; ++i) {
            if (!relative.isEmpty()) relative += QLatin1Char('/');
            relative += parts.at(i);
            if (dirs.contains(relative)) continue;
            if (QFile::decodeName(QFile::encodeName(parts.at(i))) != parts.at(i)
                || ::mkdir(QFile::encodeName(base + QLatin1Char('/') + relative).constData(), 0700) != 0)
                return fail(error, "Не удалось создать папку или обнаружен конфликт имён");
            dirs.insert(relative);
        }
        if (directory) return true;
        const QString leaf = parts.last();
        if (QFile::decodeName(QFile::encodeName(leaf)) != leaf)
            return fail(error, "Имя файла невозможно сохранить без потерь");
        fd = ::open(QFile::encodeName(base + QLatin1Char('/') + path).constData(),
                    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        return fd >= 0 || fail(error, "Не удалось создать файл или обнаружен конфликт имён");
    }
    bool write(const char *data, int size, QString &error) override {
        if (fd < 0) return fail(error, "Неожиданные данные папки ZIP");
        if (hooks && hooks->write) return hooks->write(fd, data, size) || fail(error, "Ошибка записи файла");
        int done = 0;
        while (done < size) {
            const ssize_t n = ::write(fd, data + done, size - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return fail(error, "Ошибка записи файла");
            done += int(n);
        }
        return true;
    }
    bool end(QString &error) override {
        if (fd < 0) return true;
        const bool flushed = ::fsync(fd) == 0;
        const bool closed = ::close(fd) == 0;
        fd = -1;
        return (flushed && closed) || fail(error, "Не удалось завершить запись файла");
    }
private:
    static bool fail(QString &error, const char *text) { error = QString::fromUtf8(text); return false; }
    QString base;
    const ZipExtractHooks *hooks;
    QSet<QString> dirs;
    int fd = -1;
};

// Linux's no-replace rename avoids overwriting an entry created concurrently.
// Old Kobo toolchains may not define SYS_renameat2, even if the kernel does.
int renameNoReplace(const QString &from, const QString &to) {
#if defined(SYS_renameat2)
    const long number = SYS_renameat2;
#elif defined(__arm__)
    const long number = 382;
#else
    return ENOSYS;
#endif
#if defined(SYS_renameat2) || defined(__arm__)
    const int result = int(::syscall(number, AT_FDCWD, QFile::encodeName(from).constData(),
                                     AT_FDCWD, QFile::encodeName(to).constData(), 1U));
    return result == 0 ? 0 : errno;
#endif
}

bool cleanupStage(const QString &root, const QString &path, QString &error, const ZipExtractHooks *hooks) {
    const bool ok = hooks && hooks->cleanup ? hooks->cleanup(root, path) : removeTreeNoFollow(root, path);
    if (!ok) error += QString::fromUtf8("; временная папка сохранена: ") + path;
    return ok;
}

// Old Linux kernels and FAT do not reliably offer rename-with-no-replace or
// hard links. Reserve each new name with mkdir/O_EXCL and copy only verified
// staged contents. Never delete a partly published tree on failure: its name
// and the intact staging path are reported, so it can be inspected safely.
bool copyReserved(const QString &root, const QString &from, const QString &to,
                  bool &created, QString &error, const ZipExtractHooks *hooks) {
    if (!safeLocalPath(root, from) || !safeLocalPath(root, QFileInfo(to).absolutePath())) {
        error = QString::fromUtf8("Небезопасный путь публикации"); return false;
    }
    const QFileInfo src(from);
    if (src.isDir()) {
        if (::mkdir(QFile::encodeName(to).constData(), 0700) != 0) {
            error = QString::fromUtf8("Не удалось создать папку назначения"); return false;
        }
        created = true;
        QFileInfoList children;
        if (!operationEntries(from, children)) { error = QString::fromUtf8("Не удалось прочитать временную папку"); return false; }
        for (const QFileInfo &child : children) {
            bool childCreated = false;
            if (!copyReserved(root, child.absoluteFilePath(), to + QLatin1Char('/') + child.fileName(), childCreated, error, hooks)) return false;
        }
        return true;
    }
    if (!src.isFile() || src.isSymLink()) { error = QString::fromUtf8("Недопустимый временный файл"); return false; }
    int input = ::open(QFile::encodeName(from).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (input < 0) { error = QString::fromUtf8("Не удалось открыть временный файл"); return false; }
    int output = ::open(QFile::encodeName(to).constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (output < 0) { ::close(input); error = QString::fromUtf8("Не удалось создать файл назначения"); return false; }
    created = true;
    bool ok = true; char buffer[32768];
    for (;;) {
        ssize_t n = ::read(input, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = false; break; }
        if (!n) break;
        if (hooks && hooks->copyWrite) {
            if (!hooks->copyWrite(output, buffer, int(n))) { ok = false; break; }
            continue;
        }
        ssize_t done = 0;
        while (done < n) {
            const ssize_t w = ::write(output, buffer + done, size_t(n - done));
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) { ok = false; break; }
            done += w;
        }
        if (!ok) break;
    }
    if (::fsync(output) != 0) ok = false;
    if (::close(output) != 0) ok = false;
    if (::close(input) != 0) ok = false;
    if (!ok) error = QString::fromUtf8("Ошибка записи при публикации");
    return ok;
}

bool nameOccupied(const QString &parent, const QString &name, bool &occupied) {
    QFileInfoList entries;
    if (!operationEntries(parent, entries)) return false;
    const QString folded = name.normalized(QString::NormalizationForm_C).toCaseFolded();
    occupied = false;
    for (const QFileInfo &entry : entries)
        if (entry.fileName().normalized(QString::NormalizationForm_C).toCaseFolded() == folded) { occupied = true; break; }
    const QFileInfo candidate(parent + QLatin1Char('/') + name);
    if (candidate.exists() || candidate.isSymLink()) occupied = true; // FAT short aliases
    return true;
}
}

bool extractZipArchive(const QString &root, const QString &archive, QString &published,
                       QString &error, bool allowAdds, const ZipExtractHooks *hooks) {
    published.clear(); error.clear();
    const QFileInfo info(archive);
    const QString parent = info.absolutePath();
    if (!safeLocalPath(root, archive) || !info.isFile() || protectedLocalPath(root, archive, allowAdds)
        || !safeLocalPath(root, parent) || !writableLocalDirectory(root, parent, allowAdds)) {
        error = QString::fromUtf8("Архив или папка защищены или недоступны"); return false;
    }
    const int raw = ::open(QFile::encodeName(archive).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (raw < 0) { error = QString::fromUtf8("Не удалось открыть архив"); return false; }
    QFile source;
    if (!source.open(raw, QIODevice::ReadOnly, QFile::AutoCloseHandle)) {
        ::close(raw); error = QString::fromUtf8("Не удалось открыть архив"); return false;
    }
    QTemporaryDir temporary(parent + QLatin1String("/.kobrowse-extract-XXXXXX"));
    if (!temporary.isValid() || !safeLocalPath(root, temporary.path())) {
        error = QString::fromUtf8("Не удалось создать временную папку"); return false;
    }
    temporary.setAutoRemove(false);
    const QString stage = temporary.path();
    bool extracted;
    {
        StageSink sink(stage, hooks);
        extracted = processZipArchive(source, error, &sink);
    } // Close any incomplete output before cleanup.
    source.close();
    if (!extracted) {
        if (error.isEmpty()) error = QString::fromUtf8("Не удалось проверить архив");
        cleanupStage(root, stage, error, hooks); return false;
    }
    QFileInfoList entries;
    if (!operationEntries(stage, entries)) {
        error = QString::fromUtf8("Не удалось прочитать временную папку");
        cleanupStage(root, stage, error, hooks); return false;
    }
    std::stable_sort(entries.begin(), entries.end(), browserEntryLess);
    QStringList completed;
    for (const QFileInfo &entry : entries) {
        const QString original = entry.fileName();
        QString base = original, ext;
        if (!entry.isDir()) {
            const int dot = original.lastIndexOf(QLatin1Char('.'));
            if (dot > 0) { base = original.left(dot); ext = original.mid(dot); }
        }
        bool success = false, partial = false;
        QString target;
        for (int n = 0; n < 10000; ++n) {
            const QString name = n == 0 ? original : base + QLatin1Char('_') + QString::number(n) + ext;
            target = parent + QLatin1Char('/') + name;
            bool occupied = false;
            if (!safeLocalPath(root, parent) || !writableLocalDirectory(root, parent, allowAdds)
                || protectedLocalPath(root, target, allowAdds) || !nameOccupied(parent, name, occupied)) {
                error = QString::fromUtf8("Место назначения недоступно"); break;
            }
            if (occupied) continue;
            const int result = hooks && hooks->publish ? hooks->publish(entry.absoluteFilePath(), target)
                                                        : renameNoReplace(entry.absoluteFilePath(), target);
            if (result == 0) { success = true; break; }
            if (result == EEXIST || result == ENOTEMPTY) continue;
            if (result == ENOSYS || result == EINVAL || result == EOPNOTSUPP) {
                success = copyReserved(root, entry.absoluteFilePath(), target, partial, error, hooks);
                break;
            }
            error = QString::fromUtf8("Не удалось опубликовать файл"); break;
        }
        if (!success) {
            if (error.isEmpty()) error = QString::fromUtf8("Не удалось выбрать свободное имя");
            if (partial) completed.prepend(target + QString::fromUtf8(" (не полностью)"));
            if (!completed.isEmpty()) {
                error += QString::fromUtf8("; временная папка: ") + stage
                    + QString::fromUtf8("; опубликовано или начато: ") + completed.join(QString::fromUtf8(", "));
            } else cleanupStage(root, stage, error, hooks);
            return false;
        }
        completed << target;
    }
    if (!cleanupStage(root, stage, error, hooks)) {
        error = QString::fromUtf8("Файлы извлечены, но не удалось удалить временную папку: ") + stage;
        return false;
    }
    published = parent;
    return true;
}
