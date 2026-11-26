#include "../src/file_policy.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <unistd.h>
#include <cstdlib>

static void require(bool condition, const char *message) {
    if (!condition) { QTextStream(stderr) << "FAIL: " << message << "\n"; ::exit(1); }
}
static void writeFile(const QString &path, const QByteArray &data) {
    QFile f(path);
    require(f.open(QIODevice::WriteOnly), "create fixture");
    require(f.write(data) == data.size(), "write fixture");
}
static bool contains(const QFileInfoList &entries, const QString &name) {
    for (const QFileInfo &item : entries) if (item.fileName() == name) return true;
    return false;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    require(tmp.isValid(), "temporary directory");
    const QString root = tmp.path() + "/onboard";
    require(QDir().mkpath(root + "/.kobo/Kobo"), "config folder");
    require(QDir().mkpath(root + "/.adds/app"), "hidden folder");
    require(QDir().mkpath(root + "/Books/.private"), "nested hidden folder");
    require(QDir().mkpath(root + "/Excluded"), "excluded folder");
    const QByteArray config("[FeatureSettings]\nExcludeSyncFolders=Excluded\n");
    const QString configPath = root + "/.kobo/Kobo/Kobo eReader.conf";
    writeFile(configPath, config);
    writeFile(root + "/Books/book.epub", "book");
    writeFile(root + "/.adds/app/file", "hidden");
    require(contains(browserEntries(QDir(root), true), ".adds"), "hidden folder visible");
    require(contains(browserEntries(QDir(root), true), "Excluded"), "excluded folder visible");
    require(!contains(browserEntries(QDir(root), false), ".adds"), "hidden folder disabled");
    require(contains(browserEntries(QDir(root + "/Books"), true), ".private"), "nested hidden visible");
    QFile cfg(configPath);
    require(cfg.open(QIODevice::ReadOnly) && cfg.readAll() == config, "index config unmodified");
    require(safeLocalPath(root, root + "/Books/book.epub"), "normal file allowed");
    require(!safeLocalPath(root, tmp.path()), "outside root rejected");
    require(!safeLocalPath(root, root + "-other"), "prefix collision rejected");
    require(safeLocalPath(root, root + "/new", true), "missing leaf allowed for creation");
    require(!safeLocalPath(root, root + "/missing/nested", true), "missing parent rejected");
    require(protectedLocalPath(root, root + "/.kobo/Kobo"), "system subtree protected");
    require(protectedLocalPath(root, root + "/.adds/app/file"), "plugin subtree protected");
    require(protectedLocalPath(root, root + "/.kobrowse_trash/item"), "trash subtree protected");
    require(!protectedLocalPath(root, root + "/.kobo-images"), "prefix sibling not protected");
    require(::symlink(tmp.path().toLocal8Bit().constData(), (root + "/outside").toLocal8Bit().constData()) == 0, "outside link");
    require(::symlink((root + "/.kobo").toLocal8Bit().constData(), (root + "/alias").toLocal8Bit().constData()) == 0, "protected link");
    require(::symlink("missing", (root + "/dangling").toLocal8Bit().constData()) == 0, "dangling link");
    require(::symlink("book.epub", (root + "/Books/link").toLocal8Bit().constData()) == 0, "nested file link");
    require(!safeLocalPath(root, root + "/outside"), "outside link rejected");
    require(!safeLocalPath(root, root + "/alias/Kobo"), "link parent rejected");
    require(!safeLocalPath(root, root + "/dangling", true), "dangling link rejected");
    require(!safeCopyTree(root, root + "/Books"), "nested link prevents copy");
    require(safeCopyTree(root, root + "/Excluded"), "ordinary folder can be copied");
    QTextStream(stdout) << "Filesystem tests passed\n";
    return 0;
}
