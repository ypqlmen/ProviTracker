#include <QtWidgets>
#include <QtCore>
#include <QtPrintSupport>
#include <QtNetwork>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <QLibrary>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

// ============================================================
// Domain
// ============================================================

enum class CountMode {
    None,
    Voice,
    Simo,
    Both
};

static void initAutoUpdate()
{
    static QLibrary sparkle("WinSparkle");

    typedef void (*init_t)();
    typedef void (*set_url_t)(const char*);
    typedef void (*set_details_t)(const wchar_t*, const wchar_t*, const wchar_t*);

    if (!sparkle.load()) {
        return;
    }

    auto init = (init_t)sparkle.resolve("win_sparkle_init");
    auto set_url = (set_url_t)sparkle.resolve("win_sparkle_set_appcast_url");
    auto set_details = (set_details_t)sparkle.resolve("win_sparkle_set_app_details");

    if (init && set_url && set_details) {
        set_url("https://raw.githubusercontent.com/ypqlmen/ProviTracker/main/appcast.xml");
        set_details(L"Victor Tang", L"Provi Tracker", L"1.3.7");
        init();
    }
}

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

static void migrateLegacyDataIfNeeded() {
    const QString targetDir = appStorageDir();
    const QString legacyInstallDir = QCoreApplication::applicationDirPath() + "/data";
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
    copyLegacyDataFiles(legacyInstallDir, targetDir, files);
    copyLegacyDataFiles(legacyRoamingDir, targetDir, files);

    const QString marker = targetDir + "/.migrated";
    if (!QFileInfo::exists(marker)) {
        QFile f(marker);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("ok");
        }
    }
}

static QPair<QFrame*, QVBoxLayout*> createCard(const QString& title) {
    auto* frame = new QFrame;
    frame->setProperty("kpiCard", true);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title);
    titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    titleLabel->setStyleSheet("color:#9CC7E8; font-size:12px; font-weight:700;");

    layout->addWidget(titleLabel);

    return qMakePair(frame, layout);
}

class RoundedProgressBar : public QProgressBar {
public:
    using QProgressBar::QProgressBar;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outer = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal outerRadius = qMin<qreal>(outer.height() / 2.0, 12.0);

        painter.setPen(QPen(QColor("#2A3B5F"), 1.0));
        painter.setBrush(QColor("#0B1424"));
        painter.drawRoundedRect(outer, outerRadius, outerRadius);

        const int minValue = minimum();
        const int maxValue = maximum();
        const double ratio = maxValue > minValue
            ? qBound(0.0, double(value() - minValue) / double(maxValue - minValue), 1.0)
            : 0.0;

        if (ratio > 0.0) {
            QRectF fill = outer.adjusted(1.0, 1.0, -1.0, -1.0);
            fill.setWidth(qMax<qreal>(1.0, fill.width() * ratio));

            QColor fillColor = property("progressColor").value<QColor>();
            if (!fillColor.isValid()) fillColor = QColor("#14B8A6");

            const qreal fillRadius = qMin(fill.height() / 2.0, fill.width() / 2.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fillColor);
            painter.drawRoundedRect(fill, fillRadius, fillRadius);
        }

        QFont textFont = font();
        textFont.setBold(true);
        painter.setFont(textFont);
        painter.setPen(QColor("#F8FBFF"));
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
};

class RoundedTableWidget : public QTableWidget {
public:
    using QTableWidget::QTableWidget;

protected:
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
        painter->setBrush(QColor("#13203A"));

        const int visual = visualIndex(logicalIndex);
        const bool firstSection = visual == 0;
        const bool lastSection = visual == count() - 1;
        const QRectF bg = QRectF(rect).adjusted(0.0, 0.0, 0.0, 0.0);
        const qreal radius = 10.0;

        QPainterPath bgPath;
        bgPath.moveTo(firstSection ? bg.left() + radius : bg.left(), bg.top());
        if (lastSection) {
            bgPath.lineTo(bg.right() - radius, bg.top());
            bgPath.quadTo(bg.right(), bg.top(), bg.right(), bg.top() + radius);
            bgPath.lineTo(bg.right(), bg.bottom() - radius);
            bgPath.quadTo(bg.right(), bg.bottom(), bg.right() - radius, bg.bottom());
        } else {
            bgPath.lineTo(bg.right(), bg.top());
            bgPath.lineTo(bg.right(), bg.bottom());
        }
        if (firstSection) {
            bgPath.lineTo(bg.left() + radius, bg.bottom());
            bgPath.quadTo(bg.left(), bg.bottom(), bg.left(), bg.bottom() - radius);
            bgPath.lineTo(bg.left(), bg.top() + radius);
            bgPath.quadTo(bg.left(), bg.top(), bg.left() + radius, bg.top());
        } else {
            bgPath.lineTo(bg.left(), bg.bottom());
            bgPath.lineTo(bg.left(), bg.top());
        }
        bgPath.closeSubpath();
        painter->drawPath(bgPath);

        QFont headerFont = font();
        headerFont.setBold(true);
        headerFont.setPointSizeF(qMax<qreal>(9.0, headerFont.pointSizeF()));
        painter->setFont(headerFont);
        painter->setPen(QColor("#F8FBFF"));

