#include "../src/zip_writer.h"
#include <QCoreApplication>
#include <QTextStream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() < 4) return 2;
    const QStringList args = app.arguments();
    QString error;
    if (!createZipArchive(args.at(1), args.at(2), args.mid(3), error)) {
        QTextStream(stderr) << error << "\n";
        return 1;
    }
    return 0;
}
