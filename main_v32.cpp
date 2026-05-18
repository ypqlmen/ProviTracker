#include <QtWidgets>
#include <QtCore>
#include <QtPrintSupport>
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
#include <optional>
#include <QLibrary>
#include "storage_paths.h"
#include "domain.h"
#include "credentials.h"
#include "repository.h"
#include "commission.h"
#include "report_service.h"

static constexpr const char* APP_VERSION = "1.3.20";
static constexpr const wchar_t* APP_VERSION_W = L"1.3.20";


static void initAutoUpdate()
{
    static QLibrary sparkle("WinSparkle");

    typedef void (*init_t)();
    typedef void (*set_url_t)(const char*);
    typedef void (*set_details_t)(const wchar_t*, const wchar_t*, const wchar_t*);
    typedef void (*set_auto_t)(int);
    typedef void (*set_interval_t)(int);
    typedef void (*check_without_ui_t)();
    typedef void (*check_with_ui_and_install_t)();
    typedef void (*cleanup_t)();

    if (!sparkle.load()) {
        return;
    }

    auto init = (init_t)sparkle.resolve("win_sparkle_init");
    auto set_url = (set_url_t)sparkle.resolve("win_sparkle_set_appcast_url");
    auto set_details = (set_details_t)sparkle.resolve("win_sparkle_set_app_details");
    auto set_auto = (set_auto_t)sparkle.resolve("win_sparkle_set_automatic_check_for_updates");
    auto set_interval = (set_interval_t)sparkle.resolve("win_sparkle_set_update_check_interval");
    auto check_without_ui = (check_without_ui_t)sparkle.resolve("win_sparkle_check_update_without_ui");
    auto check_with_ui_and_install = (check_with_ui_and_install_t)sparkle.resolve("win_sparkle_check_update_with_ui_and_install");
    auto cleanup = (cleanup_t)sparkle.resolve("win_sparkle_cleanup");

    if (init && set_url && set_details) {
        set_url("https://raw.githubusercontent.com/ypqlmen/ProviTracker/main/appcast.xml");
        set_details(L"Victor Tang", L"Provi Tracker", APP_VERSION_W);
        if (set_auto) set_auto(1);
        if (set_interval) set_interval(60 * 60);
        init();
        if (cleanup) {
            QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [cleanup]() {
                cleanup();
            });
        }
        if (check_with_ui_and_install || check_without_ui) {
            QTimer::singleShot(4000, qApp, [check_with_ui_and_install, check_without_ui]() {
                if (check_with_ui_and_install) {
                    check_with_ui_and_install();
                } else {
                    check_without_ui();
                }
            });
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

    card.first->setMinimumHeight(116);
    card.first->setMaximumHeight(148);
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


class SalespersonPickerDialog : public QDialog {
public:
    SalespersonPickerDialog(Repository& repo, QWidget* parent = nullptr)
        : QDialog(parent), repo(repo) {
        setWindowTitle("V?lg s?lger");
        resize(420, 300);
        auto* layout = new QVBoxLayout(this);
        list = new QListWidget;
        for (const auto& s : repo.salespeople) list->addItem(s.name);
        nameEdit = new QLineEdit;
        nameEdit->setPlaceholderText("Opret ny s?lger...");
        auto* createBtn = new QPushButton("Opret ny");
        auto* selectBtn = new QPushButton("Brug valgt s?lger");
        layout->addWidget(new QLabel("V?lg aktiv s?lger eller opret en ny:"));
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
        idEdit->setPlaceholderText("Indtast eller inds?t ordre-ID...");
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

        auto* addLineBtn = new QPushButton("Tilf?j produkt");
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
                QMessageBox::warning(this, "Manglende ordre-ID", "Du skal indtaste eller inds?tte et ordre-ID.");
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
                QMessageBox::warning(this, "Tom ordre", "Du skal tilf?je mindst ?t produkt.");
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
            btn->setToolTip(QString("Tilf?j %1").arg(p->displayName));
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
            btn->setToolTip(QString("Tilf?j %1 igen").arg(p->displayName));
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
            if (a == "Mobilt bredb?nd") return true;
            if (b == "Mobilt bredb?nd") return false;
            if (a == "Till?g") return false;
            if (b == "Till?g") return true;
            return a.localeAwareCompare(b) < 0;
        });
        return out;
    }

    QString productInfoText(const Product* p) const {
        if (!p) return "V?lg produkt";
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
    QLabel* kpiNextMonthPayLabel = nullptr;
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
    QMap<QString, QDateTime> intramanagerReportRefreshRequestedAt;

    QDoubleSpinBox* hourlyRateSpin = nullptr;
    QLineEdit* intramanagerUsernameEdit = nullptr;
    QLineEdit* intramanagerPasswordEdit = nullptr;
    QCheckBox* intramanagerEnabledCheck = nullptr;
    QLabel* intramanagerStatusLabel = nullptr;
    QLineEdit* defaultSellerInitialsEdit = nullptr;
    QLineEdit* salesRegistrationWebhookEdit = nullptr;
    QLineEdit* salesRegistrationRecipientEdit = nullptr;
    QCheckBox* salesRegistrationEnabledCheck = nullptr;
    QCheckBox* salesRegistrationOAuthCheck = nullptr;
    QLineEdit* microsoftTenantIdEdit = nullptr;
    QLineEdit* microsoftClientIdEdit = nullptr;
    QLineEdit* microsoftScopeEdit = nullptr;
    QLabel* salesRegistrationStatusLabel = nullptr;
    QString microsoftAccessToken;
    QDateTime microsoftAccessTokenExpiresAt;
    bool microsoftOAuthRunning = false;
    QTcpServer* microsoftOAuthServer = nullptr;

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
            return "Man kan kun stemple ind eller ud p? kontorets internet.";
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

    QString cleanPunchText(QString text) const {
        text.replace(QString::fromUtf8("??"), QString::fromUtf8("?"));
        text.replace(QString::fromUtf8("??"), QString::fromUtf8("?"));
        text.replace(QString::fromUtf8("??"), QString::fromUtf8("?"));
        text.replace(QString::fromUtf8("??"), QString::fromUtf8("?"));
        text.replace(QString::fromUtf8("??"), QString::fromUtf8("?"));
        text.replace(QString::fromUtf8("??"), QString::fromUtf8("?"));
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
        if (!punch.detail.isEmpty()) parts << cleanPunchText(punch.detail);
        if (!punch.lastStart.isEmpty()) parts << "Seneste ind: " + cleanPunchText(punch.lastStart);
        if (!punch.lastStop.isEmpty()) parts << "Seneste ud: " + cleanPunchText(punch.lastStop);
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
                intramanagerPunchButton->setText("Kr?ver login");
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

        QTimer::singleShot(2500, this, [this]() {
            refreshCurrentIntramanagerStateAsync();
        });

        intramanagerAutoSyncTimer = new QTimer(this);
        intramanagerAutoSyncTimer->setInterval(5 * 60 * 1000);

        connect(intramanagerAutoSyncTimer, &QTimer::timeout, this, [this]() {
            refreshCurrentIntramanagerStateAsync();
        });

        intramanagerAutoSyncTimer->start();
    }

    void fetchIntramanagerHoursAsync(
        const QString& fromDate,
        const QString& toDate,
        bool silent,
        std::function<void(bool)> afterFetch = {}
        ) {
        if (intramanagerSyncRunning) {
            QTimer::singleShot(1500, this, [this, fromDate, toDate, silent, afterFetch]() {
                fetchIntramanagerHoursAsync(fromDate, toDate, silent, afterFetch);
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
        runIntramanagerPunchWorker("punch-status", silent);
    }

    void toggleIntramanagerPunchAsync() {
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

    void refreshCurrentIntramanagerStateAsync() {
        syncIntramanagerHoursAsync(true, [this](bool) {
            refreshIntramanagerPunchStatusAsync(true);
        });
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
                    "\n\nKopi?r mappen intramanager_worker ind ved siden af .exe-filen."
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
        auto* switchBtn = new QPushButton("Skift s?lger");
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

        auto* dashboardTop = new QHBoxLayout;
        dashboardTop->setContentsMargins(0, 0, 0, 0);
        dashboardTop->addStretch();
        auto* versionLabel = new QLabel(QString("Version %1").arg(QString::fromLatin1(APP_VERSION)));
        versionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        versionLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        versionLabel->setStyleSheet("QLabel { color:#8FB7C9; font-size:12px; font-weight:800; background:transparent; }");
        dashboardTop->addWidget(versionLabel);
        layout->addLayout(dashboardTop);

        auto k1 = createKpiCard("Point i dag");
        auto k2 = createKpiCard("L?n denne m?ned");
        auto k3 = createKpiCard("L?n til n?ste m?ned");
        auto k4 = createKpiCard("Salg denne m?ned");
        auto k5 = createKpiCard("Till?g denne m?ned");
        auto k6 = createKpiCard("Resterende arbejdsdage");

        kpiTodayPointsLabel = k1.second;
        kpiMonthCommissionLabel = k2.second;
        kpiNextMonthPayLabel = k3.second;
        kpiMonthSalesLabel = k4.second;
        kpiMonthAddonsLabel = k5.second;
        kpiRemainingWorkDaysLabel = k6.second;
        kpiMonthCommissionLabel->setTextFormat(Qt::RichText);
        kpiNextMonthPayLabel->setTextFormat(Qt::RichText);
        kpiRemainingWorkDaysLabel->setWordWrap(false);

        auto* kpiGrid = new QGridLayout;
        kpiGrid->setHorizontalSpacing(14);
        kpiGrid->setVerticalSpacing(14);
        kpiGrid->addWidget(k1.first, 0, 0);
        kpiGrid->addWidget(k2.first, 0, 1);
        kpiGrid->addWidget(k3.first, 0, 2);
        kpiGrid->addWidget(k4.first, 1, 0);
        kpiGrid->addWidget(k5.first, 1, 1);
        kpiGrid->addWidget(k6.first, 1, 2);
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

        auto progressCard = createCard("M?l, provision og n?ste l?ft");
        auto* progressLayout = new QGridLayout;
        progressLayout->setHorizontalSpacing(14);
        progressLayout->setVerticalSpacing(14);

        progressLayout->addWidget(createProgressCard("Point mod m?nedens m?l", &targetProgressBar, &targetProgressHintLabel), 0, 0);
        progressLayout->addWidget(createProgressCard("Salg mod m?nedens m?l", &salesTargetProgressBar, &salesTargetProgressHintLabel), 0, 1);
        progressLayout->addWidget(createProgressCard("SIMO ? n?ste pengehop", &simoProgressBar, &simoProgressHintLabel), 1, 0);
        progressLayout->addWidget(createProgressCard("VOICE ? n?ste pengehop", &voiceProgressBar, &voiceProgressHintLabel), 1, 1);

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

        // ?? CONNECTS (HER SKAL DE ST?)
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
        reportPresetCombo->addItems({"I dag", "Denne arbejdsuge", "Seneste 2 arbejdsuger", "Denne l?nm?ned", "V?lg m?ned"});

        reportMonthEdit = new QDateEdit(QDate::currentDate());
        reportMonthEdit->setDisplayFormat("MMMM yyyy");
        reportMonthEdit->setDate(QDate::currentDate());
        reportMonthEdit->setCalendarPopup(true);
        reportMonthEdit->setLocale(QLocale(QLocale::Danish, QLocale::Denmark));

        auto* exportBtn = new QPushButton("Gem rapport");

        top->addWidget(new QLabel("Visning:"));
        top->addWidget(reportPresetCombo);
        top->addWidget(new QLabel("M?ned:"));
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

        auto goalCard = createCard("M?l");
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

        form->addRow("Pointm?l for m?neden", targetSpin);
        form->addRow("Salgsm?l for m?neden", monthlySalesTargetSpin);

        auto* saveGoalBtn = new QPushButton("Gem m?l");
        form->addRow(saveGoalBtn);

        goalCard.second->addLayout(form);
        goalCard.second->addStretch();
        left->addWidget(goalCard.first, 1);

        auto intramanagerCard = createCard("Intramanager og timel?n");

        auto* imForm = new QFormLayout;
        imForm->setSpacing(12);

        intramanagerEnabledCheck = new QCheckBox("Aktiv?r Intramanager og automatisk timehentning");
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

        auto* saveIntramanagerBtn = new QPushButton("Gem Intramanager og timel?n");

        intramanagerStatusLabel = new QLabel("Timer hentes automatisk, n?r rapporter har brug for dem.");
        intramanagerStatusLabel->setWordWrap(true);
        intramanagerStatusLabel->setTextInteractionFlags(Qt::NoTextInteraction);

        imForm->addRow(intramanagerEnabledCheck);
        imForm->addRow("Brugernavn", intramanagerUsernameEdit);
        imForm->addRow("Adgangskode", intramanagerPasswordEdit);
        imForm->addRow("Timel?n", hourlyRateSpin);
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

        salesRegistrationOAuthCheck = new QCheckBox("Brug Microsoft-login/MFA til Power Automate");
        salesRegistrationOAuthCheck->setFocusPolicy(Qt::NoFocus);

        microsoftTenantIdEdit = new QLineEdit;
        microsoftTenantIdEdit->setPlaceholderText("organizations eller tenant-id");

        microsoftClientIdEdit = new QLineEdit;
        microsoftClientIdEdit->setPlaceholderText("Client ID fra Entra app registration");

        microsoftScopeEdit = new QLineEdit;
        microsoftScopeEdit->setPlaceholderText("https://service.flow.microsoft.com//.default");

        auto* saveSalesRegistrationBtn = new QPushButton("Gem salgsregistrering");
        auto* testSalesRegistrationBtn = new QPushButton("Test webflow");
        auto* microsoftLoginBtn = new QPushButton("Log ind med Microsoft");
        auto* microsoftLogoutBtn = new QPushButton("Log ud af Microsoft");
        auto* microsoftButtonRow = new QHBoxLayout;
        microsoftButtonRow->addWidget(microsoftLoginBtn);
        microsoftButtonRow->addWidget(microsoftLogoutBtn);

        salesRegistrationStatusLabel = new QLabel("Sender salgs-reg til en webflow, der opdaterer Excel Online og Outlook.");
        salesRegistrationStatusLabel->setWordWrap(true);
        salesRegistrationStatusLabel->setTextInteractionFlags(Qt::NoTextInteraction);

        salesRegForm->addRow("S?lger initialer", defaultSellerInitialsEdit);
        salesRegForm->addRow("Webhook URL", salesRegistrationWebhookEdit);
        salesRegForm->addRow("Modtager-mail", salesRegistrationRecipientEdit);
        salesRegForm->addRow(salesRegistrationEnabledCheck);
        salesRegForm->addRow(salesRegistrationOAuthCheck);
        salesRegForm->addRow("Microsoft tenant", microsoftTenantIdEdit);
        salesRegForm->addRow("Microsoft client ID", microsoftClientIdEdit);
        salesRegForm->addRow("OAuth scope", microsoftScopeEdit);
        salesRegForm->addRow(microsoftButtonRow);
        salesRegForm->addRow(saveSalesRegistrationBtn);
        salesRegForm->addRow(testSalesRegistrationBtn);
        salesRegForm->addRow("Status", salesRegistrationStatusLabel);

        salesRegistrationCard.second->addLayout(salesRegForm);
        salesRegistrationCard.second->addStretch();
        left->addWidget(salesRegistrationCard.first, 1);

        auto* right = new QVBoxLayout;
        right->setSpacing(18);

        auto sellerCard = createCard("S?lgere");
        salespeopleList = new QListWidget;

        auto* sellerNameEdit = new QLineEdit;
        sellerNameEdit->setPlaceholderText("Nyt s?lgernavn");

        auto* addSellerBtn = new QPushButton("Tilf?j s?lger");
        auto* activateBtn = new QPushButton("S?t som aktiv");
        auto* deleteSellerBtn = new QPushButton("Slet valgt s?lger");

        sellerCard.second->addWidget(salespeopleList);
        sellerCard.second->addWidget(sellerNameEdit);
        sellerCard.second->addWidget(addSellerBtn);
        sellerCard.second->addWidget(activateBtn);
        sellerCard.second->addWidget(deleteSellerBtn);
        sellerCard.second->addStretch();

        right->addWidget(sellerCard.first, 1);

        auto backupCard = createCard("Backup");
        auto* exportBackupBtn = new QPushButton("Eksport?r backup");
        auto* importBackupBtn = new QPushButton("Import?r backup");

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
                intramanagerStatusLabel->setText("Indstillinger gemt, men adgangskoden kunne ikke gemmes krypteret.");
            } else {
                intramanagerStatusLabel->setText("Intramanager og timel?n er gemt.");
                intramanagerPasswordEdit->clear();
                intramanagerPasswordEdit->setPlaceholderText("Adgangskode er gemt krypteret lokalt");
            }

            refreshAll();
            setupIntramanagerAutoSync();
            refreshIntramanagerPunchStatusAsync(true);
        });

        connect(saveSalesRegistrationBtn, &QPushButton::clicked, this, [this]() {
            saveSalesRegistrationSettingsFromUi();
            if (salesRegistrationStatusLabel) {
                salesRegistrationStatusLabel->setText("Salgsregistrering er gemt.");
            }
        });

        connect(testSalesRegistrationBtn, &QPushButton::clicked, this, [this]() {
            saveSalesRegistrationSettingsFromUi();
            testSalesRegistrationWebhookAsync();
        });

        connect(microsoftLoginBtn, &QPushButton::clicked, this, [this]() {
            saveSalesRegistrationSettingsFromUi();
            acquireMicrosoftAccessTokenAsync(true, true, [this](bool ok, const QString&, const QString& error) {
                if (salesRegistrationStatusLabel) {
                    salesRegistrationStatusLabel->setText(ok ? "Microsoft-login er klar til salgsregistrering." : error);
                }
            });
        });

        connect(microsoftLogoutBtn, &QPushButton::clicked, this, [this]() {
            microsoftAccessToken.clear();
            microsoftAccessTokenExpiresAt = QDateTime();
            deleteMicrosoftRefreshToken();
            if (salesRegistrationStatusLabel) {
                salesRegistrationStatusLabel->setText("Microsoft-login er fjernet fra denne computer.");
            }
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
                QMessageBox::warning(this, "Kan ikke slette", "Der skal v?re mindst ?n s?lger i programmet.");
                return;
            }

            const auto seller = repo.salespeople[row];
            if (!confirmQuestion(this, "Slet s?lger", QString("Er du sikker p?, at du vil slette '%1'?").arg(seller.name))) {
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
                "Eksport?r backup",
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
                "Import?r backup",
                repo.baseDir(),
                "JSON files (*.json)"
                );
            if (path.isEmpty()) return;

            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, "Fejl", "Kunne ikke ?bne backup-filen.");
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
        const QString path = QFileDialog::getOpenFileName(this, "V?lg backup-fil", repo.baseDir(), "JSON-filer (*.json)");
        if (path.isEmpty()) return false;

        if (!confirmQuestion(this, "Import?r backup", "Det her overskriver nuv?rende lokale data. Vil du forts?tte?")) {
            return false;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Fejl", "Kunne ikke ?bne backup-filen.");
            return false;
        }

        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, "Fejl", "Backup-filen kunne ikke l?ses.");
            return false;
        }

        const QJsonObject root = doc.object();
        if (!root.contains("salespeople") || !root.contains("products") || !root.contains("orders") || !root.contains("settings")) {
            QMessageBox::warning(this, "Fejl", "Backup-filen mangler n?dvendige felter.");
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
            QMessageBox::warning(this, "Fejl", "Backup-filen indeholder ingen s?lgere.");
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

    QString salaryKpiText(double totalSalary, double baseSalary, double provision, const QDate& payoutMonth) const {
        return QString(
            "<span style=\"font-size:19px;font-weight:900;color:#FFFFFF;\">%1 kr</span><br>"
            "<span style=\"font-size:13px;font-weight:900;color:#D8F5FF;\">Udbetales: %2</span><br>"
            "<span style=\"font-size:12px;font-weight:800;color:#BFD7EE;\">Timer: %3 kr</span><br>"
            "<span style=\"font-size:12px;font-weight:800;color:#BFD7EE;\">Provision: %4 kr</span>"
            )
            .arg(money(totalSalary))
            .arg(payoutDateLabel(payoutMonth).toHtmlEscaped())
            .arg(money(baseSalary))
            .arg(money(provision));
    }

    QString plainBadgeText(const QString& label, const QString& value) const {
        return QString("<span style=\"color:#9CC7E8;\">%1</span> <span style=\"color:#F8FBFF;font-weight:700;\">%2</span>").arg(label, value);
    }

    void refreshAll() {
        const auto* s = activeSalesperson();
        activeSalespersonLabel->setText(s ? QString("Du arbejder som <b>%1</b>").arg(s->name) : "Ingen aktiv s?lger");
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
        const auto previousCalendarMonthRange = monthRange(now.addMonths(-1));
        const auto currentPayPeriod = payrollRangeEndingInMonth(now);
        const auto nextPayPeriod = payrollRangeEndingInMonth(now.addMonths(1));
        const auto dayBonusPeriod = payrollBonusRange(now);
        auto cachedHoursForRange = [this](const QPair<QDateTime, QDateTime>& range) {
            const QString from = intramanagerDate(range.first.date());
            const QString to = intramanagerDate(range.second.date());
            return cachedIntramanagerHours(from, to)
                .value_or(IntramanagerHoursEntry{from, to, 0.0, 0.0, QString()})
                .hours;
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
        const double currentPayPeriodBonus = currentPayPeriodMetrics.dayBonus;
        const double backpaidBonusThisMonth = delayedBonus(previousMonthMetrics);
        const double actualSalaryThisMonth =
            currentPayPeriodBaseSalary + currentPayPeriodBonus + backpaidBonusThisMonth;

        const double nextPayPeriodBaseSalary = cachedHoursForRange(nextPayPeriod) * repo.settings.hourlyRate;
        const double nextPayPeriodBonus = nextPayPeriodMetrics.dayBonus;
        const double backpaidBonusNextMonth = delayedBonus(currentCalendarMetrics);
        const double salaryEarnedForNextMonth =
            nextPayPeriodBaseSalary + nextPayPeriodBonus + backpaidBonusNextMonth;

        if (kpiMonthCommissionLabel) {
            kpiMonthCommissionLabel->setText(salaryKpiText(
                actualSalaryThisMonth,
                currentPayPeriodBaseSalary,
                currentPayPeriodBonus + backpaidBonusThisMonth,
                now
                ));
        }
        if (kpiNextMonthPayLabel) {
            kpiNextMonthPayLabel->setText(salaryKpiText(
                salaryEarnedForNextMonth,
                nextPayPeriodBaseSalary,
                nextPayPeriodBonus + backpaidBonusNextMonth,
                now.addMonths(1)
                ));
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
                targetProgressHintLabel->setText(QString("Du mangler <b>%1 point</b>. Det svarer til cirka <b>%2 point</b> pr. resterende arbejdsdag.").arg(money(pointsToTarget)).arg(money(requiredPointsPerRemainingDay)));
            } else {
                targetProgressHintLabel->setText(QString("Pointm?let er hjemme. Du ligger <b>%1 point</b> over m?let.").arg(money(mMonth.totalPoints - repo.settings.bonus.monthlyTargetPoints)));
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
                salesTargetProgressHintLabel->setText("S?t et salgsm?l i Indstillinger for at f? live fremdrift p? m?neden.");
            } else if (missingSalesToTarget > 0) {
                salesTargetProgressHintLabel->setText(QString("Du mangler <b>%1 salg</b> for at ramme m?let denne m?ned.").arg(missingSalesToTarget));
            } else {
                salesTargetProgressHintLabel->setText(QString("Salgsm?let er ramt. Du ligger <b>%1 salg</b> over m?let.").arg(mMonth.salesCount - repo.settings.monthlySalesTarget));
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
                simoProgressHintLabel->setText(QString("SIMO ?bner ved <b>%1</b>. Du mangler <b>%2</b> for at t?nde pengesporet.").arg(repo.settings.bonus.simoMinEligible).arg(qMax(0, repo.settings.bonus.simoMinEligible - mMonth.simoCount)));
            } else {
                simoProgressHintLabel->setText(QString("N?ste SIMO-hop ligger ved <b>%1</b>. Du mangler <b>%2</b>, og s? st?r provisionen p? %3.").arg(nextSimoStep).arg(missingToNextSimo).arg(moneySpan(nextSimoBonus, missingToNextSimo <= 1 ? "#22C55E" : "#34D399")));
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
                voiceProgressHintLabel->setText(QString("VOICE ?bner ved <b>%1</b>. Du mangler <b>%2</b>, og derefter hopper den for hver <b>10</b>.").arg(repo.settings.bonus.voiceMinEligible).arg(qMax(0, repo.settings.bonus.voiceMinEligible - mMonth.voiceCount)));
            } else {
                voiceProgressHintLabel->setText(QString("N?ste VOICE-hop ligger ved <b>%1</b>. Du mangler <b>%2</b>, og s? st?r provisionen p? %3.").arg(nextVoiceStep).arg(missingToNextVoice).arg(moneySpan(nextVoiceBonus, missingToNextVoice <= 2 ? "#22C55E" : "#34D399")));
            }
        }

        QString targetText;
        QTextStream targetTs(&targetText);
        targetTs << "Du st?r p? " << money(mMonth.totalPoints) << " point og " << mMonth.salesCount << " salg lige nu.\n";
        targetTs << "Till?g lukket: " << mMonth.addOnCount << "  ?  SIMO/VOICE: " << mMonth.simoCount << "/" << mMonth.voiceCount << "\n";
        if (repo.settings.monthlySalesTarget > 0) {
            targetTs << "Du mangler " << missingSalesToTarget << " salg for at ramme m?nedens m?l.\n";
        }
        targetTs << nextMonthlyTierHint(mMonth.totalPoints, repo.settings.bonus);
        if (targetSummaryLabel) targetSummaryLabel->setText(targetText);

        QString perfText;
        QTextStream perfTs(&perfText);
        perfTs << "Aktive salgsdage: " << activeDays << "  ?  Bedste dag: ";
        if (!bestDay.first.isEmpty()) {
            perfTs << bestDay.first << " (" << money(bestDay.second) << " point)";
        } else {
            perfTs << "-";
        }
        perfTs << "\n";
        perfTs << "Snit point pr aktiv dag: " << money(avgPointsPerActiveDay)
               << "  ?  Snit provision pr aktiv dag: " << money(avgCommissionPerActiveDay) << " kr\n";
        perfTs << "Hvis du holder tempoet, lander du omkring " << money(projectedPoints) << " point ved m?nedens slut.\n";
        if (projectedGap >= 0) {
            perfTs << "Du ligger lige nu til at lande " << money(projectedGap) << " point over m?let.";
        } else {
            perfTs << "Du ligger lige nu " << money(-projectedGap) << " point bag m?let.";
        }
        if (performanceSummaryLabel) performanceSummaryLabel->setText(perfText);

        QString simText;
        QTextStream simTs(&simText);
        simTs << "SIMO: n?ste hop ved " << nextSimoStep << "  ?  mangler " << missingToNextSimo
              << "  ?  n?ste niveau giver " << money(nextSimoBonus) << " kr\n";
        if (mMonth.voiceCount < repo.settings.bonus.voiceMinEligible) {
            simTs << "VOICE ?bner ved " << repo.settings.bonus.voiceMinEligible
                  << "  ?  mangler " << qMax(0, repo.settings.bonus.voiceMinEligible - mMonth.voiceCount)
                  << "  ?  hopper derefter for hver 10\n";
        } else {
            simTs << "VOICE: n?ste hop ved " << nextVoiceStep << "  ?  mangler " << missingToNextVoice
                  << "  ?  n?ste niveau giver " << money(nextVoiceBonus) << " kr\n";
        }
        simTs << "Resterende arbejdsdage i m?neden: " << remainingWorkingDays;
        if (simulatorSummaryLabel) simulatorSummaryLabel->setText(simText);

        QString recentText;
        QTextStream recentTs(&recentText);
        const auto idxs = activeOrderIndicesSorted();
        int shown = 0;
        for (int i = 0; i < idxs.size() && shown < 5; ++i, ++shown) {
            const auto& order = repo.orders[idxs[i]];
            double pts = 0.0;
            const QString products = orderProductsSummary(order, &pts);
            recentTs << order.createdAt.toString("dd-MM HH:mm") << "  ?  " << products.left(72) << "  ?  " << money(pts) << " point\n";
        }
        if (shown == 0) recentTs << "Der er ikke lagt nye ordrer ind endnu.";
        if (recentActivityLabel) recentActivityLabel->setText(recentText);
    }

    QString summaryCardText(const Metrics& m) const {
        QString out;
        QTextStream ts(&out);
        ts << QString("Ordrer %1 ? Salg %2 ? Till?g %3\n")
                  .arg(m.totalOrders)
                  .arg(m.salesCount)
                  .arg(m.addOnCount);

        ts << QString("Point %1 ? Provision %2 kr\n")
                  .arg(money(m.totalPoints))
                  .arg(money(m.totalCommission));
        ts << "SIMO " << m.simoCount << " ? VOICE " << m.voiceCount;
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

    QString defaultMicrosoftTenantId() const {
        return "organizations";
    }

    QString defaultMicrosoftScope() const {
        return "https://service.flow.microsoft.com//.default";
    }

    QString microsoftTenantId() const {
        const QString tenant = repo.settings.microsoftTenantId.trimmed();
        return tenant.isEmpty() ? defaultMicrosoftTenantId() : tenant;
    }

    QString microsoftClientId() const {
        return repo.settings.microsoftClientId.trimmed();
    }

    QString microsoftScope() const {
        const QString scope = repo.settings.microsoftScope.trimmed();
        return scope.isEmpty() ? defaultMicrosoftScope() : scope;
    }

    QString microsoftAuthScopes() const {
        QStringList scopes = microsoftScope().split(' ', Qt::SkipEmptyParts);
        const QStringList requiredScopes = {"offline_access", "openid", "profile"};
        for (const QString& scope : requiredScopes) {
            bool exists = false;
            for (const QString& existing : scopes) {
                if (existing.compare(scope, Qt::CaseInsensitive) == 0) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                scopes << scope;
            }
        }
        return scopes.join(' ');
    }

    QByteArray base64Url(const QByteArray& bytes) const {
        return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    }

    QString randomOAuthValue(int byteCount = 32) const {
        QByteArray bytes;
        bytes.resize(byteCount);
        for (int i = 0; i < byteCount; ++i) {
            bytes[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
        }
        return QString::fromLatin1(base64Url(bytes));
    }

    QString microsoftTokenAccountHint(const QJsonObject& tokenObject) const {
        const QString idToken = tokenObject.value("id_token").toString();
        const QStringList parts = idToken.split('.');
        if (parts.size() < 2) {
            return repo.settings.microsoftAccountHint;
        }

        QByteArray payload = parts[1].toUtf8();
        while (payload.size() % 4 != 0) {
            payload.append('=');
        }

        const QByteArray decoded = QByteArray::fromBase64(payload, QByteArray::Base64UrlEncoding);
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(decoded, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            return repo.settings.microsoftAccountHint;
        }

        const QJsonObject claims = doc.object();
        QString hint = claims.value("preferred_username").toString();
        if (hint.isEmpty()) hint = claims.value("email").toString();
        if (hint.isEmpty()) hint = claims.value("upn").toString();
        if (hint.isEmpty()) hint = claims.value("name").toString();
        return hint.isEmpty() ? repo.settings.microsoftAccountHint : hint;
    }

    QString microsoftTokenEndpoint() const {
        const QString tenant = QString::fromLatin1(QUrl::toPercentEncoding(microsoftTenantId()));
        return "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/token";
    }

    QString microsoftAuthorizeEndpoint() const {
        const QString tenant = QString::fromLatin1(QUrl::toPercentEncoding(microsoftTenantId()));
        return "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/authorize";
    }

    void saveSalesRegistrationSettingsFromUi() {
        if (defaultSellerInitialsEdit) {
            repo.settings.defaultSellerInitials = defaultSellerInitialsEdit->text().trimmed();
        }
        if (salesRegistrationWebhookEdit) {
            repo.settings.salesRegistrationWebhookUrl = salesRegistrationWebhookEdit->text().trimmed();
        }
        if (salesRegistrationRecipientEdit) {
            repo.settings.salesRegistrationRecipient = salesRegistrationRecipientEdit->text().trimmed();
        }
        if (salesRegistrationEnabledCheck) {
            repo.settings.salesRegistrationEnabled = salesRegistrationEnabledCheck->isChecked();
        }
        if (salesRegistrationOAuthCheck) {
            repo.settings.salesRegistrationOAuthEnabled = salesRegistrationOAuthCheck->isChecked();
        }
        if (microsoftTenantIdEdit) {
            repo.settings.microsoftTenantId = microsoftTenantIdEdit->text().trimmed();
        }
        if (microsoftClientIdEdit) {
            repo.settings.microsoftClientId = microsoftClientIdEdit->text().trimmed();
        }
        if (microsoftScopeEdit) {
            repo.settings.microsoftScope = microsoftScopeEdit->text().trimmed();
        }
        repo.saveSettings();
    }

    void finishMicrosoftOAuthServer() {
        if (microsoftOAuthServer) {
            microsoftOAuthServer->close();
            microsoftOAuthServer->deleteLater();
            microsoftOAuthServer = nullptr;
        }
        microsoftOAuthRunning = false;
    }

    void requestMicrosoftTokenAsync(
        const QUrlQuery& form,
        std::function<void(bool, const QString&, const QString&)> done
        ) {
        auto* manager = new QNetworkAccessManager(this);
        QNetworkRequest request{QUrl(microsoftTokenEndpoint())};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        request.setTransferTimeout(60000);

        QNetworkReply* reply = manager->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
        connect(reply, &QNetworkReply::finished, this, [this, manager, reply, done]() {
            const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray responseBody = reply->readAll();
            const QString networkError = reply->errorString();
            const bool networkOk = reply->error() == QNetworkReply::NoError && statusCode >= 200 && statusCode < 300;

            reply->deleteLater();
            manager->deleteLater();

            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody, &parseError);
            const QJsonObject object = doc.object();
            const QString errorDescription = object.value("error_description").toString();

            if (!networkOk || parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                const QString details = !errorDescription.isEmpty()
                    ? errorDescription
                    : (statusCode > 0 ? QString("Microsoft-login fejlede (%1).").arg(statusCode) : networkError);
                if (done) done(false, QString(), details);
                return;
            }

            const QString token = object.value("access_token").toString();
            if (token.isEmpty()) {
                if (done) done(false, QString(), "Microsoft svarede uden access token.");
                return;
            }

            microsoftAccessToken = token;
            const int expiresIn = qMax(60, object.value("expires_in").toInt(3600));
            microsoftAccessTokenExpiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresIn);

            const QString refreshToken = object.value("refresh_token").toString();
            if (!refreshToken.isEmpty()) {
                const QString accountHint = microsoftTokenAccountHint(object);
                repo.settings.microsoftAccountHint = accountHint;
                repo.saveSettings();
                saveMicrosoftRefreshToken(accountHint, refreshToken);
            }

            if (done) done(true, microsoftAccessToken, QString());
        });
    }

    void startMicrosoftInteractiveLogin(std::function<void(bool, const QString&, const QString&)> done) {
        if (microsoftOAuthRunning) {
            if (done) done(false, QString(), "Microsoft-login er allerede i gang.");
            return;
        }

        const QString clientId = microsoftClientId();
        if (clientId.isEmpty()) {
            if (done) done(false, QString(), "Microsoft client ID mangler i Indstillinger.");
            return;
        }

        auto* server = new QTcpServer(this);
        if (!server->listen(QHostAddress::LocalHost, 0)) {
            server->deleteLater();
            if (done) done(false, QString(), "Kunne ikke starte lokal login-modtager til Microsoft.");
            return;
        }

        microsoftOAuthRunning = true;
        microsoftOAuthServer = server;

        const QUrl redirectUri(QString("http://localhost:%1/").arg(server->serverPort()));
        const QString state = randomOAuthValue(24);
        const QString codeVerifier = randomOAuthValue(64);
        const QByteArray challengeBytes = QCryptographicHash::hash(codeVerifier.toUtf8(), QCryptographicHash::Sha256);
        const QString codeChallenge = QString::fromLatin1(base64Url(challengeBytes));

        QUrl authorizeUrl(microsoftAuthorizeEndpoint());
        QUrlQuery query;
        query.addQueryItem("client_id", clientId);
        query.addQueryItem("response_type", "code");
        query.addQueryItem("redirect_uri", redirectUri.toString());
        query.addQueryItem("response_mode", "query");
        query.addQueryItem("scope", microsoftAuthScopes());
        query.addQueryItem("state", state);
        query.addQueryItem("code_challenge", codeChallenge);
        query.addQueryItem("code_challenge_method", "S256");
        query.addQueryItem("prompt", "select_account");
        authorizeUrl.setQuery(query);

        connect(server, &QTcpServer::newConnection, this, [this, server, redirectUri, state, codeVerifier, done]() {
            QTcpSocket* socket = server->nextPendingConnection();
            if (!socket) {
                return;
            }

            connect(socket, &QTcpSocket::readyRead, this, [this, socket, redirectUri, state, codeVerifier, done]() {
                const QByteArray requestData = socket->readAll();
                const QList<QByteArray> lines = requestData.split('\n');
                const QList<QByteArray> firstLineParts = lines.value(0).trimmed().split(' ');
                const QString target = QString::fromUtf8(firstLineParts.value(1));

                QUrl callbackUrl("http://localhost" + target);
                QUrlQuery callbackQuery(callbackUrl);
                const QString returnedState = callbackQuery.queryItemValue("state");
                const QString code = callbackQuery.queryItemValue("code");
                const QString error = callbackQuery.queryItemValue("error_description");

                const bool ok = !code.isEmpty() && returnedState == state;
                const QByteArray html = ok
                    ? QByteArray("<!doctype html><html><body style=\"font-family:Segoe UI,Arial,sans-serif;\"><h2>Microsoft-login er klar</h2><p>Du kan lukke dette vindue og vende tilbage til Provi Tracker.</p></body></html>")
                    : QByteArray("<!doctype html><html><body style=\"font-family:Segoe UI,Arial,sans-serif;\"><h2>Microsoft-login fejlede</h2><p>Du kan lukke dette vindue og pr?ve igen i Provi Tracker.</p></body></html>");
                const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
                    + QByteArray::number(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
                socket->deleteLater();

                finishMicrosoftOAuthServer();

                if (!ok) {
                    const QString message = !error.isEmpty() ? error : "Microsoft-login blev afvist eller afbrudt.";
                    if (done) done(false, QString(), message);
                    return;
                }

                QUrlQuery form;
                form.addQueryItem("client_id", microsoftClientId());
                form.addQueryItem("grant_type", "authorization_code");
                form.addQueryItem("code", code);
                form.addQueryItem("redirect_uri", redirectUri.toString());
                form.addQueryItem("code_verifier", codeVerifier);
                form.addQueryItem("scope", microsoftAuthScopes());
                requestMicrosoftTokenAsync(form, done);
            });
        });

        QTimer::singleShot(300000, this, [this, server, done]() {
            if (microsoftOAuthServer != server) {
                return;
            }
            finishMicrosoftOAuthServer();
            if (done) done(false, QString(), "Microsoft-login udl?b. Pr?v igen.");
        });

        if (salesRegistrationStatusLabel) {
            salesRegistrationStatusLabel->setText("Microsoft-login ?bner i browseren...");
        }

        if (!QDesktopServices::openUrl(authorizeUrl)) {
            finishMicrosoftOAuthServer();
            if (done) done(false, QString(), "Kunne ikke ?bne browseren til Microsoft-login.");
        }
    }

    void acquireMicrosoftAccessTokenAsync(
        bool interactive,
        bool showWarnings,
        std::function<void(bool, const QString&, const QString&)> done
        ) {
        Q_UNUSED(showWarnings);

        if (!repo.settings.salesRegistrationOAuthEnabled) {
            if (done) done(true, QString(), QString());
            return;
        }

        if (!microsoftAccessToken.isEmpty()
            && microsoftAccessTokenExpiresAt > QDateTime::currentDateTimeUtc().addSecs(60)) {
            if (done) done(true, microsoftAccessToken, QString());
            return;
        }

        if (microsoftClientId().isEmpty()) {
            if (done) done(false, QString(), "Microsoft client ID mangler i Indstillinger.");
            return;
        }

        QString storedAccount;
        QString refreshToken;
        if (loadMicrosoftRefreshToken(&storedAccount, &refreshToken) && !refreshToken.isEmpty()) {
            if (salesRegistrationStatusLabel) {
                salesRegistrationStatusLabel->setText("Fornyer Microsoft-login...");
            }

            QUrlQuery form;
            form.addQueryItem("client_id", microsoftClientId());
            form.addQueryItem("grant_type", "refresh_token");
            form.addQueryItem("refresh_token", refreshToken);
            form.addQueryItem("scope", microsoftAuthScopes());

            requestMicrosoftTokenAsync(form, [this, interactive, done](bool ok, const QString& token, const QString& error) {
                if (ok) {
                    if (done) done(true, token, QString());
                    return;
                }

                deleteMicrosoftRefreshToken();
                microsoftAccessToken.clear();
                microsoftAccessTokenExpiresAt = QDateTime();

                if (interactive) {
                    startMicrosoftInteractiveLogin(done);
                } else if (done) {
                    done(false, QString(), "Microsoft-login skal fornyes i Indstillinger. " + error);
                }
            });
            return;
        }

        if (interactive) {
            startMicrosoftInteractiveLogin(done);
            return;
        }

        if (done) {
            done(false, QString(), "Log ind med Microsoft i Indstillinger for at sende salgs-reg.");
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
                intramanagerPasswordEdit->setPlaceholderText("Adgangskode er gemt krypteret lokalt");
            } else {
                intramanagerPasswordEdit->setPlaceholderText("Indtast Intramanager adgangskode");
            }
        }

        if (intramanagerStatusLabel) {
            intramanagerStatusLabel->setText("Timer hentes automatisk, n?r rapporter har brug for dem.");
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
        if (salesRegistrationOAuthCheck) {
            salesRegistrationOAuthCheck->setChecked(repo.settings.salesRegistrationOAuthEnabled);
        }
        if (microsoftTenantIdEdit) {
            microsoftTenantIdEdit->setText(repo.settings.microsoftTenantId);
        }
        if (microsoftClientIdEdit) {
            microsoftClientIdEdit->setText(repo.settings.microsoftClientId);
        }
        if (microsoftScopeEdit) {
            microsoftScopeEdit->setText(repo.settings.microsoftScope);
        }
        if (salesRegistrationStatusLabel) {
            QString storedAccount;
            QString storedRefreshToken;
            const bool hasMicrosoftLogin = loadMicrosoftRefreshToken(&storedAccount, &storedRefreshToken)
                && !storedRefreshToken.isEmpty();
            if (repo.settings.salesRegistrationOAuthEnabled) {
                salesRegistrationStatusLabel->setText(hasMicrosoftLogin
                    ? "Microsoft-login er gemt sikkert og bruges til Power Automate."
                    : "OAuth er aktivt. Log ind med Microsoft for at sende salgs-reg.");
            } else {
                salesRegistrationStatusLabel->setText("Sender salgs-reg til en webflow, der opdaterer Excel Online og Outlook.");
            }
        }

        refreshPunchCardUi();
    }

    QStringList salesRegistrationAliases(const Product& product) const {
        QStringList aliases;
        aliases << product.displayName << product.key;

        QString trimmed = product.displayName;
        trimmed.remove("Till?g ", Qt::CaseInsensitive);
        trimmed.remove("Mobil ", Qt::CaseInsensitive);
        trimmed.remove("Mobilt bredb?nd ", Qt::CaseInsensitive);
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
        if (category.compare("Till?g", Qt::CaseInsensitive) == 0) return "#FFC000";
        if (category.compare("Mobilt bredb?nd", Qt::CaseInsensitive) == 0) return "#00B0F0";
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

        auto sendPayload = [this, webhookUrl, payload, defaultSuccessText, showWarnings](const QString& bearerToken) {
            auto* manager = new QNetworkAccessManager(this);
            QNetworkRequest request(webhookUrl);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
            request.setTransferTimeout(60000);
            if (!bearerToken.isEmpty()) {
                request.setRawHeader("Authorization", "Bearer " + bearerToken.toUtf8());
            }

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
        };

        acquireMicrosoftAccessTokenAsync(showWarnings, showWarnings, [this, sendPayload, showWarnings](bool ok, const QString& token, const QString& error) {
            if (!ok) {
                const QString message = error.isEmpty() ? "Microsoft-login er ikke klar til salgsregistrering." : error;
                if (salesRegistrationStatusLabel) salesRegistrationStatusLabel->setText(message);
                if (showWarnings) {
                    QMessageBox::warning(this, "Salgsregistrering", message);
                }
                return;
            }

            sendPayload(token);
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
        if (confirmQuestion(this, "Slet ordre", "Er du sikker p?, at du vil slette den valgte ordre?")) {
            repo.orders.removeAt(repoIndex);
            repo.saveOrders();
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
                return makeReportRange("Denne m?ned", r, salaryRange, now);
            }

            case 4:
            default: {
                const QDate paymentMonth = reportMonthEdit->date();
                const auto r = monthRange(paymentMonth);
                const auto salaryRange = payrollRangeEndingInMonth(paymentMonth);
                return makeReportRange(monthKey(paymentMonth), r, salaryRange, paymentMonth);
            }
        }
    }

    // Maanedsprovision bruger kalenderm?neden; timer og dagspointbonus f?lger l?nperioden 21.-20.
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
        salary.periodProvision = payrollMetrics.dayBonus;
        salary.delayedProvision =
            previousMonthMetrics.monthlyBonus + previousMonthMetrics.simoBonus + previousMonthMetrics.voiceBonus;
        salary.totalProvision = salary.periodProvision + salary.delayedProvision;
        salary.totalSalary = salary.baseSalary + salary.totalProvision;
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

    // Kun en worker pr. periode ad gangen; flere rapportopdateringer venter p? samme cache.
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
        if (!reportHoursForRange(range).has_value()) return;

        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const QDateTime lastRequested = intramanagerReportRefreshRequestedAt.value(requestedKey);
        if (lastRequested.isValid() && lastRequested.secsTo(nowUtc) < 10 * 60) {
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

        refreshCachedReportHoursIfNeeded(range, requestedKey);

        const auto dayBonusPeriod = reportDayBonusPeriod(range);

        const auto m = CommissionEngine::calculate(
            repo,
            s->id,
            range.from,
            range.to,
            dayBonusPeriod
            );

        const QString hoursPeriod = intramanagerPeriodLabel(hoursEntry->fromDate, hoursEntry->toDate);
        const auto salary = reportSalaryBreakdown(range, *s, *hoursEntry);

        reportText->setHtml(
            ReportService::buildHtmlReport(
                repo,
                *s,
                range.label,
                m,
                hoursEntry->hours,
                repo.settings.hourlyRate,
                hoursPeriod,
                salary
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
                    "Eksporten forts?tter, n?r timerne er klar."
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

        const auto dayBonusPeriod = reportDayBonusPeriod(range);

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
        const auto salary = reportSalaryBreakdown(range, *s, *hoursEntry);

        if (ReportService::exportPdf(
                path,
                repo,
                *s,
                range.label,
                m,
                hoursEntry->hours,
                repo.settings.hourlyRate,
                hoursPeriod,
                salary
                )) {
            QMessageBox::information(this, "Eksporteret", "Rapport gemt.");
        } else {
            QMessageBox::warning(this, "Fejl", "Kunne ikke gemme rapporten.");
        }
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        // Lukning skal aldrig vente p? netv?rk/worker; det ville fryse appen for almindelige brugere.
        createAutoBackup();
        QMainWindow::closeEvent(event);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MainWindow w;
    w.show();

    QTimer::singleShot(1500, &app, []() {
        initAutoUpdate();
    });

    return app.exec();
}
