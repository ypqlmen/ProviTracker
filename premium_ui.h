#pragma once
#include <QtWidgets>
#include <cmath>

// Continuous superellipse corners, sampled as a painter path. The long edges
// remain straight, so a wide salary card does not turn into an ellipse.
static QPainterPath continuousCardPath(const QRectF& rect, qreal radius) {
    radius = qMin(radius,qMin(rect.width(),rect.height())/2);
    QPainterPath path;
    path.moveTo(rect.left()+radius,rect.top());
    const QPointF centers[]={{rect.right()-radius,rect.top()+radius},
        {rect.right()-radius,rect.bottom()-radius},{rect.left()+radius,rect.bottom()-radius},
        {rect.left()+radius,rect.top()+radius}};
    for(int corner=0;corner<4;++corner) {
        for(int step=0;step<=24;++step) {
            const double angle=(-90+corner*90+step*90.0/24)*3.141592653589793/180;
            const double c=std::cos(angle),s=std::sin(angle);
            path.lineTo(centers[corner]+QPointF(radius*std::copysign(std::sqrt(std::abs(c)),c),
                radius*std::copysign(std::sqrt(std::abs(s)),s)));
        }
    }
    path.closeSubpath();
    return path;
}

class PremiumCard : public QFrame {
public:
    explicit PremiumCard(QWidget* parent=nullptr):QFrame(parent) {
        setProperty("premiumCard",true);
        setAttribute(Qt::WA_TranslucentBackground);
        auto* shadow=new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(20);shadow->setOffset(0,3);shadow->setColor(QColor(0,0,0,46));
        setGraphicsEffect(shadow);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        const auto path=continuousCardPath(QRectF(rect()).adjusted(1,1,-1,-1),24);
        painter.fillPath(path,QColor("#24282F"));
        painter.setPen(QPen(QColor("#373E49"),1));painter.drawPath(path);
    }
};

static void applyMidnightPalette(QApplication& app) {
    QPalette palette;
    palette.setColor(QPalette::Window,QColor("#181B20"));
    palette.setColor(QPalette::WindowText,QColor("#F2F4F7"));
    palette.setColor(QPalette::Base,QColor("#24282F"));
    palette.setColor(QPalette::AlternateBase,QColor("#272C34"));
    palette.setColor(QPalette::Text,QColor("#D8DEE8"));
    palette.setColor(QPalette::Button,QColor("#24282F"));
    palette.setColor(QPalette::ButtonText,QColor("#F2F4F7"));
    palette.setColor(QPalette::Highlight,QColor("#286BCE"));
    palette.setColor(QPalette::HighlightedText,QColor("#F7FAFF"));
    palette.setColor(QPalette::PlaceholderText,QColor("#9AA5B5"));
    palette.setColor(QPalette::ToolTipBase,QColor("#343C49"));
    palette.setColor(QPalette::ToolTipText,QColor("#F2F4F7"));
    palette.setColor(QPalette::Link,QColor("#A8CBFF"));
    palette.setColor(QPalette::Disabled,QPalette::Text,QColor("#8994A5"));
    palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor("#8994A5"));
    app.setPalette(palette);
}

static QString premiumStyle() {
    return QStringLiteral(R"(
QWidget { color:#F2F4F7; font-size:13px; }
QMainWindow, QDialog { background:#181B20; }
QLabel { background:transparent; border:none; padding:0; }
QWidget#workspace { background:#181B20; }
QFrame#sidebar { background:#1E2228; border:1px solid #343B46; border-radius:24px; }
QFrame[premiumCard="true"] { background:transparent; border:none; }
QTabWidget::pane { border:none; background:transparent; }
QGroupBox { border:none; background:transparent; margin:0; }
QGroupBox::title { color:#AAB3C0; font-size:12px; font-weight:600; }
QPushButton { background:#24282F; border:1px solid #3B434F; border-radius:12px; padding:10px 16px; color:#D8DEE8; font-weight:600; min-height:20px; }
QPushButton:hover { background:#303743; border-color:#596577; }
QPushButton:pressed { background:#374251; }
QPushButton[primary="true"] { background:#286BCE; color:#F7FAFF; border:1px solid #286BCE; }
QPushButton[primary="true"]:hover { background:#3278DD; }
QPushButton:disabled { color:#8994A5; background:#292E37; border-color:#343B46; }
QLineEdit,QComboBox,QDateEdit,QDateTimeEdit,QSpinBox,QDoubleSpinBox,QTextEdit,QTextBrowser { background:#24282F; border:1px solid #3B434F; border-radius:11px; padding:9px 12px; color:#F2F4F7; selection-background-color:#25416B; selection-color:#C6DBFF; }
QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus,QDateEdit:focus,QDateTimeEdit:focus,QTextEdit:focus { border:1px solid #78ADFF; }
QLineEdit:disabled,QComboBox:disabled,QDoubleSpinBox:disabled { background:#292E37; color:#9AA5B5; }
QComboBox::drop-down,QDateEdit::drop-down,QDateTimeEdit::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:26px; border:none; background:transparent; }
QComboBox QAbstractItemView,QMenu { background:#24282F; color:#F2F4F7; border:1px solid #404957; padding:6px; selection-background-color:#2C405E; selection-color:#A8CBFF; }
QTableWidget { background:#24282F; alternate-background-color:#272C34; border:1px solid #373E49; border-radius:16px; gridline-color:#343B46; selection-background-color:#2B4161; selection-color:#C6DBFF; }
QHeaderView::section { background:#2B3039; color:#AAB3C0; border:none; padding:12px; font-weight:600; }
QTableWidget::item { padding:11px 9px; border:none; color:#D8DEE8; }
QTableWidget::item:selected { background:#2B4161; color:#C6DBFF; }
QTableCornerButton::section { background:#2B3039; border:none; }
QListWidget { background:#24282F; border:1px solid #373E49; border-radius:14px; padding:6px; }
QListWidget::item { padding:12px; border-radius:10px; }
QListWidget::item:selected { background:#2B4161; color:#A8CBFF; }
QListWidget#navigation { background:transparent; border:none; padding:0; outline:0; }
QListWidget#navigation::item { color:#AAB3C0; padding:13px 16px; margin:3px 0; border-radius:12px; font-size:14px; font-weight:500; }
QListWidget#navigation::item:selected { background:#303A49; color:#A8CBFF; font-weight:600; }
QListWidget#navigation::item:hover:!selected { background:#2B313B; }
QScrollArea,QScrollArea>QWidget,QScrollArea>QWidget>QWidget { background:transparent; border:none; }
QScrollBar:vertical { background:transparent; width:8px; margin:4px 0; }
QScrollBar:horizontal { background:transparent; height:8px; margin:0 4px; }
QScrollBar::handle:vertical,QScrollBar::handle:horizontal { background:#515D6E; border-radius:4px; min-height:28px; min-width:28px; }
QScrollBar::add-line,QScrollBar::sub-line,QScrollBar::add-page,QScrollBar::sub-page { background:transparent; border:none; height:0; width:0; }
QCheckBox { color:#D8DEE8; spacing:9px; background:transparent; }
QCheckBox::indicator { width:18px; height:18px; border:1px solid #667285; border-radius:6px; background:#24282F; }
QCheckBox::indicator:checked { background:#286BCE; border-color:#286BCE; image:url(:/settings/checkmark.svg); }
QToolTip { background:#343C49; color:white; padding:8px; border:none; }
QProgressBar { background:#343B46; border:none; border-radius:8px; }
QProgressBar::chunk { background:#78ADFF; border-radius:8px; }
    )");
}