        const QString text = model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString();
        const QRect textRect = rect.adjusted(10, 0, -10, 1);
        const QString elided = fontMetrics().elidedText(text, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, elided);
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

static void styleDataTable(QTableWidget* table, bool allowSelection = true)
{
    if (!table) return;

    table->setFrameShape(QFrame::NoFrame);
    table->setFocusPolicy(allowSelection ? Qt::ClickFocus : Qt::NoFocus);
    table->viewport()->setAutoFillBackground(false);
    table->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    table->horizontalHeader()->setFixedHeight(38);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionsClickable(false);

    table->setStyleSheet(QString(R"(
QTableWidget {
    background: #0B1424;
    border: 1px solid #1C2E4B;
    border-radius: 16px;
    padding: 0px;
    outline: 0;
    color: #EAF4FF;
    alternate-background-color: #12203A;
}
QTableWidget::viewport {
    background: transparent;
    border-radius: 16px;
}
QHeaderView::section {
    background: #13203A;
    color: #EAF4FF;
    border: none;
    padding: 8px 10px;
    font-weight: 800;
    min-height: 24px;
}
QTableCornerButton::section {
    background: #13203A;
    border: none;
}
QTableWidget::item {
    color: #EAF4FF;
    border: none;
    padding: 8px;
    min-height: 22px;
}
QTableWidget::item:selected {
    background: %1;
    color: %2;
}
)").arg(allowSelection ? "#14B8A6" : "transparent", allowSelection ? "#06202A" : "#EAF4FF"));
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

    card.first->setMinimumHeight(108);
    card.first->setMaximumHeight(126);
    card.first->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* valueLabel = new QLabel("-");
    valueLabel->setWordWrap(true);
    valueLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    valueLabel->setStyleSheet(
        "QLabel { "
        "color: #FFFFFF; "
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
    content->setStyleSheet("QLabel { color: #FFFFFF; font-size: 14px; font-weight: 600; background: transparent; }");
    card.second->addWidget(content);
    card.second->addStretch();
    return qMakePair(card.first, content);
}

static QFrame* createProgressCard(const QString& title, QProgressBar** barOut, QLabel** hintOut) {
    auto card = createCard(title);

    auto* bar = new RoundedProgressBar;
    bar->setRange(0, 100);
    bar->setMinimumHeight(26);
    bar->setMaximumHeight(26);
    bar->setProperty("progressColor", QColor("#14B8A6"));

    auto* hint = new QLabel("-");
    hint->setWordWrap(true);
    hint->setTextInteractionFlags(Qt::NoTextInteraction);
    hint->setStyleSheet("QLabel { color: #FFFFFF; font-size: 13px; background: transparent; }");

    card.second->addWidget(bar);
    card.second->addWidget(hint);

    *barOut = bar;
    *hintOut = hint;

    return card.first;
}

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

static const wchar_t* INTRAMANAGER_CREDENTIAL_TARGET = L"ProviTracker.Intramanager";

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

// ============================================================
// Storage
// ============================================================

class Repository {
public:
    QVector<Salesperson> salespeople;
    QVector<Product> products;
    QVector<Order> orders;
    AppSettings settings;

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
        saveSettings();
    }

    void saveSalespeople() const { saveVector(baseDir() + "/salespeople.json", salespeople, [](const Salesperson& s){ return toJson(s); }); }
    void saveProducts() const { saveVector(baseDir() + "/products.json", products, [](const Product& p){ return toJson(p); }); }
    void saveOrders() const { saveVector(baseDir() + "/orders.json", orders, [](const Order& o){ return toJson(o); }); }
    void saveSettings() const {
        QFile f(baseDir() + "/settings.json");
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(toJson(settings)).toJson(QJsonDocument::Indented));
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

// ============================================================
// Commission engine
// ============================================================

class CommissionEngine {
public:
    static double monthlyRatePerPoint(double points, const BonusSettings& b) {
        double best = 0.0;
        for (const auto& tier : b.monthlyRateTiers) {
            if (points >= tier.threshold) best = tier.ratePerPoint;
        }
        return best;
    }

    static double lockedStepBonus(int count, int minEligible, int stepSize, double payoutPerUnit) {
        if (count < minEligible) return 0.0;
        const int safeStep = qMax(1, stepSize);
        int lockedUnits = (count / safeStep) * safeStep;
        return lockedUnits * payoutPerUnit;
    }

    static double lockedFiveBonus(int count, int minEligible, double payoutPerUnit) {
        return lockedStepBonus(count, minEligible, 5, payoutPerUnit);
    }

    static double pointsOnly(
        const Repository& repo,
        const QString& salespersonId,
        const QDateTime& from,
        const QDateTime& to
        ) {
        double points = 0.0;

        QHash<QString, Product> productMap;
        for (const auto& p : repo.products) {
            productMap.insert(p.key, p);
        }

        for (const auto& order : repo.orders) {
            if (order.salespersonId != salespersonId) continue;
            if (order.createdAt < from || order.createdAt > to) continue;

            for (const auto& item : order.items) {
                if (!productMap.contains(item.productKey)) continue;
                const auto& p = productMap[item.productKey];
                points += p.points * item.quantity;
            }
        }

        return points;
    }

    static Metrics calculate(
        const Repository& repo,
        const QString& salespersonId,
        const QDateTime& from,
        const QDateTime& to,
        std::optional<QPair<QDateTime, QDateTime>> dayBonusRange = std::nullopt
        ) {
        Metrics m;
        QHash<QString, Product> productMap;
        for (const auto& p : repo.products) productMap.insert(p.key, p);

        for (const auto& order : repo.orders) {
            if (order.salespersonId != salespersonId) continue;
            if (order.createdAt < from || order.createdAt > to) continue;
            m.totalOrders++;
            const QString day = order.createdAt.date().toString("yyyy-MM-dd");

            for (const auto& item : order.items) {
                if (!productMap.contains(item.productKey)) continue;
                const auto& p = productMap[item.productKey];
                m.totalUnits += item.quantity;
                const double pts = p.points * item.quantity;
                m.totalPoints += pts;
                m.quantityByProduct[p.displayName] += item.quantity;
                m.quantityByCategory[p.category] += item.quantity;
                m.pointsByProduct[p.displayName] += pts;
                m.pointsByDay[day] += pts;

                if (isAddOnCategory(p.category)) m.addOnCount += item.quantity;
                else m.coreProductCount += item.quantity;

                if (p.countsAsSale) m.salesCount += item.quantity;
                if (p.countMode == CountMode::Simo || p.countMode == CountMode::Both) m.simoCount += item.quantity;
                if (p.countMode == CountMode::Voice || p.countMode == CountMode::Both) m.voiceCount += item.quantity;
            }
        }

        const auto& bonus = repo.settings.bonus;
        const double dayBonusPoints = dayBonusRange.has_value()
                                          ? pointsOnly(repo, salespersonId, dayBonusRange->first, dayBonusRange->second)
                                          : m.totalPoints;

        m.dayBonus = dayBonusPoints * bonus.dayBonusPerPoint;
        const double monthlyRate = monthlyRatePerPoint(m.totalPoints, bonus);
        m.monthlyBonus = m.totalPoints * monthlyRate;
        m.simoBonus = lockedStepBonus(m.simoCount, bonus.simoMinEligible, 5, bonus.simoPayoutPerUnit);
        m.voiceBonus = lockedStepBonus(m.voiceCount, bonus.voiceMinEligible, 10, bonus.voicePayoutPerUnit);
        m.totalCommission = m.dayBonus + m.monthlyBonus + m.simoBonus + m.voiceBonus;

        for (auto it = m.pointsByDay.begin(); it != m.pointsByDay.end(); ++it) {
            m.commissionByDay[it.key()] = it.value() * (bonus.dayBonusPerPoint + monthlyRate);
        }

        return m;
    }
};

static QString money(double v) {
    return QLocale(QLocale::Danish, QLocale::Denmark).toString(v, 'f', 2);
}

static QString monthKey(const QDate& d) {
    return QString("%1-%2").arg(d.year()).arg(d.month(), 2, 10, QLatin1Char('0'));
}

static QPair<QDateTime, QDateTime> monthRange(const QDate& date) {
    QDate first(date.year(), date.month(), 1);
    QDate last(date.year(), date.month(), date.daysInMonth());
    return {
        QDateTime(first, QTime(0,0,0)),
        QDateTime(last, QTime(23,59,59))
    };
}

static QPair<QDateTime, QDateTime> payrollBonusRange(const QDate& date) {
    QDate start;
    QDate end;

    if (date.day() <= 20) {
        start = QDate(date.year(), date.month(), 21).addMonths(-1);
        end = QDate(date.year(), date.month(), 20);
    } else {
        start = QDate(date.year(), date.month(), 21);
        end = QDate(date.year(), date.month(), 20).addMonths(1);
    }

    return {
        QDateTime(start, QTime(0, 0, 0)),
        QDateTime(end, QTime(23, 59, 59))
    };
}

static QPair<QDateTime, QDateTime> workWeekRange(const QDate& date) {
    const int dayOfWeek = date.dayOfWeek();
    const QDate monday = date.addDays(1 - dayOfWeek);
    const QDate sunday = monday.addDays(6);
    return {
        QDateTime(monday, QTime(0,0,0)),
        QDateTime(sunday, QTime(23,59,59))
    };
}

static QPair<QDateTime, QDateTime> previousAndCurrentWorkWeeksRange(const QDate& date) {
    const auto currentWeek = workWeekRange(date);
    const QDate currentMonday = currentWeek.first.date();
    const QDate previousMonday = currentMonday.addDays(-7);
    const QDate currentSunday = currentWeek.second.date();

    return {
        QDateTime(previousMonday, QTime(0,0,0)),
        QDateTime(currentSunday, QTime(23,59,59))
    };
}

static int nextLockedStep(int count, int minimumToActivate, int stepSize) {
    const int safeStep = qMax(1, stepSize);
    if (count < minimumToActivate) return minimumToActivate;
    return ((count / safeStep) + 1) * safeStep;
}

static int nextLockedFiveStep(int count, int minimumToActivate) {
    return nextLockedStep(count, minimumToActivate, 5);
}

static double potentialLockedStepBonus(int count, int simulatedMinimum, int stepSize, double payoutPerUnit) {
    if (count < simulatedMinimum) return 0.0;
    const int safeStep = qMax(1, stepSize);
    const int lockedUnits = (count / safeStep) * safeStep;
    return lockedUnits * payoutPerUnit;
}

static int activeDayCount(const Metrics& m) {
    return m.pointsByDay.size();
}

static QPair<QString, double> bestDayByPoints(const Metrics& m) {
    QString bestDate;
    double bestPoints = 0.0;
    for (auto it = m.pointsByDay.begin(); it != m.pointsByDay.end(); ++it) {
        if (bestDate.isEmpty() || it.value() > bestPoints) {
            bestDate = it.key();
            bestPoints = it.value();
        }
    }
    return {bestDate, bestPoints};
}

static int remainingDaysInMonthInclusive(const QDate& today) {
    return qMax(1, today.daysTo(QDate(today.year(), today.month(), today.daysInMonth())) + 1);
}

static int workingDaysInMonth(const QDate& date) {
    int count = 0;
    for (QDate d(date.year(), date.month(), 1); d.month() == date.month(); d = d.addDays(1)) {
        if (d.dayOfWeek() <= 5) ++count;
    }
    return count;
}

static int workingDaysElapsedInMonth(const QDate& date) {
    int count = 0;
    for (QDate d(date.year(), date.month(), 1); d <= date; d = d.addDays(1)) {
        if (d.dayOfWeek() <= 5) ++count;
    }
    return count;
}

static int remainingWorkingDaysInMonth(const QDate& date) {
    return qMax(0, workingDaysInMonth(date) - workingDaysElapsedInMonth(date));
}

static double projectedMonthPoints(double currentPoints, int elapsedWorkingDays, int totalWorkingDays) {
    if (elapsedWorkingDays <= 0 || totalWorkingDays <= 0) return 0.0;
    return (currentPoints / elapsedWorkingDays) * totalWorkingDays;
}

static QString nextMonthlyTierHint(double points, const BonusSettings& b) {
    for (const auto& tier : b.monthlyRateTiers) {
        if (points < tier.threshold) {
            return QString("Næste point-tier: %1 point (%2 kr/point), mangler %3 point")
                .arg(tier.threshold)
                .arg(money(tier.ratePerPoint))
                .arg(money(tier.threshold - points));
        }
    }
    return "Du er allerede på højeste point-tier";
}

// ============================================================
// Reports / snapshots
// ============================================================

class ReportService {
public:
    static QString buildTextReport(const Repository& repo, const Salesperson& s, const QString& label, const Metrics& m) {
        const double rate = CommissionEngine::monthlyRatePerPoint(m.totalPoints, repo.settings.bonus);
        const int activeDays = activeDayCount(m);
        const double avgPointsPerActiveDay = activeDays > 0 ? (m.totalPoints / activeDays) : 0.0;
        const double avgCommissionPerActiveDay = activeDays > 0 ? (m.totalCommission / activeDays) : 0.0;
        const auto bestDay = bestDayByPoints(m);

        QString out;
        QTextStream ts(&out);
        ts << label << " · " << s.name << "\n";
        ts << "Overblik\n";
        ts << "- Ordrer: " << m.totalOrders << "\n";
        ts << "- Salg: " << m.salesCount << "\n";
        ts << "- Tillæg: " << m.addOnCount << "\n";
        ts << "- SIMO / VOICE: " << m.simoCount << " / " << m.voiceCount << "\n";
        ts << "- Point: " << money(m.totalPoints) << "\n";
        ts << "- Provision i alt: " << money(m.totalCommission) << " kr\n\n";
        ts << "Tempo\n";
        ts << "- Aktive salgsdage: " << activeDays << "\n";
        ts << "- Snit point pr aktiv dag: " << money(avgPointsPerActiveDay) << "\n";
        ts << "- Snit provision pr aktiv dag: " << money(avgCommissionPerActiveDay) << " kr\n";
        if (!bestDay.first.isEmpty()) ts << "- Bedste dag: " << bestDay.first << " (" << money(bestDay.second) << " point)\n";
        ts << "- Aktuel månedsrate: " << money(rate) << " kr/point\n";
        ts << "- Mål for måneden: " << money(repo.settings.bonus.monthlyTargetPoints) << " point\n";
        ts << "- " << nextMonthlyTierHint(m.totalPoints, repo.settings.bonus) << "\n";
        return out;
    }

    static QString buildHtmlReport(
        const Repository& repo,
        const Salesperson& s,
        const QString& label,
        const Metrics& m,
        double workedHours = 0.0,
        double hourlyRate = 0.0,
        const QString& hoursPeriod = QString()
        ) {
        const double rate = CommissionEngine::monthlyRatePerPoint(m.totalPoints, repo.settings.bonus);
        const int activeDays = activeDayCount(m);
        const double avgPointsPerActiveDay = activeDays > 0 ? (m.totalPoints / activeDays) : 0.0;
        const double avgCommissionPerActiveDay = activeDays > 0 ? (m.totalCommission / activeDays) : 0.0;
        const auto bestDay = bestDayByPoints(m);
        const double baseSalary = workedHours * hourlyRate;
        const double totalSalary = baseSalary + m.totalCommission;

        QString html;
        QTextStream ts(&html);
        ts << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
           << "<title>Salgsrapport - " << label.toHtmlEscaped() << "</title>"
           << "<style>"
           << "body{font-family:Segoe UI,Arial,sans-serif;margin:18px;color:#E6EEF8;line-height:1.45;background:#0F172A;}"
           << "h1,h2{margin-bottom:8px;color:#F8FBFF;}"
           << "h1{font-size:22px;margin-top:0;}h2{font-size:15px;margin-top:18px;color:#9CC7E8;text-transform:none;}"
           << "table{border-collapse:collapse;width:100%;margin-top:12px;background:#0F1A2E;border-radius:14px;overflow:hidden;}"
           << "th,td{border:1px solid #22304A;padding:10px 12px;text-align:left;vertical-align:top;}"
           << "th{background:#13203A;color:#DDEBFB;}"
           << ".meta{margin-bottom:18px;color:#BFD7EE;}"
           << ".card-table{width:100%;border-collapse:separate;border-spacing:12px;margin:12px -12px;background:transparent;}"
           << ".card-table td{width:33%;border:1px solid #223556;border-radius:18px;padding:14px 16px;background:#111B2E;vertical-align:top;}"
           << ".card-label{display:block;color:#9CC7E8;font-size:12px;font-weight:800;margin-bottom:8px;}"
           << ".card-value{display:block;font-size:20px;font-weight:900;color:#FFFFFF;}"
           << ".section{border:1px solid #223556;border-radius:18px;padding:16px;background:#111B2E;box-shadow:0 10px 24px rgba(0,0,0,0.18);}"
           << ".section-title{display:block;color:#9CC7E8;font-size:12px;font-weight:800;margin-bottom:6px;}"
           << ".section{margin-top:14px;font-size:13px;font-weight:400;}"
           << ".hint{margin-top:10px;padding:12px 14px;background:#10233B;border:1px solid #24527A;border-radius:14px;color:#D8F5FF;}"
           << "</style></head><body>";

        ts << R"(
<h1>Salgsrapport</h1>
<div class="hint">Her får du det korte overblik: hvor du står nu, og hvad der skal til for næste løft.</div>
)";
        ts << "<div class=\"meta\"><strong>Sælger:</strong> " << s.name.toHtmlEscaped()
           << "<br><strong>Periode:</strong> " << label.toHtmlEscaped()
           << "<br><strong>Eksporteret:</strong> " << QDateTime::currentDateTime().toString("dd-MM-yyyy HH:mm").toHtmlEscaped()
           << "</div>";

        QVector<QPair<QString, QString>> cards;
        auto addCard = [&](const QString& title, const QString& value) {
            cards.append(qMakePair(title, value));
        };
        addCard("Ordrer", QString::number(m.totalOrders));
        addCard("Salg", QString::number(m.salesCount));
        addCard("Tillæg", QString::number(m.addOnCount));
        addCard("SIMO / VOICE", QString("%1 / %2").arg(m.simoCount).arg(m.voiceCount));
        addCard("Point nu", money(m.totalPoints));
        addCard("Løntimer", money(workedHours) + " timer");
        addCard("Timeløn", money(hourlyRate) + " kr/t");
        addCard("Grundløn", money(baseSalary) + " kr");
        addCard("Provision nu", money(m.totalCommission) + " kr");
        addCard("Total løn", money(totalSalary) + " kr");
        addCard("Aktive salgsdage", QString::number(activeDays));
        addCard("Snit point pr aktiv dag", money(avgPointsPerActiveDay));
        addCard("Snit provision pr aktiv dag", money(avgCommissionPerActiveDay) + " kr");

        ts << "<table class=\"card-table\">";
        for (int i = 0; i < cards.size(); ++i) {
            if (i % 3 == 0) ts << "<tr>";
            ts << "<td><span class=\"card-label\">" << cards.at(i).first.toHtmlEscaped()
               << "</span><span class=\"card-value\">" << cards.at(i).second.toHtmlEscaped()
               << "</span></td>";
            if (i % 3 == 2) ts << "</tr>";
        }
        const int remainder = cards.size() % 3;
        if (remainder != 0) {
            for (int i = remainder; i < 3; ++i) ts << "<td></td>";
            ts << "</tr>";
        }
        ts << "</table>";

        ts << "<div class=\"hint\">" << nextMonthlyTierHint(m.totalPoints, repo.settings.bonus).toHtmlEscaped() << "</div>";
        ts << "<h2>Løn samlet</h2><div class=\"hint\">Her kombineres løntimer fra Intramanager med provisionen i Provi Tracker.</div><table>";
        ts << "<tr><th>Felt</th><th>Værdi</th></tr>";
        ts << "<tr><td>Lønperiode</td><td>" << hoursPeriod.toHtmlEscaped() << "</td></tr>";
        ts << "<tr><td>Løntimer</td><td>" << money(workedHours) << " timer</td></tr>";
        ts << "<tr><td>Timeløn</td><td>" << money(hourlyRate) << " kr/t</td></tr>";
        ts << "<tr><td>Grundløn</td><td>" << money(baseSalary) << " kr</td></tr>";
        ts << "<tr><td>Provision</td><td>" << money(m.totalCommission) << " kr</td></tr>";
        ts << "<tr><td><strong>Total løn</strong></td><td><strong>" << money(totalSalary) << " kr</strong></td></tr>";
        ts << "</table>";

        ts << "<h2>Sådan ligger du lige nu</h2><div class=\"hint\">Her kan du se om du er foran, bagud eller lige på kanten af næste løft.</div><table>";
        ts << "<tr><th>Felt</th><th>Værdi</th></tr>";
        if (!bestDay.first.isEmpty()) {
            ts << "<tr><td>Bedste dag</td><td>" << bestDay.first.toHtmlEscaped() << " (" << money(bestDay.second) << " point)</td></tr>";
        }
        ts << "<tr><td>Aktuel månedsrate</td><td>" << money(rate) << " kr/point</td></tr>";
        ts << "<tr><td>Mål for måneden</td><td>" << money(repo.settings.bonus.monthlyTargetPoints) << " point</td></tr>";
        if (repo.settings.monthlySalesTarget > 0) {
            ts << "<tr><td>Mål for måneden (salg)</td><td>" << repo.settings.monthlySalesTarget << " salg</td></tr>";
        }
        ts << "<tr><td>Mangler til / over mål</td><td>";
        if (m.totalPoints < repo.settings.bonus.monthlyTargetPoints) {
            ts << money(repo.settings.bonus.monthlyTargetPoints - m.totalPoints) << " point mangler";
        } else {
            ts << money(m.totalPoints - repo.settings.bonus.monthlyTargetPoints) << " point over mål";
        }
        ts << "</td></tr></table>";

        ts << "<h2>Bonus og næste løft</h2><div class=\"hint\">Når du er tæt på bonus, bliver de næste pengehop fremhævet tydeligt.</div><table>";
        ts << "<tr><th>Felt</th><th>Værdi</th></tr>";
        ts << "<tr><td>Dagsbonus / pointbonus (21.–20.)</td><td>" << money(m.dayBonus) << " kr</td></tr>";
        ts << "<tr><td>Månedsbonus</td><td>" << money(m.monthlyBonus) << " kr</td></tr>";
        ts << "<tr><td>SIMO bonus</td><td>" << money(m.simoBonus) << " kr</td></tr>";
        ts << "<tr><td>VOICE bonus</td><td>" << money(m.voiceBonus) << " kr</td></tr>";
        ts << "</table>";

        ts << "<h2>Det du har lukket</h2><table><tr><th>Produkt</th><th>Antal</th><th>Point</th></tr>";
        for (auto it = m.quantityByProduct.begin(); it != m.quantityByProduct.end(); ++it) {
            ts << "<tr><td>" << it.key().toHtmlEscaped() << "</td><td>" << it.value() << "</td><td>"
               << money(m.pointsByProduct.value(it.key())) << "</td></tr>";
        }
        ts << "</table>";

        ts << "<h2>Udvikling dag for dag</h2><table><tr><th>Dag</th><th>Point</th><th>Estimeret provision</th></tr>";
        for (auto it = m.pointsByDay.begin(); it != m.pointsByDay.end(); ++it) {
            ts << "<tr><td>" << it.key().toHtmlEscaped() << "</td><td>" << money(it.value()) << "</td><td>"
               << money(m.commissionByDay.value(it.key())) << " kr</td></tr>";
        }
        ts << "</table>";

        ts << "</body></html>";
        return html;
    }

    static bool exportPdf(
        const QString& path,
        const Repository& repo,
        const Salesperson& s,
        const QString& label,
        const Metrics& m,
        double workedHours = 0.0,
        double hourlyRate = 0.0,
        const QString& hoursPeriod = QString()
        ) {
        QTextDocument doc;
        doc.setHtml(buildHtmlReport(repo, s, label, m, workedHours, hourlyRate, hoursPeriod));
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        printer.setPageMargins(QMarginsF(14, 14, 14, 14), QPageLayout::Millimeter);
        doc.print(&printer);
        return QFileInfo::exists(path);
    }

    static void autoClosePreviousMonths(Repository& repo) {
        if (repo.salespeople.isEmpty()) return;
        const QDate previousMonthDate = QDate::currentDate().addMonths(-1);
        const QString previousKey = monthKey(previousMonthDate);

        QStringList keysToClose;
        if (repo.settings.lastClosedMonthKey.isEmpty()) {
            keysToClose << previousKey;
        } else {
            QDate d = QDate::fromString(repo.settings.lastClosedMonthKey + "-01", "yyyy-MM-dd").addMonths(1);
            while (monthKey(d) <= previousKey) {
                keysToClose << monthKey(d);
                d = d.addMonths(1);
            }
        }

        for (const QString& key : keysToClose) {
            const QDate d = QDate::fromString(key + "-01", "yyyy-MM-dd");
            const auto range = monthRange(d);
            const auto dayBonusPeriod = payrollBonusRange(d);

            for (const auto& s : repo.salespeople) {
                const auto m = CommissionEngine::calculate(
                    repo,
                    s.id,
                    range.first,
                    range.second,
                    dayBonusPeriod
                    );
                const QString jsonPath = repo.snapshotDir() + "/" + s.id + "_" + key + ".json";
                QFile jf(jsonPath);
                if (jf.open(QIODevice::WriteOnly)) {
                    QJsonObject snap{{"salespersonId", s.id}, {"salespersonName", s.name}, {"monthKey", key}, {"points", m.totalPoints}, {"totalCommission", m.totalCommission}};
                    jf.write(QJsonDocument(snap).toJson(QJsonDocument::Indented));
                }
                const QString pdfPath = repo.reportDir() + "/" + s.id + "_" + key + ".pdf";
                exportPdf(pdfPath, repo, s, key, m);
            }
            repo.settings.lastClosedMonthKey = key;
        }

        repo.saveSettings();
    }
};

class SalespersonPickerDialog : public QDialog {
public:
    SalespersonPickerDialog(Repository& repo, QWidget* parent = nullptr)
        : QDialog(parent), repo(repo) {
        setWindowTitle("Vælg sælger");
        resize(420, 300);
        auto* layout = new QVBoxLayout(this);
        list = new QListWidget;
        for (const auto& s : repo.salespeople) list->addItem(s.name);
        nameEdit = new QLineEdit;
        nameEdit->setPlaceholderText("Opret ny sælger...");
        auto* createBtn = new QPushButton("Opret ny");
        auto* selectBtn = new QPushButton("Brug valgt sælger");
        layout->addWidget(new QLabel("Vælg aktiv sælger eller opret en ny:"));
        layout->addWidget(list);
        layout->addWidget(nameEdit);
        auto* row = new QHBoxLayout;
        row->addWidget(createBtn);
        row->addWidget(selectBtn);
        layout->addLayout(row);

        connect(createBtn, &QPushButton::clicked, this, [this]() {
            const QString name = nameEdit->text().trimmed();
            if (name.isEmpty()) return;
            Salesperson s{QUuid::createUuid().toString(QUuid::WithoutBraces), name};
            this->repo.salespeople.push_back(s);
            this->repo.settings.activeSalespersonId = s.id;
            this->repo.saveAll();
            accept();
        });
        connect(selectBtn, &QPushButton::clicked, this, [this]() {
            const int row = list->currentRow();
            if (row < 0 || row >= static_cast<int>(this->repo.salespeople.size())) return;
            this->repo.settings.activeSalespersonId = this->repo.salespeople[row].id;
            this->repo.saveSettings();
            accept();
        });
    }
private:
    Repository& repo;
    QListWidget* list = nullptr;
    QLineEdit* nameEdit = nullptr;
};

class OrderEditorDialog : public QDialog {
public:
    OrderEditorDialog(Repository& repo, const QString& salespersonId, std::optional<Order> existing = std::nullopt, QWidget* parent = nullptr)
        : QDialog(parent), repo(repo), salespersonId(salespersonId) {
        setWindowTitle(existing.has_value() ? "Rediger ordre" : "Ny ordre");
        resize(1120, 720);

        if (existing.has_value()) {
            order = *existing;
        } else {
            order.salespersonId = salespersonId;
            order.createdAt = QDateTime::currentDateTime();
            order.sellerInitials = repo.settings.defaultSellerInitials;
        }

        auto* mainLayout = new QVBoxLayout(this);
        auto* detailsGrid = new QGridLayout;
        detailsGrid->setHorizontalSpacing(12);
        detailsGrid->setVerticalSpacing(10);
        idEdit = new QLineEdit(order.id);
        idEdit->setPlaceholderText("Indtast eller indsæt ordre-ID...");
        idEdit->setClearButtonEnabled(true);
        idEdit->setMinimumWidth(240);
        dateEdit = new QDateTimeEdit(order.createdAt);
        dateEdit->setCalendarPopup(true);
        dateEdit->setDisplayFormat("dd-MM-yyyy HH:mm");
        dateEdit->setLocale(QLocale(QLocale::Danish, QLocale::Denmark));
        dateEdit->setMinimumWidth(190);
        dateEdit->setMinimumHeight(42);
        initialsEdit = new QLineEdit(order.sellerInitials);
        initialsEdit->setPlaceholderText("Fx 5RVPTN");
        cvrEdit = new QLineEdit(order.cvrNumber);
        cvrEdit->setPlaceholderText("CVR-nr.");
        companyNameEdit = new QLineEdit(order.companyName);
        companyNameEdit->setPlaceholderText("Firmanavn");
        phoneEdit = new QLineEdit(order.phoneNumber);
        phoneEdit->setPlaceholderText("Telefonnummer");
        noteEdit = new QLineEdit(order.note);
        noteEdit->setPlaceholderText("Valgfri note til ordren");
        detailsGrid->addWidget(new QLabel("Dato/tid:"), 0, 0);
        detailsGrid->addWidget(dateEdit, 0, 1);
        detailsGrid->addWidget(new QLabel("Initialer:"), 0, 2);
        detailsGrid->addWidget(initialsEdit, 0, 3);
        detailsGrid->addWidget(new QLabel("Ordre nummer:"), 0, 4);
        detailsGrid->addWidget(idEdit, 0, 5);
        detailsGrid->addWidget(new QLabel("CVR-nr.:"), 1, 0);
        detailsGrid->addWidget(cvrEdit, 1, 1);
        detailsGrid->addWidget(new QLabel("Firmanavn:"), 1, 2);
        detailsGrid->addWidget(companyNameEdit, 1, 3);
        detailsGrid->addWidget(new QLabel("Telefon:"), 1, 4);
        detailsGrid->addWidget(phoneEdit, 1, 5);
        detailsGrid->addWidget(new QLabel("Note:"), 2, 0);
        detailsGrid->addWidget(noteEdit, 2, 1, 1, 5);
        mainLayout->addLayout(detailsGrid);

        // ===== FAVORITTER CARD =====
        auto favCard = createCard("Dine favoritter");
        auto* favRow = new QHBoxLayout;
        favCard.second->addLayout(favRow);
        buildFavoritesBar(favRow);
        mainLayout->addWidget(favCard.first);

        // ===== RECENT CARD =====
        auto recentCard = createCard("Senest brugte produkter");
        auto* recentRow = new QHBoxLayout;
        recentCard.second->addLayout(recentRow);
        buildRecentBar(recentRow);
        mainLayout->addWidget(recentCard.first);

        // ===== TABLE CARD =====
        auto tableCard = createCard("Ordrer");
        table = new RoundedTableWidget(0, 5);
        table->setHorizontalHeader(new CleanTableHeaderView(Qt::Horizontal, table));
        table->setHorizontalHeaderLabels({"Kategori", "Produkt", "Antal", "Info", "Handling"});

        tableCard.second->addWidget(table);
        mainLayout->addWidget(tableCard.first);

        auto* header = table->horizontalHeader();

        header->setSectionResizeMode(0, QHeaderView::ResizeToContents); // Kategori
        header->setSectionResizeMode(1, QHeaderView::Stretch);          // Produkt
        header->setSectionResizeMode(2, QHeaderView::Fixed);            // Antal
        header->setSectionResizeMode(3, QHeaderView::Fixed);            // Info
        header->setSectionResizeMode(4, QHeaderView::Fixed);            // Handling

        table->setColumnWidth(2, 110);
        table->setColumnWidth(3, 220);
        table->setColumnWidth(4, 120);

        table->verticalHeader()->setVisible(false);
        table->verticalHeader()->setDefaultSectionSize(48);
        table->setWordWrap(true);
        table->setTextElideMode(Qt::ElideNone);
        table->setShowGrid(false);
        table->setAlternatingRowColors(true);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setFocusPolicy(Qt::NoFocus);
        table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        table->setMinimumHeight(270);
        table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        styleDataTable(table, false);

        auto actionCard = createCard("Handlinger");
        auto* btnRow = new QHBoxLayout;
        btnRow->setContentsMargins(0, 0, 0, 0);

        auto* addLineBtn = new QPushButton("Tilføj produkt");
        auto* duplicateLastBtn = new QPushButton("Gentag sidste produkt");
        auto* saveBtn = new QPushButton("Gem ordre");
        auto* cancelBtn = new QPushButton("Annuller");

        btnRow->addWidget(addLineBtn);
        btnRow->addWidget(duplicateLastBtn);
        btnRow->addStretch();
        btnRow->addWidget(saveBtn);
        btnRow->addWidget(cancelBtn);

        actionCard.second->addLayout(btnRow);
        mainLayout->addWidget(actionCard.first);

        for (const auto& item : order.items) addRow(item.productKey, item.quantity);
        if (order.items.isEmpty()) addRow({}, 1);

        connect(addLineBtn, &QPushButton::clicked, this, [this]() { addRow({}, 1); });
        connect(duplicateLastBtn, &QPushButton::clicked, this, [this]() {
            if (table->rowCount() == 0) {
                addRow({}, 1);
                return;
            }
            auto* productCombo = qobject_cast<QComboBox*>(table->cellWidget(table->rowCount() - 1, 1));
            auto* qtySpin = qobject_cast<QSpinBox*>(table->cellWidget(table->rowCount() - 1, 2));
            addRow(productCombo ? productCombo->currentData().toString() : QString(), qtySpin ? qtySpin->value() : 1);
        });
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

        auto* shortcutAdd = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_N), this);
        connect(shortcutAdd, &QShortcut::activated, this, [this]() { addRow({}, 1); });
        auto* shortcutDuplicate = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
        connect(shortcutDuplicate, &QShortcut::activated, this, [this]() {
            if (table->rowCount() == 0) {
                addRow({}, 1);
                return;
            }
            auto* productCombo = qobject_cast<QComboBox*>(table->cellWidget(table->rowCount() - 1, 1));
            auto* qtySpin = qobject_cast<QSpinBox*>(table->cellWidget(table->rowCount() - 1, 2));
            addRow(productCombo ? productCombo->currentData().toString() : QString(), qtySpin ? qtySpin->value() : 1);
        });
        auto* shortcutSave = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this);
        connect(shortcutSave, &QShortcut::activated, saveBtn, &QPushButton::click);
        auto* shortcutCancel = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(shortcutCancel, &QShortcut::activated, this, &QDialog::reject);

