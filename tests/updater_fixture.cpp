#include <QtCore>

// Harmless installer/application stand-in. Writes only within the supplied test directory.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--version")) {
        QTextStream(stdout) << "1.6.0\n";
        return 0;
    }
    if (QFileInfo(app.applicationFilePath()).fileName() == "ProvisionTrackerV2.exe") {
        QFile marker(QDir(app.applicationDirPath()).filePath("launched.txt"));
        if (!marker.open(QIODevice::WriteOnly)) return 9;
        marker.write("launched");
        return 0;
    }
    if (qEnvironmentVariableIsSet("PROVI_PROBE_INSTALL_FAIL")) return 12;
    for (const auto &arg : app.arguments()) {
        if (!arg.startsWith("/DIR=")) continue;
        const QString target = arg.mid(5);
        QDir().mkpath(target);
        return QFile::copy(app.applicationFilePath(), QDir(target).filePath("ProvisionTrackerV2.exe")) ? 0 : 8;
    }
    return 7;
}
