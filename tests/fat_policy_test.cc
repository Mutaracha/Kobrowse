#include "../src/file_mutations.h"
#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QTextStream>
#include <cstdlib>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/msdos_fs.h>
#include <unistd.h>

static void require(bool ok, const char *message) {
    if (!ok) { QTextStream(stderr) << "FAIL: " << message << '\n'; std::exit(1); }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    require(argc == 2, "mounted FAT test directory required");
    const QString root = QString::fromLocal8Bit(argv[1]) + "/onboard";
    const QString trash = root + "/.kobrowse_trash";
    require(QDir().mkpath(root + "/.kobo/Kobo") && QDir().mkpath(root + "/.adds/app")
            && QDir().mkpath(trash + "/nested"), "create FAT fixture");
    QFile sentinel(root + "/.kobo/Kobo/keep");
    require(sentinel.open(QIODevice::WriteOnly) && sentinel.write("keep") == 4, "write sentinel");
    sentinel.close();
    // Linux's FAT ioctl provides the actual 8.3 alias, not an inferred name.
    QMap<QString, QString> shortNames;
    int fd = ::open(QFile::encodeName(root).constData(), O_RDONLY | O_DIRECTORY);
    require(fd >= 0, "open FAT directory");
    for (;;) {
        struct __fat_dirent names[2] = {};
        const int result = ::ioctl(fd, VFAT_IOCTL_READDIR_BOTH, names);
        require(result >= 0, "read FAT short names");
        if (result == 0) break;
        if (names[1].d_reclen) shortNames.insert(QFile::decodeName(names[1].d_name), QFile::decodeName(names[0].d_name));
    }
    ::close(fd);
    require(shortNames.contains(".kobrowse_trash"), "trash has actual FAT short alias");
    const QStringList paths = QStringList() << root + "/.kobo" << root + "/.KOBO"
        << root + "/.adds" << root + "/.ADDS" << trash << root + "/" + shortNames.value(".kobrowse_trash");
    for (const QString &path : paths) {
        QString error;
        require(QFileInfo(path).isDir(), "protected spelling resolves on FAT");
        require(protectedLocalPath(root, path), "protected spelling recognized");
        require(readOnlyLocalEntry(root, trash, path), "protected spelling cannot be selected");
        require(!mayDeleteLocalEntry(root, trash, path), "protected deletion denied");
        require(!renameLocalEntry(root, path, "renamed", error), "protected rename denied");
        require(!moveLocalEntry(root, path, root + "/moved", error), "protected move denied");
        require(!moveLocalEntry(root, path, trash + "/moved", error, true), "protected trash move denied");
        require(QFileInfo(path).isDir(), "protected directory still exists");
    }
    require(!mayDeleteLocalEntry(root, trash, trash + "/../.kobo"), "trash traversal denied");
    require(!readOnlyLocalEntry(root, trash, trash + "/nested"), "trash contents remain selectable");
    require(mayDeleteLocalEntry(root, trash, trash + "/nested"), "trash contents can be deleted");
    require(sentinel.open(QIODevice::ReadOnly) && sentinel.readAll() == "keep", "sentinel unchanged");
    QTextStream(stdout) << "FAT protected-folder tests passed\n";
}
