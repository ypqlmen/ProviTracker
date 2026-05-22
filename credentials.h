#pragma once

#include <QtCore>
#include <string>
#include "storage_paths.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <wincred.h>
#endif

static const wchar_t* INTRAMANAGER_CREDENTIAL_TARGET = L"ProviTracker.Intramanager";
static const wchar_t* MICROSOFT_OAUTH_CREDENTIAL_TARGET = L"ProviTracker.MicrosoftOAuth";

static std::wstring qStringToWString(const QString& s) {
    return std::wstring(reinterpret_cast<const wchar_t*>(s.utf16()), s.size());
}

static QString wStringToQString(const std::wstring& s) {
    return QString::fromWCharArray(s.c_str(), static_cast<int>(s.size()));
}

static QString intramanagerPasswordStorePath() {
    return appStorageDir() + "/intramanager_login.json";
}

static QByteArray intramanagerPasswordEntropy() {
    return QByteArrayLiteral("ProviTracker.Intramanager.Password.v1");
}

static QByteArray cloudSessionEntropy() {
    return QByteArrayLiteral("ProviTracker.Cloud.Session.v1");
}

static QString cloudSessionStorePath() {
    return appStorageDir() + "/cloud_session.json";
}

static bool protectForCurrentWindowsUserWithEntropy(
    const QByteArray& plainText,
    const QByteArray& entropy,
    const wchar_t* description,
    QByteArray* protectedDataOut
    ) {
#ifdef Q_OS_WIN
    if (plainText.isEmpty() || !protectedDataOut) {
        return false;
    }

    DATA_BLOB plainBlob;
    plainBlob.cbData = static_cast<DWORD>(plainText.size());
    plainBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plainText.constData()));

    DATA_BLOB entropyBlob;
    entropyBlob.cbData = static_cast<DWORD>(entropy.size());
    entropyBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()));

    DATA_BLOB protectedBlob;
    ZeroMemory(&protectedBlob, sizeof(protectedBlob));

    const BOOL ok = CryptProtectData(
        &plainBlob,
        description,
        &entropyBlob,
        nullptr,
        nullptr,
        CRYPTPROTECT_UI_FORBIDDEN,
        &protectedBlob
        );

    if (!ok) {
        return false;
    }

    *protectedDataOut = QByteArray(
        reinterpret_cast<const char*>(protectedBlob.pbData),
        static_cast<int>(protectedBlob.cbData)
        );
    LocalFree(protectedBlob.pbData);
    return true;
#else
    Q_UNUSED(plainText);
    Q_UNUSED(entropy);
    Q_UNUSED(description);
    Q_UNUSED(protectedDataOut);
    return false;
#endif
}

static bool unprotectForCurrentWindowsUserWithEntropy(
    const QByteArray& protectedData,
    const QByteArray& entropy,
    QByteArray* plainTextOut
    ) {
#ifdef Q_OS_WIN
    if (protectedData.isEmpty() || !plainTextOut) {
        return false;
    }

    DATA_BLOB protectedBlob;
    protectedBlob.cbData = static_cast<DWORD>(protectedData.size());
    protectedBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(protectedData.constData()));

    DATA_BLOB entropyBlob;
    entropyBlob.cbData = static_cast<DWORD>(entropy.size());
    entropyBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()));

    DATA_BLOB plainBlob;
    ZeroMemory(&plainBlob, sizeof(plainBlob));

    const BOOL ok = CryptUnprotectData(
        &protectedBlob,
        nullptr,
        &entropyBlob,
        nullptr,
        nullptr,
        CRYPTPROTECT_UI_FORBIDDEN,
        &plainBlob
        );

    if (!ok) {
        return false;
    }

    *plainTextOut = QByteArray(
        reinterpret_cast<const char*>(plainBlob.pbData),
        static_cast<int>(plainBlob.cbData)
        );
    LocalFree(plainBlob.pbData);
    return true;
#else
    Q_UNUSED(protectedData);
    Q_UNUSED(entropy);
    Q_UNUSED(plainTextOut);
    return false;
