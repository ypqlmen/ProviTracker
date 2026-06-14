#pragma once

#include <QtCore>
#include <functional>
#include "domain.h"
#include "storage_paths.h"

// ============================================================
// Storage
// ============================================================

class Repository {
public:
    QVector<Salesperson> salespeople;
    QVector<Product> products;
    QVector<Order> orders;
    QVector<SickPayEntry> sickPayEntries;
    AppSettings settings;
    QJsonObject cloudSecrets;
    bool localPersistenceEnabled = true;
    bool cloudPersistenceEnabled = false;
    std::function<void()> cloudSaveRequested;

    QString baseDir() const {
        return appStorageDir();
    }
    QString snapshotDir() const {
        return baseDir() + "/snapshots";
    }
    QString reportDir() const {
        return baseDir() + "/reports";
    }

    void ensureDirs() const {
        QDir().mkpath(baseDir());
        QDir().mkpath(snapshotDir());
        QDir().mkpath(reportDir());
    }

    void load() {
        ensureDirs();
        salespeople = loadVector<Salesperson>(baseDir() + "/salespeople.json", [](const QJsonObject& o){ return fromSalespersonJson(o); });
        products = loadVector<Product>(baseDir() + "/products.json", [](const QJsonObject& o){ return fromProductJson(o); });
        orders = loadVector<Order>(baseDir() + "/orders.json", [](const QJsonObject& o){ return fromOrderJson(o); });
        sickPayEntries = loadVector<SickPayEntry>(baseDir() + "/sick_pay.json", [](const QJsonObject& o){ return fromSickPayEntryJson(o); });

        QFile f(baseDir() + "/settings.json");
        if (f.exists() && f.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(f.readAll());
            settings = fromSettingsJson(doc.object());
        }

        if (products.isEmpty()) {
            seedProducts();
            saveProducts();
        }

        const bool catalogChanged = migrateProductCatalog();
        if (catalogChanged) {
            saveProducts();
            saveOrders();
        }
    }

    void saveAll() {
        saveSalespeople();
        saveProducts();
        saveOrders();
        saveSickPayEntries();
        saveSettings();
    }