        connect(saveBtn, &QPushButton::clicked, this, [this]() {
            order.id = idEdit->text().trimmed();
            if (order.id.isEmpty()) {
                QMessageBox::warning(this, "Manglende ordre-ID", "Du skal indtaste eller indsætte et ordre-ID.");
                return;
            }
            order.sellerInitials = initialsEdit->text().trimmed();
            order.cvrNumber = cvrEdit->text().trimmed();
            order.companyName = companyNameEdit->text().trimmed();
            order.phoneNumber = phoneEdit->text().trimmed();
            if (order.sellerInitials.isEmpty() || order.cvrNumber.isEmpty()
                || order.companyName.isEmpty() || order.phoneNumber.isEmpty()) {
                QMessageBox::warning(
                    this,
                    "Manglende ordreoplysninger",
                    "Du skal udfylde initialer, CVR-nr., firmanavn og telefonnummer."
                    );
                return;
            }
            order.createdAt = dateEdit->dateTime();
            order.note = noteEdit->text().trimmed();
            order.items.clear();
            for (int row = 0; row < table->rowCount(); ++row) {
                auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, 1));
                auto* spin = qobject_cast<QSpinBox*>(table->cellWidget(row, 2));
                if (!combo || !spin) continue;
                const QString productKey = combo->currentData().toString();
                const int qty = spin->value();
                if (productKey.isEmpty() || qty <= 0) continue;
                order.items.push_back({productKey, qty});
            }
            if (order.items.isEmpty()) {
                QMessageBox::warning(this, "Tom ordre", "Du skal tilføje mindst ét produkt.");
                return;
            }
            accept();
        });
    }

    Order getOrder() const { return order; }