#endif
}

static bool protectForCurrentWindowsUser(const QByteArray& plainText, QByteArray* protectedDataOut) {
    return protectForCurrentWindowsUserWithEntropy(
        plainText,
        intramanagerPasswordEntropy(),
        L"ProviTracker Intramanager password",
        protectedDataOut
        );
}

static bool unprotectForCurrentWindowsUser(const QByteArray& protectedData, QByteArray* plainTextOut) {
    return unprotectForCurrentWindowsUserWithEntropy(
        protectedData,
        intramanagerPasswordEntropy(),
        plainTextOut
        );
}

static void deleteLegacyIntramanagerCredential() {
#ifdef Q_OS_WIN
    CredDeleteW(INTRAMANAGER_CREDENTIAL_TARGET, CRED_TYPE_GENERIC, 0);
#endif
}

static bool loadLegacyIntramanagerCredential(QString* usernameOut, QString* passwordOut) {
#ifdef Q_OS_WIN
    PCREDENTIALW cred = nullptr;

    if (!CredReadW(INTRAMANAGER_CREDENTIAL_TARGET, CRED_TYPE_GENERIC, 0, &cred)) {
        return false;
    }

    if (usernameOut) {
        *usernameOut = QString::fromWCharArray(cred->UserName);
    }

    if (passwordOut && cred->CredentialBlob && cred->CredentialBlobSize > 0) {
        const int wcharCount = static_cast<int>(cred->CredentialBlobSize / sizeof(wchar_t));
        *passwordOut = QString::fromWCharArray(
            reinterpret_cast<const wchar_t*>(cred->CredentialBlob),
            wcharCount
            );
    }

    CredFree(cred);
    return true;
#else
    Q_UNUSED(usernameOut);
    Q_UNUSED(passwordOut);
    return false;
#endif
}

static bool saveIntramanagerPassword(const QString& username, const QString& password) {
    if (username.trimmed().isEmpty() || password.isEmpty()) {
        return false;
    }

    QByteArray protectedPassword;
    if (!protectForCurrentWindowsUser(password.toUtf8(), &protectedPassword)) {
        return false;
    }

    QDir().mkpath(appStorageDir());

    QJsonObject login;
    login["format"] = "dpapi-current-user-v1";
    login["username"] = username.trimmed();
    login["password"] = QString::fromLatin1(protectedPassword.toBase64());
    login["savedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSaveFile file(intramanagerPasswordStorePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(login).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return false;
    }

    deleteLegacyIntramanagerCredential();
    return true;
}