    void saveSalespeople() const {
        if (localPersistenceEnabled) {
            saveVector(baseDir() + "/salespeople.json", salespeople, [](const Salesperson& s){ return toJson(s); });
        }
        requestCloudSave();
    }
    void saveProducts() const {
        if (localPersistenceEnabled) {
            saveVector(baseDir() + "/products.json", products, [](const Product& p){ return toJson(p); });
        }
        requestCloudSave();
    }
    void saveOrders() const {
        if (localPersistenceEnabled) {
            saveVector(baseDir() + "/orders.json", orders, [](const Order& o){ return toJson(o); });
        }
        requestCloudSave();
    }
    void saveSickPayEntries() const {
        if (localPersistenceEnabled) {
            saveVector(baseDir() + "/sick_pay.json", sickPayEntries, [](const SickPayEntry& e){ return toJson(e); });
        }
        requestCloudSave();
    }
    void saveSettings() const {
        if (localPersistenceEnabled) {
            QFile f(baseDir() + "/settings.json");
            if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(toJson(settings)).toJson(QJsonDocument::Indented));
        }
        requestCloudSave();
    }

    QJsonObject cloudPayload() const {
        QJsonArray salespeopleJson;
        for (const auto& s : salespeople) {
            salespeopleJson.append(toJson(s));
        }

        QJsonArray productsJson;
        for (const auto& p : products) {
            productsJson.append(toJson(p));
        }

        QJsonArray ordersJson;
        for (const auto& o : orders) {
            ordersJson.append(toJson(o));
        }

        QJsonArray sickPayJson;
        for (const auto& e : sickPayEntries) {
            sickPayJson.append(toJson(e));
        }

        QJsonObject payload;
        payload["settings"] = toJson(settings);
        payload["orders"] = ordersJson;
        payload["sickPayEntries"] = sickPayJson;
        payload["products"] = productsJson;
        payload["salesperson"] = salespeopleJson.isEmpty() ? QJsonObject() : salespeopleJson.at(0).toObject();
        payload["secrets"] = cloudSecrets;
        return payload;
    }

    void applyCloudPayload(const QJsonObject& payload, const QString& username) {
        settings = fromSettingsJson(payload.value("settings").toObject());
        cloudSecrets = payload.value("secrets").toObject();

        orders.clear();
        for (const auto& v : payload.value("orders").toArray()) {
            orders.push_back(fromOrderJson(v.toObject()));
        }

        sickPayEntries.clear();
        for (const auto& v : payload.value("sickPayEntries").toArray()) {
            sickPayEntries.push_back(fromSickPayEntryJson(v.toObject()));
        }

        products.clear();
        for (const auto& v : payload.value("products").toArray()) {
            products.push_back(fromProductJson(v.toObject()));
        }
        if (products.isEmpty()) {
            seedProducts();
        }

        salespeople.clear();
        const QJsonObject salespersonJson = payload.value("salesperson").toObject();
        Salesperson seller = fromSalespersonJson(salespersonJson);
        if (seller.id.trimmed().isEmpty()) {
            seller.id = cloudSalespersonId(username);
        }
        if (seller.name.trimmed().isEmpty()) {
            seller.name = username.trimmed();
        }
        salespeople.push_back(seller);

        normalizeForCloudUser(username);

        if (migrateProductCatalog()) {
            requestCloudSave();
        }
    }

    void normalizeForCloudUser(const QString& username) {
        const QString sellerId = cloudSalespersonId(username);
        const QString sellerName = username.trimmed().isEmpty() ? "Bruger" : username.trimmed();

        if (salespeople.isEmpty()) {
            salespeople.push_back({sellerId, sellerName});
        } else {
            salespeople = {{sellerId, sellerName}};
        }

        settings.activeSalespersonId = sellerId;

        for (auto& order : orders) {
            order.salespersonId = sellerId;
        }

        for (auto& entry : sickPayEntries) {
            entry.salespersonId = sellerId;
        }
    }

    static QString cloudSalespersonId(const QString& username) {
        const QByteArray hash = QCryptographicHash::hash(
            username.trimmed().toUtf8().toLower(),
            QCryptographicHash::Sha1
            ).toHex();
        return "cloud-" + QString::fromLatin1(hash.left(16));
    }

    const Product* findProduct(const QString& key) const {
        for (const auto& p : products) if (p.key == key) return &p;
        return nullptr;
    }

    Product* findProduct(const QString& key) {
        for (auto& p : products) if (p.key == key) return &p;
        return nullptr;
    }

    const Salesperson* findSalesperson(const QString& id) const {
        for (const auto& s : salespeople) if (s.id == id) return &s;
        return nullptr;
    }

    void seedProducts() {
        products = {
            {"mobil_10gb_0_36", "Mobil 10GB 0/36mdr", "Mobil", 1.25, CountMode::Both, true},
            {"mobil_25gb_36", "Mobil 25GB 36mdr", "Mobil", 2.0, CountMode::Both, true},
            {"mobil_50gb_36", "Mobil 50GB 36mdr", "Mobil", 2.5, CountMode::Both, true},
            {"mobil_50gb_36_term", "Mobil 50GB 36mdr + term", "Mobil", 2.0, CountMode::Voice, true},
            {"mobil_50gb_24", "Mobil 50GB 24mdr", "Mobil", 2.5, CountMode::Both, true},
            {"mobil_50gb_12", "Mobil 50GB 12mdr", "Mobil", 2.0, CountMode::Both, true},
            {"mobil_50gb_0", "Mobil 50GB 0mdr", "Mobil", 1.5, CountMode::Both, true},
            {"mobil_200gb_36", "Mobil 200GB 36mdr", "Mobil", 4.25, CountMode::Both, true},
            {"mobil_200gb_36_term", "Mobil 200GB 36mdr + term", "Mobil", 3.25, CountMode::Voice, true},
            {"mobil_200gb_24", "Mobil 200GB 24mdr", "Mobil", 3.25, CountMode::Both, true},
            {"mobil_200gb_12", "Mobil 200GB 12mdr", "Mobil", 2.5, CountMode::Both, true},
            {"mobil_200gb_0", "Mobil 200GB 0mdr", "Mobil", 2.0, CountMode::Both, true},
            {"mobil_1000gb_36", "Mobil 1000GB 36mdr", "Mobil", 4.25, CountMode::Both, true},
            {"mobil_1000gb_36_term", "Mobil 1000GB 36mdr + term", "Mobil", 4.25, CountMode::Voice, true},
            {"mobil_1000gb_24", "Mobil 1000GB 24mdr", "Mobil", 4.25, CountMode::Both, true},
            {"mobil_1000gb_12", "Mobil 1000GB 12mdr", "Mobil", 3.25, CountMode::Both, true},
            {"mobil_1000gb_0", "Mobil 1000GB 0mdr", "Mobil", 2.5, CountMode::Both, true},
            {"til_go_world", "Tillæg GoWorld", "Tillæg", 0.5, CountMode::None, false},
            {"til_true_talk_kollega", "Tillæg TrueTalk Kollega", "Tillæg", 0.5, CountMode::None, false},
            {"til_true_talk_firma", "Tillæg TrueTalk Firma/Agent", "Tillæg", 2.0, CountMode::None, false},
            {"til_1000gb_data", "Tillæg 1000GB data", "Tillæg", 0.5, CountMode::None, false},
            {"til_datakort", "Tillæg Datakort", "Tillæg", 0.5, CountMode::None, false},
            {"til_service", "Tillæg Service", "Tillæg", 0.5, CountMode::None, false},
            {"mbb_20_0", "Mobilt bredbånd 20GB 0mdr", "Mobilt bredbånd", 2.0, CountMode::None, true},
            {"mbb_20_hw_6", "Mobilt bredbånd 20GB + hardware 6mdr", "Mobilt bredbånd", 1.5, CountMode::None, true},
            {"mbb_50_0", "Mobilt bredbånd 50GB 0mdr", "Mobilt bredbånd", 2.0, CountMode::None, true},
            {"mbb_50_hw_6", "Mobilt bredbånd 50GB + hardware 6mdr", "Mobilt bredbånd", 2.5, CountMode::None, true},
            {"mbb_200_0", "Mobilt bredbånd 200GB 0mdr", "Mobilt bredbånd", 3.25, CountMode::None, true},
            {"mbb_200_hw_6", "Mobilt bredbånd 200GB + hardware 6mdr", "Mobilt bredbånd", 4.25, CountMode::None, true},
            {"mbb_1000_0", "Mobilt bredbånd 1000GB 0mdr", "Mobilt bredbånd", 4.25, CountMode::None, true},
            {"mbb_1000_hw_6", "Mobilt bredbånd 1000GB + hardware 6mdr", "Mobilt bredbånd", 4.25, CountMode::None, true},
            {"fwa_fri_12", "FWA 5G Fridata 12mdr", "FWA", 3.25, CountMode::Voice, true},
            {"fwa_fri_36", "FWA 5G Fridata 36mdr", "FWA", 2.5, CountMode::Voice, true},
            {"fiber_12", "Fiber 12mdr", "Fiber", 1.5, CountMode::None, true}
        };
    }

    bool migrateProductCatalog() {
        bool changed = false;
        constexpr const char* oldCheapDataKey = "mobil_1000plus_billig_36";
        constexpr const char* dataAddonKey = "til_1000gb_data";

        if (Product* trueTalk = findProduct("til_true_talk_firma")) {
            if (trueTalk->displayName != "Tillæg TrueTalk Firma/Agent"
                || trueTalk->points != 2.0
                || trueTalk->countsAsSale) {
                trueTalk->displayName = "Tillæg TrueTalk Firma/Agent";
                trueTalk->points = 2.0;
                trueTalk->countMode = CountMode::None;
                trueTalk->countsAsSale = false;
                changed = true;
            }
        }

        if (Product* dataAddon = findProduct(dataAddonKey)) {
            if (dataAddon->displayName != "Tillæg 1000GB data"
                || dataAddon->category != "Tillæg"
                || dataAddon->points != 0.5
                || dataAddon->countMode != CountMode::None
                || dataAddon->countsAsSale) {
                dataAddon->displayName = "Tillæg 1000GB data";
                dataAddon->category = "Tillæg";
                dataAddon->points = 0.5;
                dataAddon->countMode = CountMode::None;
                dataAddon->countsAsSale = false;
                changed = true;
            }
        } else {
            products.push_back({dataAddonKey, "Tillæg 1000GB data", "Tillæg", 0.5, CountMode::None, false});
            changed = true;
        }

        for (int i = products.size() - 1; i >= 0; --i) {
            if (products[i].key == oldCheapDataKey) {
                products.removeAt(i);
                changed = true;
            }
        }

        for (auto& order : orders) {
            for (auto& item : order.items) {
                if (item.productKey == oldCheapDataKey) {
                    item.productKey = dataAddonKey;
                    changed = true;
                }
            }
        }

        return changed;
    }

private:
    void requestCloudSave() const {
        if (cloudPersistenceEnabled && cloudSaveRequested) {
            cloudSaveRequested();
        }
    }

    template <typename T, typename Fn>
    QVector<T> loadVector(const QString& path, Fn fn) {
        QVector<T> out;
        QFile f(path);
        if (!f.exists() || !f.open(QIODevice::ReadOnly)) return out;
        const auto doc = QJsonDocument::fromJson(f.readAll());
        for (const auto& v : doc.array()) out.push_back(fn(v.toObject()));
        return out;
    }

    template <typename T, typename Fn>
    void saveVector(const QString& path, const QVector<T>& values, Fn fn) const {
        QJsonArray arr;
        for (const auto& v : values) arr.append(fn(v));
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
};
