#pragma once

#include <QtCore>

enum class CountMode {
    None,
    Voice,
    Simo,
    Both
};

static QString countModeToString(CountMode mode) {
    switch (mode) {
        case CountMode::None: return "NONE";
        case CountMode::Voice: return "VOICE";
        case CountMode::Simo: return "SIMO";
        case CountMode::Both: return "BOTH";
    }
    return "NONE";
}

static CountMode stringToCountMode(const QString& s) {
    const auto v = s.trimmed().toUpper();
    if (v == "VOICE") return CountMode::Voice;
    if (v == "SIMO") return CountMode::Simo;
    if (v == "BOTH") return CountMode::Both;
    return CountMode::None;
}


static QString countModeBadge(CountMode mode) {
    switch (mode) {
        case CountMode::None: return "-";
        case CountMode::Voice: return "VOICE";
        case CountMode::Simo: return "SIMO";
        case CountMode::Both: return "SIMO+VOICE";
    }
    return "-";
}

static bool isAddOnCategory(const QString& category) {
    return category.trimmed().compare("Tillæg", Qt::CaseInsensitive) == 0;
}

struct Product {
    QString key;
    QString displayName;
    QString category;
    double points = 0.0;
    CountMode countMode = CountMode::None;
    bool countsAsSale = true;
};

struct OrderItem {
    QString productKey;
    int quantity = 1;
};

struct Order {
    QString id;
    QString salespersonId;
    QString sellerInitials;
    QString cvrNumber;
    QString companyName;
    QString phoneNumber;
    QDateTime createdAt;
    QVector<OrderItem> items;
    QString note;
};

struct Salesperson {
    QString id;
    QString name;
};

struct MonthlyRateTier {
    int threshold = 0;
    double ratePerPoint = 0.0;
};

struct BonusSettings {
    double dayBonusPerPoint = 50.0;
    QVector<MonthlyRateTier> monthlyRateTiers {
        {100, 25.0},
        {125, 35.0},
        {150, 50.0},
        {175, 55.0},
        {200, 60.0},
        {250, 70.0},
        {300, 80.0}
    };

    int simoMinEligible = 5;
    double simoPayoutPerUnit = 200.0;

    int voiceMinEligible = 20;
    double voicePayoutPerUnit = 200.0;

    double monthlyTargetPoints = 100.0;
};

struct IntramanagerHoursEntry {
    QString fromDate;
    QString toDate;
    double hours = 0.0;
    double phoneHours = 0.0;
    QString syncedAt;
};

struct IntramanagerPunchState {
    bool known = false;
    bool clockedIn = false;
    QString statusText;
    QString detail;
    QString lastStart;
    QString lastStop;
    QString syncedAt;
};

struct AppSettings {
    QString activeSalespersonId;
    BonusSettings bonus;
    QString lastClosedMonthKey;
    int monthlySalesTarget = 0;

    double hourlyRate = 0.0;
    double taxDeduction = 0.0;
    double taxRatePercent = 0.0;
    QString intramanagerUsername;
    bool intramanagerEnabled = false;

    QString defaultSellerInitials;
    QString salesRegistrationWebhookUrl;
    QString salesRegistrationRecipient;
    bool salesRegistrationEnabled = false;

    double lastIntramanagerHours = 0.0;
    QString lastIntramanagerPeriodFrom;
    QString lastIntramanagerPeriodTo;
    QString lastIntramanagerSyncAt;

    QMap<QString, IntramanagerHoursEntry> intramanagerHoursCache;
    IntramanagerPunchState intramanagerPunch;
};

struct Metrics {
    int totalOrders = 0;
    int totalUnits = 0;
    int salesCount = 0;
    int coreProductCount = 0;
    int addOnCount = 0;
    int simoCount = 0;
    int voiceCount = 0;
    double totalPoints = 0.0;

    QMap<QString, int> quantityByProduct;
    QMap<QString, int> quantityByCategory;
    QMap<QString, double> pointsByProduct;
    QMap<QString, double> pointsByDay;
    QMap<QString, double> commissionByDay;

    double dayBonus = 0.0;
    double monthlyBonus = 0.0;
    double simoBonus = 0.0;
    double voiceBonus = 0.0;
    double totalCommission = 0.0;
};

static QJsonObject toJson(const Product& p) {
    return {
        {"key", p.key},
        {"displayName", p.displayName},
        {"category", p.category},
        {"points", p.points},
        {"countMode", countModeToString(p.countMode)},
        {"countsAsSale", p.countsAsSale}
    };
}

static Product fromProductJson(const QJsonObject& o) {
    Product p;
    p.key = o["key"].toString();
    p.displayName = o["displayName"].toString();
    p.category = o["category"].toString();
    p.points = o["points"].toDouble();
    p.countMode = stringToCountMode(o["countMode"].toString());
    p.countsAsSale = o.value("countsAsSale").toBool(true);
    return p;
}

