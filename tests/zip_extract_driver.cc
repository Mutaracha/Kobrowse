#include "../src/zip_extract.h"
#include <QCoreApplication>
#include <QTextStream>
#include <cerrno>

static bool failWrite(int, const char *, int) { return false; }
static int failPublish(const QString &, const QString &) { return EIO; }
static int oldKernel(const QString &, const QString &) { return ENOSYS; }
static int partialPublish(const QString &, const QString &) { static int calls = 0; return ++calls == 1 ? ENOSYS : EIO; }
static bool failCleanup(const QString &, const QString &) { return false; }

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3 || argc > 4) return 2;
    ZipExtractHooks hooks;
    const QByteArray mode = argc == 4 ? QByteArray(argv[3]) : QByteArray();
    if (mode == "write" || mode == "cleanup") hooks.write = failWrite;
    if (mode == "publish") hooks.publish = failPublish;
    if (mode == "old-kernel") hooks.publish = oldKernel;
    if (mode == "partial") hooks.publish = partialPublish;
    if (mode == "copy-write") { hooks.publish = oldKernel; hooks.copyWrite = failWrite; }
    if (mode == "cleanup") hooks.cleanup = failCleanup;
    QString output, error;
    const bool ok = extractZipArchive(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]),
                                      output, error, false, mode.isEmpty() ? nullptr : &hooks);
    if (!ok) { QTextStream(stderr) << error << '\n'; return 1; }
    QTextStream(stdout) << output << '\n'; return 0;
}
