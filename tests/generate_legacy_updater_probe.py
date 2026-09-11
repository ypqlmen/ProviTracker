"""Build a sandboxed harness around the exact updater shipped in 1.5.11."""
from pathlib import Path
import subprocess

source = subprocess.check_output(
    ["git", "show", "1b5b0c3:main_v32.cpp"], text=True
)
source = source[:source.index("static void initAutoUpdate()")]
source = source.replace("private:", "public:", 1)
source += r'''
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString root = app.arguments().value(1);
    QCoreApplication::setOrganizationName("ProviUpdaterProbe");
    QCoreApplication::setApplicationName(QFileInfo(root).fileName());
    auto *updater = new ZipAutoUpdater(&app);
    updater->workDir = QDir(root).filePath("work");
    updater->extractDir = QDir(updater->workDir).filePath("extract");
    updater->update.buildVersion = 10600;
    updater->update.shortVersion = "1.6.0";
    QDir().mkpath(updater->extractDir);
    QFile::copy(app.arguments().value(2),
                QDir(updater->extractDir).filePath("ProviBeregnerSetup-probe.exe"));
    QFile info(QDir(root).filePath("probe-info.json"));
    info.open(QIODevice::WriteOnly);
    info.write(QJsonDocument(QJsonObject{
        {"dataDir", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)},
        {"targetDir", updater->perUserInstallDir()}
    }).toJson());
    info.close();
    QTimer::singleShot(0, updater, [updater]() { updater->installExtractedUpdate(); });
    return app.exec();
}
'''
Path("tests/legacy_updater_probe.cpp").write_text(source)
