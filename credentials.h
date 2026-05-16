#pragma once

#include <QtCore>
#include <string>
#ifdef Q_OS_WIN
#include <windows.h>
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

static bool saveIntramanagerPassword(const QString& username, const QString& password) {
#ifdef Q_OS_WIN
    if (username.trimmed().isEmpty() || password.isEmpty()) {
        return false;
    }

    const std::wstring user = qStringToWString(username);
    const std::wstring pass = qStringToWString(password);

    CREDENTIALW cred;
    ZeroMemory(&cred, sizeof(cred));

    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(INTRAMANAGER_CREDENTIAL_TARGET);
    cred.UserName = const_cast<LPWSTR>(user.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(pass.size() * sizeof(wchar_t));
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(pass.c_str()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    return CredWriteW(&cred, 0) == TRUE;
#else
    Q_UNUSED(username);
    Q_UNUSED(password);
    return false;
#endif
}

static bool loadIntramanagerPassword(QString* usernameOut, QString* passwordOut) {
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