static QJsonObject toJson(const OrderItem& i) {
    return {
        {"productKey", i.productKey},
        {"quantity", i.quantity}
    };
}

static OrderItem fromOrderItemJson(const QJsonObject& o) {
    OrderItem i;
    i.productKey = o["productKey"].toString();
    i.quantity = o["quantity"].toInt(1);
    return i;
}

static QJsonObject toJson(const Order& o) {
    QJsonArray items;
    for (const auto& item : o.items) items.append(toJson(item));
    return {
        {"id", o.id},
        {"salespersonId", o.salespersonId},
        {"sellerInitials", o.sellerInitials},
        {"cvrNumber", o.cvrNumber},
        {"companyName", o.companyName},
        {"phoneNumber", o.phoneNumber},
        {"createdAt", o.createdAt.toString(Qt::ISODate)},
        {"items", items},
        {"note", o.note}
    };
}

static Order fromOrderJson(const QJsonObject& o) {
    Order order;
    order.id = o["id"].toString();
    order.salespersonId = o["salespersonId"].toString();
    order.sellerInitials = o.value("sellerInitials").toString();
    order.cvrNumber = o.value("cvrNumber").toString();
    order.companyName = o.value("companyName").toString();
    order.phoneNumber = o.value("phoneNumber").toString();
    order.createdAt = QDateTime::fromString(o["createdAt"].toString(), Qt::ISODate);
    for (const auto& v : o["items"].toArray()) order.items.push_back(fromOrderItemJson(v.toObject()));
    order.note = o["note"].toString();
    return order;
}

static QJsonObject toJson(const Salesperson& s) {
    return {{"id", s.id}, {"name", s.name}};
}

static Salesperson fromSalespersonJson(const QJsonObject& o) {
    Salesperson s;
    s.id = o["id"].toString();
    s.name = o["name"].toString();
    return s;
}

static QJsonObject toJson(const MonthlyRateTier& t) {
    return {{"threshold", t.threshold}, {"ratePerPoint", t.ratePerPoint}};
}

static MonthlyRateTier fromTierJson(const QJsonObject& o) {
    MonthlyRateTier t;
    t.threshold = o["threshold"].toInt();
    t.ratePerPoint = o["ratePerPoint"].toDouble();
    return t;
}

static QJsonObject toJson(const BonusSettings& b) {
    QJsonArray tiers;
    for (const auto& t : b.monthlyRateTiers) tiers.append(toJson(t));
    return {
        {"dayBonusPerPoint", b.dayBonusPerPoint},
        {"monthlyRateTiers", tiers},
        {"simoMinEligible", b.simoMinEligible},
        {"simoPayoutPerUnit", b.simoPayoutPerUnit},
        {"voiceMinEligible", b.voiceMinEligible},
        {"voicePayoutPerUnit", b.voicePayoutPerUnit},
        {"monthlyTargetPoints", b.monthlyTargetPoints}
    };
}

static BonusSettings fromBonusJson(const QJsonObject& o) {
    BonusSettings b;
    b.dayBonusPerPoint = o.value("dayBonusPerPoint").toDouble(50.0);
    b.monthlyRateTiers.clear();
    for (const auto& v : o["monthlyRateTiers"].toArray()) b.monthlyRateTiers.push_back(fromTierJson(v.toObject()));
    if (b.monthlyRateTiers.isEmpty()) {
        b.monthlyRateTiers = {{100, 25.0}, {125, 35.0}, {150, 50.0}, {175, 55.0}, {200, 60.0}, {250, 70.0}, {300, 80.0}};
    }
    b.simoMinEligible = o.value("simoMinEligible").toInt(5);
    b.simoPayoutPerUnit = o.value("simoPayoutPerUnit").toDouble(200.0);
    b.voiceMinEligible = o.value("voiceMinEligible").toInt(20);
    b.voicePayoutPerUnit = o.value("voicePayoutPerUnit").toDouble(200.0);
    b.monthlyTargetPoints = o.value("monthlyTargetPoints").toDouble(100.0);
    return b;
}

static QJsonObject toJson(const IntramanagerHoursEntry& e);
static IntramanagerHoursEntry fromIntramanagerHoursJson(const QJsonObject& o);
static QJsonObject toJson(const IntramanagerPunchState& s);
static IntramanagerPunchState fromIntramanagerPunchJson(const QJsonObject& o);

static QJsonObject toJson(const IntramanagerHoursEntry& e) {
    return {
        {"fromDate", e.fromDate},
        {"toDate", e.toDate},
        {"hours", e.hours},
        {"phoneHours", e.phoneHours},
        {"syncedAt", e.syncedAt}
    };
}

static IntramanagerHoursEntry fromIntramanagerHoursJson(const QJsonObject& o) {
    IntramanagerHoursEntry e;
    e.fromDate = o.value("fromDate").toString();
    e.toDate = o.value("toDate").toString();
    e.hours = o.value("hours").toDouble(0.0);
    e.phoneHours = o.value("phoneHours").toDouble(0.0);
    e.syncedAt = o.value("syncedAt").toString();
    return e;
}