private:
    Repository& repo;
    QString salespersonId;
    Order order;
    QLineEdit* idEdit = nullptr;
    QDateTimeEdit* dateEdit = nullptr;
    QLineEdit* initialsEdit = nullptr;
    QLineEdit* cvrEdit = nullptr;
    QLineEdit* companyNameEdit = nullptr;
    QLineEdit* phoneEdit = nullptr;
    QLineEdit* noteEdit = nullptr;
    QTableWidget* table = nullptr;

    static QString& rememberedCategoryRef() {
        static QString category = "Mobil";
        return category;
    }

    QStringList topFavoriteProductKeys(int limit = 5) const {
        QMap<QString, int> counts;
        for (const auto& existingOrder : repo.orders) {
            if (existingOrder.salespersonId != salespersonId) continue;
            for (const auto& item : existingOrder.items) {
                counts[item.productKey] += item.quantity;
            }
        }

        QVector<QPair<QString, int>> scored;
        for (auto it = counts.begin(); it != counts.end(); ++it) scored.push_back(qMakePair(it.key(), it.value()));
        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

        QStringList out;
        for (int i = 0; i < scored.size() && i < limit; ++i) out << scored[i].first;
        return out;
    }

    QStringList recentProductKeys(int limit = 5) const {
        QStringList out;
        QSet<QString> seen;
        for (int i = repo.orders.size() - 1; i >= 0 && out.size() < limit; --i) {
            const auto& existingOrder = repo.orders[i];
            if (existingOrder.salespersonId != salespersonId) continue;
            for (int j = existingOrder.items.size() - 1; j >= 0 && out.size() < limit; --j) {
                const QString key = existingOrder.items[j].productKey;
                if (!seen.contains(key)) {
                    seen.insert(key);
                    out << key;
                }
            }
        }
        return out;
    }

    void addFavoriteProduct(const QString& productKey) {
        if (productKey.isEmpty()) return;
        addRow(productKey, 1);
        table->scrollToBottom();
    }

    void buildFavoritesBar(QHBoxLayout* layout) {
        const auto favorites = topFavoriteProductKeys();
        if (favorites.isEmpty()) {
            auto* label = new QLabel("Ingen favoritter endnu. De mest solgte produkter dukker op her.");
            label->setWordWrap(true);
            layout->addWidget(label);
            layout->addStretch();
            return;
        }

        for (const auto& productKey : favorites) {
            const auto* p = repo.findProduct(productKey);
            if (!p) continue;
            auto* btn = new QPushButton(p->displayName);
            btn->setToolTip(QString("Tilføj %1").arg(p->displayName));
            layout->addWidget(btn);
            connect(btn, &QPushButton::clicked, this, [this, productKey]() { addFavoriteProduct(productKey); });
        }
        layout->addStretch();
    }

    void buildRecentBar(QHBoxLayout* layout) {
        const auto recent = recentProductKeys();
        if (recent.isEmpty()) {
            auto* label = new QLabel("Dine senest brugte produkter vises her.");
            label->setWordWrap(true);
            layout->addWidget(label);
            layout->addStretch();
            return;
        }

        for (const auto& productKey : recent) {
            const auto* p = repo.findProduct(productKey);
            if (!p) continue;
            auto* btn = new QPushButton(p->displayName);
            btn->setToolTip(QString("Tilføj %1 igen").arg(p->displayName));
            layout->addWidget(btn);
            connect(btn, &QPushButton::clicked, this, [this, productKey]() { addFavoriteProduct(productKey); });
        }
        layout->addStretch();
    }

    QStringList categories() const {
        QSet<QString> seen;
        QStringList out;
        for (const auto& p : repo.products) {
            if (!seen.contains(p.category)) {
                seen.insert(p.category);
                out << p.category;
            }
        }
        std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
            if (a == "Mobil") return true;
            if (b == "Mobil") return false;
            if (a == "Fiber") return true;
            if (b == "Fiber") return false;
            if (a == "FWA") return true;
            if (b == "FWA") return false;
            if (a == "Mobilt bredbånd") return true;
            if (b == "Mobilt bredbånd") return false;
            if (a == "Tillæg") return false;
            if (b == "Tillæg") return true;
            return a.localeAwareCompare(b) < 0;
        });
        return out;
    }

    QString productInfoText(const Product* p) const {
        if (!p) return "Vælg produkt";
        return QString("%1 point | %2").arg(money(p->points), countModeBadge(p->countMode));
    }

    void fillProductCombo(QComboBox* productCombo, const QString& category, const QString& selectedKey) {
        QSignalBlocker blocker(productCombo);
        productCombo->clear();
        for (const auto& p : repo.products) {
            if (p.category == category) {
                productCombo->addItem(QString("%1 (%2 point | %3)").arg(p.displayName, money(p.points), countModeBadge(p.countMode)), p.key);
            }
        }
        int idx = selectedKey.isEmpty() ? -1 : productCombo->findData(selectedKey);
        if (idx >= 0) productCombo->setCurrentIndex(idx);
        else if (productCombo->count() > 0) productCombo->setCurrentIndex(0);
    }

    void syncInfoLabel(int row) {
        auto* productCombo = qobject_cast<QComboBox*>(table->cellWidget(row, 1));
        auto* infoLabel = qobject_cast<QLabel*>(table->cellWidget(row, 3));
        if (!productCombo || !infoLabel) return;
        const auto* p = repo.findProduct(productCombo->currentData().toString());
        infoLabel->setText(productInfoText(p));
    }

    void addRow(const QString& productKey, int quantity) {
        const Product* existingProduct = productKey.isEmpty() ? nullptr : repo.findProduct(productKey);
        const QString rememberedCategory = rememberedCategoryRef();
        const QString initialCategory = existingProduct
            ? existingProduct->category
            : (!rememberedCategory.isEmpty() ? rememberedCategory : (categories().isEmpty() ? QString() : categories().first()));

        const int row = table->rowCount();
        table->insertRow(row);

        auto* categoryCombo = new QComboBox;
        categoryCombo->addItems(categories());
        categoryCombo->setMinimumWidth(170);
        categoryCombo->setStyleSheet(R"(
QComboBox {
    color: #EAF4FF;
    background: #0B1424;
    padding: 0 30px 0 10px;
    border-radius: 12px;
    border: 1px solid #2A3B5F;
    font-weight: 700;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 24px;
    border: none;
    background: #13203A;
    border-top-right-radius: 12px;
    border-bottom-right-radius: 12px;
}

QComboBox QAbstractItemView {
    color: #EAF4FF;
    background: #0B1424;
    selection-background-color: #14B8A6;
    selection-color: #06202A;
    border-radius: 12px;
}
)");
        int catIdx = categoryCombo->findText(initialCategory);
        if (catIdx >= 0) categoryCombo->setCurrentIndex(catIdx);

        auto* productCombo = new QComboBox;
        productCombo->setEditable(true);
        productCombo->setMinimumWidth(380);
        productCombo->setStyleSheet(R"(
QComboBox {
    color: #EAF4FF;
    background: #0B1424;
    padding-right: 28px;
    border-radius: 12px;
}

QComboBox QAbstractItemView {
    background: #0B1424;
    color: #EAF4FF;
    selection-background-color: #14B8A6;
    selection-color: #06202A;
    border-radius: 12px;
}
)");
        productCombo->setInsertPolicy(QComboBox::NoInsert);
        fillProductCombo(productCombo, categoryCombo->currentText(), productKey);
        if (auto* completer = productCombo->completer()) {
            completer->setCompletionMode(QCompleter::PopupCompletion);
            completer->setFilterMode(Qt::MatchContains);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
        }
        if (productCombo->lineEdit()) {
            productCombo->lineEdit()->setStyleSheet("QLineEdit { color:#EAF4FF; background:transparent; border:none; padding:0 18px 0 0; selection-background-color:#14B8A6; selection-color:#06202A; }");
            connect(productCombo->lineEdit(), &QLineEdit::returnPressed, this, [this]() { addRow({}, 1); });
        }

        auto* qtySpin = new QSpinBox;
        qtySpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        qtySpin->setStyleSheet(R"(
QSpinBox {
    background: #0B1424;
    color: #EAF4FF;
    border-radius: 12px;
    padding: 0 10px;
    min-height: 38px;
}

QSpinBox::up-button, QSpinBox::down-button {
    background: #14B8A6;
    width: 18px;
    border-radius: 6px;
    margin: 2px;
    color: #06202A;
    font-weight: 900;
}

QSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
}

QSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
}
)");
        qtySpin->setRange(1, 999);
        qtySpin->setValue(quantity);

        auto* infoLabel = new QLabel;
        infoLabel->setMinimumWidth(230);
        infoLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        const auto* p = repo.findProduct(productCombo->currentData().toString());
        infoLabel->setText(productInfoText(p));

        auto* removeBtn = new QPushButton("Fjern");
        connect(removeBtn, &QPushButton::clicked, this, [this, removeBtn]() {
            for (int r = 0; r < table->rowCount(); ++r) {
                if (table->cellWidget(r, 4) == removeBtn) {
                    table->removeRow(r);
                    if (table->rowCount() == 0) addRow({}, 1);
                    break;
                }
            }
        });

        connect(categoryCombo, &QComboBox::currentTextChanged, this, [this, productCombo, infoLabel](const QString& category) {
            rememberedCategoryRef() = category;
            fillProductCombo(productCombo, category, {});
            const auto* p = repo.findProduct(productCombo->currentData().toString());
            infoLabel->setText(productInfoText(p));
        });
        connect(productCombo, &QComboBox::currentIndexChanged, this, [this, row](int) {
            syncInfoLabel(row);
        });

        table->setCellWidget(row, 0, categoryCombo);
        table->setCellWidget(row, 1, productCombo);
        table->setCellWidget(row, 2, qtySpin);
        table->setCellWidget(row, 3, infoLabel);
        table->setCellWidget(row, 4, removeBtn);
        table->setRowHeight(row, 48);
    }
};

// ============================================================
// Main window
// ============================================================

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        migrateLegacyDataIfNeeded();
        repo.load();

        if (repo.salespeople.isEmpty() || repo.settings.activeSalespersonId.isEmpty() || !repo.findSalesperson(repo.settings.activeSalespersonId)) {
            SalespersonPickerDialog dlg(repo, this);
            dlg.exec();
        }

        if (repo.salespeople.isEmpty()) {
            Salesperson first{QUuid::createUuid().toString(QUuid::WithoutBraces), "Standard"};
            repo.salespeople.push_back(first);
            repo.settings.activeSalespersonId = first.id;
            repo.saveAll();
        }

        ReportService::autoClosePreviousMonths(repo);

        setupUi();
        refreshAll();
        setupIntramanagerAutoSync();
    }

