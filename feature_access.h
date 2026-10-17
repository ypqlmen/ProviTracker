#pragma once
#include <QString>

// This name comes from a successful cloud login/session validation, never from
// an editable salesperson display name or settings payload.
inline bool canUseBusinessContactSearch(const QString& authenticatedUsername, bool authenticated) {
    return authenticated && authenticatedUsername.trimmed().compare("VictorTang",Qt::CaseInsensitive)==0;
}
