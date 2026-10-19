#include <QClipboard>
#include <QtWidgets>
#include <QtCore>
#include <QtNetwork>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include "storage_paths.h"
#include "domain.h"
#include "credentials.h"
#include "repository.h"
#include "commission.h"
#include "report_service.h"
#include "payroll_forecast.h"

#include "premium_ui.h"
#include "report_view.h"
#include "orders_view.h"
#include "order_product_picker.h"
#include "updater_script.h"
#include "sales_registration.h"

static constexpr const char* APP_VERSION = "2.0.0";
static constexpr int APP_BUILD_VERSION = 20000;
static constexpr const char* UPDATE_APPCAST_URL = "https://raw.githubusercontent.com/ypqlmen/ProviTracker/main/appcast.xml";

static QString psSingleQuoted(QString value) {
    value.replace("'", "''");
    return "'" + value + "'";
}

class ZipAutoUpdater : public QObject {
public:
    explicit ZipAutoUpdater(QObject* parent = nullptr)
        : QObject(parent) {}

    static void checkOnStartup(QObject* parent) {
        static bool started = false;
        if (started) return;
        started = true;

        auto* updater = new ZipAutoUpdater(parent);
        updater->checkForUpdate();
    }

private:
    struct UpdateInfo {
        int buildVersion = 0;
        qint64 zipLength = 0;
        QString shortVersion;
        QString zipUrl;
        QString zipSha256;
        QString installerArguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /FORCECLOSEAPPLICATIONS";
    };

    QNetworkAccessManager network;
    UpdateInfo update;
    QString workDir;
    QString zipPath;
    QString extractDir;
    QFile* downloadFile = nullptr;
    QProgressDialog* progress = nullptr;

    void checkForUpdate() {
        QUrl url(QString::fromLatin1(UPDATE_APPCAST_URL));
        QUrlQuery query;
        query.addQueryItem("t", QString::number(QDateTime::currentSecsSinceEpoch()));
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        auto* reply = network.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const QByteArray data = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();

            if (!ok) {
                deleteLater();
                return;
            }

            const auto parsed = parseAppcast(data);
            if (!parsed.has_value() || parsed->buildVersion <= APP_BUILD_VERSION || parsed->zipUrl.isEmpty()) {
                deleteLater();
                return;
            }
            if (updateAttemptRecently(parsed->buildVersion)) {
                deleteLater();
                return;
            }

            update = *parsed;
            downloadUpdateZip();
        });
    }

    std::optional<UpdateInfo> parseAppcast(const QByteArray& data) const {
        QXmlStreamReader xml(data);
        UpdateInfo info;

        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement() || xml.name() != "enclosure") {
                continue;
            }

            const auto attrs = xml.attributes();
            info.shortVersion = attrs.value("http://www.andymatuschak.org/xml-namespaces/sparkle", "shortVersionString").toString();
            if (info.shortVersion.isEmpty()) info.shortVersion = attrs.value("sparkle:shortVersionString").toString();

            const QString build = attrs.value("http://www.andymatuschak.org/xml-namespaces/sparkle", "version").toString();
            info.buildVersion = build.toInt();
            if (info.buildVersion <= 0) {
                info.buildVersion = numericBuildFromVersion(info.shortVersion);
            }

            info.installerArguments = attrs.value("http://www.andymatuschak.org/xml-namespaces/sparkle", "installerArguments").toString();
            if (info.installerArguments.isEmpty()) info.installerArguments = attrs.value("sparkle:installerArguments").toString();
            if (info.installerArguments.isEmpty()) info.installerArguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /FORCECLOSEAPPLICATIONS";

            info.zipUrl = attrs.value("https://provi-tracker.local/update", "zipUrl").toString();
            if (info.zipUrl.isEmpty()) info.zipUrl = attrs.value("provi:zipUrl").toString();
            if (info.zipUrl.isEmpty()) {
                const QString enclosureUrl = attrs.value("url").toString();
                if (enclosureUrl.endsWith(".zip", Qt::CaseInsensitive)) {
                    info.zipUrl = enclosureUrl;
                    info.zipLength = attrs.value("length").toLongLong();
                }
            }

            const QString zipLength = attrs.value("https://provi-tracker.local/update", "zipLength").toString();
            info.zipLength = zipLength.isEmpty()
                ? attrs.value("provi:zipLength").toLongLong()
                : zipLength.toLongLong();

            info.zipSha256 = attrs.value("https://provi-tracker.local/update", "zipSha256").toString();
            if (info.zipSha256.isEmpty()) info.zipSha256 = attrs.value("provi:zipSha256").toString();
            return info;
        }

        return std::nullopt;
    }

    int numericBuildFromVersion(const QString& version) const {
        const QStringList parts = version.split('.');
        if (parts.size() < 3) return 0;
        return parts.value(0).toInt() * 10000 + parts.value(1).toInt() * 100 + parts.value(2).toInt();
    }

    QString updateAttemptMarkerPath(int buildVersion) const {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/updates";
        QDir().mkpath(dir);
        return QDir(dir).filePath(QString("attempt_%1.txt").arg(buildVersion));
    }

    QString perUserInstallDir() const {
        QString localAppData = qEnvironmentVariable("LOCALAPPDATA").trimmed();
        if (localAppData.isEmpty()) {
            localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        }
        return QDir(localAppData).filePath("Programs/Provi Tracker");
    }

    bool updateAttemptRecently(int buildVersion) const {
        const QFileInfo marker(updateAttemptMarkerPath(buildVersion));
        if (!marker.exists()) return false;

        QFile markerFile(marker.filePath());
        QString markerText;
        if (markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            markerText = QString::fromUtf8(markerFile.readAll()).toLower();
        }

        const int cooldownSeconds = markerText.contains("success")
            ? 24 * 60 * 60
            : 30 * 60;
        return marker.lastModified().secsTo(QDateTime::currentDateTime()) < cooldownSeconds;
    }

    void rememberUpdateAttempt(int buildVersion) const {
        QFile marker(updateAttemptMarkerPath(buildVersion));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&marker);
            ts << "started " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
            ts << QCoreApplication::applicationFilePath() << "\n";
        }
    }

    void downloadUpdateZip() {
        workDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QString("ProviTrackerUpdate/%1_%2").arg(update.shortVersion, QString::number(QDateTime::currentMSecsSinceEpoch())));
        extractDir = QDir(workDir).filePath("extract");
        zipPath = QDir(workDir).filePath("update.zip");

        QDir().mkpath(workDir);

        downloadFile = new QFile(zipPath, this);
        if (!downloadFile->open(QIODevice::WriteOnly)) {
            failUpdate("Kunne ikke klargøre update-filen.");
            return;
        }

        progress = new QProgressDialog("Henter opdatering...", QString(), 0, 100);
        progress->setWindowTitle("Provi Tracker opdatering");
        progress->setCancelButton(nullptr);
        progress->setWindowModality(Qt::ApplicationModal);
        progress->setMinimumDuration(0);
        progress->show();

        QNetworkRequest request(QUrl(update.zipUrl));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto* reply = network.get(request);

        connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
            if (downloadFile) downloadFile->write(reply->readAll());
        });

        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            if (!progress || total <= 0) return;
            progress->setValue(static_cast<int>((received * 100) / total));
        });

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (downloadFile) {
                downloadFile->write(reply->readAll());
                downloadFile->close();
            }

            const bool ok = reply->error() == QNetworkReply::NoError;
            const QString err = reply->errorString();
            reply->deleteLater();

            if (!ok) {
                failUpdate("Download af opdatering fejlede:\n" + err);
                return;
            }

            if (!verifyZip()) {
                return;
            }

            extractUpdateZip();
        });
    }

    bool verifyZip() {
        const QFileInfo zipInfo(zipPath);
        if (!zipInfo.exists() || zipInfo.size() <= 0) {
            failUpdate("Den hentede update-fil er tom.");
            return false;
        }

        if (update.zipLength > 0 && zipInfo.size() != update.zipLength) {
            failUpdate("Den hentede update-fil har forkert størrelse.");
            return false;
        }

        if (!update.zipSha256.isEmpty()) {
            QFile f(zipPath);
            if (!f.open(QIODevice::ReadOnly)) {
                failUpdate("Kunne ikke kontrollere update-filen.");
                return false;
            }

            const QString actual = QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
            if (actual.compare(update.zipSha256, Qt::CaseInsensitive) != 0) {
                failUpdate("Den hentede update-fil matcher ikke GitHub-hashen.");
                return false;
            }
        }

        return true;
    }

    void extractUpdateZip() {
        if (progress) {
            progress->setLabelText("Pakker opdatering ud...");
            progress->setRange(0, 0);
        }

        QDir().mkpath(extractDir);

        auto* process = new QProcess(this);
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process](int exitCode, QProcess::ExitStatus status) {
                    const QString error = QString::fromUtf8(process->readAllStandardError());
                    process->deleteLater();

                    if (status != QProcess::NormalExit || exitCode != 0) {
                        failUpdate("Udpakning af opdateringen fejlede:\n" + error);
                        return;
                    }

                    installExtractedUpdate();
                });

        const QString command = QString(
            "Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force"
            ).arg(psSingleQuoted(zipPath), psSingleQuoted(extractDir));
        process->start("powershell.exe", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command});
    }

    void installExtractedUpdate() {
        const QString installer = findInstaller();
        if (installer.isEmpty()) {
            failUpdate("Kunne ikke finde installeren i update-zippen.");
            return;
        }

        if (progress) {
            progress->setLabelText("Installerer opdatering...");
            progress->setRange(0, 0);
        }

        const QString helperDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        const QString scriptPath = QDir(helperDir).filePath(
            QString("ProviTrackerInstallUpdate_%1.ps1").arg(QDateTime::currentMSecsSinceEpoch())
            );
        QFile script(scriptPath);
        if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
            failUpdate("Kunne ikke oprette installer-scriptet.");
            return;
        }

        const QString targetDir = QDir::toNativeSeparators(perUserInstallDir());
        const QString targetApp = QDir::toNativeSeparators(QDir(targetDir).filePath("ProvisionTrackerV2.exe"));
        const QString currentApp = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const QString markerPath = QDir::toNativeSeparators(updateAttemptMarkerPath(update.buildVersion));
        const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/updates";
        const QString logPath = QDir::toNativeSeparators(QDir(logDir).filePath(QString("last_update_%1.log").arg(update.buildVersion)));

        rememberUpdateAttempt(update.buildVersion);

        const QString readyPath = QDir(workDir).filePath("helper-ready.txt");
        QTextStream ts(&script);
        ts.setEncoding(QStringConverter::Utf8);
        ts.setGenerateByteOrderMark(true);
        ts << "$ErrorActionPreference = 'Continue'\n";
        ts << "$installer = " << psSingleQuoted(installer) << "\n";
        ts << "$arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CLOSEAPPLICATIONS', '/FORCECLOSEAPPLICATIONS')\n";
        ts << "$targetDir = " << psSingleQuoted(targetDir) << "\n";
        ts << "$arguments += ('/DIR=\"' + $targetDir + '\"')\n";
        ts << "$targetApp = " << psSingleQuoted(targetApp) << "\n";
        ts << "$currentApp = " << psSingleQuoted(currentApp) << "\n";
        ts << "$markerPath = " << psSingleQuoted(markerPath) << "\n";
        ts << "$logPath = " << psSingleQuoted(logPath) << "\n";
        ts << "$workDir = " << psSingleQuoted(workDir) << "\n";
        ts << "$readyPath = " << psSingleQuoted(QDir::toNativeSeparators(readyPath)) << "\n";
        ts << "$expectedVersion = " << psSingleQuoted(update.shortVersion) << "\n";
        ts << "$parentPid = " << QString::number(QCoreApplication::applicationPid()) << "\n";
        ts << updateHelperBody;
        ts.flush();
        script.close();

        const bool started = QProcess::startDetached(
            "powershell.exe",
            {"-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", scriptPath},
            helperDir
            );

        if (!started) {
            failUpdate("Kunne ikke starte installeren.");
            return;
        }

        // Do not close the running app merely because PowerShell was spawned.
        // Wait for the helper to parse successfully and verify its file paths.
        auto* readyTimer = new QTimer(this);
        auto attempts = std::make_shared<int>(0);
        connect(readyTimer, &QTimer::timeout, this, [this, readyTimer, attempts, readyPath]() {
            if (QFileInfo::exists(readyPath)) {
                readyTimer->stop();
                if (progress) progress->close();
                QCoreApplication::quit();
            } else if (++*attempts >= 450) {
                readyTimer->stop();
                QFile cancelled(QDir(workDir).filePath("helper-cancelled.txt"));
                cancelled.open(QIODevice::WriteOnly);
                cancelled.close();
                failUpdate("Opdateringen kunne ikke starte. Programmet bliver åbent.\n"
                           "Du kan installere den nye version fra downloadlinket.", true);
            }
        });
        readyTimer->start(100);
    }

    QString findInstaller() const {
        QDirIterator it(extractDir, {"ProviBeregnerSetup-*.exe"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            return it.next();
        }
        return QString();
    }

    void failUpdate(const QString& message, bool preserveFiles = false) {
        if (downloadFile) {
            downloadFile->close();
        }
        if (progress) {
            progress->close();
        }
        QMessageBox::warning(nullptr, "Opdatering", message);
        if (!preserveFiles && !workDir.isEmpty()) {
            QDir(workDir).removeRecursively();
        }
        deleteLater();
    }
};


static void initAutoUpdate()
{
    ZipAutoUpdater::checkOnStartup(qApp);
}

static constexpr const char* SUPABASE_PROJECT_URL = "https://ebaruoylcnxapffpdmxb.supabase.co";
static constexpr const char* SUPABASE_PUBLISHABLE_KEY = "sb_publishable_I3jT09hqNVPnvKUvn_ZW7Q_HtpPRASZ";

class ProviCloudClient : public QObject {
public:
    struct Result {
        bool ok = false;
        bool hasData = false;
        QString error;
        QString username;
        QString token;
        QString expiresAt;
        QJsonObject data;
    };

    explicit ProviCloudClient(QObject* parent = nullptr)
        : QObject(parent) {}

    Result login(const QString& username, const QString& password) {
        return callRpcSync(
            "provi_login",
            {
                {"p_username", username.trimmed()},
                {"p_password", password}
            }
            );
    }

    Result registerUser(const QString& username, const QString& password, const QJsonObject& payload) {
        return callRpcSync(
            "provi_register",
            {
                {"p_username", username.trimmed()},
                {"p_password", password},
                {"p_payload", payload}
            }
            );
    }

    Result load(const QString& username, const QString& token) {
        return callRpcSync(
            "provi_load",
            {
                {"p_username", username.trimmed()},
                {"p_token", token}
            }
            );
    }

    Result save(const QString& username, const QString& token, const QJsonObject& payload) {
        return callRpcSync(
            "provi_save",
            {
                {"p_username", username.trimmed()},
                {"p_token", token},
                {"p_payload", payload}
            }
            );
    }

    void saveAsync(
        const QString& username,
        const QString& token,
        const QJsonObject& payload,
        std::function<void(Result)> callback
        ) {
        callRpcAsync(
            "provi_save",
            {
                {"p_username", username.trimmed()},
                {"p_token", token},
                {"p_payload", payload}
            },
            std::move(callback)
            );
    }

    void submitSalesRegistrationAsync(
        const QString& username,
        const QString& token,
        const QJsonObject& payload,
        std::function<void(Result)> callback
        ) {
        callFunctionAsync(
            "sales-registration-submit",
            {
                {"username", username.trimmed()},
                {"token", token},
                {"payload", payload}
            },
            std::move(callback)
            );
    }

    void logoutAsync(const QString& username, const QString& token) {
        callRpcAsync(
            "provi_logout",
            {
                {"p_username", username.trimmed()},
                {"p_token", token}
            },
            [](const Result&) {}
            );
    }

private:
    QNetworkAccessManager network;

    QNetworkRequest requestForRpc(const QString& functionName) const {
        QNetworkRequest request(QUrl(QString("%1/rest/v1/rpc/%2")
            .arg(QString::fromLatin1(SUPABASE_PROJECT_URL), functionName)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Accept", "application/json");
        request.setRawHeader("apikey", SUPABASE_PUBLISHABLE_KEY);
        request.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_PUBLISHABLE_KEY);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(30000);
        return request;
    }

    QNetworkRequest requestForFunction(const QString& functionName) const {
        QNetworkRequest request(QUrl(QString("%1/functions/v1/%2")
            .arg(QString::fromLatin1(SUPABASE_PROJECT_URL), functionName)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Accept", "application/json");
        request.setRawHeader("apikey", SUPABASE_PUBLISHABLE_KEY);
        request.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_PUBLISHABLE_KEY);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(45000);
        return request;
    }

    Result parseReply(QNetworkReply* reply, const QByteArray& body) const {
        Result result;
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        const QJsonObject obj = doc.object();

        if (reply->error() != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300) {
            result.error = obj.value("message").toString();
            if (result.error.isEmpty()) {
                result.error = obj.value("error").toString();
            }
            if (result.error.isEmpty()) {
                result.error = reply->errorString();
            }
            return result;
        }

        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            result.error = "Supabase svarede med ugyldige data.";
            return result;
        }

        result.ok = obj.value("ok").toBool(false);
        result.error = obj.value("error").toString();
        result.username = obj.value("username").toString();
        result.token = obj.value("token").toString();
        result.expiresAt = obj.value("expires_at").toString();
        result.hasData = obj.value("has_data").toBool(false);
        result.data = obj.value("data").toObject();

        if (!result.ok && result.error.isEmpty()) {
            result.error = "Handlingen kunne ikke gennemfores.";
        }

        return result;
    }

    Result callRpcSync(const QString& functionName, const QJsonObject& args) {
        Result result;
        QNetworkReply* reply = network.post(requestForRpc(functionName), QJsonDocument(args).toJson(QJsonDocument::Compact));

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, [&]() {
            reply->abort();
            loop.quit();
        });
        timeout.start(15000);
        loop.exec();

        const QByteArray body = reply->readAll();
        if (!timeout.isActive()) {
            result.error = "Forbindelsen til Supabase tog for lang tid.";
        } else {
            result = parseReply(reply, body);
        }
        reply->deleteLater();
        return result;
    }

    void callRpcAsync(const QString& functionName, const QJsonObject& args, std::function<void(Result)> callback) {
        QNetworkReply* reply = network.post(requestForRpc(functionName), QJsonDocument(args).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)]() mutable {
            const QByteArray body = reply->readAll();
            Result result = parseReply(reply, body);
            reply->deleteLater();
            callback(result);
        });
    }

    void callFunctionAsync(const QString& functionName, const QJsonObject& args, std::function<void(Result)> callback) {
        QNetworkReply* reply = network.post(requestForFunction(functionName), QJsonDocument(args).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)]() mutable {
            const QByteArray body = reply->readAll();
            Result result = parseReply(reply, body);
            reply->deleteLater();
            callback(result);
        });
    }
};

class CloudLoginDialog : public QDialog {
public:
    enum class Action {
        Login,
        Register
    };

    explicit CloudLoginDialog(const QString& suggestedUsername, QWidget* parent = nullptr)
        : QDialog(parent) {
        setWindowTitle("Provi Tracker login");
        setModal(true);
        resize(460, 310);
        setMinimumWidth(440);
        setStyleSheet(premiumStyle());

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(22, 22, 22, 22);
        layout->setSpacing(14);

        auto* title = new QLabel("Log ind");
        title->setStyleSheet("font-size:22px;font-weight:600;color:#F2F4F7;");
        layout->addWidget(title);

        auto* hint = new QLabel("Log ind for at åbne dit arbejdsrum.");
        hint->setWordWrap(true);
        hint->setStyleSheet("color:#AAB3C0;");
        layout->addWidget(hint);

        usernameEdit = new QLineEdit(suggestedUsername);
        usernameEdit->setPlaceholderText("Brugernavn");
        passwordEdit = new QLineEdit;
        passwordEdit->setEchoMode(QLineEdit::Password);
        passwordEdit->setPlaceholderText("Adgangskode");
        usernameEdit->setMinimumHeight(40);
        passwordEdit->setMinimumHeight(40);

        layout->addWidget(usernameEdit);
        layout->addWidget(passwordEdit);

        statusLabel = new QLabel;
        statusLabel->setWordWrap(true);
        statusLabel->setStyleSheet("color:#FF9292;");
        layout->addWidget(statusLabel);

        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(10);
        auto* loginBtn = new QPushButton("Log ind");loginBtn->setProperty("primary",true);loginBtn->setDefault(true);
        auto* registerBtn = new QPushButton("Opret bruger");
        loginBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        registerBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        buttons->addWidget(registerBtn);
        buttons->addWidget(loginBtn);
        layout->addLayout(buttons);

        connect(loginBtn, &QPushButton::clicked, this, [this]() {
            selectedAction = Action::Login;
            validateAndAccept();
        });

        connect(registerBtn, &QPushButton::clicked, this, [this]() {
            selectedAction = Action::Register;
            validateAndAccept();
        });
    }

    QString username() const { return usernameEdit->text().trimmed(); }
    QString password() const { return passwordEdit->text(); }
    Action action() const { return selectedAction; }

    void setStatus(const QString& message) {
        statusLabel->setText(message);
    }

private:
    QLineEdit* usernameEdit = nullptr;
    QLineEdit* passwordEdit = nullptr;
    QLabel* statusLabel = nullptr;
    Action selectedAction = Action::Login;

    void validateAndAccept() {
        if (username().isEmpty()) {
            setStatus("Indtast et brugernavn.");
            return;
        }
        if (password().isEmpty()) {
            setStatus("Indtast en adgangskode.");
            return;
        }
        accept();
    }
};


static QPair<QFrame*, QVBoxLayout*> createCard(const QString& title) {
    auto* frame = new PremiumCard;
    frame->setProperty("kpiCard", true);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title);
    titleLabel->setWordWrap(true);
    titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    titleLabel->setProperty("role","cardTitle");

    layout->addWidget(titleLabel);

    return qMakePair(frame, layout);
}

class RoundedProgressBar : public QProgressBar {
public:
    using QProgressBar::QProgressBar;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QFont f=font();f.setPixelSize(15);f.setWeight(QFont::DemiBold);p.setFont(f);p.setPen(QColor("#E3E9F6"));p.drawText(QRect(0,0,width(),24),Qt::AlignLeft|Qt::AlignVCenter,text());
        QRectF track(0,height()-7,width(),5);p.setPen(Qt::NoPen);p.setBrush(QColor("#3A414E"));p.drawRoundedRect(track,2.5,2.5);
        const double ratio=maximum()>minimum()?qBound(0.0,double(value()-minimum())/(maximum()-minimum()),1.0):0;
        if(ratio>0){track.setWidth(qMax(3.0,track.width()*ratio));auto c=property("progressColor").value<QColor>();p.setBrush(c.isValid()?c:QColor("#8EAFEF"));p.drawRoundedRect(track,2.5,2.5);}
    }

};

class RoundedTableWidget : public QTableWidget {
public:
    using QTableWidget::QTableWidget;

protected:
    void paintEvent(QPaintEvent* event) override {
        QTableWidget::paintEvent(event);
        if(rowCount()!=0 || property("emptyTitle").toString().isEmpty())return;
        QPainter p(viewport());p.setRenderHint(QPainter::Antialiasing);const int x=viewport()->width()/2,y=viewport()->height()/2-45;
        premiumIcon("orders",QColor("#7D91B7")).paint(&p,QRect(x-22,y-36,44,44));QFont f=font();f.setPixelSize(16);f.setWeight(QFont::DemiBold);p.setFont(f);p.setPen(QColor("#DCE3F1"));p.drawText(QRect(10,y+22,viewport()->width()-20,30),Qt::AlignCenter,property("emptyTitle").toString());
        f.setPixelSize(12);f.setWeight(QFont::Normal);p.setFont(f);p.setPen(QColor("#939FB6"));p.drawText(QRect(10,y+55,viewport()->width()-20,38),Qt::AlignCenter|Qt::TextWordWrap,property("emptyDetail").toString());
    }