private:
    Repository repo;

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
    QLabel* kpiMonthCommissionLabel = nullptr;
    QLabel* kpiMonthSalesLabel = nullptr;
    QLabel* kpiMonthAddonsLabel = nullptr;
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

    QTableWidget* ordersTable = nullptr;
    QTextEdit* reportText = nullptr;
    QComboBox* reportPresetCombo = nullptr;
    QDateEdit* reportMonthEdit = nullptr;

    QDoubleSpinBox* dayBonusSpin = nullptr;
    QSpinBox* simoMinSpin = nullptr;
    QDoubleSpinBox* simoUnitSpin = nullptr;
    QSpinBox* voiceMinSpin = nullptr;
    QDoubleSpinBox* voiceUnitSpin = nullptr;
    QDoubleSpinBox* targetSpin = nullptr;
    QSpinBox* monthlySalesTargetSpin = nullptr;
    QListWidget* salespeopleList = nullptr;
    bool intramanagerSyncRunning = false;
    bool intramanagerPunchRunning = false;
    QTimer* intramanagerAutoSyncTimer = nullptr;
    QSet<QString> intramanagerPendingFetchKeys;

    QDoubleSpinBox* hourlyRateSpin = nullptr;
    QLineEdit* intramanagerUsernameEdit = nullptr;
    QLineEdit* intramanagerPasswordEdit = nullptr;
    QCheckBox* intramanagerEnabledCheck = nullptr;
    QLabel* intramanagerStatusLabel = nullptr;
    QLineEdit* defaultSellerInitialsEdit = nullptr;
    QLineEdit* salesRegistrationWebhookEdit = nullptr;
    QLineEdit* salesRegistrationRecipientEdit = nullptr;
    QCheckBox* salesRegistrationEnabledCheck = nullptr;
    QLabel* salesRegistrationStatusLabel = nullptr;

    struct IntramanagerFetchResult {
        bool success = false;
        double hours = 0.0;
        double phoneHours = 0.0;
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

    QString intramanagerWorkerPath() const {
        return QCoreApplication::applicationDirPath()
            + "/intramanager_worker/intramanager_sync.exe";
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
        return payload;
    }

    QStringList intramanagerWorkerArgs(const QString& fromDate, const QString& toDate) const {
        return {"--action", "hours", "--from-date", fromDate, "--to-date", toDate, "--stdin-json"};
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

    void rememberIntramanagerHours(const QString& fromDate, const QString& toDate, double hours, double phoneHours) {
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

    QString punchDetailText() const {
        const auto& punch = repo.settings.intramanagerPunch;
        if (!repo.settings.intramanagerEnabled) {
            return "Gem Intramanager-login i Indstillinger for at bruge stempeluret.";
        }
        if (!punch.known) {
            return punch.detail.isEmpty() ? "Status er ikke hentet endnu." : punch.detail;
        }

        QStringList parts;
        if (!punch.detail.isEmpty()) parts << punch.detail;
        if (!punch.lastStart.isEmpty()) parts << "Seneste ind: " + punch.lastStart;
        if (!punch.lastStop.isEmpty()) parts << "Seneste ud: " + punch.lastStop;
        parts << shortPunchSyncText();
        return parts.join("\n");
    }

    void refreshPunchCardUi() {
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
                QString("QLabel { color:%1; font-size:22px; font-weight:900; background:transparent; }")
                    .arg(punch.known && punch.clockedIn ? "#34D399" : "#F8FBFF")
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

        intramanagerAutoSyncTimer = new QTimer(this);
        intramanagerAutoSyncTimer->setInterval(15 * 60 * 1000);

        connect(intramanagerAutoSyncTimer, &QTimer::timeout, this, [this]() {
            syncIntramanagerHoursAsync(true);
            refreshIntramanagerPunchStatusAsync(true);
        });

        intramanagerAutoSyncTimer->start();

        QTimer::singleShot(5000, this, [this]() {
            syncIntramanagerHoursAsync(true);
        });

        QTimer::singleShot(1500, this, [this]() {
            refreshIntramanagerPunchStatusAsync(true);
        });
    }

    void fetchIntramanagerHoursAsync(
        const QString& fromDate,
        const QString& toDate,
        bool silent,
        std::function<void(bool)> afterFetch = {}
        ) {
        if (intramanagerSyncRunning) {
            if (afterFetch) {
                QTimer::singleShot(1500, this, [this, fromDate, toDate, silent, afterFetch]() {
                    if (hasCachedIntramanagerHours(fromDate, toDate)) {
                        afterFetch(true);
                    } else {
                        fetchIntramanagerHoursAsync(fromDate, toDate, silent, afterFetch);
                    }
                });
            }
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

                    rememberIntramanagerHours(fromDate, toDate, result.hours, result.phoneHours);
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

    void runIntramanagerPunchWorker(
        const QString& action,
        bool silent,
        std::function<void(bool)> afterFetch = {}
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
                        QMessageBox::information(this, "Intramanager", result.statusText);
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
        runIntramanagerPunchWorker("punch-status", silent);
    }

    void toggleIntramanagerPunchAsync() {
        const auto& punch = repo.settings.intramanagerPunch;
        if (!punch.known) {
            refreshIntramanagerPunchStatusAsync(false);
            return;
        }

        runIntramanagerPunchWorker("punch-toggle", false);
    }

    void syncIntramanagerHoursAsync(bool silent = false) {
        const auto period = payrollBonusRange(QDate::currentDate());
        const QString fromDate = intramanagerDate(period.first.date());
        const QString toDate = intramanagerDate(period.second.date());
        fetchIntramanagerHoursAsync(fromDate, toDate, silent);
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

    const Salesperson* activeSalesperson() const {
        return repo.findSalesperson(repo.settings.activeSalespersonId);
    }

    void setupUi() {
        resize(1200, 800);
        setWindowTitle("Provi Tracker");

        setStyleSheet(R"(
            QMainWindow, QWidget {
                background: #0F172A;
                color: #E6EEF8;
                font-size: 13px;
            }
QLabel {
    color: #E6EEF8;
    background: transparent;
    padding: 0px;
    border-radius: 0px;
    font-weight: 400;
}
            QFrame, QGroupBox, QListWidget, QTextEdit, QTextBrowser, QTableWidget, QAbstractScrollArea, QLineEdit, QComboBox, QDateEdit, QDateTimeEdit, QSpinBox, QDoubleSpinBox {
                border-radius: 16px;
            }
            QTabWidget::pane {
                border: 1px solid #22304A;
                border-radius: 18px;
                background: #111B2E;
                top: -1px;
            }
            QTabBar::tab {
                background: #16233B;
                color: #C9D8EC;
                border: 1px solid #22304A;
                padding: 10px 16px;
                border-top-left-radius: 14px;
                border-top-right-radius: 14px;
                margin-right: 6px;
                min-width: 96px;
                outline: none;
            }
            QTabBar::tab:selected {
                background: #14B8A6;
                color: #06202A;
                border-color: #14B8A6;
                font-weight: 700;
            }
            QTabBar::tab:hover:!selected {
                background: #1D2D49;
            }
QGroupBox {
    border: none;
    background: transparent;
    margin-top: 0px;
    padding: 0px;
}

QGroupBox::title {
    color: #9CC7E8;
    font-size: 12px;
    font-weight: 700;
    padding-left: 4px;
}
            QPushButton {
                background: #14B8A6;
                color: #06202A;
                border: none;
                padding: 9px 14px;
                border-radius: 16px;
                font-weight: 700;
                outline: none;
            }
            QPushButton:hover { background: #2DD4BF; }
            QPushButton:pressed { background: #0EA5A4; }
            QCheckBox {
                background: transparent;
                color: #E6EEF8;
                padding: 0px;
                spacing: 8px;
                outline: none;
            }

            QLineEdit, QComboBox, QDateEdit, QDateTimeEdit, QSpinBox, QDoubleSpinBox, QTextEdit, QListWidget, QTableWidget, QAbstractScrollArea {
                background: #0B1424;
                color: #E6EEF8;
                border: 1px solid #2A3B5F;
                padding: 8px 10px;
                selection-background-color: #14B8A6;
                selection-color: #06202A;
            }
            QLineEdit:focus, QComboBox:focus, QDateEdit:focus, QDateTimeEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QTextEdit:focus, QListWidget:focus, QTableWidget:focus {
                border: 1px solid #35D8C7;
            }
QComboBox {
    padding-right: 30px;
    color: #FFFFFF;
    font-weight: 600;
}
            QComboBox:editable {
                color: #EAF4FF;
            }
QComboBox::drop-down, QDateEdit::drop-down, QDateTimeEdit::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 28px;
    border: none;
    background: #13203A;
    border-top-right-radius: 14px;
    border-bottom-right-radius: 14px;
}
QDateEdit::down-arrow, QDateTimeEdit::down-arrow, QComboBox::down-arrow {
    width: 10px;
    height: 10px;
}
            QComboBox QAbstractItemView, QListView, QMenu {
                background: #0B1424;
                color: #E6EEF8;
                border: 1px solid #2A3B5F;
                border-radius: 16px;
                selection-background-color: #14B8A6;
                selection-color: #06202A;
                padding: 6px;
                outline: 0;
            }
            QComboBox QAbstractItemView::item, QListView::item, QMenu::item {
                min-height: 28px;
                border-radius: 10px;
                padding: 6px 10px;
                margin: 2px 4px;
                color: #EAF4FF;
            }
            QComboBox QAbstractItemView::item:selected, QListView::item:selected, QMenu::item:selected {
                background: #14B8A6;
                color: #06202A;
            }
            QAbstractScrollArea {
                border-radius: 18px;
            }
            QAbstractScrollArea > QWidget {
                border-radius: 18px;
            }
            QTableWidget {
                gridline-color: #22304A;
                alternate-background-color: #12203A;
                color: #EAF4FF;
            }
QHeaderView::section {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #1A2B48,
        stop:1 #13203A);
    color: #EAF4FF;
    border: none;
    padding: 8px 10px;
    font-weight: 800;
    min-height: 24px;
}
            QTableCornerButton::section {
                background: #13203A;
                border: none;
                border-bottom: 1px solid #22304A;
                border-right: 1px solid #22304A;
                border-top-left-radius: 16px;
            }
            QListWidget::item, QTableWidget::item {
                color: #EAF4FF;
                padding: 8px;
                min-height: 22px;
            }
            QTableWidget::item:selected {
                background: #14B8A6;
                color: #06202A;
            }
            QTextEdit {
                padding: 14px;
                line-height: 1.45em;
            }
            QScrollBar:vertical, QScrollBar:horizontal {
                background: transparent;
                border: none;
                margin: 2px;
            }
            QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
                background: #243857;
                border-radius: 8px;
                min-height: 32px;
                min-width: 32px;
            }
            QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page {
                background: transparent;
                border: none;
            }
            QProgressBar {
                background: #0B1424;
                border: 1px solid #2A3B5F;
                border-radius: 12px;
                text-align: center;
                min-height: 22px;
                color: #F8FBFF;
                font-weight: 700;
            }
            QProgressBar::chunk {
                background: #14B8A6;
                border-radius: 10px;
            }
QFrame[kpiCard="true"] {
    background: qlineargradient(
        x1:0, y1:0, x2:0, y2:1,
        stop:0 #16233B,
        stop:1 #0F1A2E
    );
    border-radius: 18px;
    border: 1px solid #223556;
    padding: 0px;
}
QLineEdit, QComboBox, QDateEdit, QDateTimeEdit, QSpinBox, QDoubleSpinBox {
    background: #0B1424;
    border-radius: 16px;
    padding-right: 24px;
    color: #EAF4FF;
}

QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    background: #14B8A6;
    width: 18px;
    border-radius: 6px;
    margin: 2px;
}

QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border: 1px solid #14B8A6;
}

QTableWidget, QListWidget, QTextEdit {
    border-radius: 18px;
}

QTableWidget::item {
    border: none;
    color: #EAF4FF;
}
        )");


        auto* central = new QWidget;

        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(16, 16, 16, 16);
        root->setSpacing(14);

        auto* topBar = new QHBoxLayout;
        activeSalespersonLabel = new QLabel;
        activeSalespersonLabel->setStyleSheet("QLabel { color: #F8FBFF; font-size: 14px; font-weight: 700; }");
        auto* switchBtn = new QPushButton("Skift sælger");
        auto* newOrderBtn = new QPushButton("Ny ordre");
        topBar->addWidget(activeSalespersonLabel);
        topBar->addStretch();
        topBar->addWidget(newOrderBtn);
        topBar->addWidget(switchBtn);
        root->addLayout(topBar);
;
        auto* tabs = new QTabWidget;
        tabs->setFocusPolicy(Qt::NoFocus);
        tabs->tabBar()->setFocusPolicy(Qt::NoFocus);
        tabs->addTab(buildDashboardTab(), "Dashboard");
        tabs->addTab(buildOrdersTab(), "Ordrer");
        tabs->addTab(buildReportsTab(), "Rapporter");
        tabs->addTab(buildSettingsTab(), "Indstillinger");

        root->addWidget(tabs, 1);
        setCentralWidget(central);

        connect(newOrderBtn, &QPushButton::clicked, this, [this]() { createOrder(); });
        connect(switchBtn, &QPushButton::clicked, this, [this]() { chooseSalesperson(); });
        qApp->installEventFilter(this);
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
        if (!ordersTable || !ordersTable->selectionModel() || !ordersTable->selectionModel()->hasSelection())
            return;

        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget || isChildOf(widget, ordersTable) || isInteractiveClickTarget(widget))
            return;

        ordersTable->clearSelection();
        ordersTable->setCurrentItem(nullptr);
    }

    QWidget* buildDashboardTab() {
        auto* w = new QWidget;
        auto* layout = new QVBoxLayout(w);
        layout->setSpacing(14);

        auto k1 = createKpiCard("Point i dag");
        auto k2 = createKpiCard("Løn denne måned");
        auto k3 = createKpiCard("Salg denne måned");
        auto k4 = createKpiCard("Tillæg denne måned");

        kpiTodayPointsLabel = k1.second;
        kpiMonthCommissionLabel = k2.second;
        kpiMonthSalesLabel = k3.second;
        kpiMonthAddonsLabel = k4.second;

        auto* kpiGrid = new QGridLayout;
        kpiGrid->setHorizontalSpacing(14);
        kpiGrid->setVerticalSpacing(14);
        kpiGrid->addWidget(k1.first, 0, 0);
        kpiGrid->addWidget(k2.first, 0, 1);
        kpiGrid->addWidget(k3.first, 0, 2);
        kpiGrid->addWidget(k4.first, 0, 3);
        layout->addLayout(kpiGrid);

        auto punchCard = createCard("Stempelur");
        auto* punchTop = new QHBoxLayout;
        punchTop->setContentsMargins(0, 0, 0, 0);
        punchTop->setSpacing(12);

        intramanagerPunchStatusLabel = new QLabel("Status ukendt");
        intramanagerPunchStatusLabel->setTextInteractionFlags(Qt::NoTextInteraction);

        intramanagerPunchButton = new QPushButton("Hent status");
        intramanagerPunchButton->setMinimumWidth(140);

        punchTop->addWidget(intramanagerPunchStatusLabel);
        punchTop->addStretch();
        punchTop->addWidget(intramanagerPunchButton);

        intramanagerPunchDetailLabel = new QLabel("-");
        intramanagerPunchDetailLabel->setWordWrap(true);
        intramanagerPunchDetailLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        intramanagerPunchDetailLabel->setStyleSheet("QLabel { color:#D8F5FF; font-size:13px; font-weight:600; background:transparent; }");

        punchCard.second->addLayout(punchTop);
        punchCard.second->addWidget(intramanagerPunchDetailLabel);
        layout->addWidget(punchCard.first);

        auto progressCard = createCard("Mål, bonus og næste løft");
        auto* progressLayout = new QGridLayout;
        progressLayout->setHorizontalSpacing(14);
        progressLayout->setVerticalSpacing(14);

        progressLayout->addWidget(createProgressCard("Point mod månedens mål", &targetProgressBar, &targetProgressHintLabel), 0, 0);
        progressLayout->addWidget(createProgressCard("Salg mod månedens mål", &salesTargetProgressBar, &salesTargetProgressHintLabel), 0, 1);
        progressLayout->addWidget(createProgressCard("SIMO · næste pengehop", &simoProgressBar, &simoProgressHintLabel), 1, 0);
        progressLayout->addWidget(createProgressCard("VOICE · næste pengehop", &voiceProgressBar, &voiceProgressHintLabel), 1, 1);

        progressCard.second->addLayout(progressLayout);
        layout->addWidget(progressCard.first);

        auto recentCard = createSummaryCard("Seneste aktivitet");
        recentActivityLabel = recentCard.second;
        layout->addWidget(recentCard.first);
        layout->addStretch();

        connect(intramanagerPunchButton, &QPushButton::clicked, this, [this]() {
            toggleIntramanagerPunchAsync();
        });

        return w;
    }

    QWidget* buildOrdersTab() {
        auto* w = new QWidget;
        auto* layout = new QVBoxLayout(w);
        layout->setSpacing(18);

        auto actionsCard = createCard("Ordrer");
        auto* actionsRow = new QHBoxLayout;
        actionsRow->setSpacing(10);
        actionsRow->setContentsMargins(0, 0, 0, 0);

        auto* refreshBtn = new QPushButton("Opdater");
        auto* addBtn = new QPushButton("Ny ordre");
        auto* editBtn = new QPushButton("Ret ordre");
        auto* deleteBtn = new QPushButton("Slet ordre");

        actionsRow->addWidget(refreshBtn);
        actionsRow->addWidget(addBtn);
        actionsRow->addWidget(editBtn);
        actionsRow->addWidget(deleteBtn);
        actionsRow->addStretch();

        actionsCard.second->addLayout(actionsRow);
        layout->addWidget(actionsCard.first);

        auto tableCard = createCard("Ordreoversigt");

        ordersTable = new ClearableTableWidget(0, 5);
        ordersTable->setHorizontalHeader(new CleanTableHeaderView(Qt::Horizontal, ordersTable));
        ordersTable->setHorizontalHeaderLabels({"Tid", "Ordre-ID", "Det der er solgt", "Point", "Note"});
        ordersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        ordersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        ordersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        ordersTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        ordersTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        ordersTable->setColumnWidth(0, 120);

        ordersTable->setAlternatingRowColors(true);
        ordersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ordersTable->setSelectionMode(QAbstractItemView::SingleSelection);
        ordersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ordersTable->verticalHeader()->setVisible(false);
        ordersTable->verticalHeader()->setDefaultSectionSize(42);
        ordersTable->setWordWrap(true);
        ordersTable->setTextElideMode(Qt::ElideNone);
        ordersTable->setShowGrid(false);
        ordersTable->setMinimumHeight(220);
        ordersTable->setMaximumHeight(320);
        ordersTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        styleDataTable(ordersTable, true);

        tableCard.second->addWidget(ordersTable);
        tableCard.second->addStretch();
        layout->addWidget(tableCard.first, 1);

        // 🔌 CONNECTS (HER SKAL DE STÅ)
        connect(refreshBtn, &QPushButton::clicked, this, [this]() {
            refreshOrdersTable();
        });

        connect(addBtn, &QPushButton::clicked, this, [this]() {
            createOrder();
        });

        connect(editBtn, &QPushButton::clicked, this, [this]() {
            editSelectedOrder();
        });

        connect(deleteBtn, &QPushButton::clicked, this, [this]() {
            deleteSelectedOrder();
        });

        return w;
    }

    QWidget* buildReportsTab() {
        auto* w = new QWidget;
        auto* layout = new QVBoxLayout(w);
        layout->setSpacing(18);

        auto controlsCard = createCard("Rapportindstillinger");
        auto* top = new QHBoxLayout;
        top->setContentsMargins(0, 0, 0, 0);

        reportPresetCombo = new QComboBox;
        reportPresetCombo->addItems({"I dag", "Denne arbejdsuge", "Seneste 2 arbejdsuger", "Denne lønmåned", "Vælg måned"});

        reportMonthEdit = new QDateEdit(QDate::currentDate());
        reportMonthEdit->setDisplayFormat("MMMM yyyy");
        reportMonthEdit->setDate(QDate::currentDate());
        reportMonthEdit->setCalendarPopup(true);
        reportMonthEdit->setLocale(QLocale(QLocale::Danish, QLocale::Denmark));

        auto* exportBtn = new QPushButton("Gem rapport");

        top->addWidget(new QLabel("Visning:"));
        top->addWidget(reportPresetCombo);
        top->addWidget(new QLabel("Måned:"));
        top->addWidget(reportMonthEdit);
        top->addWidget(exportBtn);
        top->addStretch();

        controlsCard.second->addLayout(top);
        layout->addWidget(controlsCard.first);

        auto reportCard = createCard("Rapport");
        reportText = new QTextEdit;
        reportText->setReadOnly(true);
        reportText->setTextInteractionFlags(Qt::NoTextInteraction);
        reportText->setFocusPolicy(Qt::NoFocus);
        reportText->setFrameShape(QFrame::NoFrame);
        reportText->document()->setDocumentMargin(18);
        reportText->setStyleSheet(R"(
        QTextEdit {
            background: #0F172A;
            color: #EAF4FF;
            border: none;
            border-radius: 18px;
            padding: 18px;
            font-size: 14px;
        }
    )");

        reportCard.second->addWidget(reportText);
        layout->addWidget(reportCard.first, 1);

        connect(reportPresetCombo, &QComboBox::currentIndexChanged, this, [this](int) { generateReport(); });
        connect(reportMonthEdit, &QDateEdit::dateChanged, this, [this](const QDate&) { generateReport(); });
        connect(exportBtn, &QPushButton::clicked, this, [this]() { exportCurrentReport(); });

        return w;
    }

    QWidget* buildSettingsTab() {
        auto* w = new QWidget;
        auto* layout = new QHBoxLayout(w);
        layout->setSpacing(18);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* left = new QVBoxLayout;
        left->setSpacing(18);

        auto goalCard = createCard("Mål");
        auto* form = new QFormLayout;
        form->setSpacing(12);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);

        targetSpin = new QDoubleSpinBox;
        targetSpin->setRange(0, 100000);
        targetSpin->setDecimals(2);
        targetSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);

        monthlySalesTargetSpin = new QSpinBox;
        monthlySalesTargetSpin->setRange(0, 100000);
        monthlySalesTargetSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);

        form->addRow("Pointmål for måneden", targetSpin);
        form->addRow("Salgsmål for måneden", monthlySalesTargetSpin);

        auto* saveGoalBtn = new QPushButton("Gem mål");
        form->addRow(saveGoalBtn);

        goalCard.second->addLayout(form);
        goalCard.second->addStretch();
        left->addWidget(goalCard.first, 1);

        auto intramanagerCard = createCard("Intramanager og timeløn");

        auto* imForm = new QFormLayout;
        imForm->setSpacing(12);

        intramanagerEnabledCheck = new QCheckBox("Aktivér Intramanager og automatisk timehentning");
        intramanagerEnabledCheck->setFocusPolicy(Qt::NoFocus);

        intramanagerUsernameEdit = new QLineEdit;
        intramanagerUsernameEdit->setPlaceholderText("Brugernavn til Intramanager");

        intramanagerPasswordEdit = new QLineEdit;
        intramanagerPasswordEdit->setEchoMode(QLineEdit::Password);
        intramanagerPasswordEdit->setPlaceholderText("Adgangskode til Intramanager");

        hourlyRateSpin = new QDoubleSpinBox;
        hourlyRateSpin->setRange(0, 10000);
        hourlyRateSpin->setDecimals(2);
        hourlyRateSpin->setSuffix(" kr/t");
        hourlyRateSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);

        auto* saveIntramanagerBtn = new QPushButton("Gem Intramanager og timeløn");

        intramanagerStatusLabel = new QLabel("Timer hentes automatisk, når rapporter har brug for dem.");
        intramanagerStatusLabel->setWordWrap(true);
        intramanagerStatusLabel->setTextInteractionFlags(Qt::NoTextInteraction);

        imForm->addRow(intramanagerEnabledCheck);
        imForm->addRow("Brugernavn", intramanagerUsernameEdit);
        imForm->addRow("Adgangskode", intramanagerPasswordEdit);
        imForm->addRow("Timeløn", hourlyRateSpin);
        imForm->addRow(saveIntramanagerBtn);
        imForm->addRow("Status", intramanagerStatusLabel);

        intramanagerCard.second->addLayout(imForm);
        intramanagerCard.second->addStretch();

        left->addWidget(intramanagerCard.first, 1);

        auto salesRegistrationCard = createCard("Salgsregistrering");
        auto* salesRegForm = new QFormLayout;
        salesRegForm->setSpacing(12);

        defaultSellerInitialsEdit = new QLineEdit;
        defaultSellerInitialsEdit->setPlaceholderText("Standard initialer ved nye ordrer");

        salesRegistrationWebhookEdit = new QLineEdit;
        salesRegistrationWebhookEdit->setPlaceholderText("Power Automate webhook URL");

        salesRegistrationRecipientEdit = new QLineEdit;
        salesRegistrationRecipientEdit->setPlaceholderText("Mail der skal modtage salgs-reg");

        salesRegistrationEnabledCheck = new QCheckBox("Send salgs-reg automatisk ved ny ordre");
        salesRegistrationEnabledCheck->setFocusPolicy(Qt::NoFocus);

        auto* saveSalesRegistrationBtn = new QPushButton("Gem salgsregistrering");
        auto* testSalesRegistrationBtn = new QPushButton("Test webflow");

        salesRegistrationStatusLabel = new QLabel("Sender salgs-reg til en webflow, der opdaterer Excel Online og Outlook.");
        salesRegistrationStatusLabel->setWordWrap(true);
        salesRegistrationStatusLabel->setTextInteractionFlags(Qt::NoTextInteraction);

        salesRegForm->addRow("Sælger initialer", defaultSellerInitialsEdit);
        salesRegForm->addRow("Webhook URL", salesRegistrationWebhookEdit);
        salesRegForm->addRow("Modtager-mail", salesRegistrationRecipientEdit);
        salesRegForm->addRow(salesRegistrationEnabledCheck);
        salesRegForm->addRow(saveSalesRegistrationBtn);
        salesRegForm->addRow(testSalesRegistrationBtn);
        salesRegForm->addRow("Status", salesRegistrationStatusLabel);

        salesRegistrationCard.second->addLayout(salesRegForm);
        salesRegistrationCard.second->addStretch();
        left->addWidget(salesRegistrationCard.first, 1);

        auto* right = new QVBoxLayout;
        right->setSpacing(18);

        auto sellerCard = createCard("Sælgere");
        salespeopleList = new QListWidget;

        auto* sellerNameEdit = new QLineEdit;
        sellerNameEdit->setPlaceholderText("Nyt sælgernavn");

        auto* addSellerBtn = new QPushButton("Tilføj sælger");
        auto* activateBtn = new QPushButton("Sæt som aktiv");
        auto* deleteSellerBtn = new QPushButton("Slet valgt sælger");

        sellerCard.second->addWidget(salespeopleList);
        sellerCard.second->addWidget(sellerNameEdit);
        sellerCard.second->addWidget(addSellerBtn);
        sellerCard.second->addWidget(activateBtn);
        sellerCard.second->addWidget(deleteSellerBtn);
        sellerCard.second->addStretch();

        right->addWidget(sellerCard.first, 1);

        auto backupCard = createCard("Backup");
        auto* exportBackupBtn = new QPushButton("Eksportér backup");
        auto* importBackupBtn = new QPushButton("Importér backup");

        backupCard.second->addWidget(exportBackupBtn);
        backupCard.second->addWidget(importBackupBtn);
        backupCard.second->addStretch();

        right->addWidget(backupCard.first);

        layout->addLayout(left, 1);
        layout->addLayout(right, 1);

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
            repo.settings.hourlyRate = hourlyRateSpin->value();

            repo.saveSettings();

            bool passwordSaved = true;

            if (!password.isEmpty()) {
                passwordSaved = saveIntramanagerPassword(username, password);
            }

            if (!passwordSaved) {
                intramanagerStatusLabel->setText("Indstillinger gemt, men adgangskoden kunne ikke gemmes sikkert.");
            } else {
                intramanagerStatusLabel->setText("Intramanager og timeløn er gemt.");
                intramanagerPasswordEdit->clear();
                intramanagerPasswordEdit->setPlaceholderText("Adgangskode er gemt sikkert");
            }

            refreshAll();
            setupIntramanagerAutoSync();
            refreshIntramanagerPunchStatusAsync(true);
        });

        connect(saveSalesRegistrationBtn, &QPushButton::clicked, this, [this]() {
            repo.settings.defaultSellerInitials = defaultSellerInitialsEdit->text().trimmed();
            repo.settings.salesRegistrationWebhookUrl = salesRegistrationWebhookEdit->text().trimmed();
            repo.settings.salesRegistrationRecipient = salesRegistrationRecipientEdit->text().trimmed();
            repo.settings.salesRegistrationEnabled = salesRegistrationEnabledCheck->isChecked();
            repo.saveSettings();
            if (salesRegistrationStatusLabel) {
                salesRegistrationStatusLabel->setText("Salgsregistrering er gemt.");
            }
        });

        connect(testSalesRegistrationBtn, &QPushButton::clicked, this, [this]() {
            repo.settings.defaultSellerInitials = defaultSellerInitialsEdit->text().trimmed();
            repo.settings.salesRegistrationWebhookUrl = salesRegistrationWebhookEdit->text().trimmed();
            repo.settings.salesRegistrationRecipient = salesRegistrationRecipientEdit->text().trimmed();
            repo.settings.salesRegistrationEnabled = salesRegistrationEnabledCheck->isChecked();
            repo.saveSettings();
            testSalesRegistrationWebhookAsync();
        });

        connect(addSellerBtn, &QPushButton::clicked, this, [this, sellerNameEdit]() {
            const QString name = sellerNameEdit->text().trimmed();
            if (name.isEmpty()) return;

            repo.salespeople.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces), name});
            repo.saveSalespeople();
            sellerNameEdit->clear();
            refreshSalespeopleUi();
        });

        connect(activateBtn, &QPushButton::clicked, this, [this]() {
            const int row = salespeopleList->currentRow();
            if (row < 0 || row >= static_cast<int>(repo.salespeople.size())) return;

            repo.settings.activeSalespersonId = repo.salespeople[row].id;
            repo.saveSettings();
            refreshAll();
        });

        connect(deleteSellerBtn, &QPushButton::clicked, this, [this]() {
            const int row = salespeopleList->currentRow();
            if (row < 0 || row >= static_cast<int>(repo.salespeople.size())) return;
            if (repo.salespeople.size() <= 1) {
                QMessageBox::warning(this, "Kan ikke slette", "Der skal være mindst én sælger i programmet.");
                return;
            }

            const auto seller = repo.salespeople[row];
            if (!confirmQuestion(this, "Slet sælger", QString("Er du sikker på, at du vil slette '%1'?").arg(seller.name))) {
                return;
            }

            repo.salespeople.removeAt(row);

            if (repo.settings.activeSalespersonId == seller.id) {
                repo.settings.activeSalespersonId =
                    repo.salespeople.isEmpty() ? QString() : repo.salespeople.first().id;
            }

            repo.saveSalespeople();
            repo.saveSettings();
            refreshAll();
        });

        connect(exportBackupBtn, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getSaveFileName(
                this,
                "Eksportér backup",
                repo.baseDir() + "/backup.json",
                "JSON files (*.json)"
                );
            if (path.isEmpty()) return;

            QJsonObject root;
            QJsonArray salespeopleJson;
            for (const auto& s : repo.salespeople) salespeopleJson.append(toJson(s));

            QJsonArray productsJson;
            for (const auto& p : repo.products) productsJson.append(toJson(p));

            QJsonArray ordersJson;
            for (const auto& o : repo.orders) ordersJson.append(toJson(o));

            root["salespeople"] = salespeopleJson;
            root["products"] = productsJson;
            root["orders"] = ordersJson;
            root["settings"] = toJson(repo.settings);

            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                QMessageBox::warning(this, "Fejl", "Kunne ikke gemme backup.");
                return;
            }

            f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            QMessageBox::information(this, "Backup gemt", "Backup blev eksporteret.");
        });

        connect(importBackupBtn, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this,
                "Importér backup",
                repo.baseDir(),
                "JSON files (*.json)"
                );
            if (path.isEmpty()) return;

            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, "Fejl", "Kunne ikke åbne backup-filen.");
                return;
            }

            const auto doc = QJsonDocument::fromJson(f.readAll());
            if (!doc.isObject()) {
                QMessageBox::warning(this, "Fejl", "Backup-filen er ugyldig.");
                return;
            }

            const auto rootObj = doc.object();

            QVector<Salesperson> importedSalespeople;
            for (const auto& v : rootObj["salespeople"].toArray()) {
                importedSalespeople.push_back(fromSalespersonJson(v.toObject()));
            }

            QVector<Product> importedProducts;
            for (const auto& v : rootObj["products"].toArray()) {
                importedProducts.push_back(fromProductJson(v.toObject()));
            }

            QVector<Order> importedOrders;
            for (const auto& v : rootObj["orders"].toArray()) {
                importedOrders.push_back(fromOrderJson(v.toObject()));
            }

            AppSettings importedSettings = fromSettingsJson(rootObj["settings"].toObject());

            repo.salespeople = importedSalespeople;
            repo.products = importedProducts;
            repo.orders = importedOrders;
            repo.settings = importedSettings;
            repo.saveAll();
            refreshAll();

            QMessageBox::information(this, "Backup importeret", "Backup blev importeret.");
        });

        return w;
    }

    bool exportBackup() {
        const QString defaultPath = repo.baseDir() + "/backup_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".json";
        const QString path = QFileDialog::getSaveFileName(this, "Gem backup", defaultPath, "JSON-filer (*.json)");
        if (path.isEmpty()) return false;

        QJsonArray salespeopleArray;
        for (const auto& s : repo.salespeople) salespeopleArray.append(toJson(s));
        QJsonArray productsArray;
        for (const auto& p : repo.products) productsArray.append(toJson(p));
        QJsonArray ordersArray;
        for (const auto& o : repo.orders) ordersArray.append(toJson(o));

        QJsonObject root{
            {"exportedAt", QDateTime::currentDateTime().toString(Qt::ISODate)},
            {"salespeople", salespeopleArray},
            {"products", productsArray},
            {"orders", ordersArray},
            {"settings", toJson(repo.settings)}
        };

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Fejl", "Kunne ikke gemme backup-filen.");
            return false;
        }
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        QMessageBox::information(this, "Backup gemt", "Backup-filen blev gemt.");
        return true;
    }

    bool importBackup() {
        const QString path = QFileDialog::getOpenFileName(this, "Vælg backup-fil", repo.baseDir(), "JSON-filer (*.json)");
        if (path.isEmpty()) return false;

        if (!confirmQuestion(this, "Importér backup", "Det her overskriver nuværende lokale data. Vil du fortsætte?")) {
            return false;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Fejl", "Kunne ikke åbne backup-filen.");
            return false;
        }

        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, "Fejl", "Backup-filen kunne ikke læses.");
            return false;
        }

        const QJsonObject root = doc.object();
        if (!root.contains("salespeople") || !root.contains("products") || !root.contains("orders") || !root.contains("settings")) {
            QMessageBox::warning(this, "Fejl", "Backup-filen mangler nødvendige felter.");
            return false;
        }

        QVector<Salesperson> importedSalespeople;
        for (const auto& v : root["salespeople"].toArray()) importedSalespeople.push_back(fromSalespersonJson(v.toObject()));
        QVector<Product> importedProducts;
        for (const auto& v : root["products"].toArray()) importedProducts.push_back(fromProductJson(v.toObject()));
        QVector<Order> importedOrders;
        for (const auto& v : root["orders"].toArray()) importedOrders.push_back(fromOrderJson(v.toObject()));
        const auto importedSettings = fromSettingsJson(root["settings"].toObject());

        if (importedSalespeople.isEmpty()) {
            QMessageBox::warning(this, "Fejl", "Backup-filen indeholder ingen sælgere.");
            return false;
        }

        repo.salespeople = importedSalespeople;
        repo.products = importedProducts;
        repo.orders = importedOrders;
        repo.settings = importedSettings;
        if (!repo.findSalesperson(repo.settings.activeSalespersonId)) {
            repo.settings.activeSalespersonId = repo.salespeople.first().id;
        }
        repo.saveAll();
        refreshAll();
        QMessageBox::information(this, "Backup importeret", "Backup-filen blev importeret.");
        return true;
    }

    void cleanupAutoBackups() {
        QDir dir(repo.baseDir());
        const QStringList files = dir.entryList({"auto_backup_*.json"}, QDir::Files, QDir::Time);
        for (int i = 1; i < files.size(); ++i) {
            dir.remove(files[i]);
        }
    }

    void createAutoBackup() {
        QJsonArray salespeopleArray;
        for (const auto& s : repo.salespeople) salespeopleArray.append(toJson(s));
        QJsonArray productsArray;
        for (const auto& p : repo.products) productsArray.append(toJson(p));
        QJsonArray ordersArray;
        for (const auto& o : repo.orders) ordersArray.append(toJson(o));

        QJsonObject root{
            {"exportedAt", QDateTime::currentDateTime().toString(Qt::ISODate)},
            {"salespeople", salespeopleArray},
            {"products", productsArray},
            {"orders", ordersArray},
            {"settings", toJson(repo.settings)}
        };

        const QString path = repo.baseDir() + "/auto_backup_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".json";
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
            cleanupAutoBackups();
        }
    }

    void chooseSalesperson() {
        SalespersonPickerDialog dlg(repo, this);
        dlg.exec();
        refreshAll();
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
            parts << QString("%1 x%2").arg(p->displayName).arg(item.quantity);
            points += p->points * item.quantity;
        }
        if (totalPoints) *totalPoints = points;
        return parts.join(" | ");
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
        QString chunkColor = "#14B8A6";
        if (ratio >= 1.0 || bonusClose) chunkColor = "#22C55E";
        else if (ratio >= 0.80) chunkColor = "#34D399";
        else if (ratio >= 0.55) chunkColor = "#14B8A6";
        else chunkColor = "#0EA5A4";

        bar->setProperty("progressColor", QColor(chunkColor));
        bar->update();
    }

    QString moneySpan(double amount, const QString& color = "#34D399") const {
        return QString("<span style=\"color:%1;font-weight:700;\">%2 kr</span>").arg(color, money(amount));
    }

    QString countSpan(int value, const QString& color = "#34D399") const {
        return QString("<span style=\"color:%1;font-weight:700;\">%2</span>").arg(color).arg(value);
    }

    QString plainBadgeText(const QString& label, const QString& value) const {
        return QString("<span style=\"color:#9CC7E8;\">%1</span> <span style=\"color:#F8FBFF;font-weight:700;\">%2</span>").arg(label, value);
    }

    void refreshAll() {
        const auto* s = activeSalesperson();
        activeSalespersonLabel->setText(s ? QString("Du arbejder som <b>%1</b>").arg(s->name) : "Ingen aktiv sælger");
        refreshDashboard();
        refreshOrdersTable();
        refreshSalespeopleUi();
        refreshSettingsUi();
        generateReport();
    }

    void refreshDashboard() {
        const auto* s = activeSalesperson();
        if (!s) return;
        refreshPunchCardUi();

        const QDate now = QDate::currentDate();
        const auto todayRange = qMakePair(QDateTime(now, QTime(0,0,0)), QDateTime(now, QTime(23,59,59)));
        const auto calendarMonthRange = monthRange(now);
        const auto dayBonusPeriod = payrollBonusRange(now);

        const auto mDay = CommissionEngine::calculate(repo, s->id, todayRange.first, todayRange.second);
        const auto mMonth = CommissionEngine::calculate(
            repo,
            s->id,
            calendarMonthRange.first,
            calendarMonthRange.second,
            dayBonusPeriod
            );

        if (daySummaryLabel) daySummaryLabel->setText(summaryCardText(mDay));
        if (monthSummaryLabel) monthSummaryLabel->setText(summaryCardText(mMonth));

        if (kpiTodayPointsLabel) kpiTodayPointsLabel->setText(money(mDay.totalPoints));

        const QString currentHoursFrom = intramanagerDate(dayBonusPeriod.first.date());
        const QString currentHoursTo = intramanagerDate(dayBonusPeriod.second.date());
        const double workedHoursForCurrentPeriod =
            cachedIntramanagerHours(currentHoursFrom, currentHoursTo)
                .value_or(IntramanagerHoursEntry{currentHoursFrom, currentHoursTo, 0.0, 0.0, QString()})
                .hours;
        const double baseSalary = workedHoursForCurrentPeriod * repo.settings.hourlyRate;
        const double totalSalary = baseSalary + mMonth.totalCommission;

        if (kpiMonthCommissionLabel) {
            kpiMonthCommissionLabel->setText(
                QString(
                    "Løn denne måned: %1 kr\n"
                    "Grundløn indtil videre: %2 kr\n"
                    "Provision indtil videre: %3 kr"
                    )
                    .arg(money(totalSalary))
                    .arg(money(baseSalary))
                    .arg(money(mMonth.totalCommission))
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
                targetProgressHintLabel->setText(QString("Du mangler <b>%1 point</b>. Det svarer til cirka <b>%2 point</b> pr. resterende arbejdsdag.").arg(money(pointsToTarget)).arg(money(requiredPointsPerRemainingDay)));
            } else {
                targetProgressHintLabel->setText(QString("Pointmålet er hjemme. Du ligger <b>%1 point</b> over målet.").arg(money(mMonth.totalPoints - repo.settings.bonus.monthlyTargetPoints)));
            }
        }

        if (salesTargetProgressBar) {
            const int salesMax = qMax(1, repo.settings.monthlySalesTarget > 0 ? repo.settings.monthlySalesTarget : qMax(1, mMonth.salesCount));
            salesTargetProgressBar->setRange(0, salesMax);
            const int salesValue = qMin(mMonth.salesCount, salesMax);
            animateProgressBar(salesTargetProgressBar, salesValue);
            salesTargetProgressBar->setFormat(repo.settings.monthlySalesTarget > 0
                ? QString("%1 / %2 salg").arg(mMonth.salesCount).arg(repo.settings.monthlySalesTarget)
                : QString("%1 salg").arg(mMonth.salesCount));
            applyProgressMood(salesTargetProgressBar, salesMax > 0 ? double(salesValue) / salesMax : 0.0, missingSalesToTarget <= 3 && repo.settings.monthlySalesTarget > 0);
        }
        if (salesTargetProgressHintLabel) {
            if (repo.settings.monthlySalesTarget <= 0) {
                salesTargetProgressHintLabel->setText("Sæt et salgsmål i Indstillinger for at få live fremdrift på måneden.");
            } else if (missingSalesToTarget > 0) {
                salesTargetProgressHintLabel->setText(QString("Du mangler <b>%1 salg</b> for at ramme målet denne måned.").arg(missingSalesToTarget));
            } else {
                salesTargetProgressHintLabel->setText(QString("Salgsmålet er ramt. Du ligger <b>%1 salg</b> over målet.").arg(mMonth.salesCount - repo.settings.monthlySalesTarget));
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
                simoProgressHintLabel->setText(QString("SIMO åbner ved <b>%1</b>. Du mangler <b>%2</b> for at tænde pengesporet.").arg(repo.settings.bonus.simoMinEligible).arg(qMax(0, repo.settings.bonus.simoMinEligible - mMonth.simoCount)));
            } else {
                simoProgressHintLabel->setText(QString("Næste SIMO-hop ligger ved <b>%1</b>. Du mangler <b>%2</b>, og så står bonussen på %3.").arg(nextSimoStep).arg(missingToNextSimo).arg(moneySpan(nextSimoBonus, missingToNextSimo <= 1 ? "#22C55E" : "#34D399")));
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
                voiceProgressHintLabel->setText(QString("VOICE åbner ved <b>%1</b>. Du mangler <b>%2</b>, og derefter hopper den for hver <b>10</b>.").arg(repo.settings.bonus.voiceMinEligible).arg(qMax(0, repo.settings.bonus.voiceMinEligible - mMonth.voiceCount)));
            } else {
                voiceProgressHintLabel->setText(QString("Næste VOICE-hop ligger ved <b>%1</b>. Du mangler <b>%2</b>, og så står bonussen på %3.").arg(nextVoiceStep).arg(missingToNextVoice).arg(moneySpan(nextVoiceBonus, missingToNextVoice <= 2 ? "#22C55E" : "#34D399")));
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

    void refreshOrdersTable() {
        const auto indices = activeOrderIndicesSorted();
        ordersTable->setRowCount(indices.size());
        auto makeOrderItem = [](const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setForeground(QBrush(QColor("#EAF4FF")));
            return item;
        };
        for (int row = 0; row < static_cast<int>(indices.size()); ++row) {
            const auto& order = repo.orders[indices[row]];
            double pts = 0.0;
            const QString products = orderProductsSummary(order, &pts);
            ordersTable->setItem(row, 0, makeOrderItem(order.createdAt.toString("dd-MM-yyyy HH:mm")));
            ordersTable->setItem(row, 1, makeOrderItem(order.id));
            ordersTable->setItem(row, 2, makeOrderItem(products));
            ordersTable->setItem(row, 3, makeOrderItem(money(pts)));
            ordersTable->setItem(row, 4, makeOrderItem(order.note));
            ordersTable->item(row, 1)->setToolTip(order.id);
            ordersTable->item(row, 2)->setToolTip(products);
            ordersTable->item(row, 4)->setToolTip(order.note);
        }
        ordersTable->resizeRowsToContents();
        ordersTable->resizeColumnToContents(0);
        ordersTable->resizeColumnToContents(1);
        ordersTable->horizontalHeader()->setMinimumSectionSize(80);
    }

    void refreshSalespeopleUi() {
        salespeopleList->clear();
        for (const auto& s : repo.salespeople) {
            QString text = s.name;
            if (s.id == repo.settings.activeSalespersonId) text += " (aktiv)";
            salespeopleList->addItem(text);
        }
    }

    void refreshSettingsUi() {
        if (targetSpin) targetSpin->setValue(repo.settings.bonus.monthlyTargetPoints);
        if (monthlySalesTargetSpin) monthlySalesTargetSpin->setValue(repo.settings.monthlySalesTarget);

        if (hourlyRateSpin) {
            hourlyRateSpin->setValue(repo.settings.hourlyRate);
        }

        if (intramanagerUsernameEdit) {
            intramanagerUsernameEdit->setText(repo.settings.intramanagerUsername);
        }

        if (intramanagerEnabledCheck) {
            intramanagerEnabledCheck->setChecked(repo.settings.intramanagerEnabled);
        }

        if (intramanagerPasswordEdit) {
            QString storedUser;
            QString storedPass;
            const bool hasPassword = loadIntramanagerPassword(&storedUser, &storedPass);

            intramanagerPasswordEdit->clear();

            if (hasPassword && !storedPass.isEmpty()) {
                intramanagerPasswordEdit->setPlaceholderText("Adgangskode er gemt sikkert");
            } else {
                intramanagerPasswordEdit->setPlaceholderText("Indtast Intramanager adgangskode");
            }
        }

        if (intramanagerStatusLabel) {
            intramanagerStatusLabel->setText("Timer hentes automatisk, når rapporter har brug for dem.");
        }

        if (defaultSellerInitialsEdit) {
            defaultSellerInitialsEdit->setText(repo.settings.defaultSellerInitials);
        }
        if (salesRegistrationWebhookEdit) {
            salesRegistrationWebhookEdit->setText(repo.settings.salesRegistrationWebhookUrl);
        }
        if (salesRegistrationRecipientEdit) {
            salesRegistrationRecipientEdit->setText(repo.settings.salesRegistrationRecipient);
        }
        if (salesRegistrationEnabledCheck) {
            salesRegistrationEnabledCheck->setChecked(repo.settings.salesRegistrationEnabled);
        }
        if (salesRegistrationStatusLabel) {
            salesRegistrationStatusLabel->setText("Sender salgs-reg til en webflow, der opdaterer Excel Online og Outlook.");
        }

        refreshPunchCardUi();
    }

    QStringList salesRegistrationAliases(const Product& product) const {
        QStringList aliases;
        aliases << product.displayName << product.key;

        QString trimmed = product.displayName;
        trimmed.remove("Tillæg ", Qt::CaseInsensitive);
        trimmed.remove("Mobil ", Qt::CaseInsensitive);
        trimmed.remove("Mobilt bredbånd ", Qt::CaseInsensitive);
        trimmed.remove("mdr", Qt::CaseInsensitive);
        aliases << trimmed.trimmed();

        if (product.key == "til_1000gb_data") {
            aliases << "1000GB data" << "1000 GB data" << "Add-on" << "Addon";
        } else if (product.key == "til_true_talk_firma") {
            aliases << "TrueTalk Firma/Agent" << "TrueTalk Firma + Agent" << "Truetalk firma";
        }

        aliases.removeDuplicates();
        return aliases;
    }

    QString salesRegistrationCategoryColor(const QString& category) const {
        if (category.compare("Mobil", Qt::CaseInsensitive) == 0) return "#92D050";
        if (category.compare("Tillæg", Qt::CaseInsensitive) == 0) return "#FFC000";
        if (category.compare("Mobilt bredbånd", Qt::CaseInsensitive) == 0) return "#00B0F0";
        if (category.compare("FWA", Qt::CaseInsensitive) == 0) return "#ED7D31";
        if (category.compare("Fiber", Qt::CaseInsensitive) == 0) return "#FF66A1";
        return "#BFBFBF";
    }

    QString htmlEscape(const QString& text) const {
        QString out = text;
        out.replace("&", "&amp;");
        out.replace("<", "&lt;");
        out.replace(">", "&gt;");
        out.replace("\"", "&quot;");
        return out;
    }

    QString salesRegistrationMailHtml(const Order& order) const {
        QMap<QString, int> quantities;
        for (const auto& item : order.items) {
            quantities[item.productKey] += item.quantity;
        }

        QString html = "<table style=\"border-collapse:collapse;font-family:Arial,sans-serif;font-size:11px;\">";
        html += "<tr>";
        const QString fixedHeaderStyle = "border:1px solid #111;padding:4px 6px;background:#A6A6A6;color:#111;font-weight:700;white-space:nowrap;";
        const QStringList fixedHeaders = {"Dato", "Initialer", "OSE-nr", "Cvr nr.", "Firmanavn", "Telefon"};
        for (const QString& header : fixedHeaders) {
            html += "<th style=\"" + fixedHeaderStyle + "\">" + htmlEscape(header) + "</th>";
        }
        for (const auto& product : repo.products) {
            const QString style = "border:1px solid #111;padding:4px 6px;background:" + salesRegistrationCategoryColor(product.category)
                + ";color:#111;font-weight:700;white-space:nowrap;";
            html += "<th style=\"" + style + "\">" + htmlEscape(product.displayName) + "</th>";
        }
        html += "</tr><tr>";

        const QString valueStyle = "border:1px solid #111;padding:4px 6px;background:#fff;color:#111;white-space:nowrap;";
        const QStringList fixedValues = {
            order.createdAt.date().toString("dd.MM.yy"),
            order.sellerInitials,
            order.id,
            order.cvrNumber,
            order.companyName,
            order.phoneNumber
        };
        for (const QString& value : fixedValues) {
            html += "<td style=\"" + valueStyle + "\">" + htmlEscape(value) + "</td>";
        }
        for (const auto& product : repo.products) {
            const int qty = quantities.value(product.key, 0);
            html += "<td style=\"" + valueStyle + "\">" + (qty > 0 ? QString::number(qty) : QString()) + "</td>";
        }
        html += "</tr></table>";
        return html;
    }

    QJsonObject salesRegistrationPayload(const Order& order) const {
        QJsonArray items;
        for (const auto& item : order.items) {
            const Product* product = repo.findProduct(item.productKey);
            if (!product) continue;

            QJsonArray aliases;
            for (const QString& alias : salesRegistrationAliases(*product)) {
                if (!alias.trimmed().isEmpty()) aliases.append(alias.trimmed());
            }

            QJsonObject obj;
            obj["key"] = product->key;
            obj["productName"] = product->displayName;
            obj["category"] = product->category;
            obj["quantity"] = item.quantity;
            obj["points"] = product->points;
            obj["aliases"] = aliases;
            items.append(obj);
        }

        QJsonObject payload;
        payload["source"] = "Provi Tracker";
        payload["type"] = "sales_registration";
        payload["recipient"] = repo.settings.salesRegistrationRecipient;
        payload["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        payload["date"] = order.createdAt.date().toString("dd.MM.yy");
        payload["sellerInitials"] = order.sellerInitials;
        payload["orderNumber"] = order.id;
        payload["cvrNumber"] = order.cvrNumber;
        payload["companyName"] = order.companyName;
        payload["phoneNumber"] = order.phoneNumber;
        payload["note"] = order.note;
        payload["items"] = items;
        payload["mailSubject"] = QString("Salgs reg - %1 - %2").arg(order.companyName, order.id);
        payload["mailHtml"] = salesRegistrationMailHtml(order);
        return payload;
    }

    bool prepareSalesRegistrationWebhook(QUrl* urlOut, QString* errorOut) const {
        const QUrl webhookUrl(repo.settings.salesRegistrationWebhookUrl.trimmed());
        if (!webhookUrl.isValid() || webhookUrl.scheme().isEmpty()
            || (webhookUrl.scheme() != "https" && webhookUrl.scheme() != "http")
            || repo.settings.salesRegistrationRecipient.trimmed().isEmpty()) {
            if (errorOut) {
                *errorOut = "Salgsregistrering mangler webhook URL eller modtager-mail.";
            }
            return false;
        }

        if (urlOut) {
            *urlOut = webhookUrl;
        }
        return true;
    }

    void postSalesRegistrationPayloadAsync(
        const QJsonObject& payload,
        const QString& sendingText,
        const QString& defaultSuccessText,
        bool showWarnings
        ) {
        QUrl webhookUrl;
        QString configError;
        if (!prepareSalesRegistrationWebhook(&webhookUrl, &configError)) {
            if (salesRegistrationStatusLabel) {
                salesRegistrationStatusLabel->setText(configError);
            }
            return;
        }

        if (salesRegistrationStatusLabel) {
            salesRegistrationStatusLabel->setText(sendingText);
        }

        auto* manager = new QNetworkAccessManager(this);
        QNetworkRequest request(webhookUrl);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
        request.setTransferTimeout(60000);

        const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        QNetworkReply* reply = manager->post(request, body);

        connect(reply, &QNetworkReply::finished, this, [this, manager, reply, defaultSuccessText, showWarnings]() {
            const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray responseBody = reply->readAll();
            const QString networkError = reply->errorString();
            const bool ok = reply->error() == QNetworkReply::NoError && statusCode >= 200 && statusCode < 300;

            reply->deleteLater();
            manager->deleteLater();

            if (!ok) {
                const QString message = QString("Salgs-reg kunne ikke sendes til webflow (%1).").arg(statusCode > 0 ? QString::number(statusCode) : networkError);
                if (salesRegistrationStatusLabel) salesRegistrationStatusLabel->setText(message);
                if (showWarnings) {
                    QMessageBox::warning(this, "Salgsregistrering", message + "\n\n" + QString::fromUtf8(responseBody));
                }
                return;
            }

            QString message = defaultSuccessText;
            QJsonParseError err;
            const auto doc = QJsonDocument::fromJson(responseBody, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QString flowMessage = doc.object().value("message").toString();
                if (!flowMessage.isEmpty()) {
                    message = flowMessage;
                }
            }
            if (salesRegistrationStatusLabel) salesRegistrationStatusLabel->setText(message);
        });
    }

    void submitSalesRegistrationAsync(const Order& order) {
        if (!repo.settings.salesRegistrationEnabled) {
            return;
        }

        postSalesRegistrationPayloadAsync(
            salesRegistrationPayload(order),
            "Salgs-reg sendes til webflow...",
            "Salgs-reg sendt til webflow.",
            true
            );
    }

    void testSalesRegistrationWebhookAsync() {
        Order sample;
        sample.id = "TEST-" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
        sample.salespersonId = repo.settings.activeSalespersonId;
        sample.sellerInitials = repo.settings.defaultSellerInitials.isEmpty() ? "TEST" : repo.settings.defaultSellerInitials;
        sample.cvrNumber = "00000000";
        sample.companyName = "Webhook test";
        sample.phoneNumber = "00000000";
        sample.createdAt = QDateTime::currentDateTime();
        sample.note = "Test fra Provi Tracker";
        if (!repo.products.isEmpty()) {
            sample.items.push_back({repo.products.first().key, 1});
        }

        QJsonObject payload = salesRegistrationPayload(sample);
        payload["type"] = "sales_registration_test";
        payload["isTest"] = true;

        postSalesRegistrationPayloadAsync(
            payload,
            "Tester webflow...",
            "Webflow test OK.",
            true
            );
    }

    void createOrder() {
        const auto* s = activeSalesperson();
        if (!s) return;
        OrderEditorDialog dlg(repo, s->id, std::nullopt, this);
        if (dlg.exec() == QDialog::Accepted) {
            const Order order = dlg.getOrder();
            repo.orders.push_back(order);
            repo.saveOrders();
            submitSalesRegistrationAsync(order);
            refreshAll();
        }
    }

    int selectedOrderRepoIndex() const {
        const int selectedRow = ordersTable->currentRow();
        const auto indices = activeOrderIndicesSorted();
        if (selectedRow < 0 || selectedRow >= static_cast<int>(indices.size())) return -1;
        return indices[selectedRow];
    }

    void editSelectedOrder() {
        const int repoIndex = selectedOrderRepoIndex();
        if (repoIndex < 0) return;
        const auto* s = activeSalesperson();
        if (!s) return;
        OrderEditorDialog dlg(repo, s->id, repo.orders[repoIndex], this);
        if (dlg.exec() == QDialog::Accepted) {
            repo.orders[repoIndex] = dlg.getOrder();
            repo.saveOrders();
            refreshAll();
        }
    }

    void deleteSelectedOrder() {
        const int repoIndex = selectedOrderRepoIndex();
        if (repoIndex < 0) return;
        if (confirmQuestion(this, "Slet ordre", "Er du sikker på, at du vil slette den valgte ordre?")) {
            repo.orders.removeAt(repoIndex);
            repo.saveOrders();
            refreshAll();
        }
    }

    struct ReportRange {
        QString label;
        QDateTime from;
        QDateTime to;
    };

    ReportRange currentReportRange() const {
        const QDate now = QDate::currentDate();

        switch (reportPresetCombo->currentIndex()) {
            case 0:
                return {"I dag", QDateTime(now, QTime(0,0,0)), QDateTime(now, QTime(23,59,59))};

            case 1: {
                const auto r = workWeekRange(now);
                return {"Denne arbejdsuge", r.first, r.second};
            }

            case 2: {
                const auto r = previousAndCurrentWorkWeeksRange(now);
                return {"Seneste 2 arbejdsuger", r.first, r.second};
            }

            case 3: {
                const auto r = payrollBonusRange(now);
                return {"Denne lønmåned", r.first, r.second};
            }

            case 4:
            default: {
                const auto r = monthRange(reportMonthEdit->date());
                return {monthKey(reportMonthEdit->date()), r.first, r.second};
            }
        }
    }

    // Rapporten bruger timer for den valgte UI-periode, ikke bare "sidst hentet".
    QPair<QString, QString> reportHoursDates(const ReportRange& range) const {
        return {intramanagerDate(range.from.date()), intramanagerDate(range.to.date())};
    }

    QString reportHoursKey(const ReportRange& range) const {
        const auto dates = reportHoursDates(range);
        return intramanagerPeriodKey(dates.first, dates.second);
    }

    QString reportStatusHtml(const QString& title, const QString& message, const QString& detail = QString()) const {
        QString html;
        QTextStream ts(&html);
        ts << "<div style=\"font-family:Segoe UI,Arial,sans-serif;background:#0F172A;color:#E5EEF9;padding:24px;\">";
        ts << "<h1 style=\"margin:0 0 12px;color:#F8FBFF;\">" << title.toHtmlEscaped() << "</h1>";
        ts << "<div style=\"border:1px solid #223556;background:#111B2E;border-radius:18px;padding:16px;color:#D8F5FF;\">";
        ts << message.toHtmlEscaped();
        if (!detail.isEmpty()) {
            ts << "<br><span style=\"color:#BFD7EE;\">" << detail.toHtmlEscaped() << "</span>";
        }
        ts << "</div></div>";
        return html;
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
    void requestReportHours(const ReportRange& range, std::function<void(bool)> afterFetch) {
        if (!repo.settings.intramanagerEnabled) {
            if (afterFetch) afterFetch(false);
            return;
        }

        const auto dates = reportHoursDates(range);
        const QString key = intramanagerPeriodKey(dates.first, dates.second);

        if (intramanagerPendingFetchKeys.contains(key)) {
            QTimer::singleShot(1500, this, [this, range, afterFetch]() {
                if (reportHoursForRange(range).has_value()) {
                    if (afterFetch) afterFetch(true);
                } else {
                    requestReportHours(range, afterFetch);
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

    void generateReport() {
        const auto* s = activeSalesperson();
        if (!s) return;

        const auto range = currentReportRange();
        const auto dates = reportHoursDates(range);
        const QString requestedKey = reportHoursKey(range);
        const auto hoursEntry = reportHoursForRange(range);

        if (!hoursEntry.has_value()) {
            reportText->setHtml(
                reportStatusHtml(
                    "timer hentes...",
                    "Intramanager henter timer for " + intramanagerPeriodLabel(dates.first, dates.second) + ".",
                    "Rapporten opdateres automatisk bagefter."
                    )
                );

            requestReportHours(range, [this, requestedKey](bool ok) {
                if (reportHoursKey(currentReportRange()) != requestedKey) return;

                if (ok) {
                    generateReport();
                } else {
                    reportText->setHtml(
                        reportStatusHtml(
                            "timer kunne ikke hentes",
                            "Rapporten kunne ikke hente Intramanager-timer for den valgte periode.",
                            "Kontroller Intramanager-login og worker-filen i indstillinger."
                            )
                        );
                }
            });
            return;
        }

        const auto dayBonusPeriod = payrollBonusRange(range.to.date());

        const auto m = CommissionEngine::calculate(
            repo,
            s->id,
            range.from,
            range.to,
            dayBonusPeriod
            );

        const QString hoursPeriod = intramanagerPeriodLabel(hoursEntry->fromDate, hoursEntry->toDate);

        reportText->setHtml(
            ReportService::buildHtmlReport(
                repo,
                *s,
                range.label,
                m,
                hoursEntry->hours,
                repo.settings.hourlyRate,
                hoursPeriod
                )
            );
    }

    void exportCurrentReport() {
        const auto* s = activeSalesperson();
        if (!s) return;

        const auto range = currentReportRange();
        const auto dates = reportHoursDates(range);
        const auto hoursEntry = reportHoursForRange(range);

        if (!hoursEntry.has_value()) {
            reportText->setHtml(
                reportStatusHtml(
                    "timer hentes...",
                    "Intramanager henter timer for " + intramanagerPeriodLabel(dates.first, dates.second) + ".",
                    "Eksporten fortsætter, når timerne er klar."
                    )
                );

            requestReportHours(range, [this](bool ok) {
                if (ok) {
                    exportCurrentReport();
                } else {
                    reportText->setHtml(
                        reportStatusHtml(
                            "timer kunne ikke hentes",
                            "PDF-eksporten blev stoppet, fordi timerne ikke kunne hentes.",
                            "Kontroller Intramanager-login og worker-filen i indstillinger."
                            )
                        );
                }
            });
            return;
        }

        const auto dayBonusPeriod = payrollBonusRange(range.to.date());

        const auto m = CommissionEngine::calculate(
            repo,
            s->id,
            range.from,
            range.to,
            dayBonusPeriod
            );

        QString safeLabel = range.label;
        safeLabel.replace(' ', '_');
        const QString defaultPath = repo.reportDir() + "/" + s->id + "_" + safeLabel + ".pdf";
        const QString path = QFileDialog::getSaveFileName(this, "Gem rapport", defaultPath, "PDF-filer (*.pdf)");
        if (path.isEmpty()) return;
        const QString hoursPeriod = intramanagerPeriodLabel(hoursEntry->fromDate, hoursEntry->toDate);

        if (ReportService::exportPdf(
                path,
                repo,
                *s,
                range.label,
                m,
                hoursEntry->hours,
                repo.settings.hourlyRate,
                hoursPeriod
                )) {
            QMessageBox::information(this, "Eksporteret", "Rapport gemt.");
        } else {
            QMessageBox::warning(this, "Fejl", "Kunne ikke gemme rapporten.");
        }
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        // Lukning skal aldrig vente på netværk/worker; det ville fryse appen for almindelige brugere.
        createAutoBackup();
        QMainWindow::closeEvent(event);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    initAutoUpdate();

    MainWindow w;
    w.show();

    return app.exec();
}