static QJsonObject toJson(const IntramanagerPunchState& s) {
    return {
        {"known", s.known},
        {"clockedIn", s.clockedIn},
        {"statusText", s.statusText},
        {"detail", s.detail},
        {"lastStart", s.lastStart},
        {"lastStop", s.lastStop},
        {"syncedAt", s.syncedAt}
    };
}

static IntramanagerPunchState fromIntramanagerPunchJson(const QJsonObject& o) {
    IntramanagerPunchState s;
    s.known = o.value("known").toBool(false);
    s.clockedIn = o.value("clockedIn").toBool(false);
    s.statusText = o.value("statusText").toString();
    s.detail = o.value("detail").toString();
    s.lastStart = o.value("lastStart").toString();
    s.lastStop = o.value("lastStop").toString();
    s.syncedAt = o.value("syncedAt").toString();
    return s;
}

static QJsonObject toJson(const AppSettings& s) {
    QJsonObject hoursCacheJson;

    for (auto it = s.intramanagerHoursCache.begin(); it != s.intramanagerHoursCache.end(); ++it) {
        hoursCacheJson[it.key()] = toJson(it.value());
    }

    return {
        {"activeSalespersonId", s.activeSalespersonId},
        {"bonus", toJson(s.bonus)},
        {"lastClosedMonthKey", s.lastClosedMonthKey},
        {"monthlySalesTarget", s.monthlySalesTarget},
        {"hourlyRate", s.hourlyRate},
        {"taxDeduction", s.taxDeduction},
        {"taxRatePercent", s.taxRatePercent},
        {"intramanagerUsername", s.intramanagerUsername},
        {"intramanagerEnabled", s.intramanagerEnabled},
        {"defaultSellerInitials", s.defaultSellerInitials},
        {"salesRegistrationWebhookUrl", s.salesRegistrationWebhookUrl},
        {"salesRegistrationRecipient", s.salesRegistrationRecipient},
        {"salesRegistrationEnabled", s.salesRegistrationEnabled},
        {"lastIntramanagerHours", s.lastIntramanagerHours},
        {"lastIntramanagerPeriodFrom", s.lastIntramanagerPeriodFrom},
        {"lastIntramanagerPeriodTo", s.lastIntramanagerPeriodTo},
        {"lastIntramanagerSyncAt", s.lastIntramanagerSyncAt},
        {"intramanagerHoursCache", hoursCacheJson},
        {"intramanagerPunch", toJson(s.intramanagerPunch)}
    };
}

static AppSettings fromSettingsJson(const QJsonObject& o) {
    AppSettings s;
    s.activeSalespersonId = o["activeSalespersonId"].toString();
    s.bonus = fromBonusJson(o["bonus"].toObject());
    s.lastClosedMonthKey = o["lastClosedMonthKey"].toString();
    s.monthlySalesTarget = o.value("monthlySalesTarget").toInt(0);
    s.hourlyRate = o.value("hourlyRate").toDouble(0.0);
    s.taxDeduction = o.value("taxDeduction").toDouble(0.0);
    s.taxRatePercent = o.value("taxRatePercent").toDouble(0.0);
    s.intramanagerUsername = o.value("intramanagerUsername").toString();
    s.intramanagerEnabled = o.value("intramanagerEnabled").toBool(false);
    s.defaultSellerInitials = o.value("defaultSellerInitials").toString();
    s.salesRegistrationWebhookUrl = o.value("salesRegistrationWebhookUrl").toString();
    if (s.salesRegistrationWebhookUrl.isEmpty()) {
        s.salesRegistrationWebhookUrl = o.value("salesMasterWorkbookPath").toString();
    }
    s.salesRegistrationRecipient = o.value("salesRegistrationRecipient").toString();
    s.salesRegistrationEnabled = o.value("salesRegistrationEnabled").toBool(false);
    s.lastIntramanagerHours = o.value("lastIntramanagerHours").toDouble(0.0);
    s.lastIntramanagerPeriodFrom = o.value("lastIntramanagerPeriodFrom").toString();
    s.lastIntramanagerPeriodTo = o.value("lastIntramanagerPeriodTo").toString();
    s.lastIntramanagerSyncAt = o.value("lastIntramanagerSyncAt").toString();

    const QJsonObject hoursCacheJson = o.value("intramanagerHoursCache").toObject();

    for (auto it = hoursCacheJson.begin(); it != hoursCacheJson.end(); ++it) {
        s.intramanagerHoursCache[it.key()] = fromIntramanagerHoursJson(it.value().toObject());
    }

    s.intramanagerPunch = fromIntramanagerPunchJson(o.value("intramanagerPunch").toObject());

    return s;
}
