#include "../src/file_policy.h"
#include "../src/file_mutations.h"
#include "../src/pagination.h"
#include "../src/home_folder.h"
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
    PageState p = pageState(0, 0, 100);
    require(p.current == 1 && p.total == 1 && !p.previous && !p.next, "single page");
    p = pageState(0, 220, 100);
    require(p.current == 1 && p.total == 4 && !p.previous && p.next, "first page");
    p = pageState(100, 220, 100);
    require(p.current == 2 && p.total == 4 && p.previous && p.next, "middle page");
    p = pageState(220, 220, 100);
    require(p.current == 4 && p.total == 4 && p.previous && !p.next, "short last page");
    require(pageSwipeDirection(1, -90, 40) == 1, "swipe up advances");
    require(pageSwipeDirection(1, 90, 40) == -1, "swipe down goes back");
    require(pageSwipeDirection(100, 60, 40) == 0 && pageSwipeDirection(0, 20, 40) == 0, "ignore horizontal and small gestures");
    QTemporaryDir tmp;
    require(tmp.isValid(), "temporary directory");
    const QString root = tmp.path() + "/onboard";
    require(QDir().mkpath(root + "/.kobo/Kobo"), "config folder");
    require(QDir().mkpath(root + "/.adds/app"), "hidden folder");
    require(QDir().mkpath(root + "/.disabled"), "second hidden folder");
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
    // Reproduce the device log: raw readdir sees names that Qt omitted.
    QFileInfoList filtered = browserQtEntries(QDir(root), true);
    for (int i = filtered.size() - 1; i >= 0; --i)
        if (filtered.at(i).fileName() == ".adds" || filtered.at(i).fileName() == ".disabled") filtered.removeAt(i);
    const QFileInfoList recovered = completeBrowserEntries(QDir(root), filtered, true);
    require(contains(recovered, ".adds") && contains(recovered, ".disabled"), "recover omitted folders");
    require(!contains(completeBrowserEntries(QDir(root), filtered, false), ".adds"), "disabled means no fallback");
    int addsCount = 0;
    for (const QFileInfo &fi : recovered) if (fi.fileName() == ".adds") ++addsCount;
    require(addsCount == 1, "fallback does not duplicate entries");
    for (int i = 1; i < recovered.size(); ++i)
        require(!browserEntryLess(recovered.at(i), recovered.at(i - 1)), "fallback keeps sorted order");
    QFileInfoList nestedFiltered = browserQtEntries(QDir(root + "/Books"), false);
    require(contains(completeBrowserEntries(QDir(root + "/Books"), nestedFiltered, true), ".private"),
            "recover nested hidden folder");
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
    const QString homeFile = tmp.path() + "/home";
    require(readHomeFolder(root, homeFile) == root, "missing home falls back");
    require(writeHomeFolder(root, homeFile, root + "/Books"), "save home");
    require(readHomeFolder(root, homeFile) == root + "/Books", "load home");
    require(!writeHomeFolder(root, homeFile, root + "/outside"), "reject symlink home");
    require(!writeHomeFolder(root, homeFile, root + "/Books/book.epub"), "reject file home");
    require(!writeHomeFolder(root, homeFile, tmp.path()), "reject outside home");
    require(readHomeFolder(root, homeFile) == root + "/Books", "failed writes preserve home");
    writeFile(homeFile, (root + "/missing").toUtf8());
    require(readHomeFolder(root, homeFile) == root, "missing saved folder falls back");
    require(resetHomeFolder(homeFile) && readHomeFolder(root, homeFile) == root, "reset home");
    const QString trash = root + "/.kobrowse_trash";
    require(QDir().mkpath(trash + "/nested"), "trash folder");
    writeFile(trash + "/nested/book", "trash");
    require(::symlink((root + "/Books/book.epub").toLocal8Bit().constData(),
                      (trash + "/nested/link").toLocal8Bit().constData()) == 0, "trash link");
    require(!mayDeleteLocalEntry(root, trash, trash), "trash root cannot be deleted");
    require(!mayDeleteLocalEntry(root, trash, root + "/.kobo/Kobo"), "system folder cannot be deleted");
    require(!mayDeleteLocalEntry(root, trash, root + "/alias"), "link outside trash cannot be deleted");
    require(mayDeleteLocalEntry(root, trash, trash + "/nested"), "trash entry can be deleted");
    require(removeTreeNoFollow(root, trash + "/nested"), "remove trash tree without following links");
    require(QFileInfo(root + "/Books/book.epub").exists() && QFileInfo(trash).isDir(),
            "link target and trash root survive");
    const QString folder = root + "/Archives";
    require(QDir().mkpath(folder), "archive folder");
    require(archiveDestination(folder, "book.fb2") == folder + "/book.fb2.zip", "first archive name");
    writeFile(folder + "/book.fb2.zip", "keep");
    writeFile(folder + "/book.fb2_1.zip", "keep");
    require(archiveDestination(folder, "book.fb2") == folder + "/book.fb2_2.zip", "archive counter");
    require(QDir().mkpath(root + "/.adds/kobrowse/sub"), "app data");
    require(QDir().mkpath(root + "/.adds/nm"), "menu directory");
    writeFile(root + "/.adds/kobrowse/.ini", "config");
    writeFile(root + "/.adds/nm/kobrowse", "menu");
    writeFile(root + "/.adds/nm/other", "other");
    writeFile(root + "/outside.txt", "retain");
    require(::symlink((root + "/outside.txt").toLocal8Bit().constData(),
                      (root + "/.adds/kobrowse/sub/link").toLocal8Bit().constData()) == 0, "cleanup symlink");
    require(cleanupAppInstall(root), "uninstall cleanup");
    require(!QFileInfo(root + "/.adds/kobrowse").exists(), "app data removed");
    require(!QFileInfo(root + "/.adds/nm/kobrowse").exists(), "app menu removed");
    require(QFileInfo(root + "/.adds/nm/other").exists() && QFileInfo(root + "/outside.txt").exists(),
            "other data and link target retained");
    QTextStream(stdout) << "Filesystem tests passed\n";
    return 0;
}