static bool loadStoredIntramanagerPassword(QString* usernameOut, QString* passwordOut) {
    QFile file(intramanagerPasswordStorePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonObject login = doc.object();
    if (login.value("format").toString() != "dpapi-current-user-v1") {
        return false;
    }

    const QByteArray protectedPassword = QByteArray::fromBase64(
        login.value("password").toString().toLatin1()
        );

    QByteArray plainPassword;
    if (!unprotectForCurrentWindowsUser(protectedPassword, &plainPassword)) {
        return false;
    }

    const QString password = QString::fromUtf8(plainPassword);
    if (password.isEmpty()) {
        return false;
    }

    if (usernameOut) {
        *usernameOut = login.value("username").toString();
    }
    if (passwordOut) {
        *passwordOut = password;
    }

    return true;
}

static bool loadIntramanagerPassword(QString* usernameOut, QString* passwordOut) {
    if (loadStoredIntramanagerPassword(usernameOut, passwordOut)) {
        return true;
    }

    QString legacyUsername;
    QString legacyPassword;
    if (!loadLegacyIntramanagerCredential(&legacyUsername, &legacyPassword) || legacyPassword.isEmpty()) {
        return false;
    }

    if (usernameOut) {
        *usernameOut = legacyUsername;
    }
    if (passwordOut) {
        *passwordOut = legacyPassword;
    }

    if (!legacyUsername.trimmed().isEmpty()) {
        saveIntramanagerPassword(legacyUsername, legacyPassword);
    }

    return true;
}

static bool saveCloudSession(const QString& username, const QString& token, const QString& expiresAt) {
    if (username.trimmed().isEmpty() || token.isEmpty()) {
        return false;
    }

    QByteArray protectedToken;
    if (!protectForCurrentWindowsUserWithEntropy(
            token.toUtf8(),
            cloudSessionEntropy(),
            L"ProviTracker cloud session",
            &protectedToken
            )) {
        return false;
    }

    QDir().mkpath(appStorageDir());

    QJsonObject session;
    session["format"] = "dpapi-current-user-cloud-v1";
    session["username"] = username.trimmed();
    session["token"] = QString::fromLatin1(protectedToken.toBase64());
    session["expiresAt"] = expiresAt;
    session["savedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSaveFile file(cloudSessionStorePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(session).toJson(QJsonDocument::Indented));
    return file.commit();
}

static bool loadCloudSession(QString* usernameOut, QString* tokenOut, QString* expiresAtOut = nullptr) {
    QFile file(cloudSessionStorePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonObject session = doc.object();
    if (session.value("format").toString() != "dpapi-current-user-cloud-v1") {
        return false;
    }

    const QByteArray protectedToken = QByteArray::fromBase64(
        session.value("token").toString().toLatin1()
        );

    QByteArray plainToken;
    if (!unprotectForCurrentWindowsUserWithEntropy(
            protectedToken,
            cloudSessionEntropy(),
            &plainToken
            )) {
        return false;
    }

    const QString token = QString::fromUtf8(plainToken);
    if (token.isEmpty()) {
        return false;
    }

    if (usernameOut) {
        *usernameOut = session.value("username").toString();
    }
    if (tokenOut) {
        *tokenOut = token;
    }
    if (expiresAtOut) {
        *expiresAtOut = session.value("expiresAt").toString();
    }

    return true;
}

static void deleteCloudSession() {
    QFile::remove(cloudSessionStorePath());
}

static bool saveMicrosoftRefreshToken(const QString& accountHint, const QString& refreshToken) {
#ifdef Q_OS_WIN
    if (refreshToken.isEmpty()) {
        return false;
    }

    const std::wstring account = qStringToWString(accountHint.trimmed());
    const std::wstring token = qStringToWString(refreshToken);

    CREDENTIALW cred;
    ZeroMemory(&cred, sizeof(cred));

    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(MICROSOFT_OAUTH_CREDENTIAL_TARGET);
    cred.UserName = const_cast<LPWSTR>(account.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(token.size() * sizeof(wchar_t));
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(token.c_str()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    return CredWriteW(&cred, 0) == TRUE;
#else
    Q_UNUSED(accountHint);
    Q_UNUSED(refreshToken);
    return false;
#endif
}

static bool loadMicrosoftRefreshToken(QString* accountHintOut, QString* refreshTokenOut) {
#ifdef Q_OS_WIN
    PCREDENTIALW cred = nullptr;

    if (!CredReadW(MICROSOFT_OAUTH_CREDENTIAL_TARGET, CRED_TYPE_GENERIC, 0, &cred)) {
        return false;
    }

    if (accountHintOut && cred->UserName) {
        *accountHintOut = QString::fromWCharArray(cred->UserName);
    }

    if (refreshTokenOut && cred->CredentialBlob && cred->CredentialBlobSize > 0) {
        const int wcharCount = static_cast<int>(cred->CredentialBlobSize / sizeof(wchar_t));
        *refreshTokenOut = QString::fromWCharArray(
            reinterpret_cast<const wchar_t*>(cred->CredentialBlob),
            wcharCount
            );
    }

    CredFree(cred);
    return true;
#else
    Q_UNUSED(accountHintOut);
    Q_UNUSED(refreshTokenOut);
    return false;
#endif
}

static void deleteMicrosoftRefreshToken() {
#ifdef Q_OS_WIN
    CredDeleteW(MICROSOFT_OAUTH_CREDENTIAL_TARGET, CRED_TYPE_GENERIC, 0);
#endif
}
