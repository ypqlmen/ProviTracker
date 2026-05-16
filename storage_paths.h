#pragma once

#include <QtCore>

static QString appStorageDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + "/ProviTracker";
}

static void copyLegacyDataFiles(const QString& sourceDir, const QString& targetDir, const QStringList& files)
{
    if (sourceDir.isEmpty() || !QDir(sourceDir).exists())
        return;

    if (QDir(sourceDir).absolutePath() == QDir(targetDir).absolutePath())
        return;

    QDir().mkpath(targetDir);

    for (const QString& name : files) {
        const QString src = sourceDir + "/" + name;
        const QString dst = targetDir + "/" + name;
        if (QFileInfo::exists(src) && !QFileInfo::exists(dst)) {
            QFile::copy(src, dst);
        }
    }
}

static bool copyDirectoryContentsIfMissing(const QString& sourceDir, const QString& targetDir)
{
    if (sourceDir.isEmpty() || !QDir(sourceDir).exists() || QDir(targetDir).exists())
        return false;

    QDir().mkpath(targetDir);

    QDir source(sourceDir);
    const QFileInfoList entries = source.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System
        );

    for (const QFileInfo& entry : entries) {
        const QString dst = targetDir + "/" + entry.fileName();
        if (entry.isDir()) {
            copyDirectoryContentsIfMissing(entry.absoluteFilePath(), dst);
        } else if (!QFileInfo::exists(dst)) {
            QFile::copy(entry.absoluteFilePath(), dst);
        }
    }

    return true;
}

static QString legacy11InstallLocation()
{
#ifdef Q_OS_WIN
    QSettings legacyKey(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{CBA2670F-F574-46E0-8913-FBEF7822C1B7}_is1",
        QSettings::NativeFormat
        );
    return legacyKey.value("InstallLocation").toString();
#else
    return {};
#endif
}

static void migrateLegacyDataIfNeeded() {
    const QString targetDir = appStorageDir();
    const QString legacyInstallRoot = QCoreApplication::applicationDirPath();
    const QString legacyInstallDir = legacyInstallRoot + "/data";
    const QString legacy11Dir = legacy11InstallLocation();
    const QString legacyRoamingDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/ProviTracker";

    const QStringList files = {
        "salespeople.json",
        "products.json",
        "orders.json",
        "settings.json"
    };

    // Version 1.1 gemte JSON-filer ved siden af programmet. En tidligere
    // migrering kopierede dem til Roaming, så begge kilder tjekkes her.
    copyLegacyDataFiles(legacyInstallRoot, targetDir, files);
    copyLegacyDataFiles(legacyInstallDir, targetDir, files);
    copyLegacyDataFiles(legacy11Dir, targetDir, files);
    copyLegacyDataFiles(legacy11Dir + "/data", targetDir, files);
    copyLegacyDataFiles(legacyRoamingDir, targetDir, files);
    copyDirectoryContentsIfMissing(legacyInstallRoot + "/snapshots", targetDir + "/snapshots");
    copyDirectoryContentsIfMissing(legacyInstallDir + "/snapshots", targetDir + "/snapshots");
    copyDirectoryContentsIfMissing(legacy11Dir + "/snapshots", targetDir + "/snapshots");
    copyDirectoryContentsIfMissing(legacy11Dir + "/data/snapshots", targetDir + "/snapshots");
    copyDirectoryContentsIfMissing(legacyRoamingDir + "/snapshots", targetDir + "/snapshots");
    copyDirectoryContentsIfMissing(legacyInstallRoot + "/reports", targetDir + "/reports");
    copyDirectoryContentsIfMissing(legacyInstallDir + "/reports", targetDir + "/reports");
    copyDirectoryContentsIfMissing(legacy11Dir + "/reports", targetDir + "/reports");
    copyDirectoryContentsIfMissing(legacy11Dir + "/data/reports", targetDir + "/reports");
    copyDirectoryContentsIfMissing(legacyRoamingDir + "/reports", targetDir + "/reports");

    const QString marker = targetDir + "/.migrated";
    if (!QFileInfo::exists(marker)) {
        QFile f(marker);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("ok");
        }
    }
}
