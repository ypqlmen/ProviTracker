#pragma once

#include <QtCore>
#include "storage_paths.h"

static QJsonObject withoutRetiredIntegrationSecrets(QJsonObject secrets) {
    secrets.remove("kvikocLogin");
    return secrets;
}

static bool removeRetiredIntegrationFilesFrom(const QString& root) {
    if (root.isEmpty()) return true;
    bool ok = true;
    const QStringList files = {"kvikoc_login.json", "kvikoc_session.json"};
    for (const auto& name : files) {
        const QString path = QDir(root).filePath(name);
        if (QFileInfo::exists(path) && !QFile::remove(path)) ok = false;
    }
    const QStringList debugRoots = {root, QDir(root).filePath("debug_intramanager")};
    for (const auto& debugRoot : debugRoots) {
        const QDir dir(debugRoot);
        for (const auto& name : dir.entryList({"kvikoc_*"}, QDir::Files | QDir::Hidden)) {
            if (!QFile::remove(dir.filePath(name))) ok = false;
        }
    }
    return ok;
}

static void removeRetiredIntegrationFiles() {
    QStringList roots = {
        appStorageDir(),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ProviTracker",
        QCoreApplication::applicationDirPath()
    };
    roots.removeDuplicates();
    for (const auto& root : roots) {
        if (!removeRetiredIntegrationFilesFrom(root))
            qWarning("Could not remove all retired integration files; cleanup will retry at next startup.");
    }
}
