#include "../repository.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) return 1;
    const QDir root(temp.path());
    root.mkdir("debug_intramanager");
    const QStringList removed = {
        "kvikoc_login.json", "kvikoc_session.json",
        "debug_intramanager/kvikoc_search_failed.html",
        "debug_intramanager/kvikoc_login_failed.png"
    };
    const QStringList retained = {
        "intramanager_login.json", "intramanager_session.json", "cloud_session.json",
        "orders.json", "settings.json", "debug_intramanager/history.png"
    };
    for (const auto& name : removed + retained) {
        QFile file(root.filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write("fixture") != 7) return 2;
    }
    if (!removeRetiredIntegrationFilesFrom(temp.path())) return 3;
    if (!removeRetiredIntegrationFilesFrom(temp.path())) return 4;
    for (const auto& name : removed) {
        if (QFileInfo::exists(root.filePath(name))) return 5;
    }
    for (const auto& name : retained) {
        QFile file(root.filePath(name));
        if (!file.open(QIODevice::ReadOnly) || file.readAll() != "fixture") return 6;
    }

    Repository repo;
    const QJsonObject secrets{
        {"kvikocLogin", QJsonObject{{"encrypted", "retired"}}},
        {"intramanagerLogin", QJsonObject{{"encrypted", "retain"}}}
    };
    const auto expected = secrets.value("intramanagerLogin");
    repo.applyCloudPayload(QJsonObject{{"secrets", secrets}}, "VictorTang");
    if (repo.cloudSecrets.contains("kvikocLogin")) return 7;
    if (repo.cloudSecrets.value("intramanagerLogin") != expected) return 8;
    // Even a stale backup populated directly in memory cannot upload the retired login.
    repo.cloudSecrets = secrets;
    const auto uploaded = repo.cloudPayload().value("secrets").toObject();
    if (uploaded.contains("kvikocLogin") || uploaded.value("intramanagerLogin") != expected) return 9;
    return 0;
}
