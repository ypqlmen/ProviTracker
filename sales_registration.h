#pragma once
#include "domain.h"

// Kept with the order so retries use the originally registered products,
// not a later version of the product catalogue.
static QJsonObject makeSalesRegistration(const Order& order, const QVector<Product>& products) {
    QJsonArray items;
    for (const auto& item : order.items) {
        QString name = item.productKey;
        for (const auto& product : products) {
            if (product.key == item.productKey) { name = product.displayName; break; }
        }
        items.append(QJsonObject{{"key", item.productKey}, {"productName", name}, {"quantity", item.quantity}});
    }
    return QJsonObject{
        {"source", "Provi Tracker"}, {"type", "sales_registration"}, {"schemaVersion", 2},
        {"date", order.createdAt.date().toString("dd.MM.yyyy")},
        {"sellerInitials", order.sellerInitials}, {"orderNumber", order.id},
        {"cvrNumber", order.cvrNumber}, {"companyName", order.companyName},
        {"phoneNumber", order.phoneNumber}, {"note", order.note}, {"items", items}
    };
}

static QString salesRegistrationStateText(const Order& order) {
    if (order.masterRegistrationState == "registered") return "Registreret i masterark";
    if (order.masterRegistrationState == "pending" || order.masterRegistrationState == "transferring") return "Afventer masterark";
    if (order.masterRegistrationState == "error") return "Kræver handling";
    if (order.masterRegistrationState == "changed") return "Ændret – kontrollér masterark";
    return "Ikke oprettet";
}

static bool isMasterWorkbookUrl(const QString& value) {
    const QUrl url(value);
    const QString host = url.host().toLower();
    return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty()
        && host.endsWith(".sharepoint.com") && !url.path().isEmpty();
}