    void resizeEvent(QResizeEvent* event) override {
        QTableWidget::resizeEvent(event);

        QPainterPath path;
        path.addRoundedRect(rect(), 16, 16);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }
};

class CleanTableHeaderView : public QHeaderView {
public:
    explicit CleanTableHeaderView(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QHeaderView(orientation, parent)
    {
        setFixedHeight(38);
        setMinimumSectionSize(86);
        setDefaultAlignment(Qt::AlignCenter);
        setHighlightSections(false);
        setSectionsClickable(false);
        setStretchLastSection(false);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override {
        if (!painter || !model() || rect.isEmpty())
            return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#2B3039"));

        painter->drawRect(rect);

        QFont headerFont = font();
        headerFont.setWeight(QFont::Medium);
        headerFont.setPointSizeF(qMax<qreal>(9.0, headerFont.pointSizeF()));
        painter->setFont(headerFont);
        painter->setPen(QColor("#A4AFC3"));

        const QString text = model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString();
        const QRect textRect = rect.adjusted(14, 0, -10, 1);
        const QString elided = fontMetrics().elidedText(text, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, elided);
        painter->restore();
    }
};

class ClearableTableWidget : public RoundedTableWidget {
public:
    using RoundedTableWidget::RoundedTableWidget;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (!itemAt(event->pos())) {
            clearSelection();
            setCurrentItem(nullptr);
        }
        RoundedTableWidget::mousePressEvent(event);
    }
};

static void styleDataTable(QTableWidget* table,bool allowSelection=true) {
    if(!table)return;table->setFrameShape(QFrame::NoFrame);table->setFocusPolicy(allowSelection?Qt::ClickFocus:Qt::NoFocus);
    table->horizontalHeader()->setFixedHeight(40);table->horizontalHeader()->setHighlightSections(false);table->setShowGrid(false);
    table->setStyleSheet("QTableWidget {border:none;background:transparent;} QTableWidget::item {padding:12px 10px;}");
}

static bool confirmQuestion(QWidget* parent, const QString& title, const QString& message)
{
    QMessageBox box(parent);
    box.setWindowTitle(title);
    box.setText(message);
    box.setIcon(QMessageBox::Question);
    auto* yesButton = box.addButton("Ja", QMessageBox::YesRole);
    auto* noButton = box.addButton("Nej", QMessageBox::NoRole);
    box.setDefaultButton(noButton);
    box.exec();
    return box.clickedButton() == yesButton;
}

static QPair<QFrame*, QLabel*> createKpiCard(const QString& title) {
    auto card = createCard(title);

    card.first->setMinimumHeight(118);
    card.first->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* valueLabel = new QLabel("-");
    valueLabel->setWordWrap(true);
    valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    valueLabel->setMinimumHeight(48);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    valueLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    valueLabel->setStyleSheet(
        "QLabel { "
        "color: #F2F4F7; "
        "font-size: 18px; "
        "font-weight: 900; "
        "background: transparent; "
        "line-height: 130%; "
        "}"
        );

    card.second->addWidget(valueLabel);
    card.second->addStretch();

    return qMakePair(card.first, valueLabel);
}

static QPair<QFrame*, QLabel*> createSummaryCard(const QString& title) {
    auto card = createCard(title);
    auto* content = new QLabel("-");
    content->setWordWrap(true);
    content->setTextInteractionFlags(Qt::NoTextInteraction);
    content->setStyleSheet("QLabel { color:#A5AFC2; font-size:13px; font-weight:400; background:transparent; }");
    card.second->addWidget(content);
    card.second->addStretch();
    return qMakePair(card.first, content);
}

static QFrame* createProgressCard(const QString& title, QProgressBar** barOut, QLabel** hintOut) {
    auto card = createCard(title);

    auto* bar = new RoundedProgressBar;
    bar->setRange(0, 100);
    bar->setMinimumHeight(39);
    bar->setMaximumHeight(39);
    bar->setProperty("progressColor", QColor("#78ADFF"));

    auto* hint = new QLabel("-");
    hint->setWordWrap(true);
    hint->setTextInteractionFlags(Qt::NoTextInteraction);
    hint->setStyleSheet("QLabel { color:#9DA9BF; font-size:12px; background:transparent; }");

    card.second->addWidget(bar);
    card.second->addWidget(hint);

    *barOut = bar;
    *hintOut = hint;

    return card.first;
}

static void configureSettingsForm(QFormLayout* form) {
    if (!form) return;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(18);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
}

static void configureSettingsField(QWidget* widget, int minWidth = 220) {
    if (!widget) return;
    widget->setMinimumHeight(34);
    widget->setMinimumWidth(minWidth);
    widget->setMaximumWidth(460);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

static void configureSettingsButton(QPushButton* button) {
    if (!button) return;
    button->setMinimumHeight(36);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

static QWidget* createSettingsButtonGrid(const QVector<QPushButton*>& buttons, int columns = 2) {
    auto* container = new QWidget;
    auto* grid = new QGridLayout(container);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(0, 1);
    if (columns > 1) grid->setColumnStretch(1, 1);

    for (int i = 0; i < buttons.size(); ++i) {
        configureSettingsButton(buttons[i]);
        grid->addWidget(buttons[i], i / columns, i % columns);
    }

    container->setMinimumWidth(columns > 1 ? 320 : 0);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return container;
}


class OrderEditorDialog : public QDialog {
    Order order;
    QLineEdit *idEdit,*initialsEdit,*cvrEdit,*companyNameEdit,*phoneEdit,*noteEdit;
    QDateTimeEdit* dateEdit;
    QTabWidget* steps;
    OrderProductPicker* products;
    void saveOrder(){
        const QVector<QPair<QLineEdit*,QString>> required={{idEdit,"ordrenummer"},{initialsEdit,"sælgerinitialer"},{cvrEdit,"CVR-nummer"},{companyNameEdit,"virksomhed"},{phoneEdit,"telefonnummer"}};
        for(const auto& field:required)if(field.first->text().trimmed().isEmpty()){steps->setCurrentIndex(0);field.first->setFocus();QMessageBox::warning(this,"Manglende ordreoplysninger","Udfyld "+field.second+" for at gemme ordren.");return;}
        if(products->items().isEmpty()){steps->setCurrentIndex(1);products->focusSearch();QMessageBox::warning(this,"Tom ordre","Tilføj mindst ét produkt.");return;}
        order.id=idEdit->text().trimmed();order.sellerInitials=initialsEdit->text().trimmed();order.cvrNumber=cvrEdit->text().trimmed();order.companyName=companyNameEdit->text().trimmed();order.phoneNumber=phoneEdit->text().trimmed();order.createdAt=dateEdit->dateTime();order.note=noteEdit->text().trimmed();order.items=products->items();accept();
    }
public:
    OrderEditorDialog(Repository& repo,const QString& salespersonId,std::optional<Order> existing=std::nullopt,QWidget* parent=nullptr):QDialog(parent){
        setWindowTitle(existing?"Rediger ordre":"Ny ordre");setMinimumSize(960,600);QSize size(1120,790);if(auto* screen=QGuiApplication::primaryScreen())size=size.boundedTo(screen->availableGeometry().size()-QSize(36,48));resize(size);
        if(existing)order=*existing;else{order.salespersonId=salespersonId;order.createdAt=QDateTime::currentDateTime();order.sellerInitials=repo.settings.defaultSellerInitials;}
        auto* shell=new QVBoxLayout(this);shell->setContentsMargins(26,24,26,22);shell->setSpacing(18);shell->addWidget(premiumLabel(existing?"Rediger ordre":"Ny ordre","pageTitle"));
        steps=new QTabWidget;steps->setObjectName("orderSteps");auto* detailsScroll=new QScrollArea;detailsScroll->setWidgetResizable(true);auto* detailsBody=new QWidget;auto* detailsLayout=new QVBoxLayout(detailsBody);detailsLayout->setContentsMargins(0,18,8,8);detailsLayout->setSpacing(18);
        auto* details=new PremiumCard;auto* card=new QVBoxLayout(details);card->setContentsMargins(24,22,24,24);card->setSpacing(18);card->addWidget(premiumLabel("Ordreoplysninger","sectionTitle"));auto* grid=new QGridLayout;grid->setHorizontalSpacing(16);grid->setVerticalSpacing(18);
        idEdit=new QLineEdit(order.id);idEdit->setPlaceholderText("Ordrenummer");idEdit->setClearButtonEnabled(true);idEdit->setObjectName("orderId");
        dateEdit=new QDateTimeEdit(order.createdAt);dateEdit->setCalendarPopup(true);dateEdit->setDisplayFormat("dd-MM-yyyy HH:mm");dateEdit->setLocale(QLocale(QLocale::Danish,QLocale::Denmark));
        initialsEdit=new QLineEdit(order.sellerInitials);initialsEdit->setObjectName("orderInitials");initialsEdit->setPlaceholderText("Fx 5RVPTN");cvrEdit=new QLineEdit(order.cvrNumber);cvrEdit->setObjectName("orderCvr");cvrEdit->setPlaceholderText("CVR-nummer");companyNameEdit=new QLineEdit(order.companyName);companyNameEdit->setObjectName("orderCompany");companyNameEdit->setPlaceholderText("Virksomhedens navn");phoneEdit=new QLineEdit(order.phoneNumber);phoneEdit->setObjectName("orderPhone");phoneEdit->setPlaceholderText("Telefonnummer");noteEdit=new QLineEdit(order.note);noteEdit->setPlaceholderText("Valgfri note til ordren");
        const QVector<QPair<QString,QWidget*>> fields={{"Ordrenummer",idEdit},{"Dato & tidspunkt",dateEdit},{"Sælgerinitialer",initialsEdit},{"Virksomhed",companyNameEdit},{"CVR-nummer",cvrEdit},{"Telefonnummer",phoneEdit}};
        for(int i=0;i<fields.size();++i){auto* cell=new QVBoxLayout;cell->setSpacing(8);cell->addWidget(premiumLabel(fields[i].first,"caption"));cell->addWidget(fields[i].second);grid->addLayout(cell,i/3,i%3);}for(int i=0;i<3;++i)grid->setColumnStretch(i,1);card->addLayout(grid);
        card->addWidget(premiumLabel("Note · valgfri","caption"));card->addWidget(noteEdit);detailsLayout->addWidget(details);detailsLayout->addStretch();detailsScroll->setWidget(detailsBody);steps->addTab(detailsScroll,"1  Ordreoplysninger");
        auto* productPage=new QWidget;auto* productBody=new QVBoxLayout(productPage);productBody->setContentsMargins(0,18,0,0);products=new OrderProductPicker(repo,salespersonId);products->setItems(order.items);productBody->addWidget(products);steps->addTab(productPage,"2  Produkter");shell->addWidget(steps,1);
        shell->addWidget(premiumDivider());auto* footer=new QHBoxLayout;footer->setSpacing(12);auto* summary=premiumLabel(products->summary(),"detailValue");footer->addWidget(summary,1);
        auto* back=new QPushButton("Tilbage");back->setAutoDefault(false);footer->addWidget(back);back->hide();auto* cancel=new QPushButton("Annuller");cancel->setAutoDefault(false);footer->addWidget(cancel);auto* next=new QPushButton("Vælg produkter");next->setObjectName("orderNext");next->setAutoDefault(false);next->setProperty("primary",true);footer->addWidget(next);shell->addLayout(footer);
        products->selectionChanged=[this,summary](){summary->setText(products->summary());};
        connect(steps,&QTabWidget::currentChanged,this,[back,next](int index){back->setVisible(index==1);next->setText(index==1?"Gem ordre":"Vælg produkter");});connect(back,&QPushButton::clicked,this,[this](){steps->setCurrentIndex(0);});connect(next,&QPushButton::clicked,this,[this](){if(steps->currentIndex()==0){steps->setCurrentIndex(1);products->focusSearch();}else saveOrder();});connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
        auto* add=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_N),this);connect(add,&QShortcut::activated,this,[this](){steps->setCurrentIndex(1);products->focusSearch();});
        auto* duplicate=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_D),this);connect(duplicate,&QShortcut::activated,this,[this](){const auto selected=products->items();if(!selected.isEmpty())products->addProduct(selected.last().productKey,selected.last().quantity);});
        auto* save=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_S),this);connect(save,&QShortcut::activated,this,[this](){saveOrder();});
    }
    Order getOrder()const{return order;}
};

// ============================================================
// Main window
// ============================================================

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        migrateLegacyDataIfNeeded();
        removeRetiredIntegrationFiles();
        repo.load();

        if (qEnvironmentVariableIsSet("PROVI_VISUAL_REVIEW_DIR")) {
            ensureLocalFallbackUser();
        } else {
            if (!authenticateCloudUser()) {
                startupAborted = true;
                QTimer::singleShot(0, qApp, &QCoreApplication::quit);
                return;
            }

            repo.localPersistenceEnabled = false;
            repo.cloudPersistenceEnabled = true;
            repo.cloudSaveRequested = [this]() { scheduleCloudSave(); };

            cloudSaveTimer = new QTimer(this);
            cloudSaveTimer->setSingleShot(true);
            connect(cloudSaveTimer, &QTimer::timeout, this, [this]() { flushCloudSave(); });
        }

        ReportService::autoClosePreviousMonths(repo);

        setupUi();
        refreshAll();
        setupIntramanagerAutoSync();
        if (!qEnvironmentVariableIsSet("PROVI_VISUAL_REVIEW_DIR"))
            QTimer::singleShot(0, this, [this]() { updateInstalledChromeExtension(); });
        QTimer::singleShot(2000, this, [this]() { processMasterQueue(); });
        auto* masterRetryTimer = new QTimer(this);
        connect(masterRetryTimer, &QTimer::timeout, this, [this]() { processMasterQueue(); });
        masterRetryTimer->start(60000);
    }

    bool startupWasAborted() const {
        return startupAborted;
    }

    void captureVisualReviewAndQuit(const QString& outputDir) {
        QDir().mkpath(outputDir);
        resize(1280,900);
        show();
        qApp->processEvents();

        if(qApp->windowIcon().isNull())qFatal("Program icon missing");qApp->windowIcon().pixmap(256,256).save(QDir(outputDir).filePath("30-program-icon.png"));
        const auto* avatar=findChild<QLabel*>("brandAvatar");
        if(!avatar || avatar->pixmap().isNull())qFatal("Brand photo was not loaded");
        const QImage windowImage=grab().toImage();const qreal ratio=windowImage.devicePixelRatio();const QPoint origin=avatar->mapTo(this,QPoint());QSet<QRgb> photoColors;
        for(int y=3;y<41;y+=2)for(int x=3;x<41;x+=2)photoColors.insert(windowImage.pixel(qRound((origin.x()+x)*ratio),qRound((origin.y()+y)*ratio)));
        if(photoColors.size()<40)qFatal("Brand photo is missing from rendered window");

        auto saveWidget = [](QWidget* widget, const QString& path) {
            widget->grab().save(path);
        };

        if (auto* tabs = findChild<QTabWidget*>("mainPages")) {
            const QMap<QString,QString> tabNames = {
                {"Overblik","01-dashboard.png"}, {"Ordrer","02-orders.png"},
                {"Rapporter","03-reports.png"},
                {"Indstillinger","05-settings.png"}
            };
            const int count = tabs->count();if(count!=4)qFatal("Unexpected retired navigation page");
            for(const auto* button:findChildren<QPushButton*>())if(button->text().contains("Fejlsøgning"))qFatal("Retired Intramanager diagnosis button is present");
            for (int i = 0; i < count; ++i) {
                tabs->setCurrentIndex(i);
                if(tabs->tabText(i)=="Rapporter" && reportPresetCombo)reportPresetCombo->setCurrentIndex(3);
                qApp->processEvents();
                saveWidget(this, QDir(outputDir).filePath(tabNames.value(tabs->tabText(i))));
                if(tabs->tabText(i)=="Rapporter"){
                    if(auto* scroll=findChild<QScrollArea*>("reportScroll")){scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());qApp->processEvents();saveWidget(this,QDir(outputDir).filePath("18-report-details.png"));scroll->verticalScrollBar()->setValue(0);}
                    resize(1000,640);qApp->processEvents();saveWidget(this,QDir(outputDir).filePath("19-report-compact.png"));resize(1280,900);
                    reportPresetCombo->setCurrentIndex(4);reportMonthEdit->setDate(QDate(2020,1,1));qApp->processEvents();saveWidget(this,QDir(outputDir).filePath("20-report-empty.png"));reportPresetCombo->setCurrentIndex(3);qApp->processEvents();
                }
                if (tabs->tabText(i) == "Indstillinger") {
                    if (salesRegistrationEnabledCheck) {
                        const QSignalBlocker blocker(salesRegistrationEnabledCheck);
                        salesRegistrationEnabledCheck->setChecked(true);
                    }
                    if(auto* sections=findChild<QTabWidget*>("settingsTabs")){
                        const QStringList names={"05-settings.png","08-settings-payroll.png","09-settings-intramanager.png","10-settings-masterark.png","11-settings-account.png"};
                        for(int j=0;j<sections->count();++j){sections->setCurrentIndex(j);qApp->processEvents();saveWidget(this,QDir(outputDir).filePath(names[j]));}
                    }

                }
            }
        } else {
            saveWidget(this, QDir(outputDir).filePath("01-window.png"));
        }

        if (const auto* s = activeSalesperson()) {
            OrderEditorDialog orderDialog(repo, s->id, std::nullopt, this);
            orderDialog.show();
            qApp->processEvents();
            saveWidget(&orderDialog, QDir(outputDir).filePath("06-new-order-dialog.png"));
            orderDialog.resize(960,620);qApp->processEvents();saveWidget(&orderDialog,QDir(outputDir).filePath("15-new-order-compact.png"));
            if(auto* steps=orderDialog.findChild<QTabWidget*>("orderSteps")){
                orderDialog.resize(1120,790);steps->setCurrentIndex(1);qApp->processEvents();
                if(auto* add=orderDialog.findChild<QAbstractButton*>("catalog_mobil_25gb_36"))add->click();
                if(auto* add=orderDialog.findChild<QAbstractButton*>("catalog_til_datakort"))add->click();
                if(auto* quantity=orderDialog.findChild<QSpinBox*>("quantity_mobil_25gb_36"))quantity->setValue(8);
                qApp->processEvents();saveWidget(&orderDialog,QDir(outputDir).filePath("21-product-picker.png"));
                if(auto* search=orderDialog.findChild<QLineEdit*>("productSearch")){
                    search->setText("50GB");qApp->processEvents();saveWidget(&orderDialog,QDir(outputDir).filePath("22-product-search.png"));search->setText("ingenprodukt123");qApp->processEvents();saveWidget(&orderDialog,QDir(outputDir).filePath("23-product-empty.png"));search->clear();
                }
                orderDialog.resize(960,620);qApp->processEvents();saveWidget(&orderDialog,QDir(outputDir).filePath("24-product-compact.png"));
            }
            const QVector<QPair<QString,QString>> demoFields={{"orderId","VISUAL-DEMO-ORDER"},{"orderInitials","DEMO"},{"orderCvr","12345678"},{"orderCompany","Fiktiv virksomhed"},{"orderPhone","12345678"}};
            for(const auto& field:demoFields){auto* edit=orderDialog.findChild<QLineEdit*>(field.first);if(!edit)qFatal("Order field missing from visual review");edit->setText(field.second);}
            auto* submit=orderDialog.findChild<QPushButton*>("orderNext");if(!submit)qFatal("Order save control missing");submit->click();
            const auto saved=orderDialog.getOrder();
            if(orderDialog.result()!=QDialog::Accepted || saved.items.size()!=2 || saved.items[0].productKey!="mobil_25gb_36" || saved.items[0].quantity!=8 || saved.items[1].productKey!="til_datakort" || saved.items[1].quantity!=1)qFatal("Product picker did not save selected quantities");
            Order registered=saved;registered.masterRegistrationState="registered";registered.masterRegistration=QJsonObject{{"recordId","fake-demo-record"}};
            OrderEditorDialog editDialog(repo,s->id,registered,this);editDialog.show();editDialog.findChild<QTabWidget*>("orderSteps")->setCurrentIndex(1);qApp->processEvents();
            auto* editQuantity=editDialog.findChild<QSpinBox*>("quantity_mobil_25gb_36");if(!editQuantity)qFatal("Existing product missing from editor");editQuantity->setValue(10);qApp->processEvents();saveWidget(&editDialog,QDir(outputDir).filePath("25-edit-order.png"));
            editDialog.findChild<QPushButton*>("orderNext")->click();registered.items[0].quantity=10;
            if(editDialog.result()!=QDialog::Accepted || toJson(editDialog.getOrder())!=toJson(registered))qFatal("Editing quantities changed order metadata");
            orderDialog.close();
        }


        if(auto* pages=findChild<QTabWidget*>("mainPages")){
            pages->setCurrentIndex(0);resize(1080,720);qApp->processEvents();saveWidget(this,QDir(outputDir).filePath("12-dashboard-compact.png"));
            pages->setCurrentIndex(1);if(ordersList){const auto saved=repo.orders;repo.orders.clear();refreshOrders();qApp->processEvents();saveWidget(this,QDir(outputDir).filePath("13-orders-empty.png"));repo.orders=saved;refreshOrders();}
            pages->setCurrentIndex(0);resize(1000,640);qApp->processEvents();saveWidget(this,QDir(outputDir).filePath("14-dashboard-small.png"));
            pages->setCurrentIndex(1);resize(1280,900);if(orderSearchEdit && ordersList->count()>0){orderSearchEdit->setText(ordersList->item(0)->data(Qt::UserRole).toMap().value("id").toString());qApp->processEvents();ordersList->setCurrentRow(0);saveWidget(this,QDir(outputDir).filePath("16-orders-filtered.png"));orderSearchEdit->clear();}
            pages->setCurrentIndex(1);resize(1000,640);qApp->processEvents();if(ordersList->count()>2){ordersList->setCurrentRow(2);ordersList->scrollToItem(ordersList->item(2),QAbstractItemView::PositionAtCenter);}qApp->processEvents();saveWidget(this,QDir(outputDir).filePath("26-orders-compact.png"));resize(1280,900);
            orderSearchEdit->setText("no-matching-order-123");qApp->processEvents();if(selectedOrderRepoIndex()!=-1)qFatal("Hidden order remained selected");saveWidget(this,QDir(outputDir).filePath("27-orders-no-results.png"));orderSearchEdit->clear();
            orderStatusFilter->setCurrentIndex(3);qApp->processEvents();saveWidget(this,QDir(outputDir).filePath("28-orders-status.png"));
            for(int row=0;row<ordersList->count();++row)if(!ordersList->item(row)->isHidden()){ordersList->setCurrentRow(row);if(repo.orders[selectedOrderRepoIndex()].masterRegistrationState!="error")qFatal("Status filter selected the wrong order");break;}orderStatusFilter->setCurrentIndex(0);
            if(repo.orders.size()>1){const auto original=repo.orders[1].id;repo.orders[1].id=repo.orders[0].id;refreshOrders();
                for(int row=0;row<ordersList->count();++row)if(ordersList->item(row)->data(Qt::UserRole).toMap().value("repoIndex").toInt()==1){ordersList->setCurrentRow(row);break;}
                if(selectedOrderRepoIndex()!=1)qFatal("Duplicate order number selected another order");repo.orders[1].id=original;refreshOrders();}


        }



        QTimer::singleShot(0,this,[this,outputDir](){auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)qFatal("Setup dialog did not open");qApp->processEvents();dialog->grab().save(QDir(outputDir).filePath("29-master-setup.png"));dialog->reject();});showMasterScriptGuide();
        CloudLoginDialog loginPreview("Demo");loginPreview.show();qApp->processEvents();saveWidget(&loginPreview,QDir(outputDir).filePath("17-login.png"));loginPreview.close();

        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    }

