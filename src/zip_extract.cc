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
        const quint64 needed = total + quint64(count) * unit + 1024 * 1024;
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
    QString base = info.completeBaseName().left(60);
    if (!base.isEmpty() && base.at(base.size() - 1).isHighSurrogate()) base.chop(1);
    if (base.isEmpty()) base = QString::fromUtf8("архив");
    const QString prefix = parent + QString::fromUtf8("/Распаковано-") + base;
    for (int n = 0; n < 10000; ++n) {
        const QString target = prefix + (n ? QLatin1Char('-') + QString::number(n) : QString());
        if (!safeLocalPath(root, parent) || !writableLocalDirectory(root, parent, allowAdds)
            || protectedLocalPath(root, target, allowAdds)) {
            error = QString::fromUtf8("Место назначения недоступно"); break;
        }
        const int result = hooks && hooks->publish ? hooks->publish(stage, target) : renameNoReplace(stage, target);
        if (result == 0) { published = target; return true; }
        if (result == EEXIST || result == ENOTEMPTY) continue;
        if (result == ENOSYS || result == EINVAL || result == EOPNOTSUPP) {
            // Old kernels/filesystems may lack renameat2. Reserve a fresh outer
            // directory atomically, then publish into its empty interior. The
            // resulting extra level is deliberate; existing files are untouched.
            if (::mkdir(QFile::encodeName(target).constData(), 0700) != 0) {
                if (errno == EEXIST) continue;
                error = QString::fromUtf8("Не удалось создать папку назначения"); break;
            }
            const QString nested = target + QString::fromUtf8("/Содержимое");
            if (!QFileInfo(nested).exists() && !QFileInfo(nested).isSymLink()
                && ::rename(QFile::encodeName(stage).constData(), QFile::encodeName(nested).constData()) == 0) {
                published = nested; return true;
            }
            if (::rmdir(QFile::encodeName(target).constData()) != 0)
                error = QString::fromUtf8("Не удалось опубликовать; папка назначения осталась: ") + target;
            else error = QString::fromUtf8("Не удалось опубликовать распакованные файлы");
            break;
        }
        error = QString::fromUtf8("Не удалось опубликовать распакованные файлы"); break;
    }
    if (error.isEmpty()) error = QString::fromUtf8("Не удалось выбрать свободное имя папки");
    cleanupStage(root, stage, error, hooks);
    return false;
}
