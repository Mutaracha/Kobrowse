#include "../src/file_policy.h"
#include "../src/file_mutations.h"
#include "../src/pagination.h"
#include "../src/home_folder.h"
#include "../src/file_kind.h"
#include "../src/zip_path_manifest.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>

static void require(bool condition, const char *message) {
    if (!condition) { QTextStream(stderr) << "FAIL: " << message << "\n"; ::exit(1); }
}
static void writeFile(const QString &path, const QByteArray &data) {
    QFile f(path);
    require(f.open(QIODevice::WriteOnly), "create fixture");
    require(f.write(data) == data.size(), "write fixture");
}
static int copiesBeforeFailure = 0;
static bool copyWithFailure(const QString &src, const QString &dst) {
    if (copiesBeforeFailure-- <= 0) return false;
    return QFile::copy(src, dst);
}
static bool contains(const QFileInfoList &entries, const QString &name) {
    for (const QFileInfo &item : entries) if (item.fileName() == name) return true;
    return false;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QString zipError;
    ZipPathManifest manifest;
    require(manifest.add("books/book.epub", false, false, zipError), "zip normal nested name");
    require(manifest.add("books/", false, true, zipError), "zip explicit parent after child");
    require(!manifest.add("books/", false, true, zipError), "zip duplicate directory rejected");
    require(!manifest.add("Books/other", false, false, zipError), "zip case alias rejected");
    require(!manifest.add("books/book.epub/nested", false, false, zipError), "zip file as parent rejected");
    require(!manifest.add("books/book.epub", false, false, zipError), "zip duplicate file rejected");
    require(manifest.add(QString::fromUtf8("книги/текст.txt").toUtf8(), true, false, zipError), "zip UTF-8 supported");
    require(!manifest.add(QString::fromUtf8("другое.txt").toUtf8(), false, false, zipError), "zip unflagged non-ASCII rejected");
    const char *badNames[] = { "../escape", "/absolute", "dir/../bad", "a//b", "a\\\\b", "C:drive", ".kobo/file", ".ADDS/x", ".kobrowse_trash/x", "con.txt", "AUX", "lpt1.log", "trailing.", "trailing ", "wild?", "file/" };
    for (const char *name : badNames) {
        ZipPathManifest rejected;
        require(!rejected.add(QByteArray(name), true, false, zipError), "unsafe zip path rejected");
    }
    ZipPathManifest normalization;
    require(normalization.add(QString::fromUtf8("é.txt").toUtf8(), true, false, zipError), "zip composed unicode");
    require(!normalization.add(QString::fromUtf8("é.txt").toUtf8(), true, false, zipError), "zip canonical collision rejected");
    ZipPathManifest malformed;
    require(!malformed.add(QByteArray("\xff", 1), true, false, zipError), "invalid UTF-8 rejected");
    require(!malformed.add(QByteArray("a\0b", 3), true, false, zipError), "NUL rejected");
    require(fileKind("Книга.KEPUB.EPUB") == FileEpub && fileKind("read.PDF") == FilePdf
            && fileKind("novel.FB2") == FileFb2 && fileKind("book.mobi") == FileBook, "book format icons");
    require(fileKind("picture.TIFF") == FileImage && fileKind("photo.JPEG") == FileImage, "image icons");
    require(fileKind("book.fb2.zip") == FileArchive && fileKind("comic.CBR") == FileArchive, "archive icons");
    require(fileKind("file.zip.txt") == FileBook && fileKind("folder-epub") == FileGeneric, "file extension boundaries");
    PageState p = pageState(0, 0, 100);
    require(p.current == 1 && p.total == 1 && !p.previous && !p.next, "single page");
    p = pageState(0, 220, 100);
    require(p.current == 1 && p.total == 4 && !p.previous && p.next, "first page");
    p = pageState(100, 220, 100);
    require(p.current == 2 && p.total == 4 && p.previous && p.next, "middle page");
    p = pageState(220, 220, 100);
    require(p.current == 4 && p.total == 4 && p.previous && !p.next, "short last page");
    const std::vector<int> rows = { 40, 40, 60, 40, 140, 30 };
    const std::vector<int> boundaries = pageBoundaries(rows, 100);
    require(boundaries == std::vector<int>({0, 2, 4, 5, 6}), "whole row page boundaries");
    require(pageBoundaries(std::vector<int>(), 100) == std::vector<int>({0, 0}), "empty page");
    require(pageBoundaries(std::vector<int>({50, 50}), 100) == std::vector<int>({0, 2}), "exact fit");
    require(pageBoundaries(std::vector<int>({1, 1, 1}), 0) == std::vector<int>({0, 1, 2, 3}), "zero viewport safe");
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
    QString createError;
    require(createLocalDirectory(root, root + "/Books", QString::fromUtf8("Новая папка"), createError), "create folder in fixture");
    require(QFileInfo(root + QString::fromUtf8("/Books/Новая папка")).isDir(), "created folder exists");
    require(!createLocalDirectory(root, root + "/Books", QString::fromUtf8("Новая папка"), createError), "existing folder not replaced");
    require(!createLocalDirectory(root, root + "/Books", "../bad", createError), "folder traversal rejected");
    require(!createLocalDirectory(root, root + "/.kobo", "bad", createError), "protected folder rejected");
    require(!createLocalDirectory(root, root, ".kobo", createError), "protected destination rejected");
    QFileInfoList sorted;
    sorted << QFileInfo(root + "/Books/book.epub") << QFileInfo(root + "/Books/Новая папка") << QFileInfo(root + "/.adds/app/file");
    sortBrowserEntries(sorted, SortName, false);
    require(sorted.first().isDir(), "directories first even descending");
    sortBrowserEntries(sorted, SortSize, true);
    require(sorted.first().isDir() && sorted.at(1).size() <= sorted.at(2).size(), "sort sizes numerically ascending");
    sortBrowserEntries(sorted, SortSize, false);
    require(sorted.first().isDir() && sorted.at(1).size() >= sorted.at(2).size(), "sort sizes numerically descending");
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
    require(protectedLocalPath(root, root + "/.adds", true), "adds root stays protected when enabled");
    require(!protectedLocalPath(root, root + "/.adds/app/file", true), "adds contents editable when enabled");
    require(!protectedLocalPath(root, root + "/.ADDS/app/file", true), "case insensitive adds contents policy");
    require(writableLocalDirectory(root, root + "/.adds", true), "adds root is a destination when enabled");
    require(!writableLocalDirectory(root, root + "/.adds", false), "adds root is read only when disabled");
    require(protectedLocalPath(root, root + "/.kobo/Kobo", true), "kobo remains protected");
    QString addsError;
    require(!renameLocalEntry(root, root + "/.adds/app/file", "renamed", addsError), "disabled adds rename denied");
    require(renameLocalEntry(root, root + "/.adds/app/file", "renamed", addsError, true), "enabled adds rename");
    require(copyStagedTree(root, root + "/Books/book.epub", root + "/.adds/copy.epub", addsError, nullptr, true), "enabled adds destination");
    require(!moveLocalEntry(root, root + "/.adds", root + "/moved", addsError, false, true), "adds root cannot move");
    require(!mayDeleteLocalEntry(root, root + "/.kobrowse_trash", root + "/.adds", true), "adds root cannot be deleted");
    require(mayDeleteLocalEntry(root, root + "/.kobrowse_trash", root + "/.adds/copy.epub", true), "adds contents can be deleted");
    require(protectedLocalPath(root, root + "/.kobrowse_trash/item"), "trash subtree protected");
    require(protectedLocalPath(root, root + "/.KOBO/Kobo"), "system subtree protected ignoring case");
    require(protectedLocalPath(root, root + "/.AddS/app"), "plugin subtree protected ignoring case");
    require(protectedLocalPath(root, root + "/.KOBROWSE_TRASH/item"), "trash subtree protected ignoring case");
    require(!protectedLocalPath(root, root + "/.kobo-images"), "prefix sibling not protected");
    require(!protectedLocalPath(root, root + "/Books/.kobo"), "nested same name not system root");
    require(!protectedLocalPath(root, root + "-other/.kobo"), "outside prefix not protected");
    // Hard-linked files model two names for the same inode on this test host.
    // Real FAT short-name aliases for directories also need a device test.
    const QString aliasRoot = tmp.path() + "/identity-root";
    require(QDir().mkpath(aliasRoot), "identity fixture root");
    writeFile(aliasRoot + "/.kobrowse_trash", "protected fixture");
    require(::link(QFile::encodeName(aliasRoot + "/.kobrowse_trash").constData(),
                   QFile::encodeName(aliasRoot + "/SHORT_ALIAS").constData()) == 0, "identity alias fixture");
    require(protectedLocalPath(aliasRoot, aliasRoot + "/SHORT_ALIAS"), "protected identity alias rejected");
    require(protectedLocalPath(aliasRoot, aliasRoot + "/SHORT_ALIAS/child"), "alias subtree rejected");
    QString aliasError;
    require(!renameLocalEntry(aliasRoot, aliasRoot + "/SHORT_ALIAS", "renamed", aliasError),
            "cannot rename a protected identity alias");
    require(QFileInfo(aliasRoot + "/SHORT_ALIAS").exists(), "identity alias preserved");
    require(::symlink(tmp.path().toLocal8Bit().constData(), (root + "/outside").toLocal8Bit().constData()) == 0, "outside link");
    require(::symlink((root + "/.kobo").toLocal8Bit().constData(), (root + "/alias").toLocal8Bit().constData()) == 0, "protected link");
    require(::symlink("missing", (root + "/dangling").toLocal8Bit().constData()) == 0, "dangling link");
    require(::symlink("book.epub", (root + "/Books/link").toLocal8Bit().constData()) == 0, "nested file link");
    require(!safeLocalPath(root, root + "/outside"), "outside link rejected");
    require(!safeLocalPath(root, root + "/alias/Kobo"), "link parent rejected");
    require(!safeLocalPath(root, root + "/dangling", true), "dangling link rejected");
    require(!safeCopyTree(root, root + "/Books"), "nested link prevents copy");
    require(safeCopyTree(root, root + "/Excluded"), "ordinary folder can be copied");
    const QString homeFile = root + "/.adds/app/home-test";
    require(readHomeFolder(root, homeFile) == root, "missing home falls back");
    require(writeHomeFolder(root, homeFile, root + "/Books"), "save home");
    require(readHomeFolder(root, homeFile) == root + "/Books", "load home");
    require(!writeHomeFolder(root, homeFile, root + "/outside"), "reject symlink home");
    require(!writeHomeFolder(root, homeFile, root + "/Books/book.epub"), "reject file home");
    require(!writeHomeFolder(root, homeFile, tmp.path()), "reject outside home");
    require(readHomeFolder(root, homeFile) == root + "/Books", "failed writes preserve home");
    writeFile(homeFile, (root + "/missing").toUtf8());
    require(readHomeFolder(root, homeFile) == root, "missing saved folder falls back");
    require(resetHomeFolder(root, homeFile) && readHomeFolder(root, homeFile) == root, "reset home");
    const QString externalHome = tmp.path() + "/external-home";
    writeFile(externalHome, (root + "/Books").toUtf8());
    require(::symlink(QFile::encodeName(externalHome).constData(), QFile::encodeName(homeFile).constData()) == 0,
            "home file symlink fixture");
    require(readHomeFolder(root, homeFile) == root, "home file link cannot be read");
    require(!writeHomeFolder(root, homeFile, root + "/Books"), "home file link cannot be written");
    require(!resetHomeFolder(root, homeFile), "home file link cannot be removed");
    require(QFileInfo(homeFile).isSymLink() && QFileInfo(externalHome).exists(), "home link and target preserved");
    require(QFile::remove(homeFile), "remove home test link");
    require(!writeHomeFolder(root, externalHome, root + "/Books") && !resetHomeFolder(root, externalHome),
            "home file outside root rejected");
    QString renameError;
    writeFile(root + "/Books/existing", "keep");
    require(!renameLocalEntry(root, root + "/Books/book.epub", "existing", renameError), "rename collision rejected");
    require(!renameLocalEntry(root, root + "/Books/book.epub", "../bad", renameError), "rename traversal rejected");
    require(!renameLocalEntry(root, root + "/.kobo", "system", renameError), "rename system rejected");
    require(!renameLocalEntry(root, root + "/alias", "link2", renameError), "rename symlink rejected");
    require(renameLocalEntry(root, root + "/Books/existing", "new name", renameError), "rename file");
    require(QFileInfo(root + "/Books/new name").exists(), "renamed file exists");
    require(renameLocalEntry(root, root + "/Excluded", "Renamed folder", renameError), "rename folder");
    require(QFileInfo(root + "/Renamed folder").isDir(), "renamed folder exists");
    const QString sourceDir = root + "/Copy source";
    const QString destDir = root + "/Copy destination";
    require(QDir().mkpath(sourceDir + "/sub") && QDir().mkpath(destDir), "copy fixtures");
    writeFile(sourceDir + "/first", "first");
    writeFile(sourceDir + "/sub/.hidden", "hidden");
    writeFile(destDir + "/result.kobrowse_tmp", "user data");
    QString copyError;
    copiesBeforeFailure = 1;
    require(!copyStagedTree(root, sourceDir, destDir + "/result", copyError, copyWithFailure), "failed copy reported");
    require(!QFileInfo(destDir + "/result").exists(), "failure leaves no partial destination");
    require(QFileInfo(destDir + "/result.kobrowse_tmp").exists(), "user temporary-name file preserved");
    require(QDir(destDir).entryList(QStringList() << ".kobrowse-copy-*", QDir::Dirs | QDir::Hidden).isEmpty(), "failed stage cleaned");
    require(copyStagedTree(root, sourceDir + "/first", destDir + "/single", copyError), "copy file");
    QFile copied(destDir + "/single");
    require(copied.open(QIODevice::ReadOnly) && copied.readAll() == "first", "copied file contents");
    copied.close();
    require(copyStagedTree(root, sourceDir, destDir + "/result", copyError), "copy folder");
    require(QFileInfo(destDir + "/result/sub/.hidden").exists(), "nested hidden copied");
    require(!copyStagedTree(root, sourceDir, destDir + "/result", copyError), "copy collision rejected");
    require(QFileInfo(destDir + "/result/first").exists(), "collision preserves destination");
    require(!copyStagedTree(root, sourceDir, sourceDir + "/sub/copy", copyError), "copy into self rejected");
    require(!copyStagedTree(root, root + "/Books", destDir + "/unsafe", copyError), "nested symlink copy rejected");
    require(!copyStagedTree(root, sourceDir, root + "/.kobo/copied", copyError), "copy into protected folder rejected");
    require(!copyStagedTree(root, root, destDir + "/root-copy", copyError), "copy root denied");
    require(!copyStagedTree(root, root + "/.kobo/Kobo", destDir + "/system-copy", copyError), "copy protected source denied");
    require(!copyStagedTree(root, root + "/.adds/app/renamed", destDir + "/adds-copy", copyError), "disabled adds source denied");
    require(copyStagedTree(root, root + "/.adds/app/renamed", destDir + "/adds-copy", copyError, nullptr, true), "enabled adds source allowed");
    require(QDir(destDir).entryList(QStringList() << ".kobrowse-copy-*", QDir::Dirs | QDir::Hidden).isEmpty(), "successful stage cleaned");
    const QString unreadableNameDir = root + "/unrepresentable";
    require(QDir().mkdir(unreadableNameDir), "invalid name directory fixture");
    writeFile(unreadableNameDir + "/keep", "keep");
    const QByteArray invalidName("\xff", 1);
    if (QFile::encodeName(QFile::decodeName(invalidName)) != invalidName) {
        const QByteArray rawPath = QFile::encodeName(unreadableNameDir) + "/" + invalidName;
        const int fd = ::open(rawPath.constData(), O_CREAT | O_WRONLY | O_EXCL, 0600);
        require(fd >= 0, "create unrepresentable name"); ::close(fd);
        QFileInfoList entries;
        require(!operationEntries(unreadableNameDir, entries) && entries.isEmpty(), "reject lossy directory names");
        require(!safeCopyTree(root, unreadableNameDir), "unrepresentable source rejected");
        require(!copyStagedTree(root, unreadableNameDir, destDir + "/bad-names", copyError), "no incomplete copy published");
        require(!removeTreeNoFollow(root, unreadableNameDir) && QFileInfo(unreadableNameDir + "/keep").exists(),
                "deletion does not proceed after failed listing");
        require(::unlink(rawPath.constData()) == 0, "remove unrepresentable test file");
    }
    QFileInfoList missingEntries;
    require(!operationEntries(root + "/missing-dir", missingEntries), "failed directory open is not an empty listing");
    const QString trash = root + "/.kobrowse_trash";
    require(QDir().mkpath(trash + "/nested"), "trash folder");
    writeFile(trash + "/nested/book", "trash");
    require(copyStagedTree(root, trash + "/nested/book", destDir + "/trash-copy", copyError), "copy trash contents remains available");
    require(::symlink((root + "/Books/book.epub").toLocal8Bit().constData(),
                      (trash + "/nested/link").toLocal8Bit().constData()) == 0, "trash link");
    require(!mayDeleteLocalEntry(root, trash, trash), "trash root cannot be deleted");
    require(!mayDeleteLocalEntry(root, trash, root + "/.kobo/Kobo"), "system folder cannot be deleted");
    require(!mayDeleteLocalEntry(root, trash, trash + "/../.kobo"), "trash traversal cannot bypass system protection");
    require(!mayDeleteLocalEntry(root, trash, trash + "/nested/../.."), "trash traversal cannot delete root");
    require(readOnlyLocalEntry(root, trash, root + "/.kobo"), "system folder shown as read-only");
    require(readOnlyLocalEntry(root, trash, root + "/.adds/app"), "system contents shown as read-only");
    require(readOnlyLocalEntry(root, trash, trash), "trash root shown as read-only");
    require(!readOnlyLocalEntry(root, trash, trash + "/nested"), "trash contents remain selectable");
    const QStringList protectedEntries = QStringList() << root + "/.kobo" << root + "/.adds" << trash;
    for (const QString &protectedEntry : protectedEntries) {
        QString error;
        require(!mayDeleteLocalEntry(root, trash, protectedEntry), "protected folder deletion denied");
        require(!renameLocalEntry(root, protectedEntry, "renamed-system", error), "protected folder rename denied");
        require(!moveLocalEntry(root, protectedEntry, root + "/moved-system", error), "protected folder move denied");
        require(!moveLocalEntry(root, protectedEntry, trash + "/protected", error, true), "protected folder trash move denied");
        require(QFileInfo(protectedEntry).isDir(), "protected folder preserved");
    }
    QString moveError;
    writeFile(root + "/to-move", "move");
    require(moveLocalEntry(root, root + "/to-move", root + "/moved", moveError), "normal move");
    require(moveLocalEntry(root, root + "/moved", trash + "/moved", moveError, true), "normal move to trash");
    require(QFileInfo(trash + "/moved").exists(), "trash move retained data");
    const QString spareRoot = tmp.path() + "/spare-root";
    require(QDir().mkpath(spareRoot), "spare root");
    writeFile(spareRoot + "/user", "keep");
    require(!renameLocalEntry(spareRoot, spareRoot + "/user", ".KoBo", moveError), "rename to reserved name denied");
    require(!moveLocalEntry(spareRoot, spareRoot + "/user", spareRoot + "/.AdDs", moveError), "move to reserved name denied");
    require(!copyStagedTree(spareRoot, spareRoot + "/user", spareRoot + "/.KOBROWSE_TRASH", moveError), "copy to reserved name denied");
    require(QFileInfo(spareRoot + "/user").exists(), "reserved destinations preserve source");
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
    const QString marker = root + "/.adds/kobrowse/uninstall";
    const QString plugin = root + "/plugin.so";
    writeFile(marker, "marker");
    writeFile(plugin, "plugin");
    QString uninstallError;
    require(!uninstallRequested(root) && !cleanupAppInstall(root, plugin, uninstallError), "marker prevents removal");
    require(QFile::remove(marker) && uninstallRequested(root), "marker removal requests uninstall");
    require(!cleanupAppInstall(root, root + "/missing.so", uninstallError), "missing plugin aborts uninstall");
    require(QFileInfo(root + "/.adds/kobrowse/.ini").exists(), "failure preserves data");
    require(cleanupAppInstall(root, plugin, uninstallError), "uninstall cleanup");
    require(!QFileInfo(plugin).exists() && !QFileInfo(root + "/.adds/kobrowse").exists(), "plugin removed and app data moved");
    const QStringList backups = QDir(trash).entryList(QStringList() << "kobrowse-uninstall-*", QDir::Dirs | QDir::NoDotAndDotDot);
    require(backups.size() == 1 && QFileInfo(trash + "/" + backups.first() + "/.ini").exists(), "config preserved in trash");
    require(!QFileInfo(root + "/.adds/nm/kobrowse").exists(), "app menu removed");
    require(QFileInfo(root + "/.adds/nm/other").exists() && QFileInfo(root + "/outside.txt").exists(),
            "other data and link target retained");
    const QString brokenRoot = tmp.path() + "/broken";
    require(QDir().mkpath(brokenRoot + "/.adds/kobrowse"), "uninstall error fixture");
    writeFile(brokenRoot + "/.adds/kobrowse/kobrowse.ini", "settings");
    writeFile(brokenRoot + "/.kobrowse_trash", "not a directory");
    writeFile(brokenRoot + "/plugin.so", "plugin");
    require(!cleanupAppInstall(brokenRoot, brokenRoot + "/plugin.so", uninstallError), "invalid trash aborts uninstall");
    require(QFileInfo(brokenRoot + "/.adds/kobrowse/kobrowse.ini").exists()
            && QFileInfo(brokenRoot + "/plugin.so").exists(), "invalid trash preserves plugin and settings");
    require(::symlink("missing-target", (brokenRoot + "/.adds/kobrowse/uninstall").toLocal8Bit().constData()) == 0,
            "dangling marker fixture");
    require(!uninstallRequested(brokenRoot), "dangling marker is not absent");
    require(!uninstallRequested(tmp.path() + "/not-mounted"), "missing root is not uninstall request");
    QTextStream(stdout) << "Filesystem tests passed\n";
    return 0;
}