private:
    Repository repo;
    ProviCloudClient cloudClient;
    bool startupAborted = false;
    bool masterRegistrationRunning = false;
    QString cloudUsername;
    QString cloudToken;
    QByteArray cloudSecretKey;
    QString cloudStatusText;
    QTimer* cloudSaveTimer = nullptr;
    bool cloudSaveInFlight = false;
    bool cloudSaveAgain = false;
    bool cloudSaveDirty = false;
    QLabel* cloudAccountStatusLabel = nullptr;

    QLabel* activeSalespersonLabel = nullptr;
    QLabel* daySummaryLabel = nullptr;
    QLabel* weekSummaryLabel = nullptr;
    QLabel* fortnightSummaryLabel = nullptr;
    QLabel* monthSummaryLabel = nullptr;
    QLabel* targetSummaryLabel = nullptr;
    QLabel* performanceSummaryLabel = nullptr;
    QLabel* simulatorSummaryLabel = nullptr;
    QLabel* recentActivityLabel = nullptr;

    QLabel* kpiTodayPointsLabel = nullptr;
    PremiumSalaryCard* kpiMonthCommissionLabel = nullptr;
    PremiumSalaryCard* kpiNextMonthPayLabel = nullptr;
    QLabel* kpiMonthSalesLabel = nullptr;
    QLabel* kpiMonthAddonsLabel = nullptr;
    QLabel* kpiRemainingWorkDaysLabel = nullptr;
    QLabel* intramanagerPunchStatusLabel = nullptr;
    QLabel* intramanagerPunchDetailLabel = nullptr;
    QPushButton* intramanagerPunchButton = nullptr;

    QProgressBar* targetProgressBar = nullptr;
    QProgressBar* salesTargetProgressBar = nullptr;
    QProgressBar* simoProgressBar = nullptr;
    QProgressBar* voiceProgressBar = nullptr;
    QLabel* targetProgressHintLabel = nullptr;
    QLabel* salesTargetProgressHintLabel = nullptr;
    QLabel* simoProgressHintLabel = nullptr;
    QLabel* voiceProgressHintLabel = nullptr;

    PremiumOrdersList* ordersList = nullptr;
    QComboBox* orderStatusFilter = nullptr;
    QLineEdit* orderSearchEdit = nullptr;
    QLabel* ordersCountLabel = nullptr;
    PremiumReportView* reportView = nullptr;
    QComboBox* reportPresetCombo = nullptr;
    QDateEdit* reportMonthEdit = nullptr;

    QDoubleSpinBox* dayBonusSpin = nullptr;
    QSpinBox* simoMinSpin = nullptr;
    QDoubleSpinBox* simoUnitSpin = nullptr;
    QSpinBox* voiceMinSpin = nullptr;
    QDoubleSpinBox* voiceUnitSpin = nullptr;
    QDoubleSpinBox* targetSpin = nullptr;
    QSpinBox* monthlySalesTargetSpin = nullptr;
    QLabel* accountIdentityLabel = nullptr;
    bool intramanagerSyncRunning = false;
    QMap<QString,QList<std::function<void(bool)>>> intramanagerHoursWaiters;
    bool intramanagerPunchRunning = false;
    QTimer* intramanagerAutoSyncTimer = nullptr;
    QDateTime intramanagerLastAutoHoursRefreshUtc;
    QSet<QString> intramanagerPendingFetchKeys;
    QMap<QString, QDateTime> intramanagerReportRefreshRequestedAt;
    QMap<QString, QDateTime> intramanagerDashboardRefreshRequestedAt;

    QDoubleSpinBox* hourlyRateSpin = nullptr;
    QDoubleSpinBox* taxDeductionSpin = nullptr;
    QDoubleSpinBox* taxRateSpin = nullptr;
    QCheckBox* intramanagerChromeCheck = nullptr;
    QLineEdit* intramanagerProfileEdit = nullptr;
    QLineEdit* intramanagerUsernameEdit = nullptr;
    QLineEdit* intramanagerPasswordEdit = nullptr;
    QCheckBox* intramanagerEnabledCheck = nullptr;
    QLabel* intramanagerStatusLabel = nullptr;
    QLineEdit* defaultSellerInitialsEdit = nullptr;
    QLineEdit* masterWorkbookUrlEdit = nullptr;
    QCheckBox* salesRegistrationEnabledCheck = nullptr;
    QLabel* salesRegistrationStatusLabel = nullptr;
    QPointer<QPlainTextEdit> masterSetupLog;

    struct IntramanagerFetchResult {
        bool success = false;
        double hours = 0.0;
        double phoneHours = 0.0;
        double sickHours = 0.0;
        double sickPay = 0.0;
        double cancellations = 0.0;
        QString error;
    };

    struct IntramanagerPunchResult {
        bool success = false;
        bool statusKnown = false;
        bool clockedIn = false;
        QString statusText;
        QString detail;
        QString lastStart;
        QString lastStop;
        QString error;
    };

    struct IntramanagerOverviewResult {
        bool success = false;
        bool hoursSuccess = false;
        bool punchSuccess = false;
        double hours = 0.0;
        double phoneHours = 0.0;
        double sickHours = 0.0;
        double sickPay = 0.0;
        double cancellations = 0.0;
        IntramanagerPunchResult punch;
        QString hoursError;
        QString punchError;
        QString error;
    };

    // Intramanager dates are persisted in the same format the worker expects.
    QString intramanagerDate(const QDate& date) const {
        return date.toString("dd-MM-yyyy");
    }

    QString intramanagerPeriodKey(const QString& fromDate, const QString& toDate) const {
        return fromDate + "_" + toDate;
    }

    QString intramanagerPeriodLabel(const QString& fromDate, const QString& toDate) const {
        return fromDate + " til " + toDate;
    }

    bool hasCachedIntramanagerHours(const QString& fromDate, const QString& toDate) const {
        return repo.settings.intramanagerHoursCache.contains(intramanagerPeriodKey(fromDate, toDate));
    }

    std::optional<IntramanagerHoursEntry> cachedIntramanagerHours(const QString& fromDate, const QString& toDate) const {
        const QString key = intramanagerPeriodKey(fromDate, toDate);
        if (!repo.settings.intramanagerHoursCache.contains(key)) {
            return std::nullopt;
        }
        return repo.settings.intramanagerHoursCache.value(key);
    }

    bool intramanagerHoursEntryIsFresh(const IntramanagerHoursEntry& entry, int maxAgeMinutes) const {
        const QDateTime synced = QDateTime::fromString(entry.syncedAt, Qt::ISODate);
        if (entry.payrollSchema < 2 || !synced.isValid()) {
            return false;
        }

        return synced.toUTC().secsTo(QDateTime::currentDateTimeUtc()) < maxAgeMinutes * 60;
    }

    bool intramanagerHoursCacheIsFresh(const QString& fromDate, const QString& toDate, int maxAgeMinutes) const {
        const auto cached = cachedIntramanagerHours(fromDate, toDate);
        return cached.has_value() && intramanagerHoursEntryIsFresh(*cached, maxAgeMinutes);
    }

    QString intramanagerWorkerPath() const {
        return QCoreApplication::applicationDirPath()
            + "/intramanager_worker/intramanager_sync.exe";
    }

    QString intramanagerSessionStatePath() const {
        return appStorageDir() + "/intramanager_session.json";
    }

    bool prepareIntramanagerFetch(QString* usernameOut, QString* passwordOut, QString* workerPathOut, QString* errorOut) const {
        if (!repo.settings.intramanagerEnabled) {
            if (errorOut) *errorOut = "Intramanager er ikke aktiveret.";
            return false;
        }

        const QString username = repo.settings.intramanagerUsername.trimmed();
        if (username.isEmpty()) {
            if (errorOut) *errorOut = "Intramanager brugernavn mangler.";
            return false;
        }

        QString password;
        if (!loadIntramanagerPassword(nullptr, &password) || password.isEmpty()) {
            if (errorOut) *errorOut = "Der er ikke gemt en Intramanager adgangskode.";
            return false;
        }

        const QString workerPath = intramanagerWorkerPath();
        if (!QFileInfo::exists(workerPath)) {
            if (errorOut) *errorOut = "Worker-filen blev ikke fundet:\n" + workerPath;
            return false;
        }

        if (usernameOut) *usernameOut = username;
        if (passwordOut) *passwordOut = password;
        if (workerPathOut) *workerPathOut = workerPath;
        return true;
    }

    QJsonObject intramanagerPayload(const QString& username, const QString& password, const QString& fromDate, const QString& toDate) const {
        QJsonObject payload;
        payload["username"] = username;
        payload["password"] = password;
        payload["fromDate"] = fromDate;
        payload["toDate"] = toDate;
        payload["sessionState"] = intramanagerSessionStatePath();
        return payload;
    }

    QStringList intramanagerWorkerArgs(const QString& fromDate, const QString& toDate) const {
        return {"--action", "hours", "--from-date", fromDate, "--to-date", toDate, "--stdin-json"};
    }

    QStringList intramanagerOverviewWorkerArgs(const QString& fromDate, const QString& toDate) const {
        return {"--action", "overview", "--from-date", fromDate, "--to-date", toDate, "--stdin-json"};
    }

    QStringList intramanagerPunchWorkerArgs(const QString& action) const {
        return {"--action", action, "--stdin-json"};
    }



    IntramanagerFetchResult parseIntramanagerWorkerOutput(const QByteArray& stdoutData, const QByteArray& stderrData) const {
        IntramanagerFetchResult result;
        QJsonParseError jsonError;
        const auto doc = QJsonDocument::fromJson(stdoutData, &jsonError);

        if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
            result.error =
                "Worker returnerede ikke gyldig JSON.\n\nOutput:\n" +
                QString::fromUtf8(stdoutData) +
                "\n\nFejl:\n" +
                QString::fromUtf8(stderrData);
            return result;
        }

        const QJsonObject obj = doc.object();
        if (!obj.value("success").toBool(false)) {
            result.error = obj.value("error").toString("Ukendt fejl ved hentning af timer.");
            return result;
        }

        result.success = true;
        result.hours = obj.value("hours").toDouble(0.0);
        result.phoneHours = obj.value("phoneHours").toDouble(0.0);
        result.sickHours = obj.value("sickHours").toDouble(0.0);
        result.sickPay = obj.value("sickPay").toDouble(0.0);
        result.cancellations = obj.value("cancellations").toDouble(0.0);
        return result;
    }

    IntramanagerPunchResult parseIntramanagerPunchWorkerOutput(const QByteArray& stdoutData, const QByteArray& stderrData) const {
        IntramanagerPunchResult result;
        QJsonParseError jsonError;
        const auto doc = QJsonDocument::fromJson(stdoutData, &jsonError);

        if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
            result.error =
                "Worker returnerede ikke gyldig JSON.\n\nOutput:\n" +
                QString::fromUtf8(stdoutData) +
                "\n\nFejl:\n" +
                QString::fromUtf8(stderrData);
            return result;
        }

        const QJsonObject obj = doc.object();
        result.statusKnown = obj.value("statusKnown").toBool(false);
        result.clockedIn = obj.value("clockedIn").toBool(false);
        result.statusText = obj.value("statusText").toString();
        result.detail = obj.value("detail").toString();
        result.lastStart = obj.value("lastStart").toString();
        result.lastStop = obj.value("lastStop").toString();

        if (!obj.value("success").toBool(false)) {
            result.error = obj.value("error").toString("Ukendt fejl ved stempelstatus.");
            return result;
        }

        result.success = true;
        return result;
    }

    IntramanagerOverviewResult parseIntramanagerOverviewWorkerOutput(const QByteArray& stdoutData, const QByteArray& stderrData) const {
        IntramanagerOverviewResult result;
        QJsonParseError jsonError;
        const auto doc = QJsonDocument::fromJson(stdoutData, &jsonError);

        if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
            result.error =
                "Worker returnerede ikke gyldig JSON.\n\nOutput:\n" +
                QString::fromUtf8(stdoutData) +
                "\n\nFejl:\n" +
                QString::fromUtf8(stderrData);
            return result;
        }

        const QJsonObject obj = doc.object();
        result.success = obj.value("success").toBool(false);
        result.hoursSuccess = obj.value("hoursSuccess").toBool(false);
        result.punchSuccess = obj.value("punchSuccess").toBool(false);
        result.hours = obj.value("hours").toDouble(0.0);
        result.phoneHours = obj.value("phoneHours").toDouble(0.0);
        result.sickHours = obj.value("sickHours").toDouble(0.0);
        result.sickPay = obj.value("sickPay").toDouble(0.0);
        result.cancellations = obj.value("cancellations").toDouble(0.0);
        result.hoursError = obj.value("hoursError").toString();
        result.punchError = obj.value("punchError").toString();

        result.punch.success = result.punchSuccess;
        result.punch.statusKnown = obj.value("statusKnown").toBool(false);
        result.punch.clockedIn = obj.value("clockedIn").toBool(false);
        result.punch.statusText = obj.value("statusText").toString();
        result.punch.detail = obj.value("detail").toString();
        result.punch.lastStart = obj.value("lastStart").toString();
        result.punch.lastStop = obj.value("lastStop").toString();
        result.punch.error = result.punchError;

        if (!result.success) {
            QStringList errors;
            if (!result.hoursSuccess) errors << (result.hoursError.isEmpty() ? "Timer kunne ikke hentes." : result.hoursError);
            if (!result.punchSuccess) errors << (result.punchError.isEmpty() ? "Stempelstatus kunne ikke hentes." : result.punchError);
            result.error = errors.join("\n");
        }

        return result;
    }

    QString friendlyPunchError(const QString& rawError) const {
        const QString text = rawError.trimmed();
        const QString lower = text.toLower();
        if (lower.contains("kontorets") || lower.contains("kontor")
            || lower.contains("ip-adresse") || lower.contains("ip adresse")
            || lower.contains("forbidden") || lower.contains("permission")) {
            return "Man kan kun stemple ind eller ud på kontorets internet.";
        }
        return text.isEmpty() ? "Ukendt fejl ved stempelstatus." : text;
    }

    void rememberIntramanagerHours(
        const QString& fromDate,
        const QString& toDate,
        double hours,
        double phoneHours,
        double sickHours,
        double sickPay,
        double cancellations = 0.0
        ) {
        const QString syncedAt = QDateTime::currentDateTime().toString(Qt::ISODate);

        repo.settings.lastIntramanagerHours = hours;
        repo.settings.lastIntramanagerPeriodFrom = fromDate;
        repo.settings.lastIntramanagerPeriodTo = toDate;
        repo.settings.lastIntramanagerSyncAt = syncedAt;

        IntramanagerHoursEntry entry;
        entry.fromDate = fromDate;
        entry.toDate = toDate;
        entry.hours = hours;
        entry.phoneHours = phoneHours;
        entry.sickHours = sickHours;
        entry.sickPay = sickPay;
        entry.cancellations = cancellations;
        entry.payrollSchema = 2;
        entry.syncedAt = syncedAt;

        repo.settings.intramanagerHoursCache[intramanagerPeriodKey(fromDate, toDate)] = entry;
    }

    void rememberIntramanagerPunchState(const IntramanagerPunchResult& result) {
        repo.settings.intramanagerPunch.known = result.statusKnown;
        repo.settings.intramanagerPunch.clockedIn = result.clockedIn;
        repo.settings.intramanagerPunch.statusText = result.statusText;
        repo.settings.intramanagerPunch.detail = result.detail;
        repo.settings.intramanagerPunch.lastStart = result.lastStart;
        repo.settings.intramanagerPunch.lastStop = result.lastStop;
        repo.settings.intramanagerPunch.syncedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    }

    QString shortPunchSyncText() const {
        const QDateTime synced = QDateTime::fromString(repo.settings.intramanagerPunch.syncedAt, Qt::ISODate);
        if (!synced.isValid()) return "Ikke hentet endnu";
        return "Opdateret " + synced.toString("dd-MM HH:mm");
    }

    QString cleanPunchText(QString text) const {
        text.replace(QString::fromUtf8("Ã¦"), QString::fromUtf8("æ"));
        text.replace(QString::fromUtf8("Ã¸"), QString::fromUtf8("ø"));
        text.replace(QString::fromUtf8("Ã¥"), QString::fromUtf8("å"));
        text.replace(QString::fromUtf8("Ã†"), QString::fromUtf8("Æ"));
        text.replace(QString::fromUtf8("Ã˜"), QString::fromUtf8("Ø"));
        text.replace(QString::fromUtf8("Ã…"), QString::fromUtf8("Å"));
        return text;
    }

    QString punchDetailText() const {
        const auto& punch = repo.settings.intramanagerPunch;
        if (!repo.settings.intramanagerEnabled) {
            return "Gem Intramanager-login i Indstillinger for at bruge stempeluret.";
        }
        if (!punch.known) {
            return punch.detail.isEmpty() ? "Status er ikke hentet endnu." : cleanPunchText(punch.detail);
        }

        QStringList parts;
        if (punch.clockedIn && !punch.lastStart.isEmpty()) {
            parts << "Stemplet ind siden: " + cleanPunchText(punch.lastStart);
        } else if (!punch.detail.isEmpty()) {
            parts << cleanPunchText(punch.detail);
        }
        if (!punch.clockedIn && !punch.lastStart.isEmpty()) parts << "Seneste ind: " + cleanPunchText(punch.lastStart);
        if (!punch.lastStop.isEmpty()) parts << "Seneste ud: " + cleanPunchText(punch.lastStop);
        parts << shortPunchSyncText();
        return parts.join("\n");
    }

    void refreshPunchCardUi() {
        if(repo.settings.intramanagerViaChrome && intramanagerPunchStatusLabel && intramanagerPunchButton) {
            intramanagerPunchStatusLabel->setText("Intramanager via Chrome");
            intramanagerPunchButton->setText("Åbn stempelur");intramanagerPunchButton->setEnabled(repo.settings.intramanagerEnabled);
            if(intramanagerPunchDetailLabel)intramanagerPunchDetailLabel->setText("Løndata hentes med din browserkonto. Stempling foretages på Intramanagers egen side.");
            return;
        }

        const auto& punch = repo.settings.intramanagerPunch;

        if (intramanagerPunchStatusLabel) {
            QString status = "Status ukendt";
            if (intramanagerPunchRunning) {
                status = "Kontakter Intramanager...";
            } else if (!repo.settings.intramanagerEnabled) {
                status = "Intramanager er ikke aktiv";
            } else if (punch.known) {
                status = punch.clockedIn ? "Stemplet ind" : "Stemplet ud";
            }

            intramanagerPunchStatusLabel->setText(status);
            intramanagerPunchStatusLabel->setStyleSheet(
                QString("QLabel { color:%1; font-size:15px; font-weight:600; background:transparent; }")
                    .arg(punch.known && punch.clockedIn ? "#65D2A0" : "#F2F4F7")
                );
        }

        if (intramanagerPunchDetailLabel) {
            intramanagerPunchDetailLabel->setText(punchDetailText());
        }

        if (intramanagerPunchButton) {
            intramanagerPunchButton->setEnabled(repo.settings.intramanagerEnabled && !intramanagerPunchRunning);
            if (!repo.settings.intramanagerEnabled) {
                intramanagerPunchButton->setText("Kræver login");
            } else if (intramanagerPunchRunning) {
                intramanagerPunchButton->setText("Arbejder...");
            } else if (!punch.known) {
                intramanagerPunchButton->setText("Hent status");
            } else {
                intramanagerPunchButton->setText(punch.clockedIn ? "Stempel ud" : "Stempel ind");
            }
        }
    }

    void setupIntramanagerAutoSync() {
        if (intramanagerAutoSyncTimer) {
            intramanagerAutoSyncTimer->stop();
            intramanagerAutoSyncTimer->deleteLater();
            intramanagerAutoSyncTimer = nullptr;
        }

        if (!repo.settings.intramanagerEnabled) {
            return;
        }

        QTimer::singleShot(800, this, [this]() {
            refreshCurrentIntramanagerStateAsync();
        });

        intramanagerAutoSyncTimer = new QTimer(this);
        intramanagerAutoSyncTimer->setInterval(5 * 60 * 1000);

        connect(intramanagerAutoSyncTimer, &QTimer::timeout, this, [this]() {
            refreshCurrentIntramanagerStateAsync();
        });

        intramanagerAutoSyncTimer->start();
    }

    void runChromeIntramanager(const QString& operation, const QString& from, const QString& to, std::function<void(QJsonObject)> complete) {
        auto* process = new QProcess(this);
        const QJsonObject payload{{"action","chrome-intramanager"},{"operation",operation},{"fromDate",from},{"toDate",to},{"expectedProfile",repo.settings.intramanagerBrowserProfile}};
        auto finished=std::make_shared<bool>(false);
        const auto done=[finished,complete](QJsonObject result){if(!*finished){*finished=true;complete(result);}};
        connect(process,&QProcess::started,this,[process,payload](){process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));process->closeWriteChannel();});
        connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[process,done](int,QProcess::ExitStatus){
            const auto document=QJsonDocument::fromJson(process->readAllStandardOutput());
            done(document.isObject()?document.object():QJsonObject{{"success",false},{"error","Chrome returnerede ikke et gyldigt svar fra Intramanager."}});process->deleteLater();
        });
        connect(process,&QProcess::errorOccurred,this,[process,done](QProcess::ProcessError error){if(error==QProcess::FailedToStart){done({{"success",false},{"error","Chrome-forbindelsen kunne ikke starte. Geninstaller Provi Tracker."}});process->deleteLater();}});
        QTimer::singleShot(95000,process,[process](){if(process->state()!=QProcess::NotRunning)process->kill();});
        process->start(intramanagerWorkerPath(),{"--stdin-json"});
    }

    void fetchIntramanagerHoursAsync(const QString& fromDate,const QString& toDate,bool silent,std::function<void(bool)> afterFetch = {}) {
        const QString key=fromDate+"|"+toDate;
        if(intramanagerHoursWaiters.contains(key)){if(afterFetch)intramanagerHoursWaiters[key].append(afterFetch);return;}
        intramanagerHoursWaiters.insert(key,{});
        if(afterFetch)intramanagerHoursWaiters[key].append(afterFetch);
        performIntramanagerHoursFetch(fromDate,toDate,silent,[this,key](bool ok){
            const auto callbacks=intramanagerHoursWaiters.take(key);
            for(const auto& callback:callbacks)callback(ok);
        });
    }

    void performIntramanagerHoursFetch(
        const QString& fromDate,
        const QString& toDate,
        bool silent,
        std::function<void(bool)> afterFetch = {}
        ) {
        if (intramanagerSyncRunning) {
            QTimer::singleShot(1500, this, [this, fromDate, toDate, silent, afterFetch]() {
                performIntramanagerHoursFetch(fromDate, toDate, silent, afterFetch);
            });
            return;
        }

        if(repo.settings.intramanagerViaChrome) {
            if(!repo.settings.intramanagerEnabled){if(afterFetch)afterFetch(false);return;}
            intramanagerSyncRunning=true;
            runChromeIntramanager("hours",fromDate,toDate,[this,fromDate,toDate,silent,afterFetch](QJsonObject answer){
                intramanagerSyncRunning=false;
                const auto result=parseIntramanagerWorkerOutput(QJsonDocument(answer).toJson(),{});
                if(result.success){rememberIntramanagerHours(fromDate,toDate,result.hours,result.phoneHours,result.sickHours,result.sickPay,result.cancellations);repo.saveSettings();refreshAll();}
                if(intramanagerStatusLabel)intramanagerStatusLabel->setText(result.success?"Løndata er opdateret via din Chrome-konto.":result.error);
                if(!result.success && !silent)QMessageBox::information(this,"Intramanager via Chrome",result.error);
                if(afterFetch)afterFetch(result.success);
            });
            return;
        }

        QString username;
        QString password;
        QString workerPath;
        QString error;

        if (!prepareIntramanagerFetch(&username, &password, &workerPath, &error)) {
            if (!silent) QMessageBox::warning(this, "Intramanager", error);
            if (afterFetch) afterFetch(false);
            return;
        }

        intramanagerSyncRunning = true;

        QProgressDialog* progress = nullptr;

        if (!silent) {
            progress = new QProgressDialog("Henter timer fra Intramanager...", QString(), 0, 0, this);
            progress->setWindowTitle("Intramanager");
            progress->setCancelButton(nullptr);
            progress->setWindowModality(Qt::ApplicationModal);
            progress->setMinimumDuration(0);
            progress->show();
        }

        auto* process = new QProcess(this);
        const QJsonObject payload = intramanagerPayload(username, password, fromDate, toDate);

        connect(process, &QProcess::started, this, [process, payload]() {
            process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
            process->closeWriteChannel();
        });

        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process, progress, silent, fromDate, toDate, afterFetch](int, QProcess::ExitStatus) {
                    if (progress) {
                        progress->close();
                        progress->deleteLater();
                    }

                    intramanagerSyncRunning = false;

                    const QByteArray stdoutData = process->readAllStandardOutput();
                    const QByteArray stderrData = process->readAllStandardError();
                    process->deleteLater();

                    const IntramanagerFetchResult result = parseIntramanagerWorkerOutput(stdoutData, stderrData);

                    if (!result.success) {
                        if (!silent) QMessageBox::warning(this, "Intramanager", result.error);
                        if (afterFetch) afterFetch(false);
                        return;
                    }

                    rememberIntramanagerHours(fromDate, toDate, result.hours, result.phoneHours, result.sickHours, result.sickPay, result.cancellations);
                    repo.saveSettings();
                    refreshAll();

                    if (!silent) {
                        QMessageBox::information(
                            this,
                            "Timer hentet",
                            QString("Der blev hentet %1 timer for perioden %2 til %3.")
                                .arg(result.hours, 0, 'f', 2)
                                .arg(fromDate)
                                .arg(toDate)
                            );
                    }

                    if (afterFetch) afterFetch(true);
                });

        connect(process, &QProcess::errorOccurred, this, [this, process, progress, silent, afterFetch](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;

            if (progress) {
                progress->close();
                progress->deleteLater();
            }

            intramanagerSyncRunning = false;

            const QString err = process->errorString();
            process->deleteLater();

            if (!silent) {
                QMessageBox::warning(this, "Intramanager", "Kunne ikke starte Intramanager-worker:\n" + err);
            }
            if (afterFetch) afterFetch(false);
        });

        process->start(workerPath, intramanagerWorkerArgs(fromDate, toDate));
    }

    void refreshIntramanagerOverviewAsync(bool silent = true, std::function<void(bool)> afterFetch = {}) {
        if(repo.settings.intramanagerViaChrome) {
            const auto range=payrollBonusRange(QDate::currentDate());
            fetchIntramanagerHoursAsync(intramanagerDate(range.first.date()),intramanagerDate(range.second.date()),silent,afterFetch);
            refreshPunchCardUi();return;
        }

        if (intramanagerSyncRunning || intramanagerPunchRunning) {
            QTimer::singleShot(1500, this, [this, silent, afterFetch]() {
                refreshIntramanagerOverviewAsync(silent, afterFetch);
            });
            return;
        }

        const auto period = payrollBonusRange(QDate::currentDate());
        const QString fromDate = intramanagerDate(period.first.date());
        const QString toDate = intramanagerDate(period.second.date());

        QString username;
        QString password;
        QString workerPath;
        QString error;

        if (!prepareIntramanagerFetch(&username, &password, &workerPath, &error)) {
            if (!silent) QMessageBox::warning(this, "Intramanager", error);
            if (afterFetch) afterFetch(false);
            refreshPunchCardUi();
            return;
        }

        intramanagerSyncRunning = true;
        intramanagerPunchRunning = true;
        refreshPunchCardUi();

        auto* process = new QProcess(this);
        const QJsonObject payload = intramanagerPayload(username, password, fromDate, toDate);

        connect(process, &QProcess::started, this, [process, payload]() {
            process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
            process->closeWriteChannel();
        });

        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process, silent, fromDate, toDate, afterFetch](int, QProcess::ExitStatus) {
                    intramanagerSyncRunning = false;
                    intramanagerPunchRunning = false;

                    const QByteArray stdoutData = process->readAllStandardOutput();
                    const QByteArray stderrData = process->readAllStandardError();
                    process->deleteLater();

                    const IntramanagerOverviewResult result = parseIntramanagerOverviewWorkerOutput(stdoutData, stderrData);

                    if (result.hoursSuccess) {
                        rememberIntramanagerHours(fromDate, toDate, result.hours, result.phoneHours, result.sickHours, result.sickPay, result.cancellations);
                    }

                    if (result.punchSuccess) {
                        rememberIntramanagerPunchState(result.punch);
                    } else if (!result.punchError.isEmpty()) {
                        repo.settings.intramanagerPunch.known = false;
                        repo.settings.intramanagerPunch.detail = friendlyPunchError(result.punchError);
                        repo.settings.intramanagerPunch.syncedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
                    }

                    repo.saveSettings();
                    refreshAll();

                    if (!result.success && !silent) {
                        QMessageBox::warning(this, "Intramanager", result.error);
                    }

                    if (afterFetch) afterFetch(result.hoursSuccess || result.punchSuccess);
                });

        connect(process, &QProcess::errorOccurred, this, [this, process, silent, afterFetch](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;

            intramanagerSyncRunning = false;
            intramanagerPunchRunning = false;
            const QString err = process->errorString();
            process->deleteLater();

            repo.settings.intramanagerPunch.known = false;
            repo.settings.intramanagerPunch.detail = "Kunne ikke starte Intramanager-worker:\n" + err;
            repo.settings.intramanagerPunch.syncedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
            repo.saveSettings();
            refreshPunchCardUi();

            if (!silent) {
                QMessageBox::warning(this, "Intramanager", repo.settings.intramanagerPunch.detail);
            }
            if (afterFetch) afterFetch(false);
        });

        process->start(workerPath, intramanagerOverviewWorkerArgs(fromDate, toDate));
    }

    void runIntramanagerPunchWorker(
        const QString& action,
        bool silent,
        std::function<void(bool)> afterFetch = {},
        const QString& targetAction = {}
        ) {
        if (intramanagerPunchRunning) {
            if (afterFetch) afterFetch(false);
            return;
        }

        QString username;
        QString password;
        QString workerPath;
        QString error;

        if (!prepareIntramanagerFetch(&username, &password, &workerPath, &error)) {
            if (!silent) QMessageBox::warning(this, "Intramanager", error);
            if (afterFetch) afterFetch(false);
            refreshPunchCardUi();
            return;
        }

        intramanagerPunchRunning = true;
        refreshPunchCardUi();

        auto* process = new QProcess(this);
        QJsonObject payload;
        payload["username"] = username;
        payload["password"] = password;
        payload["action"] = action;
        payload["onDate"] = intramanagerDate(QDate::currentDate());
        payload["sessionState"] = intramanagerSessionStatePath();
        if (!targetAction.isEmpty()) {
            payload["targetAction"] = targetAction;
        }

        connect(process, &QProcess::started, this, [process, payload]() {
            process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
            process->closeWriteChannel();
        });

        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process, silent, action, afterFetch](int, QProcess::ExitStatus) {
                    intramanagerPunchRunning = false;

                    const QByteArray stdoutData = process->readAllStandardOutput();
                    const QByteArray stderrData = process->readAllStandardError();
                    process->deleteLater();

                    const IntramanagerPunchResult result = parseIntramanagerPunchWorkerOutput(stdoutData, stderrData);

                    if (!result.success) {
                        const QString friendlyError = friendlyPunchError(result.error);
                        if (result.statusKnown) {
                            IntramanagerPunchResult remembered = result;
                            remembered.detail = friendlyError;
                            rememberIntramanagerPunchState(remembered);
                        } else {
                            repo.settings.intramanagerPunch.known = false;
                            repo.settings.intramanagerPunch.detail = friendlyError;
                            repo.settings.intramanagerPunch.syncedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
                        }
                        repo.saveSettings();
                        refreshPunchCardUi();
                        if (!silent) QMessageBox::warning(this, "Intramanager", friendlyError);
                        if (afterFetch) afterFetch(false);
                        return;
                    }

                    rememberIntramanagerPunchState(result);
                    repo.saveSettings();
                    refreshPunchCardUi();

                    if (!silent && action == "punch-toggle") {
                        QMessageBox::information(
                            this,
                            "Intramanager",
                            result.clockedIn ? "Du er stemplet ind." : "Du er stemplet ud."
                            );
                    }

                    if (action == "punch-toggle") {
                        syncIntramanagerHoursAsync(true);
                    }

                    if (afterFetch) afterFetch(true);
                });

        connect(process, &QProcess::errorOccurred, this, [this, process, silent, afterFetch](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;

            intramanagerPunchRunning = false;
            const QString err = process->errorString();
            process->deleteLater();

            repo.settings.intramanagerPunch.known = false;
            repo.settings.intramanagerPunch.detail = "Kunne ikke starte Intramanager-worker:\n" + err;
            repo.settings.intramanagerPunch.syncedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
            repo.saveSettings();
            refreshPunchCardUi();

            if (!silent) {
                QMessageBox::warning(this, "Intramanager", repo.settings.intramanagerPunch.detail);
            }
            if (afterFetch) afterFetch(false);
        });

        process->start(workerPath, intramanagerPunchWorkerArgs(action));
    }

    void refreshIntramanagerPunchStatusAsync(bool silent = true) {
        if (repo.settings.intramanagerViaChrome) { refreshPunchCardUi(); return; }
        runIntramanagerPunchWorker("punch-status", silent);
    }

    void toggleIntramanagerPunchAsync() {
        if(repo.settings.intramanagerViaChrome){QDesktopServices::openUrl(QUrl("https://5r.intramanager.com/reports/punch-in/"));return;}

        const auto& punch = repo.settings.intramanagerPunch;
        if (!punch.known) {
            refreshIntramanagerPunchStatusAsync(false);
            return;
        }

        const QString targetAction = punch.clockedIn ? "out" : "in";
        runIntramanagerPunchWorker("punch-toggle", false, {}, targetAction);
    }

    void syncIntramanagerHoursAsync(bool silent = false, std::function<void(bool)> afterFetch = {}) {
        const auto period = payrollBonusRange(QDate::currentDate());
        const QString fromDate = intramanagerDate(period.first.date());
        const QString toDate = intramanagerDate(period.second.date());
        fetchIntramanagerHoursAsync(fromDate, toDate, silent, afterFetch ? afterFetch : [](bool) {});
    }

    void refreshCurrentPayrollHoursIfStaleAsync(int maxAgeMinutes = 60) {
        if (!repo.settings.intramanagerEnabled || intramanagerSyncRunning) {
            return;
        }

        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        if (intramanagerLastAutoHoursRefreshUtc.isValid()
            && intramanagerLastAutoHoursRefreshUtc.secsTo(nowUtc) < 15 * 60) {
            return;
        }

        const auto period = payrollBonusRange(QDate::currentDate());
        const QString fromDate = intramanagerDate(period.first.date());
        const QString toDate = intramanagerDate(period.second.date());
        if (intramanagerHoursCacheIsFresh(fromDate, toDate, maxAgeMinutes)) {
            return;
        }

        intramanagerLastAutoHoursRefreshUtc = nowUtc;
        refreshIntramanagerOverviewAsync(true);
    }

    void refreshCurrentIntramanagerStateAsync() {
        if (!repo.settings.intramanagerEnabled) {
            return;
        }

        const auto period = payrollBonusRange(QDate::currentDate());
        const QString fromDate = intramanagerDate(period.first.date());
        const QString toDate = intramanagerDate(period.second.date());

        if (!intramanagerHoursCacheIsFresh(fromDate, toDate, 60)) {
            refreshCurrentPayrollHoursIfStaleAsync();
            return;
        }

        refreshIntramanagerPunchStatusAsync(true);
    }

    void syncIntramanagerHours() {
        // Legacy entry point kept as a wrapper around the shared async fetch path.
        syncIntramanagerHoursAsync(false);
        return;

        if (!repo.settings.intramanagerEnabled) {
            QMessageBox::warning(this, "Intramanager", "Intramanager timehentning er ikke aktiveret.");
            return;
        }

        const QString username = repo.settings.intramanagerUsername.trimmed();

        QString storedUser;
        QString password;

        if (!loadIntramanagerPassword(&storedUser, &password) || password.isEmpty()) {
            QMessageBox::warning(this, "Intramanager", "Der er ikke gemt en Intramanager adgangskode.");
            return;
        }

        if (username.isEmpty()) {
            QMessageBox::warning(this, "Intramanager", "Intramanager brugernavn mangler.");
            return;
        }

        const auto period = payrollBonusRange(QDate::currentDate());
        const QString fromDate = intramanagerDate(period.first.date());
        const QString toDate = intramanagerDate(period.second.date());

        const QString workerPath =
            QCoreApplication::applicationDirPath()
            + "/intramanager_worker/intramanager_sync.exe";

        if (!QFileInfo::exists(workerPath)) {
            QMessageBox::warning(
                this,
                "Intramanager",
                "Worker-filen blev ikke fundet:\n" + workerPath +
                    "\n\nKopiér mappen intramanager_worker ind ved siden af .exe-filen."
                );
            return;
        }

        QProgressDialog progress("Henter timer fra Intramanager...", QString(), 0, 0, this);
        progress.setWindowTitle("Intramanager");
        progress.setCancelButton(nullptr);
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setMinimumDuration(0);
        progress.show();
        QApplication::processEvents();

        QProcess process;

        QStringList args;
        args << "--from-date" << fromDate
             << "--to-date" << toDate
             << "--stdin-json";

        QJsonObject payload;
        payload["username"] = username;
        payload["password"] = password;
        payload["fromDate"] = fromDate;
        payload["toDate"] = toDate;

        process.start(workerPath, args);

        if (!process.waitForStarted(10000)) {
            QMessageBox::warning(this, "Intramanager", "Kunne ikke starte Python/Intramanager-worker.");
            return;
        }

        process.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        process.closeWriteChannel();

        const QByteArray stdoutData = process.readAllStandardOutput();
        const QByteArray stderrData = process.readAllStandardError();

        const auto doc = QJsonDocument::fromJson(stdoutData);

        if (!doc.isObject()) {
            QMessageBox::warning(
                this,
                "Intramanager",
                "Worker returnerede ikke gyldig JSON.\n\nOutput:\n" +
                    QString::fromUtf8(stdoutData) +
                    "\n\nFejl:\n" +
                    QString::fromUtf8(stderrData)
                );
            return;
        }

        const QJsonObject obj = doc.object();

        if (!obj.value("success").toBool(false)) {
            QMessageBox::warning(
                this,
                "Intramanager",
                obj.value("error").toString("Ukendt fejl ved hentning af timer.")
                );
            return;
        }

        const double hours = obj.value("hours").toDouble(0.0);

        const double phoneHours = obj.value("phoneHours").toDouble(0.0);
        const QString syncedAt = QDateTime::currentDateTime().toString(Qt::ISODate);

        repo.settings.lastIntramanagerHours = hours;
        repo.settings.lastIntramanagerPeriodFrom = fromDate;
        repo.settings.lastIntramanagerPeriodTo = toDate;
        repo.settings.lastIntramanagerSyncAt = syncedAt;

        IntramanagerHoursEntry entry;
        entry.fromDate = fromDate;
        entry.toDate = toDate;
        entry.hours = hours;
        entry.phoneHours = phoneHours;
        entry.syncedAt = syncedAt;

        repo.settings.intramanagerHoursCache[intramanagerPeriodKey(fromDate, toDate)] = entry;

        repo.saveSettings();

        refreshAll();

        QMessageBox::information(
            this,
            "Timer hentet",
            QString("Der blev hentet %1 timer for perioden %2 til %3.")
                .arg(hours, 0, 'f', 2)
                .arg(fromDate)
                .arg(toDate)
            );
    }

    QJsonObject localPayloadForCloudUser(const QString& username) const {
        Repository snapshot = repo;
        snapshot.localPersistenceEnabled = false;
        snapshot.cloudPersistenceEnabled = false;
        snapshot.cloudSaveRequested = nullptr;
        if (snapshot.products.isEmpty()) {
            snapshot.seedProducts();
        }
        snapshot.normalizeForCloudUser(username);
        return snapshot.cloudPayload();
    }

    bool applyCloudResult(const ProviCloudClient::Result& result, bool migrateLocalIfCloudIsEmpty) {
        if (cloudUsername.trimmed().isEmpty() || cloudToken.isEmpty()) {
            QMessageBox::warning(this, "Login", "Cloud-login mangler brugernavn eller session.");
            return false;
        }

        if (migrateLocalIfCloudIsEmpty && !result.hasData) {
            repo.normalizeForCloudUser(cloudUsername);
            if (repo.products.isEmpty()) {
                repo.seedProducts();
            }

            const auto saveResult = cloudClient.save(cloudUsername, cloudToken, localPayloadForCloudUser(cloudUsername));
            if (!saveResult.ok) {
                QMessageBox::warning(
                    this,
                    "Migration fejlede",
                    saveResult.error.isEmpty() ? "Den lokale opsætning kunne ikke uploades til cloud." : saveResult.error
                    );
                return false;
            }

            cloudStatusText = "Lokal opsætning blev migreret til cloud.";
            return true;
        }

        if (result.data.isEmpty()) {
            repo.normalizeForCloudUser(cloudUsername);
            if (repo.products.isEmpty()) {
                repo.seedProducts();
            }
        } else {
            repo.applyCloudPayload(result.data, cloudUsername);
        }

        cloudStatusText = "Cloud-data er indlæst.";
        return true;
    }

    bool hasCloudSecret(const QString& key) const {
        return repo.cloudSecrets.value(key).isObject()
            && repo.cloudSecrets.value(key).toObject().value("format").toString() == "aes-256-gcm-pbkdf2-v1";
    }

    bool storeEncryptedCloudSecret(const QString& key, const QJsonObject& plainSecret) {
        if (cloudSecretKey.size() != 32 || plainSecret.isEmpty()) {
            return false;
        }

        QJsonObject secret = plainSecret;
        secret["savedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        const QJsonObject encrypted = encryptCloudSecretJson(secret, cloudSecretKey);
        if (encrypted.isEmpty()) {
            return false;
        }

        repo.cloudSecrets[key] = encrypted;
        return true;
    }

    bool storeIntramanagerSecret(const QString& username, const QString& password) {
        if (username.trimmed().isEmpty() || password.isEmpty()) {
            return false;
        }

        QJsonObject secret;
        secret["username"] = username.trimmed();
        secret["password"] = password;
        return storeEncryptedCloudSecret("intramanagerLogin", secret);
    }

    void applyCloudSecretsToLocal() {
        if (cloudSecretKey.size() != 32) {
            return;
        }

        const QJsonObject intramanagerSecret = decryptCloudSecretJson(
            repo.cloudSecrets.value("intramanagerLogin").toObject(),
            cloudSecretKey
            );
        const QString imUsername = intramanagerSecret.value("username").toString();
        const QString imPassword = intramanagerSecret.value("password").toString();
        if (!imUsername.trimmed().isEmpty() && !imPassword.isEmpty()) {
            saveIntramanagerPassword(imUsername, imPassword);
        }

    }

    void migrateLocalSecretsToCloudIfNeeded() {
        if (cloudUsername.trimmed().isEmpty() || cloudToken.isEmpty() || cloudSecretKey.size() != 32) {
            return;
        }

        bool changed = false;
        if (!hasCloudSecret("intramanagerLogin")) {
            QString username;
            QString password;
            if (loadIntramanagerPassword(&username, &password) && !password.isEmpty()) {
                if (username.trimmed().isEmpty()) {
                    username = repo.settings.intramanagerUsername;
                }
                changed = storeIntramanagerSecret(username, password) || changed;
            }
        }

        if (changed) {
            const auto saveResult = cloudClient.save(cloudUsername, cloudToken, localPayloadForCloudUser(cloudUsername));
            cloudStatusText = saveResult.ok
                ? "Lokale adgangsoplysninger blev krypteret og migreret til cloud."
                : (saveResult.error.isEmpty() ? "Cloud-gemning af adgangsoplysninger fejlede." : saveResult.error);
        }
    }

    bool authenticateCloudUser() {
        QString storedUsername;
        QString storedToken;
        QByteArray storedSecretKey;
        if (loadCloudSession(&storedUsername, &storedToken, nullptr, &storedSecretKey)
            && storedSecretKey.size() == 32) {
            const auto loadResult = cloudClient.load(storedUsername, storedToken);
            if (loadResult.ok) {
                cloudUsername = storedUsername;
                cloudToken = storedToken;
                cloudSecretKey = storedSecretKey;
                if (applyCloudResult(loadResult, true)) {
                    applyCloudSecretsToLocal();
                    migrateLocalSecretsToCloudIfNeeded();
                    cloudStatusText = "Logget ind som " + cloudUsername + ".";
                    return true;
                }
            }
            deleteCloudSession();
        }

        QString suggestedUsername = repo.settings.intramanagerUsername.trimmed();
        if (suggestedUsername.isEmpty() && !repo.salespeople.isEmpty()) {
            suggestedUsername = repo.salespeople.first().name;
        }

        while (true) {
            CloudLoginDialog dialog(suggestedUsername, this);
            if (dialog.exec() != QDialog::Accepted) {
                return false;
            }

            const QString username = dialog.username();
            const QString password = dialog.password();
            suggestedUsername = username;

            QByteArray candidateSecretKey;
            if (!deriveCloudSecretKey(username, password, &candidateSecretKey)) {
                QMessageBox::warning(this, "Login", "Kunne ikke danne krypteringsnøgle til cloud-adgangsoplysninger.");
                continue;
            }

            ProviCloudClient::Result result;
            const bool isRegister = dialog.action() == CloudLoginDialog::Action::Register;
            if (isRegister) {
                result = cloudClient.registerUser(username, password, localPayloadForCloudUser(username));
            } else {
                result = cloudClient.login(username, password);
            }

            if (!result.ok) {
                QMessageBox::warning(
                    this,
                    isRegister ? "Opret bruger" : "Login",
                    result.error.isEmpty() ? "Kunne ikke logge ind." : result.error
                    );
                continue;
            }

            cloudUsername = result.username.isEmpty() ? username : result.username;
            cloudToken = result.token;
            cloudSecretKey = candidateSecretKey;
            if (!saveCloudSession(cloudUsername, cloudToken, result.expiresAt, cloudSecretKey)) {
                QMessageBox::warning(
                    this,
                    "Login",
                    "Du er logget ind, men sessionen kunne ikke gemmes krypteret på computeren."
                    );
            }

            if (!applyCloudResult(result, !isRegister)) {
                continue;
            }

            applyCloudSecretsToLocal();
            migrateLocalSecretsToCloudIfNeeded();

            cloudStatusText = isRegister
                ? "Bruger oprettet og lokal opsætning migreret til cloud."
                : "Logget ind som " + cloudUsername + ".";
            return true;
        }
    }

    void ensureLocalFallbackUser() {
        if (repo.salespeople.isEmpty() || repo.settings.activeSalespersonId.isEmpty() || !repo.findSalesperson(repo.settings.activeSalespersonId)) {
            if (repo.salespeople.isEmpty()) {
                Salesperson first{QUuid::createUuid().toString(QUuid::WithoutBraces), "Standard"};
                repo.salespeople.push_back(first);
            }
            repo.settings.activeSalespersonId = repo.salespeople.first().id;
            repo.saveAll();
        }
    }

    void scheduleCloudSave(int delayMs = 100) {
        if (cloudUsername.trimmed().isEmpty() || cloudToken.isEmpty()) {
            return;
        }

        cloudSaveDirty = true;

        if (cloudSaveInFlight) {
            cloudSaveAgain = true;
            return;
        }

        if (cloudSaveTimer) {
            cloudSaveTimer->start(qMax(0, delayMs));
        }
    }

    void flushCloudSave() {
        if (cloudUsername.trimmed().isEmpty() || cloudToken.isEmpty()) {
            return;
        }
        if (!cloudSaveDirty && !cloudSaveAgain) {
            return;
        }

        if (cloudSaveInFlight) {
            cloudSaveAgain = true;
            return;
        }

        cloudSaveInFlight = true;
        cloudSaveAgain = false;
        cloudSaveDirty = false;
        const QJsonObject payload = localPayloadForCloudUser(cloudUsername);

        cloudClient.saveAsync(cloudUsername, cloudToken, payload, [this](ProviCloudClient::Result result) {
            cloudSaveInFlight = false;
            if (result.ok) {
                cloudStatusText.clear();
            } else {
                cloudSaveDirty = true;
                cloudStatusText = result.error.isEmpty() ? "Cloud-gemning fejlede." : result.error;
            }
            updateCloudAccountStatus();

            if (cloudSaveAgain) {
                scheduleCloudSave();
            } else if (cloudSaveDirty) {
                scheduleCloudSave(10000);
            }
        });
    }

    bool waitForCloudSaveInFlight(int timeoutMs = 12000) {
        QElapsedTimer timer;
        timer.start();
        while (cloudSaveInFlight && timer.elapsed() < timeoutMs) {
            qApp->processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(25);
        }
        return !cloudSaveInFlight;
    }

    bool flushCloudSaveSync(const QString& warningMessage = QString()) {
        if (cloudUsername.trimmed().isEmpty() || cloudToken.isEmpty()) {
            return true;
        }

        if (cloudSaveTimer && cloudSaveTimer->isActive()) {
            cloudSaveTimer->stop();
        }

        if (!waitForCloudSaveInFlight()) {
            cloudSaveDirty = true;
            cloudSaveAgain = true;
            scheduleCloudSave(1000);
            if (!warningMessage.isEmpty()) {
                QMessageBox::warning(this, "Cloud-gemning", warningMessage);
            }
            return false;
        }

        cloudSaveDirty = false;
        cloudSaveAgain = false;
        const auto result = cloudClient.save(cloudUsername, cloudToken, localPayloadForCloudUser(cloudUsername));
        if (result.ok) {
            cloudStatusText.clear();
            updateCloudAccountStatus();
            return true;
        }

        cloudSaveDirty = true;
        cloudStatusText = result.error.isEmpty() ? "Cloud-gemning fejlede." : result.error;
        updateCloudAccountStatus();
        scheduleCloudSave(10000);
        if (!warningMessage.isEmpty()) {
            QMessageBox::warning(
                this,
                "Cloud-gemning",
                warningMessage + "\n\n" + cloudStatusText
                );
        }
        return false;
    }

    void saveOrdersAndSyncCloud() {
        repo.saveOrders();
        if (repo.cloudPersistenceEnabled) {
            flushCloudSaveSync("Ordren er gemt i programmet, men kunne ikke gemmes i skyen lige nu. Programmet prøver igen automatisk.");
        }
    }

    void updateCloudAccountStatus() {
        if(!cloudAccountStatusLabel)return;
        const bool showError=cloudSaveDirty && !cloudSaveInFlight && !cloudStatusText.isEmpty();
        cloudAccountStatusLabel->setText(showError?cloudStatusText:QString());cloudAccountStatusLabel->setVisible(showError);
    }

    void logoutCloudUser() {
        if (masterRegistrationRunning) {
            QMessageBox::information(this, "Salgsregistrering", "Vent, til registreringen i Chrome er afsluttet.");
            return;
        }
        if (!confirmQuestion(this, "Log ud", "Vil du logge ud af Provi Tracker cloud på denne computer?")) {
            return;
        }

        if (!cloudUsername.trimmed().isEmpty() && !cloudToken.isEmpty()) {
            cloudClient.logoutAsync(cloudUsername, cloudToken);
        }
        deleteCloudSession();
        QMessageBox::information(this, "Logget ud", "Du er logget ud. Aabn programmet igen for at logge ind.");
        qApp->quit();
    }

    const Salesperson* activeSalesperson() const {
        return repo.findSalesperson(repo.settings.activeSalespersonId);
    }

    void setupUi() {
        setMinimumSize(1000,640);QSize size(1280,860);if(auto* screen=QGuiApplication::primaryScreen())size=size.boundedTo(screen->availableGeometry().size()-QSize(36,48));resize(size);setWindowTitle("Provi Tracker");
        auto* central=new PremiumWorkspace;central->setObjectName("workspace");auto* root=new QHBoxLayout(central);root->setContentsMargins(0,0,0,0);root->setSpacing(0);
        auto* sidebar=new QFrame;sidebar->setObjectName("sidebar");sidebar->setFixedWidth(218);auto* side=new QVBoxLayout(sidebar);side->setContentsMargins(18,30,18,20);side->setSpacing(12);
        auto* brandRow=new QHBoxLayout;auto* mark=new PremiumAvatar;
        auto* brand=new QLabel("provi");brand->setStyleSheet("font-size:28px;font-weight:600;color:#FAFBFE;");brandRow->addWidget(mark);brandRow->addSpacing(5);brandRow->addWidget(brand);brandRow->addStretch();side->addLayout(brandRow);
        auto* caption=premiumLabel("Victor er farmand","caption");caption->setContentsMargins(3,12,0,12);side->addWidget(caption);
        auto* navigation=new QListWidget;navigation->setObjectName("navigation");navigation->setIconSize(QSize(19,19));navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);navigation->setFocusPolicy(Qt::NoFocus);
        const QVector<QPair<QString,QString>> links={{"Overblik","grid"},{"Ordrer","orders"},{"Rapporter","reports"},{"Indstillinger","settings"}};
        for(const auto& link:links){new QListWidgetItem(premiumIcon(link.second),link.first,navigation);}
        side->addWidget(navigation,1);side->addWidget(premiumDivider());
        activeSalespersonLabel=premiumLabel("","cardTitle");activeSalespersonLabel->setWordWrap(true);side->addSpacing(8);side->addWidget(activeSalespersonLabel);
        side->addWidget(premiumLabel(QString("Provi Tracker · %1").arg(APP_VERSION),"caption"));root->addWidget(sidebar);
        auto* content=new QVBoxLayout;content->setContentsMargins(30,27,30,22);content->setSpacing(24);
        auto* top=new QHBoxLayout;auto* titleBlock=new QVBoxLayout;titleBlock->setSpacing(6);auto* title=premiumLabel("Overblik","pageTitle");auto* subtitle=premiumLabel("","muted");subtitle->hide();titleBlock->addWidget(title);titleBlock->addWidget(subtitle);top->addLayout(titleBlock);top->addStretch();
        auto* date=premiumLabel(QLocale(QLocale::Danish).toString(QDate::currentDate(),"d. MMMM yyyy"),"muted");top->addWidget(date);top->addSpacing(15);
        auto* newOrderBtn=new QPushButton("Ny ordre");newOrderBtn->setIcon(premiumIcon("plus",Qt::white));newOrderBtn->setProperty("primary",true);top->addWidget(newOrderBtn);content->addLayout(top);
        auto* tabs=new QTabWidget;tabs->setObjectName("mainPages");tabs->setFocusPolicy(Qt::NoFocus);tabs->tabBar()->hide();
        tabs->addTab(buildDashboardTab(),"Overblik");tabs->addTab(buildOrdersTab(),"Ordrer");tabs->addTab(buildReportsTab(),"Rapporter");tabs->addTab(buildSettingsTab(),"Indstillinger");
        content->addWidget(tabs,1);root->addLayout(content,1);setCentralWidget(central);
        connect(navigation,&QListWidget::currentRowChanged,tabs,&QTabWidget::setCurrentIndex);
        connect(tabs,&QTabWidget::currentChanged,navigation,[navigation](int row){navigation->setCurrentRow(row);});
        connect(navigation,&QListWidget::currentRowChanged,this,[title,subtitle,navigation](int row){if(row<0)return;const auto text=navigation->item(row)->text();title->setText(text);
            const QMap<QString,QString> descriptions;subtitle->setText(descriptions.value(text));subtitle->setVisible(!subtitle->text().isEmpty());});navigation->setCurrentRow(0);
        connect(newOrderBtn,&QPushButton::clicked,this,[this](){createOrder();});qApp->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress) {
            clearOrderSelectionOnBackgroundClick(watched);
        }
        return QMainWindow::eventFilter(watched, event);
    }

    bool isChildOf(QWidget* child, QWidget* parent) const {
        for (auto* w = child; w; w = w->parentWidget()) {
            if (w == parent) return true;
        }
        return false;
    }

    bool isInteractiveClickTarget(QWidget* widget) const {
        for (auto* w = widget; w; w = w->parentWidget()) {
            if (qobject_cast<QAbstractButton*>(w)
                || qobject_cast<QAbstractSpinBox*>(w)
                || qobject_cast<QComboBox*>(w)
                || qobject_cast<QLineEdit*>(w)
                || qobject_cast<QTextEdit*>(w)
                || qobject_cast<QListWidget*>(w)
                || qobject_cast<QTabBar*>(w)
                || qobject_cast<QScrollBar*>(w)
                || qobject_cast<QHeaderView*>(w)) {
                return true;
            }
        }
        return false;
    }

    void clearOrderSelectionOnBackgroundClick(QObject* watched) {
        if (!ordersList || !ordersList->selectionModel() || !ordersList->selectionModel()->hasSelection())
            return;

        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget || isChildOf(widget, ordersList) || isInteractiveClickTarget(widget))
            return;

        ordersList->clearSelection();
        ordersList->setCurrentRow(-1);
    }

    QWidget* buildDashboardTab() {
        auto* page=new QWidget;auto* outer=new QVBoxLayout(page);outer->setContentsMargins(0,0,0,0);
        auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* body=new QWidget;auto* layout=new QVBoxLayout(body);layout->setContentsMargins(0,0,8,8);layout->setSpacing(18);
        auto* salaries=new QHBoxLayout;salaries->setSpacing(18);kpiMonthCommissionLabel=new PremiumSalaryCard("Løn denne måned",true);kpiNextMonthPayLabel=new PremiumSalaryCard("Løn næste måned",false);salaries->addWidget(kpiMonthCommissionLabel,1);salaries->addWidget(kpiNextMonthPayLabel,1);layout->addLayout(salaries);
        auto* statistics=new PremiumCard;auto* stats=new QHBoxLayout(statistics);stats->setContentsMargins(24,20,24,20);stats->setSpacing(24);
        const QStringList labels={"Point i dag","Salg denne måned","Tillæg denne måned","Arbejdsdage tilbage"};QVector<QLabel*> values;
        for(int i=0;i<4;++i){if(i)stats->addWidget(premiumDivider(true));auto* cell=new QVBoxLayout;cell->setSpacing(9);cell->addWidget(premiumLabel(labels[i],"caption"));auto* value=premiumLabel("—","statValue");cell->addWidget(value);values<<value;stats->addLayout(cell,1);}
        kpiTodayPointsLabel=values[0];kpiMonthSalesLabel=values[1];kpiMonthAddonsLabel=values[2];kpiRemainingWorkDaysLabel=values[3];layout->addWidget(statistics);
        auto* punch=new PremiumCard;auto* punchRow=new QHBoxLayout(punch);punchRow->setContentsMargins(22,18,22,18);punchRow->setSpacing(16);
        auto* clock=new QLabel;clock->setPixmap(premiumIcon("clock").pixmap(25,25));clock->setStyleSheet("background:rgba(255,255,255,8);border-radius:12px;padding:10px;");punchRow->addWidget(clock);
        auto* punchText=new QVBoxLayout;punchText->setSpacing(5);intramanagerPunchStatusLabel=premiumLabel("Stempelur","cardTitle");intramanagerPunchDetailLabel=premiumLabel("—","muted");intramanagerPunchDetailLabel->setWordWrap(true);punchText->addWidget(intramanagerPunchStatusLabel);punchText->addWidget(intramanagerPunchDetailLabel);punchRow->addLayout(punchText,1);
        intramanagerPunchButton=new QPushButton("Hent status");punchRow->addWidget(intramanagerPunchButton);layout->addWidget(punch);
        auto* goalTitle=new QHBoxLayout;goalTitle->addWidget(premiumLabel("Månedens mål & næste bonus","sectionTitle"));goalTitle->addStretch();layout->addLayout(goalTitle);
        auto* progress=new QGridLayout;progress->setHorizontalSpacing(18);progress->setVerticalSpacing(18);
        progress->addWidget(createProgressCard("Pointmål",&targetProgressBar,&targetProgressHintLabel),0,0);progress->addWidget(createProgressCard("Salgsmål",&salesTargetProgressBar,&salesTargetProgressHintLabel),0,1);
        progress->addWidget(createProgressCard("SIMO-bonus",&simoProgressBar,&simoProgressHintLabel),1,0);progress->addWidget(createProgressCard("VOICE-bonus",&voiceProgressBar,&voiceProgressHintLabel),1,1);layout->addLayout(progress);
        auto recent=createSummaryCard("Seneste aktivitet");recentActivityLabel=recent.second;layout->addWidget(recent.first);layout->addStretch();
        connect(intramanagerPunchButton,&QPushButton::clicked,this,[this](){toggleIntramanagerPunchAsync();});scroll->setWidget(body);outer->addWidget(scroll);return page;
    }

    QWidget* buildOrdersTab() {
        auto* page=new QWidget;auto* layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);layout->setSpacing(18);
        auto* filters=new QHBoxLayout;filters->setSpacing(12);orderSearchEdit=new QLineEdit;orderSearchEdit->setPlaceholderText("Søg på virksomhed, ordre eller produkt…");orderSearchEdit->addAction(premiumIcon("search"),QLineEdit::LeadingPosition);orderSearchEdit->setClearButtonEnabled(true);filters->addWidget(orderSearchEdit,1);
        orderStatusFilter=new QComboBox;orderStatusFilter->addItem("Alle statusser",QString());orderStatusFilter->addItem("Afventer masterark","pending");orderStatusFilter->addItem("Registreret","registered");orderStatusFilter->addItem("Kræver handling","error");filters->addWidget(orderStatusFilter);layout->addLayout(filters);
        auto* heading=new QHBoxLayout;heading->setSpacing(12);heading->addWidget(premiumLabel("Alle ordrer","sectionTitle"));ordersCountLabel=premiumLabel("","badge");heading->addWidget(ordersCountLabel);heading->addStretch();layout->addLayout(heading);
        auto* actions=new QHBoxLayout;actions->setSpacing(9);auto* refreshBtn=new QPushButton;refreshBtn->setIcon(premiumIcon("refresh"));refreshBtn->setToolTip("Opdater ordrer");refreshBtn->setAccessibleName("Opdater ordrer");actions->addWidget(refreshBtn);actions->addStretch();
        auto* editBtn=new QPushButton("Rediger");auto* registerBtn=new QPushButton("Registrer / prøv igen");auto* deleteBtn=new QPushButton("Slet");deleteBtn->setProperty("destructive",true);for(auto* button:{editBtn,registerBtn,deleteBtn})actions->addWidget(button);layout->addLayout(actions);
        ordersList=new PremiumOrdersList;layout->addWidget(ordersList,1);
        connect(orderSearchEdit,&QLineEdit::textChanged,this,[this](){applyOrderFilter();});connect(orderStatusFilter,&QComboBox::currentIndexChanged,this,[this](){applyOrderFilter();});
        const auto updateActions=[this,editBtn,registerBtn,deleteBtn](){const bool selected=!ordersList->selectedItems().isEmpty() && ordersList->currentItem() && !ordersList->currentItem()->isHidden();for(auto* button:{editBtn,registerBtn,deleteBtn})button->setEnabled(selected);};connect(ordersList,&QListWidget::itemSelectionChanged,this,updateActions);updateActions();
        connect(ordersList,&QListWidget::itemDoubleClicked,this,[this](){editSelectedOrder();});
        connect(refreshBtn,&QPushButton::clicked,this,[this](){refreshOrders();});connect(editBtn,&QPushButton::clicked,this,[this](){editSelectedOrder();});connect(registerBtn,&QPushButton::clicked,this,[this](){retrySelectedMasterRegistration();});connect(deleteBtn,&QPushButton::clicked,this,[this](){deleteSelectedOrder();});return page;
    }

    void applyOrderFilter() {
        if(!ordersList || !orderSearchEdit)return;const QString query=orderSearchEdit->text().simplified();const QString status=orderStatusFilter->currentData().toString();int visible=0;
        for(int row=0;row<ordersList->count();++row){auto* item=ordersList->item(row);const auto data=item->data(Qt::UserRole).toMap();const QString state=data.value("state").toString();const QString category=state=="registered"?state:state=="error"?state:QString("pending");
            bool match=status.isEmpty() || status==category;for(const auto& word:query.split(' ',Qt::SkipEmptyParts))if(!item->text().contains(word,Qt::CaseInsensitive))match=false;
            item->setHidden(!match);if(match)++visible;}
        if(ordersList->currentItem() && ordersList->currentItem()->isHidden()){ordersList->clearSelection();ordersList->setCurrentRow(-1);}
        ordersCountLabel->setText(query.isEmpty() && status.isEmpty()?QString::number(visible):QString("%1 af %2").arg(visible).arg(ordersList->count()));ordersList->doItemsLayout();ordersList->viewport()->update();
    }

    QWidget* buildReportsTab() {
        auto* page=new QWidget;auto* layout=new QVBoxLayout(page);layout->setSpacing(18);layout->setContentsMargins(0,0,0,0);
        auto* controls=new QHBoxLayout;controls->setSpacing(12);controls->addWidget(premiumLabel("Periode","caption"));
        reportPresetCombo=new QComboBox;reportPresetCombo->addItems({"I dag","Denne arbejdsuge","Seneste 2 arbejdsuger","Denne lønmåned","Vælg måned"});reportPresetCombo->setCurrentIndex(3);controls->addWidget(reportPresetCombo);
        reportMonthEdit=new QDateEdit(QDate::currentDate());reportMonthEdit->setDisplayFormat("MMMM yyyy");reportMonthEdit->setCalendarPopup(true);reportMonthEdit->setLocale(QLocale(QLocale::Danish,QLocale::Denmark));reportMonthEdit->hide();controls->addWidget(reportMonthEdit);controls->addStretch();layout->addLayout(controls);
        auto* scroll=new QScrollArea;scroll->setObjectName("reportScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        reportView=new PremiumReportView;scroll->setWidget(reportView);layout->addWidget(scroll,1);
        connect(reportPresetCombo,&QComboBox::currentIndexChanged,this,[this,scroll](int index){reportMonthEdit->setVisible(index==4);scroll->verticalScrollBar()->setValue(0);generateReport();});
        connect(reportMonthEdit,&QDateEdit::dateChanged,this,[this,scroll](const QDate&){scroll->verticalScrollBar()->setValue(0);generateReport();});
        return page;
    }

    QWidget* buildSettingsTab() {
        auto* w=new QWidget;auto* outerLayout=new QVBoxLayout(w);outerLayout->setContentsMargins(0,0,0,0);outerLayout->setSpacing(18);
        auto goalCard = createCard("Mål");
        auto* form = new QFormLayout;
        configureSettingsForm(form);

        targetSpin = new QDoubleSpinBox;
        targetSpin->setRange(0, 100000);
        targetSpin->setDecimals(2);
        targetSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        configureSettingsField(targetSpin);

        monthlySalesTargetSpin = new QSpinBox;
        monthlySalesTargetSpin->setRange(0, 100000);
        monthlySalesTargetSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        configureSettingsField(monthlySalesTargetSpin);

        form->addRow("Pointmål for måneden", targetSpin);
        form->addRow("Salgsmål for måneden", monthlySalesTargetSpin);

        auto* saveGoalBtn = new QPushButton("Gem mål");
        configureSettingsButton(saveGoalBtn);
        saveGoalBtn->setProperty("primary",true);saveGoalBtn->setMaximumWidth(160);form->addRow(QString(),saveGoalBtn);

        goalCard.second->addLayout(form);

        auto intramanagerCard = createCard("Forbind Intramanager");

        auto* imForm = new QFormLayout;
        configureSettingsForm(imForm);

        intramanagerEnabledCheck = new QCheckBox("Aktivér Intramanager og automatisk timehentning");
        intramanagerEnabledCheck->setFocusPolicy(Qt::NoFocus);

        intramanagerUsernameEdit = new QLineEdit;
        intramanagerUsernameEdit->setPlaceholderText("Brugernavn til Intramanager");
        configureSettingsField(intramanagerUsernameEdit);

        intramanagerPasswordEdit = new QLineEdit;
        intramanagerPasswordEdit->setEchoMode(QLineEdit::Password);
        intramanagerPasswordEdit->setPlaceholderText("Adgangskode til Intramanager");
        configureSettingsField(intramanagerPasswordEdit);

        hourlyRateSpin = new QDoubleSpinBox;
        hourlyRateSpin->setRange(0, 10000);
        hourlyRateSpin->setDecimals(2);
        hourlyRateSpin->setSuffix(" kr/t");
        hourlyRateSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        configureSettingsField(hourlyRateSpin);

        taxDeductionSpin = new QDoubleSpinBox;
        taxDeductionSpin->setRange(0, 100000);
        taxDeductionSpin->setDecimals(2);
        taxDeductionSpin->setSuffix(" kr/md");
        taxDeductionSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        configureSettingsField(taxDeductionSpin);

        taxRateSpin = new QDoubleSpinBox;
        taxRateSpin->setRange(0, 100);
        taxRateSpin->setDecimals(2);
        taxRateSpin->setSuffix(" %");
        taxRateSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        configureSettingsField(taxRateSpin);

        auto* saveIntramanagerBtn = new QPushButton("Gem forbindelse");
        saveIntramanagerBtn->setObjectName("intramanagerSaveButton");saveIntramanagerBtn->setProperty("primary",true);
        configureSettingsButton(saveIntramanagerBtn);

        intramanagerStatusLabel = new QLabel("Timer hentes automatisk.");
        intramanagerStatusLabel->setWordWrap(true);
        intramanagerStatusLabel->setTextInteractionFlags(Qt::NoTextInteraction);

        imForm->addRow(intramanagerEnabledCheck);
        imForm->addRow("Brugernavn", intramanagerUsernameEdit);
        imForm->addRow("Adgangskode", intramanagerPasswordEdit);

        intramanagerChromeCheck=new QCheckBox("Brug Chrome til timer, sygeløn og annulleringer");
        imForm->addRow(intramanagerChromeCheck);
        intramanagerProfileEdit=new QLineEdit;intramanagerProfileEdit->setPlaceholderText("Fuldt kontonavn fra Intramanager");configureSettingsField(intramanagerProfileEdit);
        imForm->addRow("Chrome-konto",intramanagerProfileEdit);
        auto* bindChrome=new QPushButton("Forbind konto i Chrome");configureSettingsButton(bindChrome);imForm->addRow(bindChrome);
        connect(intramanagerChromeCheck,&QCheckBox::toggled,intramanagerUsernameEdit,&QWidget::setDisabled);
        connect(intramanagerChromeCheck,&QCheckBox::toggled,intramanagerPasswordEdit,&QWidget::setDisabled);
        connect(bindChrome,&QPushButton::clicked,this,[this](){
            if(intramanagerSyncRunning){QMessageBox::information(this,"Intramanager","Vent til den aktuelle hentning er afsluttet.");return;}
            intramanagerSyncRunning=true;
            runChromeIntramanager("profile",{}, {},[this](QJsonObject answer){
                intramanagerSyncRunning=false;
                if(!answer.value("success").toBool()){QMessageBox::information(this,"Forbind Intramanager",answer.value("error").toString());return;}
                const QString profile=answer.value("profileName").toString();
                if(!confirmQuestion(this,"Bekræft din konto",QString("Chrome er logget ind som %1. Er det din Intramanager-konto?").arg(profile)))return;
                repo.settings.intramanagerBrowserProfile=profile;repo.settings.intramanagerViaChrome=true;repo.settings.intramanagerEnabled=true;
                repo.saveSettings();refreshAll();setupIntramanagerAutoSync();
            });
        });
        auto payrollCard=createCard("Løn & skat");auto* payrollForm=new QFormLayout;configureSettingsForm(payrollForm);
        auto* payrollHelp=premiumLabel("Indtast timeløn, månedsfradrag og trækprocent fra dit skattekort.","muted");payrollHelp->setWordWrap(true);payrollCard.second->addWidget(payrollHelp);payrollCard.second->addSpacing(12);
        payrollForm->addRow("Timeløn",hourlyRateSpin);payrollForm->addRow("Månedsfradrag",taxDeductionSpin);payrollForm->addRow("Trækprocent",taxRateSpin);payrollCard.second->addLayout(payrollForm);
        auto* savePayrollBtn=new QPushButton("Gem lønindstillinger");savePayrollBtn->setProperty("primary",true);payrollCard.second->addSpacing(12);payrollCard.second->addWidget(savePayrollBtn,0,Qt::AlignRight);
        auto* payrollSaved=premiumLabel("","muted");payrollSaved->hide();payrollCard.second->addWidget(payrollSaved);
        connect(savePayrollBtn,&QPushButton::clicked,this,[this,payrollSaved](){repo.settings.hourlyRate=hourlyRateSpin->value();repo.settings.taxDeduction=taxDeductionSpin->value();repo.settings.taxRatePercent=taxRateSpin->value();repo.saveSettings();refreshDashboard();generateReport();payrollSaved->setText("Dine lønindstillinger er gemt.");payrollSaved->show();});
        imForm->addRow(saveIntramanagerBtn);
        imForm->addRow("Status", intramanagerStatusLabel);

        intramanagerCard.second->addLayout(imForm);


        auto salesRegistrationCard = createCard("Salgsregistrering");
        auto* salesRegForm = new QFormLayout;
        configureSettingsForm(salesRegForm);
        salesRegForm->setRowWrapPolicy(QFormLayout::WrapLongRows);

        defaultSellerInitialsEdit = new QLineEdit;
        defaultSellerInitialsEdit->setPlaceholderText("Initialer ved nye ordrer");
        configureSettingsField(defaultSellerInitialsEdit);

        masterWorkbookUrlEdit = new QLineEdit;
        masterWorkbookUrlEdit->setPlaceholderText("Link til dit masterark i Excel Online");
        configureSettingsField(masterWorkbookUrlEdit);

        salesRegistrationEnabledCheck = new QCheckBox("Registrer automatisk i masterarket");
        salesRegistrationEnabledCheck->setFocusPolicy(Qt::NoFocus);


        auto* saveSalesRegistrationBtn = new QPushButton("Gem salgsregistrering");
        auto* testSalesRegistrationBtn = new QPushButton("Forbind masterark");
        auto* installChromeBtn = new QPushButton("Installer Chrome-udvidelse");
        auto* probeChromeBtn = new QPushButton("Kontrollér Chrome");
        auto* salesActionRow = createSettingsButtonGrid(QVector<QPushButton*>{saveSalesRegistrationBtn, testSalesRegistrationBtn, installChromeBtn, probeChromeBtn}, 2);
        saveSalesRegistrationBtn->setProperty("primary",true);
        connect(installChromeBtn, &QPushButton::clicked, this, [this, installChromeBtn]() {
            installChromeBtn->setEnabled(false);
            auto* installer = new QProcess(this);
            connect(installer, &QProcess::started, this, [installer]() {
                installer->write("{\"action\":\"chrome-install\"}");
                installer->closeWriteChannel();
            });
            connect(installer, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, installer, installChromeBtn](int, QProcess::ExitStatus) {
                installChromeBtn->setEnabled(true);
                const auto result = QJsonDocument::fromJson(installer->readAllStandardOutput()).object();
                installer->deleteLater();
                const QString path = result.value("extensionPath").toString();
                if (!result.value("success").toBool() || result.value("status").toString() != "chrome-prepared"
                    || !QFileInfo::exists(path + "/manifest.json")) {
                    QMessageBox::warning(this, "Chrome-udvidelse", result.value("error").toString("Udvidelsen kunne ikke klargøres. Prøv igen."));
                    return;
                }
                QDialog dialog(this);
                dialog.setWindowTitle("Installer Chrome-udvidelse");
                dialog.setMinimumWidth(560);
                auto* layout = new QVBoxLayout(&dialog);
                auto* instructions = new QLabel("<b>Udvidelsen er klargjort på din computer.</b><br><br>"
                    "1. Åbn <b>chrome://extensions</b> i Chrome.<br>"
                    "2. Slå <b>Udviklertilstand</b> til øverst til højre, og behold den slået til.<br>"
                    "3. Vælg <b>Indlæs udpakket</b>, og vælg mappen nedenfor.<br><br>"
                    "Har du den gamle udvidelse 0.1.1, skal du genindlæse den én gang i Chrome.<br>"
                    "Fremover opdateres udvidelsen automatisk sammen med Provi Tracker.<br>"
                    "Vælg derefter <b>Kontrollér Chrome</b> i Provi Tracker.");
                instructions->setWordWrap(true);
                layout->addWidget(instructions);
                auto* folder = new QLineEdit(QDir::toNativeSeparators(path));
                folder->setReadOnly(true);
                layout->addWidget(folder);
                auto* buttons = new QHBoxLayout;
                auto* copy = new QPushButton("Kopier mappesti");
                auto* open = new QPushButton("Åbn Chrome");
                auto* done = new QPushButton("Luk");
                buttons->addWidget(copy); buttons->addWidget(open); buttons->addWidget(done);
                layout->addLayout(buttons);
                connect(copy, &QPushButton::clicked, &dialog, [folder]() { QApplication::clipboard()->setText(folder->text()); });
                connect(open, &QPushButton::clicked, &dialog, [&dialog]() {
                    QSettings user("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe", QSettings::NativeFormat);
                    QSettings machine("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe", QSettings::NativeFormat);
                    QString chrome = user.value(".").toString();
                    if (chrome.isEmpty()) chrome = machine.value(".").toString();
                    if (chrome.isEmpty() || !QProcess::startDetached(chrome, {"chrome://extensions/"}))
                        QMessageBox::information(&dialog, "Åbn Chrome", "Åbn Chrome, og skriv chrome://extensions i adresselinjen.");
                });
                connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);
                dialog.exec();
            });
            connect(installer, &QProcess::errorOccurred, this, [this, installer, installChromeBtn](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) return;
                installChromeBtn->setEnabled(true);
                installer->deleteLater();
                QMessageBox::warning(this, "Chrome-udvidelse", "Hjælperen kunne ikke starte. Geninstaller den nyeste version af Provi Tracker.");
            });
            QTimer::singleShot(30000, installer, [installer]() { if (installer->state() != QProcess::NotRunning) installer->kill(); });
            installer->start(intramanagerWorkerPath(), {"--stdin-json"});
        });
        connect(probeChromeBtn, &QPushButton::clicked, this, [this, probeChromeBtn]() {
            saveSalesRegistrationSettingsFromUi();
            if (!isMasterWorkbookUrl(repo.settings.masterWorkbookUrl)) {
                salesRegistrationStatusLabel->setText("Gem dit masterark-link først.");
                return;
            }
            probeChromeBtn->setEnabled(false);
            auto* probe = new QProcess(this);
            const QJsonObject input{{"action", "chrome-probe"}, {"workbookUrl", repo.settings.masterWorkbookUrl}};
            salesRegistrationStatusLabel->setText("Kontrollerer det åbne masterark i Chrome. Der skrives ingen salg i denne kontrol.");
            connect(probe, &QProcess::started, this, [probe, input]() {
                probe->write(QJsonDocument(input).toJson(QJsonDocument::Compact));
                probe->closeWriteChannel();
            });
            connect(probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, probe, probeChromeBtn](int, QProcess::ExitStatus) {
                const auto result = QJsonDocument::fromJson(probe->readAllStandardOutput()).object();
                const bool ok = result.value("success").toBool() && result.value("status").toString() == "chrome-connected";
                salesRegistrationStatusLabel->setText(ok ? "Chrome har forbindelse til dit masterark. Vælg Forbind masterark for at kontrollere salgsregistreringen."
                    : result.value("error").toString("Chrome-kontrollen mislykkedes. Prøv igen."));
                probeChromeBtn->setEnabled(true);
                probe->deleteLater();
            });
            connect(probe, &QProcess::errorOccurred, this, [this, probe, probeChromeBtn](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) return;
                salesRegistrationStatusLabel->setText("Chrome-hjælperen kunne ikke starte. Installer den nyeste version af Provi Tracker.");
                probeChromeBtn->setEnabled(true);
                probe->deleteLater();
            });
            QTimer::singleShot(45000, probe, [probe]() { if (probe->state() != QProcess::NotRunning) probe->kill(); });
            probe->start(intramanagerWorkerPath(), {"--stdin-json"});
        });

        salesRegistrationStatusLabel = new QLabel("Forbind dit masterark. Microsoft-login foregår i browseren.");
        salesRegistrationStatusLabel->setWordWrap(true);
        salesRegistrationStatusLabel->setMinimumWidth(0);
        salesRegistrationStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        salesRegistrationStatusLabel->setTextInteractionFlags(Qt::NoTextInteraction);

        salesRegForm->addRow("Sælger initialer", defaultSellerInitialsEdit);
        salesRegForm->addRow("Masterark", masterWorkbookUrlEdit);
        salesRegForm->addRow(salesRegistrationEnabledCheck);
        salesRegForm->addRow(salesActionRow);
        salesRegForm->addRow(salesRegistrationStatusLabel);

        salesRegistrationCard.second->addLayout(salesRegForm);


        auto sellerCard=createCard("Konto");
        accountIdentityLabel=premiumLabel("","sectionTitle");accountIdentityLabel->setWordWrap(true);sellerCard.second->addWidget(accountIdentityLabel);
        cloudAccountStatusLabel=premiumLabel("","muted");cloudAccountStatusLabel->setWordWrap(true);sellerCard.second->addWidget(cloudAccountStatusLabel);
        auto* logoutCloudBtn=new QPushButton("Log ud");logoutCloudBtn->setProperty("destructive",true);sellerCard.second->addWidget(logoutCloudBtn,0,Qt::AlignLeft);

        auto* categories=new QTabWidget;categories->setObjectName("settingsTabs");
        const auto addSettingsPage=[categories](const QString& name,const QVector<QFrame*>& cards){
            auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            auto* body=new QWidget;auto* row=new QHBoxLayout(body);row->setContentsMargins(0,18,8,8);row->setSpacing(0);
            auto* column=new QWidget;column->setMaximumWidth(820);auto* stack=new QVBoxLayout(column);stack->setContentsMargins(0,0,0,0);stack->setSpacing(18);
            for(auto* card:cards){card->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Maximum);stack->addWidget(card);}stack->addStretch();row->addWidget(column,1);row->addStretch(0);scroll->setWidget(body);categories->addTab(scroll,name);
        };
        addSettingsPage("Generelt",{goalCard.first});addSettingsPage("Løn",{payrollCard.first});
        addSettingsPage("Intramanager",{intramanagerCard.first});addSettingsPage("Masterark",{salesRegistrationCard.first});addSettingsPage("Konto",{sellerCard.first});outerLayout->addWidget(categories);

        connect(saveGoalBtn, &QPushButton::clicked, this, [this]() {
            repo.settings.bonus.monthlyTargetPoints = targetSpin->value();
            repo.settings.monthlySalesTarget = monthlySalesTargetSpin->value();
            repo.saveSettings();
            refreshAll();
        });

        connect(saveIntramanagerBtn, &QPushButton::clicked, this, [this]() {
            const QString username = intramanagerUsernameEdit->text().trimmed();
            const QString password = intramanagerPasswordEdit->text();

            repo.settings.intramanagerEnabled = intramanagerEnabledCheck->isChecked();
            repo.settings.intramanagerUsername = username;
            repo.settings.intramanagerViaChrome = intramanagerChromeCheck->isChecked();
            repo.settings.intramanagerBrowserProfile = intramanagerProfileEdit->text().trimmed();

            repo.saveSettings();

            bool passwordSaved = true;

            if (!password.isEmpty()) {
                passwordSaved = saveIntramanagerPassword(username, password);
                if (passwordSaved && storeIntramanagerSecret(username, password)) {
                    scheduleCloudSave();
                }
            }

            if (!passwordSaved) {
                intramanagerStatusLabel->setText("Indstillinger gemt, men adgangskoden kunne ikke gemmes krypteret.");
            } else {
                intramanagerStatusLabel->setText("Intramanager og løn er gemt.");
                intramanagerPasswordEdit->clear();
                intramanagerPasswordEdit->setPlaceholderText(
                    hasCloudSecret("intramanagerLogin")
                        ? "Adgangskode er gemt krypteret i cloud"
                        : "Adgangskode er gemt krypteret lokalt"
                    );
            }

            refreshAll();
            setupIntramanagerAutoSync();
            refreshIntramanagerPunchStatusAsync(true);
        });

        connect(saveSalesRegistrationBtn, &QPushButton::clicked, this, [this]() {
            saveSalesRegistrationSettingsFromUi();
            if (salesRegistrationStatusLabel) {
                salesRegistrationStatusLabel->setText("Salgsregistrering er gemt.");
                QTimer::singleShot(0, this, [this]() { processMasterQueue(); });
            }
        });

        connect(testSalesRegistrationBtn, &QPushButton::clicked, this, [this]() {
            saveSalesRegistrationSettingsFromUi();
            showMasterScriptGuide();
        });

        connect(logoutCloudBtn, &QPushButton::clicked, this, [this]() {
            logoutCloudUser();
        });

        return w;
    }

    QVector<int> activeOrderIndicesSorted() const {
        QVector<int> out;
        const auto* s = activeSalesperson();
        if (!s) return out;
        for (int i = 0; i < repo.orders.size(); ++i) {
            if (repo.orders[i].salespersonId == s->id) out.push_back(i);
        }
        std::sort(out.begin(), out.end(), [&](int a, int b) {
            return repo.orders[a].createdAt > repo.orders[b].createdAt;
        });
        return out;
    }

    QString orderProductsSummary(const Order& order, double* totalPoints = nullptr) const {
        QStringList parts;
        double points = 0.0;
        for (const auto& item : order.items) {
            const auto* p = repo.findProduct(item.productKey);
            if (!p) continue;
            parts << QString("%1 × %2").arg(p->displayName).arg(item.quantity);
            points += p->points * item.quantity;
        }
        if (totalPoints) *totalPoints = points;
        return parts.join("  ·  ");
    }

    void animateProgressBar(QProgressBar* bar, int targetValue) {
        if (!bar) return;
        auto* anim = new QPropertyAnimation(bar, "value", bar);
        anim->setDuration(550);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setStartValue(bar->value());
        anim->setEndValue(targetValue);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void applyProgressMood(QProgressBar* bar, double ratio, bool bonusClose = false) {
        if (!bar) return;
        QString chunkColor = "#78ADFF";
        if (ratio >= 1.0 || bonusClose) chunkColor = "#65D2A0";
        else if (ratio >= 0.80) chunkColor = "#65D2A0";
        else if (ratio >= 0.55) chunkColor = "#78ADFF";
        else chunkColor = "#4BBFB0";

        bar->setProperty("progressColor", QColor(chunkColor));
        bar->update();
    }

    QString moneySpan(double amount, const QString& color = "#65D2A0") const {
        return QString("<span style=\"color:%1;font-weight:700;\">%2 kr</span>").arg(color, money(amount));
    }

    QString countSpan(int value, const QString& color = "#65D2A0") const {
        return QString("<span style=\"color:%1;font-weight:700;\">%2</span>").arg(color).arg(value);
    }

    void updateSalaryCard(PremiumSalaryCard* card,double totalSalary,double baseSalary,double sickPay,double provision,const QDate& payoutMonth,const QString& hourlyForecastText,double cancellations=0.0,bool cancellationsKnown=true) const {
        if(!card)return;
        const QString takeHome=repo.settings.taxRatePercent>0?"Efter skat · "+money(estimatedNetSalary(totalSalary,repo.settings.taxDeduction,repo.settings.taxRatePercent))+" kr (estimat)":"Angiv fradrag og trækprocent i Indstillinger";
        card->present(money(totalSalary)+" kr",takeHome,"Udbetales "+payoutDateLabel(payoutMonth),
            {money(baseSalary)+" kr",money(sickPay)+" kr",money(provision)+" kr",cancellationsKnown?money(cancellations)+" kr":QString("Afventer Intramanager")},"Prognose af timeløn · "+hourlyForecastText);
    }

    QString plainBadgeText(const QString& label, const QString& value) const {
        return QString("<span style=\"color:#AAB3C0;\">%1</span> <span style=\"color:#F2F4F7;font-weight:700;\">%2</span>").arg(label, value);
    }

    void refreshAll() {
        if(!cloudUsername.trimmed().isEmpty())repo.normalizeForCloudUser(cloudUsername);
        const auto* s = activeSalesperson();
        activeSalespersonLabel->setText(s ? s->name.toHtmlEscaped() : "Ingen aktiv sælger");
        refreshDashboard();
        refreshOrders();
        refreshAccountUi();
        refreshSettingsUi();
        generateReport();
    }

    void requestDashboardHoursIfNeeded(const QPair<QDateTime, QDateTime>& range, int maxAgeMinutes = 60) {
        if (!repo.settings.intramanagerEnabled) return;

        const QString fromDate = intramanagerDate(range.first.date());
        const QString toDate = intramanagerDate(range.second.date());
        const QString key = "dashboard:" + intramanagerPeriodKey(fromDate, toDate);
        const auto cached = cachedIntramanagerHours(fromDate, toDate);
        const bool periodEnded = range.second.date() < QDate::currentDate();

        if (cached.has_value()) {
            if (periodEnded && intramanagerHoursEntryCompletesRange(*cached, range.second)) return;
            if (!periodEnded && intramanagerHoursEntryIsFresh(*cached, maxAgeMinutes)) return;
        }

        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const QDateTime lastRequested = intramanagerDashboardRefreshRequestedAt.value(key);
        if (lastRequested.isValid() && lastRequested.secsTo(nowUtc) < 30 * 60) {
            return;
        }

        intramanagerDashboardRefreshRequestedAt[key] = nowUtc;
        fetchIntramanagerHoursAsync(fromDate, toDate, true, [this, key](bool ok) {
            if (!ok) intramanagerDashboardRefreshRequestedAt.remove(key);
        });
    }

    void refreshDashboardPayrollHoursIfNeeded(
        const QPair<QDateTime, QDateTime>& currentPayPeriod,
        const QPair<QDateTime, QDateTime>& nextPayPeriod
        ) {
        requestDashboardHoursIfNeeded(currentPayPeriod);
        requestDashboardHoursIfNeeded(nextPayPeriod);

    }

    void refreshDashboard() {
        const auto* s = activeSalesperson();
        if (!s) return;
        refreshPunchCardUi();

        const QDate now = QDate::currentDate();
        const auto todayRange = qMakePair(QDateTime(now, QTime(0,0,0)), QDateTime(now, QTime(23,59,59)));
        const auto calendarMonthRange = monthRange(now);
        const auto previousCalendarMonthRange = monthRange(now.addMonths(-1));
        const auto currentPayPeriod = payrollRangeEndingInMonth(now);
        const auto nextPayPeriod = payrollRangeEndingInMonth(now.addMonths(1));
        const auto dayBonusPeriod = payrollBonusRange(now);
        refreshDashboardPayrollHoursIfNeeded(currentPayPeriod, nextPayPeriod);
        auto cachedHoursForRange = [this](const QPair<QDateTime, QDateTime>& range) {
            const QString from = intramanagerDate(range.first.date());
            const QString to = intramanagerDate(range.second.date());
            const auto cached = cachedIntramanagerHours(from, to);
            return cached.has_value() ? cached->hours : 0.0;
        };
        auto cachedSickPayForRange = [this](const QPair<QDateTime, QDateTime>& range) {
            const QString from = intramanagerDate(range.first.date());
            const QString to = intramanagerDate(range.second.date());
            const auto cached = cachedIntramanagerHours(from, to);
            return cached.has_value() ? cached->sickPay : 0.0;
        };
        auto hourlyForecastForRange = [this, now](const QPair<QDateTime, QDateTime>& range) -> QString {
            if (range.first.date() > now) return "Perioden er ikke startet";
            const auto cached = cachedIntramanagerHours(
                intramanagerDate(range.first.date()), intramanagerDate(range.second.date()));
            if (!cached) return "Afventer timedata";
            const QDateTime synced = QDateTime::fromString(cached->syncedAt, Qt::ISODate);
            const auto forecast = forecastHourlyPay(
                cached->hours * repo.settings.hourlyRate, range.first.date(), range.second.date(),
                synced.isValid() ? synced.toLocalTime().date() : QDate(), now);
            if (!forecast) return "Afventer beregningsgrundlag";
            return money(forecast->amount) + " kr";
        };
        auto delayedBonus = [](const Metrics& metrics) {
            return metrics.monthlyBonus + metrics.simoBonus + metrics.voiceBonus;
        };

        const auto mDay = CommissionEngine::calculate(repo, s->id, todayRange.first, todayRange.second);
        const auto mMonth = CommissionEngine::calculate(
            repo,
            s->id,
            calendarMonthRange.first,
            calendarMonthRange.second,
            dayBonusPeriod
            );
        const auto previousMonthMetrics = CommissionEngine::calculate(
            repo,
            s->id,
            previousCalendarMonthRange.first,
            previousCalendarMonthRange.second
            );
        const auto currentPayPeriodMetrics = CommissionEngine::calculate(
            repo,
            s->id,
            currentPayPeriod.first,
            currentPayPeriod.second,
            currentPayPeriod
            );
        const auto nextPayPeriodMetrics = CommissionEngine::calculate(
            repo,
            s->id,
            nextPayPeriod.first,
            nextPayPeriod.second,
            nextPayPeriod
            );
        const auto currentCalendarMetrics = CommissionEngine::calculate(
            repo,
            s->id,
            calendarMonthRange.first,
            calendarMonthRange.second
            );

        if (daySummaryLabel) daySummaryLabel->setText(summaryCardText(mDay));
        if (monthSummaryLabel) monthSummaryLabel->setText(summaryCardText(mMonth));

        if (kpiTodayPointsLabel) kpiTodayPointsLabel->setText(money(mDay.totalPoints));

        const double currentPayPeriodBaseSalary = cachedHoursForRange(currentPayPeriod) * repo.settings.hourlyRate;
        const double currentPayPeriodSickPay = cachedSickPayForRange(currentPayPeriod);
        const double currentPayPeriodBonus = currentPayPeriodMetrics.dayBonus;
        const double backpaidBonusThisMonth = delayedBonus(previousMonthMetrics);
        const auto currentCached = cachedIntramanagerHours(intramanagerDate(currentPayPeriod.first.date()), intramanagerDate(currentPayPeriod.second.date()));
        const auto nextCached = cachedIntramanagerHours(intramanagerDate(nextPayPeriod.first.date()), intramanagerDate(nextPayPeriod.second.date()));
        const double currentCancellations = currentCached ? currentCached->cancellations : 0.0;
        const double nextCancellations = nextCached ? nextCached->cancellations : 0.0;
        const double actualSalaryThisMonth =
            currentPayPeriodBaseSalary + currentPayPeriodSickPay + currentPayPeriodBonus + backpaidBonusThisMonth + currentCancellations;

        const double nextPayPeriodBaseSalary = cachedHoursForRange(nextPayPeriod) * repo.settings.hourlyRate;
        const double nextPayPeriodSickPay = cachedSickPayForRange(nextPayPeriod);
        const double nextPayPeriodBonus = nextPayPeriodMetrics.dayBonus;
        const double backpaidBonusNextMonth = delayedBonus(currentCalendarMetrics);
        const double salaryEarnedForNextMonth =
            nextPayPeriodBaseSalary + nextPayPeriodSickPay + nextPayPeriodBonus + backpaidBonusNextMonth + nextCancellations;

        if (kpiMonthCommissionLabel) {
            updateSalaryCard(kpiMonthCommissionLabel,
                actualSalaryThisMonth,
                currentPayPeriodBaseSalary,
                currentPayPeriodSickPay,
                currentPayPeriodBonus + backpaidBonusThisMonth,
                now, hourlyForecastForRange(currentPayPeriod), currentCancellations, currentCached && currentCached->payrollSchema >= 2
                );
        }
        if (kpiNextMonthPayLabel) {
            updateSalaryCard(kpiNextMonthPayLabel,
                salaryEarnedForNextMonth,
                nextPayPeriodBaseSalary,
                nextPayPeriodSickPay,
                nextPayPeriodBonus + backpaidBonusNextMonth,
                now.addMonths(1), hourlyForecastForRange(nextPayPeriod), nextCancellations, nextCached && nextCached->payrollSchema >= 2
                );
        }

        if (kpiMonthSalesLabel) kpiMonthSalesLabel->setText(QString::number(mMonth.salesCount));
        if (kpiMonthAddonsLabel) kpiMonthAddonsLabel->setText(QString::number(mMonth.addOnCount));

        const int nextSimoStep = nextLockedFiveStep(mMonth.simoCount, repo.settings.bonus.simoMinEligible);
        const int nextVoiceStep = nextLockedStep(mMonth.voiceCount, repo.settings.bonus.voiceMinEligible, 10);
        const int missingToNextSimo = qMax(0, nextSimoStep - mMonth.simoCount);
        const int missingToNextVoice = qMax(0, nextVoiceStep - mMonth.voiceCount);
        const double nextSimoBonus = CommissionEngine::lockedFiveBonus(nextSimoStep, repo.settings.bonus.simoMinEligible, repo.settings.bonus.simoPayoutPerUnit);
        const double nextVoiceBonus = CommissionEngine::lockedStepBonus(nextVoiceStep, repo.settings.bonus.voiceMinEligible, 10, repo.settings.bonus.voicePayoutPerUnit);

        const int activeDays = activeDayCount(mMonth);
        const auto bestDay = bestDayByPoints(mMonth);
        const int totalWorkingDays = workingDaysInMonth(now);
        const int elapsedWorkingDays = qMax(1, workingDaysElapsedInMonth(now));
        const int remainingWorkingDays = qMax(0, totalWorkingDays - elapsedWorkingDays);
        if (kpiRemainingWorkDaysLabel) {
            kpiRemainingWorkDaysLabel->setText(
                remainingWorkingDays == 1
                    ? QString("1 dag")
                    : QString("%1 dage").arg(remainingWorkingDays)
                );
        }
        const double avgPointsPerActiveDay = activeDays > 0 ? (mMonth.totalPoints / activeDays) : 0.0;
        const double avgCommissionPerActiveDay = activeDays > 0 ? (mMonth.totalCommission / activeDays) : 0.0;
        const double pointsToTarget = qMax(0.0, repo.settings.bonus.monthlyTargetPoints - mMonth.totalPoints);
        const double requiredPointsPerRemainingDay = remainingWorkingDays > 0 ? (pointsToTarget / remainingWorkingDays) : 0.0;
        const double projectedPoints = projectedMonthPoints(mMonth.totalPoints, elapsedWorkingDays, totalWorkingDays);
        const double projectedGap = projectedPoints - repo.settings.bonus.monthlyTargetPoints;
        const int missingSalesToTarget = repo.settings.monthlySalesTarget > 0 ? qMax(0, repo.settings.monthlySalesTarget - mMonth.salesCount) : 0;

        if (targetProgressBar) {
            const int targetMax = qMax(1, static_cast<int>(std::ceil(repo.settings.bonus.monthlyTargetPoints)));
            const int targetVal = qMin(targetMax, static_cast<int>(std::floor(mMonth.totalPoints)));
            targetProgressBar->setRange(0, targetMax);
            animateProgressBar(targetProgressBar, targetVal);
            targetProgressBar->setFormat(QString("%1 / %2 point").arg(money(mMonth.totalPoints)).arg(money(repo.settings.bonus.monthlyTargetPoints)));
            applyProgressMood(targetProgressBar, targetMax > 0 ? double(targetVal) / targetMax : 0.0);
        }
        if (targetProgressHintLabel) {
            if (pointsToTarget > 0.0) {
                targetProgressHintLabel->setText(QString("<b>%1 point</b> til målet · ca. %2 point pr. arbejdsdag").arg(money(pointsToTarget)).arg(money(requiredPointsPerRemainingDay)));
            } else {
                targetProgressHintLabel->setText(QString("Mål nået · %1 point over målet").arg(money(mMonth.totalPoints - repo.settings.bonus.monthlyTargetPoints)));
            }
        }

        if (salesTargetProgressBar) {
            const int salesMax = qMax(1, repo.settings.monthlySalesTarget > 0 ? repo.settings.monthlySalesTarget : qMax(1, mMonth.salesCount));
            salesTargetProgressBar->setRange(0, salesMax);
            const int salesValue = repo.settings.monthlySalesTarget>0?qMin(mMonth.salesCount,salesMax):0;
            animateProgressBar(salesTargetProgressBar, salesValue);
            salesTargetProgressBar->setFormat(repo.settings.monthlySalesTarget > 0
                ? QString("%1 / %2 salg").arg(mMonth.salesCount).arg(repo.settings.monthlySalesTarget)
                : QString("%1 salg").arg(mMonth.salesCount));
            applyProgressMood(salesTargetProgressBar, salesMax > 0 ? double(salesValue) / salesMax : 0.0, missingSalesToTarget <= 3 && repo.settings.monthlySalesTarget > 0);
        }
        if (salesTargetProgressHintLabel) {
            if (repo.settings.monthlySalesTarget <= 0) {
                salesTargetProgressHintLabel->setText("Angiv et salgsmål under Indstillinger → Generelt.");
            } else if (missingSalesToTarget > 0) {
                salesTargetProgressHintLabel->setText(QString("<b>%1 salg</b> til månedens mål").arg(missingSalesToTarget));
            } else {
                salesTargetProgressHintLabel->setText(QString("Mål nået · %1 salg over målet").arg(mMonth.salesCount - repo.settings.monthlySalesTarget));
            }
        }

        if (simoProgressBar) {
            int simoStart = 0;
            int simoEnd = repo.settings.bonus.simoMinEligible;
            if (mMonth.simoCount >= repo.settings.bonus.simoMinEligible) {
                simoEnd = nextSimoStep;
                simoStart = qMax(repo.settings.bonus.simoMinEligible, nextSimoStep - 5);
            }
            simoProgressBar->setRange(simoStart, qMax(simoStart + 1, simoEnd));
            const int simoValue = qMin(mMonth.simoCount, simoEnd);
            animateProgressBar(simoProgressBar, simoValue);
            simoProgressBar->setFormat(QString("%1 / %2").arg(mMonth.simoCount).arg(simoEnd));
            applyProgressMood(simoProgressBar, double(qMax(0, simoValue - simoStart)) / qMax(1, simoEnd - simoStart), missingToNextSimo <= 1);
        }
        if (simoProgressHintLabel) {
            if (mMonth.simoCount < repo.settings.bonus.simoMinEligible) {
                simoProgressHintLabel->setText(QString("Bonus fra %1 SIMO · %2 tilbage").arg(repo.settings.bonus.simoMinEligible).arg(qMax(0, repo.settings.bonus.simoMinEligible - mMonth.simoCount)));
            } else {
                simoProgressHintLabel->setText(QString("Næste niveau: %1 SIMO · %2 tilbage · %3").arg(nextSimoStep).arg(missingToNextSimo).arg(moneySpan(nextSimoBonus, missingToNextSimo <= 1 ? "#65D2A0" : "#65D2A0")));
            }
        }

        if (voiceProgressBar) {
            int voiceStart = 0;
            int voiceEnd = repo.settings.bonus.voiceMinEligible;
            if (mMonth.voiceCount >= repo.settings.bonus.voiceMinEligible) {
                voiceEnd = nextVoiceStep;
                voiceStart = qMax(repo.settings.bonus.voiceMinEligible, nextVoiceStep - 10);
            }
            voiceProgressBar->setRange(voiceStart, qMax(voiceStart + 1, voiceEnd));
            const int voiceValue = qMin(mMonth.voiceCount, voiceEnd);
            animateProgressBar(voiceProgressBar, voiceValue);
            voiceProgressBar->setFormat(QString("%1 / %2").arg(mMonth.voiceCount).arg(voiceEnd));
            applyProgressMood(voiceProgressBar, double(qMax(0, voiceValue - voiceStart)) / qMax(1, voiceEnd - voiceStart), missingToNextVoice <= 2);
        }
        if (voiceProgressHintLabel) {
            if (mMonth.voiceCount < repo.settings.bonus.voiceMinEligible) {
                voiceProgressHintLabel->setText(QString("Bonus fra %1 VOICE · %2 tilbage").arg(repo.settings.bonus.voiceMinEligible).arg(qMax(0, repo.settings.bonus.voiceMinEligible - mMonth.voiceCount)));
            } else {
                voiceProgressHintLabel->setText(QString("Næste niveau: %1 VOICE · %2 tilbage · %3").arg(nextVoiceStep).arg(missingToNextVoice).arg(moneySpan(nextVoiceBonus, missingToNextVoice <= 2 ? "#65D2A0" : "#65D2A0")));
            }
        }

        QString targetText;
        QTextStream targetTs(&targetText);
        targetTs << "Du står på " << money(mMonth.totalPoints) << " point og " << mMonth.salesCount << " salg lige nu.\n";
        targetTs << "Tillæg lukket: " << mMonth.addOnCount << "  •  SIMO/VOICE: " << mMonth.simoCount << "/" << mMonth.voiceCount << "\n";
        if (repo.settings.monthlySalesTarget > 0) {
            targetTs << "Du mangler " << missingSalesToTarget << " salg for at ramme månedens mål.\n";
        }
        targetTs << nextMonthlyTierHint(mMonth.totalPoints, repo.settings.bonus);
        if (targetSummaryLabel) targetSummaryLabel->setText(targetText);

        QString perfText;
        QTextStream perfTs(&perfText);
        perfTs << "Aktive salgsdage: " << activeDays << "  •  Bedste dag: ";
        if (!bestDay.first.isEmpty()) {
            perfTs << bestDay.first << " (" << money(bestDay.second) << " point)";
        } else {
            perfTs << "-";
        }
        perfTs << "\n";
        perfTs << "Snit point pr aktiv dag: " << money(avgPointsPerActiveDay)
               << "  •  Snit provision pr aktiv dag: " << money(avgCommissionPerActiveDay) << " kr\n";
        perfTs << "Hvis du holder tempoet, lander du omkring " << money(projectedPoints) << " point ved månedens slut.\n";
        if (projectedGap >= 0) {
            perfTs << "Du ligger lige nu til at lande " << money(projectedGap) << " point over målet.";
        } else {
            perfTs << "Du ligger lige nu " << money(-projectedGap) << " point bag målet.";
        }
        if (performanceSummaryLabel) performanceSummaryLabel->setText(perfText);

        QString simText;
        QTextStream simTs(&simText);
        simTs << "SIMO: næste hop ved " << nextSimoStep << "  •  mangler " << missingToNextSimo
              << "  •  næste niveau giver " << money(nextSimoBonus) << " kr\n";
        if (mMonth.voiceCount < repo.settings.bonus.voiceMinEligible) {
            simTs << "VOICE åbner ved " << repo.settings.bonus.voiceMinEligible
                  << "  •  mangler " << qMax(0, repo.settings.bonus.voiceMinEligible - mMonth.voiceCount)
                  << "  •  hopper derefter for hver 10\n";
        } else {
            simTs << "VOICE: næste hop ved " << nextVoiceStep << "  •  mangler " << missingToNextVoice
                  << "  •  næste niveau giver " << money(nextVoiceBonus) << " kr\n";
        }
        simTs << "Resterende arbejdsdage i måneden: " << remainingWorkingDays;
        if (simulatorSummaryLabel) simulatorSummaryLabel->setText(simText);

        QString recentText;
        QTextStream recentTs(&recentText);
        const auto idxs = activeOrderIndicesSorted();
        int shown = 0;
        for (int i = 0; i < idxs.size() && shown < 5; ++i, ++shown) {
            const auto& order = repo.orders[idxs[i]];
            double pts = 0.0;
            const QString products = orderProductsSummary(order, &pts);
            recentTs << order.createdAt.toString("dd-MM HH:mm") << "  •  " << products.left(72) << "  •  " << money(pts) << " point\n";
        }
        if (shown == 0) recentTs << "Der er ikke lagt nye ordrer ind endnu.";
        if (recentActivityLabel) recentActivityLabel->setText(recentText);
    }

    QString summaryCardText(const Metrics& m) const {
        QString out;
        QTextStream ts(&out);
        ts << QString("Ordrer %1 · Salg %2 · Tillæg %3\n")
                  .arg(m.totalOrders)
                  .arg(m.salesCount)
                  .arg(m.addOnCount);

        ts << QString("Point %1 · Provision %2 kr\n")
                  .arg(money(m.totalPoints))
                  .arg(money(m.totalCommission));
        ts << "SIMO " << m.simoCount << " · VOICE " << m.voiceCount;
        return out;
    }

    void refreshOrders() {
        if(!ordersList)return;ordersList->clear();const auto indices=activeOrderIndicesSorted();
        for(const int repoIndex:indices){const auto& order=repo.orders[repoIndex];double points=0;const QString products=orderProductsSummary(order,&points);qint64 units=0;for(const auto& product:order.items)units+=product.quantity;
            const QString company=order.companyName.isEmpty()?"Ordre "+order.id:order.companyName;
            const QVariantMap data={{"company",company},{"id",order.id},{"date",order.createdAt.toString("dd-MM-yyyy HH:mm")},{"products",products},{"note",order.note},{"points",points},{"pointsText",money(points)},{"units",units},{"status",salesRegistrationStateText(order)},{"state",order.masterRegistrationState},{"repoIndex",repoIndex}};
            auto* item=new QListWidgetItem(company+" "+order.id+" "+products+" "+order.note+" "+data.value("date").toString()+" "+data.value("status").toString(),ordersList);item->setData(Qt::UserRole,data);item->setToolTip(order.masterRegistrationError.isEmpty()?company+"\n"+order.id+"\n"+products:order.masterRegistrationError);
        }
        ordersList->setCurrentRow(-1);applyOrderFilter();
    }

    void refreshAccountUi() {
        updateCloudAccountStatus();
        if(accountIdentityLabel){const auto* seller=activeSalesperson();accountIdentityLabel->setText(cloudUsername.isEmpty()?(seller?seller->name:QString("Demo")):cloudUsername);}
    }

    void saveSalesRegistrationSettingsFromUi() {
        if (defaultSellerInitialsEdit) {
            repo.settings.defaultSellerInitials = defaultSellerInitialsEdit->text().trimmed();
        }
        if (masterWorkbookUrlEdit) {
            repo.settings.masterWorkbookUrl = masterWorkbookUrlEdit->text().trimmed();
        }
        if (salesRegistrationEnabledCheck) {
            repo.settings.masterRegistrationEnabled = salesRegistrationEnabledCheck->isChecked();
        }
        repo.saveSettings();
    }

    void refreshSettingsUi() {
        updateCloudAccountStatus();

        if (targetSpin) targetSpin->setValue(repo.settings.bonus.monthlyTargetPoints);
        if (monthlySalesTargetSpin) monthlySalesTargetSpin->setValue(repo.settings.monthlySalesTarget);

        if (hourlyRateSpin) {
            hourlyRateSpin->setValue(repo.settings.hourlyRate);
        }

        if (taxDeductionSpin) {
            taxDeductionSpin->setValue(repo.settings.taxDeduction);
        }

        if (taxRateSpin) {
            taxRateSpin->setValue(repo.settings.taxRatePercent);
        }
        if (intramanagerUsernameEdit) {
            intramanagerUsernameEdit->setText(repo.settings.intramanagerUsername);
        }

        if(intramanagerChromeCheck)intramanagerChromeCheck->setChecked(repo.settings.intramanagerViaChrome);
        if(intramanagerProfileEdit)intramanagerProfileEdit->setText(repo.settings.intramanagerBrowserProfile);
        if (intramanagerEnabledCheck) {
            intramanagerEnabledCheck->setChecked(repo.settings.intramanagerEnabled);
        }

        if (intramanagerPasswordEdit) {
            QString storedUser;
            QString storedPass;
            const bool hasPassword = loadIntramanagerPassword(&storedUser, &storedPass);

            intramanagerPasswordEdit->clear();

            if (hasPassword && !storedPass.isEmpty()) {
                intramanagerPasswordEdit->setPlaceholderText(
                    hasCloudSecret("intramanagerLogin")
                        ? "Adgangskode er gemt krypteret i cloud"
                        : "Adgangskode er gemt krypteret lokalt"
                    );
            } else {
                intramanagerPasswordEdit->setPlaceholderText("Indtast Intramanager adgangskode");
            }
        }

        if (intramanagerStatusLabel) {
            intramanagerStatusLabel->setText("Timer hentes automatisk.");
        }

        if (defaultSellerInitialsEdit) {
            defaultSellerInitialsEdit->setText(repo.settings.defaultSellerInitials);
        }
        if (masterWorkbookUrlEdit) {
            masterWorkbookUrlEdit->setText(repo.settings.masterWorkbookUrl);
        }
        if (salesRegistrationEnabledCheck) {
            salesRegistrationEnabledCheck->setChecked(repo.settings.masterRegistrationEnabled);
        }
        if (salesRegistrationStatusLabel) {
            salesRegistrationStatusLabel->setText("Nye salg vises øverst i masterarket og gemmes i historikken via Chrome. Se overførselsstatus på Ordrer.");
        }

        refreshPunchCardUi();
    }

    void updateInstalledChromeExtension() {
        auto* updater = new QProcess(this);
        connect(updater, &QProcess::started, this, [updater]() {
            updater->write("{\"action\":\"chrome-update\"}");
            updater->closeWriteChannel();
        });
        connect(updater, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [updater](int, QProcess::ExitStatus) {
            updater->deleteLater();
        });
        connect(updater, &QProcess::errorOccurred, updater, [updater](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) updater->deleteLater();
        });
        QTimer::singleShot(30000, updater, [updater]() {
            if (updater->state() != QProcess::NotRunning) updater->kill();
        });
        updater->start(intramanagerWorkerPath(), {"--stdin-json"});
    }

    void prepareMasterRegistration(Order& order) {
        const QString identity = order.masterRegistration.value("registrationId").toString();
        const QString worksheetName = order.masterRegistration.value("worksheetName").toString();
        order.masterRegistration = makeSalesRegistration(order, repo.products);
        order.masterRegistration["registrationId"] = identity.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : identity;
        if (!worksheetName.isEmpty()) order.masterRegistration["worksheetName"] = worksheetName;
        order.masterRegistrationState = "pending";
        order.masterRegistrationError.clear();
        order.masterWorkbookUrl = repo.settings.masterWorkbookUrl.trimmed();
    }

    QString masterSetupPhaseText(const QString& phase) const {
        static const QMap<QString,QString> names={{"login","Afventer Microsoft-login i Chrome. Opsætningen fortsætter automatisk efter login."},{"probe","Finder masterarket og den valgte arkfane"},{"install-open","Åbner Office Scripts"},{"install-find","Finder eller opretter Provi Trackers script"},{"install-name","Navngiver det nye script"},{"install-write","Indsætter den medfølgende kode"},{"install-save","Kontrollerer koden og gemmer scriptet"},{"install-reopen","Genåbner scriptet fra Excel"},{"install-verify","Kontrollerer den gemte kode"},{"open","Åbner den skrivefri kontrol"},{"run","Starter kontrollen"},{"parameter","Udfylder kontrolparametrene"},{"submit","Sender kontrollen til Excel"},{"receipt","Afventer Excels bekræftelse"}};
        return names.value(phase);
    }

    void showMasterScriptGuide() {
        QDialog dialog(this);dialog.setObjectName("masterSetupDialog");dialog.setWindowTitle("Forbind masterark");dialog.resize(650,650);
        auto* layout=new QVBoxLayout(&dialog);layout->setContentsMargins(26,24,26,24);layout->setSpacing(16);
        layout->addWidget(premiumLabel("Dit masterark. Klar fra appen.","sectionTitle"));
        auto* intro=premiumLabel("Udvidelsen åbner dit gemte masterark i Chrome, opretter sit eget script og kontrollerer opsætningen. Log ind i Microsoft, hvis browseren beder om det.","muted");intro->setWordWrap(true);layout->addWidget(intro);
        auto* start=new QPushButton("Opsæt automatisk i Chrome");start->setProperty("primary",true);layout->addWidget(start);
        auto* check=new QPushButton("Kontrollér eksisterende opsætning");layout->addWidget(check);
        masterSetupLog=new QPlainTextEdit;masterSetupLog->setReadOnly(true);masterSetupLog->setStyleSheet("QPlainTextEdit{background:#20242C;border:1px solid #363D49;border-radius:14px;padding:12px;color:#C7D1E2;}");masterSetupLog->setMinimumHeight(170);masterSetupLog->setPlaceholderText("Forløbet vises her, når opsætningen starter.");layout->addWidget(masterSetupLog,1);
        auto* manual=new QPushButton("Manuel opsætning ▾");manual->setCheckable(true);layout->addWidget(manual);auto* manualBody=new QWidget;auto* bodyLayout=new QVBoxLayout(manualBody);bodyLayout->setContentsMargins(0,0,0,0);
        auto* instructions=premiumLabel("Hvis automatisk opsætning stopper: Åbn Automatiser → Nyt script → Opret i Kodeeditor. Navngiv scriptet ProviTrackerSalesRegistrationV4, indsæt den kopierede kode, og gem. Lad editoren stå åben, og vælg Kontrollér eksisterende opsætning.","muted");instructions->setWordWrap(true);bodyLayout->addWidget(instructions);
        auto* copy=new QPushButton("Kopier Excel-script");bodyLayout->addWidget(copy);layout->addWidget(manualBody);manualBody->hide();connect(manual,&QPushButton::toggled,manualBody,&QWidget::setVisible);
        auto* close=new QPushButton("Luk");layout->addWidget(close);connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
        connect(start,&QPushButton::clicked,&dialog,[this](){if(masterSetupLog)masterSetupLog->clear();runMasterWorker(true,{},true);});
        connect(check,&QPushButton::clicked,&dialog,[this](){runMasterWorker(true);});
        connect(copy,&QPushButton::clicked,&dialog,[this,&dialog](){QFile file(QFileInfo(intramanagerWorkerPath()).absolutePath()+"/_internal/chrome_extension/excel_online_sales_registration_v4.ts");if(!file.open(QIODevice::ReadOnly)){QMessageBox::warning(&dialog,"Excel-script","Scriptet mangler. Geninstaller den nyeste version af Provi Tracker.");return;}QApplication::clipboard()->setText(QString::fromUtf8(file.readAll()));});
        auto* timer=new QTimer(&dialog);const auto update=[this,start,check,copy,close](){start->setEnabled(!masterRegistrationRunning);check->setEnabled(!masterRegistrationRunning);copy->setEnabled(!masterRegistrationRunning);close->setText(masterRegistrationRunning?"Skjul – opsætningen fortsætter":"Luk");};connect(timer,&QTimer::timeout,&dialog,update);timer->start(200);update();dialog.exec();masterSetupLog=nullptr;
    }

    void runMasterWorker(bool setup, QString registrationId = QString(), bool automaticSetup = false) {
        if (masterRegistrationRunning) return;
        if (cloudUsername.isEmpty()) {
            QMessageBox::information(this, "Salgsregistrering", "Log ind i Provi Tracker først.");
            return;
        }
        QJsonObject registration;
        QString url = repo.settings.masterWorkbookUrl;
        QString orderNumber;
        if (!setup) {
            const int index = salesRegistrationIndex(repo.orders, registrationId);
            if (index < 0) {
                if (salesRegistrationStatusLabel) salesRegistrationStatusLabel->setText("Ordren mangler et entydigt registreringsnummer. Kontrollér masterarket manuelt.");
                return;
            }
            registration = repo.orders[index].masterRegistration;
            orderNumber = registration.value("orderNumber").toString();
            url = repo.orders[index].masterWorkbookUrl;
            if (registration.value("schemaVersion").toInt() != 3 || orderNumber.isEmpty()) return;
            repo.orders[index].masterRegistrationState = "transferring";
            repo.saveOrders();
        }
        if (!isMasterWorkbookUrl(url)) {
            if (salesRegistrationStatusLabel) salesRegistrationStatusLabel->setText("Gem linket til dit masterark i Indstillinger først.");
            if(setup && masterSetupLog)masterSetupLog->appendPlainText("Gem et direkte link til masterarket i Indstillinger først.");
            return;
        }
        masterRegistrationRunning = true;
        if (!setup && repo.cloudPersistenceEnabled && !flushCloudSaveSync()) {
            masterRegistrationRunning = false;
            if (salesRegistrationStatusLabel) salesRegistrationStatusLabel->setText("Salget afventer forbindelse til skyen. Prøv igen fra Ordrer.");
            return;
        }
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        registration["requestId"] = requestId;
        if (setup) registration["isTest"] = true;
        const QJsonObject input{{"action", automaticSetup ? "chrome-autosetup" : setup ? "chrome-setup" : "chrome-register"},
            {"workbookUrl", url}, {"registration", registration}};
        auto* process = new QProcess(this);
        masterRegistrationRunning = true;
        if (salesRegistrationStatusLabel) salesRegistrationStatusLabel->setText(setup
            ? "Kontrollerer salgsregistreringsscriptet i dit åbne masterark i Chrome. Der skrives ingen salg."
            : "Registrerer salget i masterarket...");
        if(automaticSetup || !setup){
            if(automaticSetup && masterSetupLog)masterSetupLog->appendPlainText("Starter automatisk opsætning. Eksisterende scripts bevares.");
            auto buffer=std::make_shared<QByteArray>();
            connect(process,&QProcess::readyReadStandardError,this,[this,process,buffer](){
                buffer->append(process->readAllStandardError());
                while(buffer->contains('\n')){const int end=buffer->indexOf('\n');const auto line=buffer->left(end);buffer->remove(0,end+1);if(!line.startsWith("PROVITRACKER_PROGRESS:"))continue;
                    const QString text=masterSetupPhaseText(QJsonDocument::fromJson(line.mid(22)).object().value("phase").toString());if(text.isEmpty())continue;if(masterSetupLog)masterSetupLog->appendPlainText(text);if(salesRegistrationStatusLabel)salesRegistrationStatusLabel->setText(text);
                }
            });
        }
        connect(process, &QProcess::started, this, [process, input]() {
            process->write(QJsonDocument(input).toJson(QJsonDocument::Compact));
            process->closeWriteChannel();
        });
        const auto finish = [this, process, setup, automaticSetup, registrationId, orderNumber, requestId, url](QJsonObject result) {
            masterRegistrationRunning = false;
            const QString status = result.value("status").toString();
            const bool ok = result.value("success").toBool() && result.value("scriptVersion").toInt() == 4
                && result.value("requestId").toString() == requestId
                && !result.value("worksheetName").toString().isEmpty() && !result.value("worksheetId").toString().isEmpty()
                && (setup ? (status == "checked" && result.value("row").toInt() == 0 && result.value("orderNumber").toString().isEmpty())
                    : ((status == "registered" || status == "already_registered")
                    && result.value("orderNumber").toString() == orderNumber && result.value("registrationId").toString() == registrationId
                    && result.value("row").toInt() >= 16));
            const QString error = result.value("error").toString("Excel bekræftede ikke registreringen. Kontrollér masterarket og prøv igen.");
            if(automaticSetup){if(masterSetupLog)masterSetupLog->appendPlainText(ok?"Klar. Excel har bekræftet opsætningen; ingen salg er registreret.":"Stoppet: "+error);}
            if (!setup) {
                const int index = salesRegistrationIndex(repo.orders, registrationId);
                if (index >= 0) {
                    const QString worksheetName = result.value("worksheetName").toString();
                    if (ok && !worksheetName.isEmpty()) repo.orders[index].masterRegistration["worksheetName"] = worksheetName;
                    repo.orders[index].masterRegistrationState = ok ? "registered" : (status == "chrome-sleeping" || status == "chrome-login-required" || status == "chrome-update-required") ? "waiting-browser" : "error";
                    repo.orders[index].masterRegistrationError = ok ? QString() : error;
                }
                repo.saveOrders();
                refreshOrders();
            }
            if (salesRegistrationStatusLabel) salesRegistrationStatusLabel->setText(ok
                ? (setup ? "Masterarket er klar. Slå automatisk registrering til, og gem indstillingerne." : "Salget er registreret i masterarket.") : error);
            process->deleteLater();
            if (ok && !setup) QTimer::singleShot(200, this, [this]() { processMasterQueue(); });
        };
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [process, finish](int, QProcess::ExitStatus) {
                finish(QJsonDocument::fromJson(process->readAllStandardOutput()).object());
            });
        connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(QJsonObject{{"error", "Browserhjælperen kunne ikke starte. Geninstaller den nyeste Provi Tracker-version."}});
        });
        QTimer::singleShot(!setup ? 450000 : automaticSetup ? 690000 : 210000, process, [process]() { if (process->state() != QProcess::NotRunning) process->kill(); });
        process->start(intramanagerWorkerPath(), {"--stdin-json"});
    }

    void processMasterQueue() {
        if (masterRegistrationRunning || !repo.settings.masterRegistrationEnabled) return;
        for (const auto& order : repo.orders) {
            if ((order.masterRegistrationState == "pending" || order.masterRegistrationState == "transferring" || order.masterRegistrationState == "waiting-browser") && isMasterWorkbookUrl(order.masterWorkbookUrl)) {
                if (order.masterRegistration.value("schemaVersion").toInt() != 3 || order.masterRegistration.value("registrationId").toString().isEmpty()) continue;
                runMasterWorker(false, order.masterRegistration.value("registrationId").toString());
                return;
            }
        }
    }

    void retrySelectedMasterRegistration() {
        const int index = selectedOrderRepoIndex();
        if (index < 0 || masterRegistrationRunning) return;
        auto& order = repo.orders[index];
        if (order.masterRegistrationState == "registered" || order.masterRegistrationState == "changed") {
            QMessageBox::information(this, "Salgsregistrering", salesRegistrationStateText(order)
                + ". Rettelser til allerede registrerede salg skal kontrolleres i masterarket.");
            return;
        }
        if (!order.masterRegistration.isEmpty() && order.masterRegistration.value("schemaVersion").toInt() != 3) {
            QMessageBox::information(this, "Salgsregistrering", "Denne ordre stammer fra den tidligere overførsel. Kontrollér den manuelt i masterarket, så salget ikke registreres to gange.");
            return;
        }
        if (order.masterRegistration.isEmpty()) prepareMasterRegistration(order);
        if (order.masterWorkbookUrl.isEmpty()) order.masterWorkbookUrl = repo.settings.masterWorkbookUrl;
        order.masterRegistrationState = "pending";
        order.masterRegistrationError.clear();
        const QString identity = order.masterRegistration.value("registrationId").toString();
        saveOrdersAndSyncCloud();
        refreshOrders();
        runMasterWorker(false, identity);
    }

    void createOrder() {
        const auto* s = activeSalesperson();
        if (!s) return;
        OrderEditorDialog dlg(repo, s->id, std::nullopt, this);
        if (dlg.exec() == QDialog::Accepted) {
            Order order = dlg.getOrder();
            prepareMasterRegistration(order);
            repo.orders.push_back(order);
            saveOrdersAndSyncCloud();
            processMasterQueue();
            refreshAll();
        }
    }

    int selectedOrderRepoIndex() const {
        if(!ordersList || ordersList->selectedItems().isEmpty() || !ordersList->currentItem() || ordersList->currentItem()->isHidden())return -1;
        const int index=ordersList->currentItem()->data(Qt::UserRole).toMap().value("repoIndex",-1).toInt();
        return index>=0 && index<repo.orders.size()?index:-1;
    }

    void editSelectedOrder() {
        if (masterRegistrationRunning) return;
        const int repoIndex = selectedOrderRepoIndex();
        if (repoIndex < 0) return;
        const auto* s = activeSalesperson();
        if (!s) return;
        OrderEditorDialog dlg(repo, s->id, repo.orders[repoIndex], this);
        if (dlg.exec() == QDialog::Accepted) {
            Order updated = dlg.getOrder();
            const auto& previous = repo.orders[repoIndex];
            updated.masterRegistration = previous.masterRegistration;
            updated.masterRegistrationState = previous.masterRegistrationState;
            updated.masterRegistrationError = previous.masterRegistrationError;
            updated.masterWorkbookUrl = previous.masterWorkbookUrl;
            if (makeSalesRegistration(updated, repo.products) != makeSalesRegistration(previous, repo.products)) {
                if (previous.masterRegistrationState == "pending" || previous.masterRegistration.isEmpty()) {
                    prepareMasterRegistration(updated);
                } else {
                    updated.masterRegistrationState = "changed";
                    updated.masterRegistrationError = "Ordren er rettet. Kontrollér den tidligere registrering i masterarket.";
                }
            }
            repo.orders[repoIndex] = updated;
            saveOrdersAndSyncCloud();
            refreshAll();
        }
    }

    void deleteSelectedOrder() {
        if (masterRegistrationRunning) return;
        const int repoIndex = selectedOrderRepoIndex();
        if (repoIndex < 0) return;
        if (confirmQuestion(this, "Slet ordre", "Er du sikker på, at du vil slette den valgte ordre?")) {
            repo.orders.removeAt(repoIndex);
            saveOrdersAndSyncCloud();
            refreshAll();
        }
    }

    struct ReportRange {
        QString label;
        QDateTime from;
        QDateTime to;
        QDateTime hoursFrom;
        QDateTime hoursTo;
        QDateTime dayBonusFrom;
        QDateTime dayBonusTo;
        QDate paymentMonth;
    };

    ReportRange makeReportRange(
        const QString& label,
        const QPair<QDateTime, QDateTime>& commissionRange,
        std::optional<QPair<QDateTime, QDateTime>> payrollRange = std::nullopt,
        QDate paymentMonth = QDate()
        ) const {
        const auto salaryRange = payrollRange.value_or(commissionRange);
        return {
            label,
            commissionRange.first,
            commissionRange.second,
            salaryRange.first,
            salaryRange.second,
            salaryRange.first,
            salaryRange.second,
            paymentMonth
        };
    }

    ReportRange currentReportRange() const {
        const QDate now = QDate::currentDate();

        switch (reportPresetCombo->currentIndex()) {
            case 0:
                return makeReportRange(
                    "I dag",
                    {QDateTime(now, QTime(0,0,0)), QDateTime(now, QTime(23,59,59))}
                    );

            case 1: {
                const auto r = workWeekRange(now);
                return makeReportRange("Denne arbejdsuge", r);
            }

            case 2: {
                const auto r = previousAndCurrentWorkWeeksRange(now);
                return makeReportRange("Seneste 2 arbejdsuger", r);
            }

            case 3: {
                const auto r = monthRange(now);
                const auto salaryRange = payrollRangeEndingInMonth(now);
                return makeReportRange("Denne måned", r, salaryRange, now);
            }

            case 4:
            default: {
                const QDate paymentMonth = reportMonthEdit->date();
                const auto r = monthRange(paymentMonth);
                const auto salaryRange = payrollRangeEndingInMonth(paymentMonth);
                return makeReportRange(QLocale(QLocale::Danish,QLocale::Denmark).toString(paymentMonth,"MMMM yyyy"), r, salaryRange, paymentMonth);
            }
        }
    }

    // Månedsprovision bruger kalendermåneden; timer og dagspointbonus følger lønperioden 21.-20.
    QPair<QString, QString> reportHoursDates(const ReportRange& range) const {
        return {intramanagerDate(range.hoursFrom.date()), intramanagerDate(range.hoursTo.date())};
    }

    QPair<QDateTime, QDateTime> reportDayBonusPeriod(const ReportRange& range) const {
        return {range.dayBonusFrom, range.dayBonusTo};
    }

    ReportSalaryBreakdown reportSalaryBreakdown(
        const ReportRange& range,
        const Salesperson& salesperson,
        const IntramanagerHoursEntry& hoursEntry
        ) const {
        ReportSalaryBreakdown salary;
        if (!range.paymentMonth.isValid()) {
            salary.sickPay = hoursEntry.sickPay;
        salary.cancellations = hoursEntry.cancellations;
        salary.cancellationsKnown = hoursEntry.payrollSchema >= 2;
            return salary;
        }

        const QDate paymentMonth(range.paymentMonth.year(), range.paymentMonth.month(), 1);
        const QDate previousMonth = paymentMonth.addMonths(-1);
        const auto payrollRange = payrollRangeEndingInMonth(paymentMonth);
        const auto previousMonthRange = monthRange(previousMonth);

        const auto payrollMetrics = CommissionEngine::calculate(
            repo,
            salesperson.id,
            payrollRange.first,
            payrollRange.second,
            payrollRange
            );
        const auto previousMonthMetrics = CommissionEngine::calculate(
            repo,
            salesperson.id,
            previousMonthRange.first,
            previousMonthRange.second
            );

        salary.usesPaymentMonthRules = true;
        salary.baseSalary = hoursEntry.hours * repo.settings.hourlyRate;
        salary.sickPay = hoursEntry.sickPay;
        salary.cancellations = hoursEntry.cancellations;
        salary.cancellationsKnown = hoursEntry.payrollSchema >= 2;
        salary.periodProvision = payrollMetrics.dayBonus;
        salary.delayedProvision =
            previousMonthMetrics.monthlyBonus + previousMonthMetrics.simoBonus + previousMonthMetrics.voiceBonus;
        salary.totalProvision = salary.periodProvision + salary.delayedProvision;
        salary.totalSalary = salary.baseSalary + salary.sickPay + salary.totalProvision + salary.cancellations;
        salary.taxConfigured = repo.settings.taxRatePercent > 0.0;
        salary.taxDeduction = repo.settings.taxDeduction;
        salary.taxRatePercent = repo.settings.taxRatePercent;
        const SalaryTaxEstimate taxEstimate = estimateSalaryTax(
            salary.totalSalary,
            salary.taxDeduction,
            salary.taxRatePercent
            );
        salary.amBidrag = taxEstimate.amBidrag;
        salary.aTax = taxEstimate.aTax;
        salary.taxAmount = taxEstimate.totalTax;
        salary.netSalary = taxEstimate.netSalary;
        salary.salaryPeriod = intramanagerPeriodLabel(
            intramanagerDate(payrollRange.first.date()),
            intramanagerDate(payrollRange.second.date())
            );
        salary.delayedPeriod = QLocale(QLocale::Danish, QLocale::Denmark).toString(previousMonth, "MMMM yyyy");
        return salary;
    }

    QString reportHoursKey(const ReportRange& range) const {
        const auto dates = reportHoursDates(range);
        return intramanagerPeriodKey(dates.first, dates.second);
    }

    std::optional<IntramanagerHoursEntry> reportHoursForRange(const ReportRange& range) const {
        const auto dates = reportHoursDates(range);
        if (auto cached = cachedIntramanagerHours(dates.first, dates.second)) {
            return cached;
        }

        if (!repo.settings.intramanagerEnabled) {
            IntramanagerHoursEntry empty;
            empty.fromDate = dates.first;
            empty.toDate = dates.second;
            return empty;
        }

        return std::nullopt;
    }

    // Kun en worker pr. periode ad gangen; flere rapportopdateringer venter på samme cache.
    void requestReportHours(const ReportRange& range, std::function<void(bool)> afterFetch, bool forceFetch = false) {
        if (!repo.settings.intramanagerEnabled) {
            if (afterFetch) afterFetch(false);
            return;
        }

        const auto dates = reportHoursDates(range);
        const QString key = intramanagerPeriodKey(dates.first, dates.second);

        if (!forceFetch && reportHoursForRange(range).has_value()) {
            if (afterFetch) afterFetch(true);
            return;
        }

        if (intramanagerPendingFetchKeys.contains(key)) {
            QTimer::singleShot(1500, this, [this, range, afterFetch, forceFetch]() {
                if (!forceFetch && reportHoursForRange(range).has_value()) {
                    if (afterFetch) afterFetch(true);
                } else {
                    requestReportHours(range, afterFetch, forceFetch);
                }
            });
            return;
        }

        intramanagerPendingFetchKeys.insert(key);
        fetchIntramanagerHoursAsync(dates.first, dates.second, true, [this, key, afterFetch](bool ok) {
            intramanagerPendingFetchKeys.remove(key);
            if (afterFetch) afterFetch(ok);
        });
    }

    void refreshCachedReportHoursIfNeeded(const ReportRange& range, const QString& requestedKey) {
        if (!repo.settings.intramanagerEnabled) return;
        const auto dates = reportHoursDates(range);
        const auto cached = cachedIntramanagerHours(dates.first, dates.second);
        if (!cached.has_value()) return;
        if (intramanagerHoursEntryIsFresh(*cached, 60)) return;

        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const QDateTime lastRequested = intramanagerReportRefreshRequestedAt.value(requestedKey);
        if (lastRequested.isValid() && lastRequested.secsTo(nowUtc) < 30 * 60) {
            return;
        }

        intramanagerReportRefreshRequestedAt[requestedKey] = nowUtc;
        requestReportHours(range, [this, requestedKey](bool ok) {
            if (!ok) return;
            if (reportHoursKey(currentReportRange()) != requestedKey) return;
            generateReport();
        }, true);
    }

    void generateReport() {
        if(!reportView)return;
        const auto* salesperson=activeSalesperson();
        if(!salesperson){reportView->showStatus("Vælg en sælger","Vælg en sælger for at se løn og salg i perioden.");return;}
        const auto range=currentReportRange();const auto dates=reportHoursDates(range);const QString requestedKey=reportHoursKey(range);const auto hoursEntry=reportHoursForRange(range);
        if(!hoursEntry.has_value()){
            reportView->showStatus("Henter timer","Intramanager henter timer for "+intramanagerPeriodLabel(dates.first,dates.second)+".","Rapporten opdateres automatisk, når timerne er klar.");
            requestReportHours(range,[this,requestedKey](bool ok){
                if(reportHoursKey(currentReportRange())!=requestedKey)return;
                if(ok)generateReport();else reportView->showStatus("Timerne kunne ikke hentes","Kontrollér din Intramanager-forbindelse under Indstillinger → Intramanager.","Vælg perioden igen for at prøve på ny.");
            });return;
        }
        refreshCachedReportHoursIfNeeded(range,requestedKey);
        const auto metrics=CommissionEngine::calculate(repo,salesperson->id,range.from,range.to,reportDayBonusPeriod(range));
        reportView->present(repo,*salesperson,range.label,metrics,hoursEntry->hours,intramanagerPeriodLabel(dates.first,dates.second),reportSalaryBreakdown(range,*salesperson,*hoursEntry),range.paymentMonth.isValid()?"Udbetales "+payoutDateLabel(range.paymentMonth):QString("Løn for den valgte periode"));
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (masterRegistrationRunning) {
            QMessageBox::information(this, "Salgsregistrering", "Vent, til registreringen i Chrome er afsluttet.");
            event->ignore();
            return;
        }
        if (repo.cloudPersistenceEnabled && (cloudSaveDirty || cloudSaveInFlight || cloudSaveAgain)) {
            flushCloudSaveSync();
        }
        QMainWindow::closeEvent(event);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    if (app.arguments().contains("--version")) {
        QTextStream(stdout) << APP_VERSION << "\n";
        return 0;
    }

    app.setWindowIcon(QIcon(":/settings/provi-icon.png"));
    applyMidnightPalette(app);
    app.setStyleSheet(premiumStyle());
    MainWindow w;
    if (w.startupWasAborted()) {
        return 0;
    }
    w.show();

    const QString visualReviewDir = qEnvironmentVariable("PROVI_VISUAL_REVIEW_DIR");
    if (!visualReviewDir.isEmpty()) {
        QTimer::singleShot(500, &w, [&w, visualReviewDir]() {
            w.captureVisualReviewAndQuit(visualReviewDir);
        });
    } else {
        QTimer::singleShot(1500, &app, []() {
            initAutoUpdate();
        });
    }

    return app.exec();
}
