#pragma once
#include <QtWidgets>
#include <cmath>

static QPainterPath continuousCardPath(const QRectF& r,qreal radius) {
    radius=qMin(radius,qMin(r.width(),r.height())/2);
    QPainterPath path;path.moveTo(r.left()+radius,r.top());
    const QPointF centers[]={{r.right()-radius,r.top()+radius},{r.right()-radius,r.bottom()-radius},{r.left()+radius,r.bottom()-radius},{r.left()+radius,r.top()+radius}};
    for(int corner=0;corner<4;++corner)for(int step=0;step<=32;++step){
        const double a=(-90+corner*90+step*90.0/32)*3.141592653589793/180,c=std::cos(a),s=std::sin(a);
        path.lineTo(centers[corner]+QPointF(radius*std::copysign(std::sqrt(std::abs(c)),c),radius*std::copysign(std::sqrt(std::abs(s)),s)));
    }
    path.closeSubpath();return path;
}

static QIcon premiumIcon(const QString& name,const QColor& color=QColor("#A8AFBF")) {
    QPixmap pixmap(48,48);pixmap.fill(Qt::transparent);QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.scale(2,2);
    p.setPen(QPen(color,1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    if(name=="grid")for(int x:{4,14})for(int y:{4,14})p.drawRoundedRect(QRectF(x,y,6,6),1.7,1.7);
    else if(name=="orders"){p.drawRoundedRect(QRectF(5,3,14,18),3,3);p.drawLine(9,8,15,8);p.drawLine(9,12,15,12);p.drawLine(9,16,13,16);}
    else if(name=="reports"){p.drawRoundedRect(QRectF(3,3,18,18),4,4);p.drawLine(7,16,7,12);p.drawLine(12,16,12,8);p.drawLine(17,16,17,10);}
    else if(name=="contacts" || name=="person"){p.drawEllipse(QRectF(8,3,8,8));p.drawArc(QRectF(4,12,16,13),0,180*16);}
    else if(name=="settings"){for(int y:{6,12,18})p.drawLine(4,y,20,y);p.setBrush(QColor("#23262E"));p.drawEllipse(QPointF(9,6),2.5,2.5);p.drawEllipse(QPointF(15,12),2.5,2.5);p.drawEllipse(QPointF(9,18),2.5,2.5);}
    else if(name=="search"){p.drawEllipse(QRectF(4,4,12,12));p.drawLine(14,14,20,20);}
    else if(name=="wallet"){p.drawRoundedRect(QRectF(3,5,18,15),3,3);p.drawRoundedRect(QRectF(14,10,7,6),2,2);p.drawPoint(QPointF(17,13));p.drawLine(6,5,17,2);}
    else if(name=="trend"){p.drawPolyline(QPolygonF({{3,17},{9,11},{13,14},{21,5}}));p.drawLine(15,5,21,5);p.drawLine(21,5,21,11);}
    else if(name=="calendar"){p.drawRoundedRect(QRectF(3,5,18,16),3,3);p.drawLine(3,10,21,10);p.drawLine(8,2,8,7);p.drawLine(16,2,16,7);}
    else if(name=="clock"){p.drawEllipse(QRectF(3,3,18,18));p.drawLine(12,6,12,12);p.drawLine(12,12,16,14);}
    else if(name=="plus"){p.drawLine(12,5,12,19);p.drawLine(5,12,19,12);}
    else if(name=="arrow"){p.drawLine(5,12,19,12);p.drawLine(14,7,19,12);p.drawLine(14,17,19,12);}
    else if(name=="refresh"){p.drawArc(QRectF(4,4,16,16),35*16,285*16);p.drawLine(20,4,20,10);p.drawLine(14,10,20,10);}
    return QIcon(pixmap);
}

class PremiumAvatar : public QLabel {
public:
    PremiumAvatar(){
        setObjectName("brandAvatar");setFixedSize(44,44);setAccessibleName("Provi profilbillede");
        const QPixmap original(":/settings/provi-avatar.jpg");
        if(original.isNull()){setText("P");return;}
        const qreal ratio=qApp->devicePixelRatio();const QSize pixels(qRound(44*ratio),qRound(44*ratio));
        QPixmap avatar(pixels);avatar.fill(Qt::transparent);QPainter painter(&avatar);painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.scale(ratio,ratio);
        const auto path=continuousCardPath(QRectF(.5,.5,43,43),14);painter.setClipPath(path);const auto scaled=original.scaled(pixels,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
        painter.drawPixmap(QRectF(0,0,44,44),scaled,QRectF((scaled.width()-pixels.width())/2.0,(scaled.height()-pixels.height())/2.0,pixels.width(),pixels.height()));
        painter.setClipping(false);painter.setPen(QColor(255,255,255,40));painter.drawPath(path);painter.end();avatar.setDevicePixelRatio(ratio);setPixmap(avatar);
    }
};

class PremiumWorkspace : public QWidget {
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.fillRect(rect(),QColor("#15171D"));
        QRadialGradient glow(QPointF(width()*.76,-120),width()*.8);glow.setColorAt(0,QColor(79,101,155,23));glow.setColorAt(1,QColor(21,23,29,0));p.fillRect(rect(),glow);
    }
};

class PremiumCard:public QFrame {
public:
    explicit PremiumCard(QWidget* parent=nullptr):QFrame(parent){setProperty("premiumCard",true);setAttribute(Qt::WA_TranslucentBackground);}
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const auto path=continuousCardPath(QRectF(rect()).adjusted(1,1,-1,-1),22);
        QLinearGradient fill(0,0,width()*.3,height());fill.setColorAt(0,QColor(property("hero").toBool()?"#2B303C":"#252830"));fill.setColorAt(1,QColor("#20232A"));p.fillPath(path,fill);
        if(property("hero").toBool()){p.save();p.setClipPath(path);QRadialGradient glow(QPointF(width(),0),width()*.8);glow.setColorAt(0,QColor(94,126,202,25));glow.setColorAt(1,QColor(0,0,0,0));p.fillRect(rect(),glow);p.restore();}
        QLinearGradient edge(0,0,0,height());edge.setColorAt(0,QColor(255,255,255,24));edge.setColorAt(1,QColor(255,255,255,9));p.setPen(QPen(QBrush(edge),1));p.drawPath(path);
    }
};

static QLabel* premiumLabel(const QString& text,const char* role,QWidget* parent=nullptr){auto* label=new QLabel(text,parent);label->setProperty("role",role);return label;}
static QFrame* premiumDivider(bool vertical=false){auto* line=new QFrame;line->setObjectName("divider");if(vertical)line->setFixedWidth(1);else line->setFixedHeight(1);return line;}

class PremiumSalaryCard:public PremiumCard {
    QLabel *amount,*net,*payout,*forecast;QFrame* forecastDivider;QVector<QLabel*> values;
public:
    explicit PremiumSalaryCard(const QString& title,bool current,QWidget* parent=nullptr):PremiumCard(parent){
        setProperty("hero",current);setMinimumHeight(292);auto* layout=new QVBoxLayout(this);layout->setContentsMargins(26,24,26,22);layout->setSpacing(12);
        auto* top=new QHBoxLayout;auto* icon=new QLabel;icon->setPixmap(premiumIcon("wallet",QColor(current?"#B5C9FA":"#A3ABBA")).pixmap(19,19));
        top->addWidget(icon);top->addSpacing(3);top->addWidget(premiumLabel(title,"cardTitle"));top->addStretch();
        layout->addLayout(top);
        amount=premiumLabel("—","salary");layout->addWidget(amount);
        net=premiumLabel("—","net");layout->addWidget(net);
        payout=premiumLabel("—","muted");layout->addWidget(payout);layout->addSpacing(3);layout->addWidget(premiumDivider());
        auto* details=new QGridLayout;details->setHorizontalSpacing(20);details->setVerticalSpacing(7);
        const QStringList labels={"Løn fra timer","Sygeløn","Provision","Annulleringer"};
        for(int i=0;i<4;++i){auto* cell=new QVBoxLayout;cell->setSpacing(5);cell->addWidget(premiumLabel(labels[i],"caption"));auto* value=premiumLabel("—","detailValue");value->setWordWrap(true);values<<value;cell->addWidget(value);details->addLayout(cell,i/2,i%2);}
        layout->addLayout(details);layout->addSpacing(2);forecastDivider=premiumDivider();layout->addWidget(forecastDivider);
        forecast=premiumLabel("—","forecast");forecast->setWordWrap(true);layout->addWidget(forecast);layout->addStretch();
    }
    void present(const QString& gross,const QString& takeHome,const QString& payDate,const QStringList& detailValues,const QString& projection){
        amount->setText(gross);net->setText(takeHome);payout->setText(payDate);
        for(int i=0;i<values.size();++i)values[i]->setText(detailValues.value(i));forecast->setText(projection);forecast->setVisible(!projection.isEmpty());forecastDivider->setVisible(!projection.isEmpty());
    }
};

class BusinessContactsTable:public QTableWidget {
public:using QTableWidget::QTableWidget;
protected:
    void paintEvent(QPaintEvent* event)override{
        QTableWidget::paintEvent(event);if(rowCount())return;QPainter p(viewport());p.setRenderHint(QPainter::Antialiasing);const int x=viewport()->width()/2,y=viewport()->height()/2-55;
        premiumIcon("contacts",QColor("#8197BB")).paint(&p,QRect(x-22,y,44,44));QFont f=font();f.setPixelSize(16);f.setWeight(QFont::DemiBold);p.setFont(f);p.setPen(QColor("#DDE5F5"));p.drawText(QRect(10,y+57,viewport()->width()-20,28),Qt::AlignCenter,"Start med et navn");f.setPixelSize(12);f.setWeight(QFont::Normal);p.setFont(f);p.setPen(QColor("#97A5BE"));p.drawText(QRect(10,y+90,viewport()->width()-20,40),Qt::AlignCenter|Qt::TextWordWrap,"Mulige match vises her med et direkte arbejdsnummer og en kilde.");
    }
};
class BusinessContactView:public QWidget {
public:
    QLineEdit *name,*company,*website;QPushButton* search;QLabel* status;QTableWidget* results;
    BusinessContactView(){
        auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);root->setSpacing(18);auto* card=new PremiumCard;auto* body=new QVBoxLayout(card);body->setContentsMargins(26,24,26,24);body->setSpacing(16);
        body->addWidget(premiumLabel("Find en erhvervskontakt","sectionTitle"));
        auto* form=new QGridLayout;form->setHorizontalSpacing(20);form->setVerticalSpacing(16);
        name=new QLineEdit;name->setPlaceholderText("Fornavn og efternavn");name->setMaxLength(100);company=new QLineEdit;company->setPlaceholderText("Virksomhed eller by");company->setMaxLength(140);website=new QLineEdit;website->setPlaceholderText("Virksomhedens hjemmeside");website->setMaxLength(300);
        form->addWidget(premiumLabel("Fuldt navn","caption"),0,0);form->addWidget(premiumLabel("Virksomhed / by · valgfrit","caption"),0,1);form->addWidget(name,1,0);form->addWidget(company,1,1);form->addWidget(premiumLabel("Hjemmeside · valgfrit","caption"),2,0,1,2);form->addWidget(website,3,0,1,2);form->setColumnStretch(0,1);form->setColumnStretch(1,1);body->addLayout(form);
        auto* actions=new QHBoxLayout;status=premiumLabel("","muted");status->setWordWrap(true);actions->addWidget(status,1);search=new QPushButton("Find kontakt");search->setIcon(premiumIcon("search",Qt::white));search->setProperty("primary",true);actions->addWidget(search);body->addLayout(actions);root->addWidget(card);
        auto* resultCard=new PremiumCard;auto* resultLayout=new QVBoxLayout(resultCard);resultLayout->setContentsMargins(24,22,24,22);resultLayout->setSpacing(12);resultLayout->addWidget(premiumLabel("Resultater","cardTitle"));
        results=new BusinessContactsTable(0,3);results->setHorizontalHeaderLabels({"Navn","Direkte arbejdsnummer","Kilde · dobbeltklik for at åbne"});results->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);results->verticalHeader()->hide();results->verticalHeader()->setDefaultSectionSize(52);results->setEditTriggers(QAbstractItemView::NoEditTriggers);results->setSelectionBehavior(QAbstractItemView::SelectRows);results->setShowGrid(false);resultLayout->addWidget(results,1);root->addWidget(resultCard,1);
    }
};

static void applyMidnightPalette(QApplication& app){
    app.setStyle(QStyleFactory::create("Fusion"));QLocale::setDefault(QLocale(QLocale::Danish,QLocale::Denmark));
    QFont font=QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#ifdef Q_OS_WIN
    if(QFontDatabase::families().contains("Segoe UI Variable"))font.setFamily("Segoe UI Variable");else font.setFamily("Segoe UI");
#endif
    font.setPointSize(10);font.setHintingPreference(QFont::PreferNoHinting);app.setFont(font);
    QPalette p;p.setColor(QPalette::Window,QColor("#15171D"));p.setColor(QPalette::Base,QColor("#20232A"));p.setColor(QPalette::AlternateBase,QColor("#24272F"));
    for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText})p.setColor(role,QColor("#F0F1F5"));
    p.setColor(QPalette::Button,QColor("#2C3039"));p.setColor(QPalette::Highlight,QColor("#385EA6"));p.setColor(QPalette::HighlightedText,QColor("#FFFFFF"));p.setColor(QPalette::PlaceholderText,QColor("#7D8596"));
    p.setColor(QPalette::Link,QColor("#A9C2FA"));p.setColor(QPalette::Disabled,QPalette::Text,QColor("#71798A"));p.setColor(QPalette::Disabled,QPalette::ButtonText,QColor("#71798A"));app.setPalette(p);
}

static QString premiumStyle(){return QStringLiteral(R"(
QWidget {color:#EDEFF5;font-size:13px;} QMainWindow,QDialog{background:#15171D;}
QLabel{background:transparent;border:none;padding:0;} QFrame[premiumCard="true"]{background:transparent;border:none;}
QFrame#sidebar{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #20232B,stop:1 #1B1E25);border:none;border-right:1px solid #30343E;}
QFrame#divider{background:#383D49;border:none;}
QLabel[role="cardTitle"]{font-size:14px;font-weight:600;color:#D9DEEA;}
QLabel[role="caption"]{font-size:11px;color:#969FAF;font-weight:500;}
QLabel[role="muted"]{font-size:12px;color:#929BAC;}
QLabel[role="badge"]{font-size:10px;font-weight:500;color:#B2BBCB;background:rgba(255,255,255,9);border:1px solid rgba(255,255,255,12);border-radius:9px;padding:4px 8px;}
QLabel[role="salary"]{font-size:38px;font-weight:500;color:#FAFBFF;}
QLabel[role="net"]{font-size:13px;color:#7AD1AD;font-weight:500;}
QLabel[role="detailValue"]{font-size:13px;color:#DBE0EB;font-weight:500;}
QLabel[role="forecast"]{font-size:11px;color:#A0AABC;}
QLabel[role="statValue"]{font-size:25px;font-weight:600;color:#F2F4FA;}
QLabel[role="pageTitle"]{font-size:30px;font-weight:600;color:#F7F8FC;}
QLabel[role="sectionTitle"]{font-size:16px;font-weight:600;color:#E7EAF2;}
QPushButton{background:rgba(255,255,255,8);border:1px solid rgba(255,255,255,20);border-radius:10px;padding:8px 14px;color:#D5DBE8;font-weight:500;min-height:18px;}
QPushButton:focus{border-color:#93AFE6;} QPushButton:hover{background:rgba(255,255,255,18);border-color:rgba(255,255,255,36);} QPushButton:pressed{background:rgba(255,255,255,29);}
QPushButton[primary="true"]{background:#3B6BD2;border:1px solid #6388DC;color:#FFFFFF;font-weight:600;}
QPushButton[primary="true"]:hover{background:#4879DF;} QPushButton[destructive="true"]{color:#F2A2A8;}
QPushButton[quiet="true"]{border:1px solid transparent;background:transparent;color:#AAB5C9;}
QPushButton[quiet="true"]:hover{background:rgba(255,255,255,11);color:#F1F4FB;}
QPushButton[catalogItem="true"]{background:#1C2028;border:1px solid #353C49;border-radius:13px;padding:0;text-align:left;}
QPushButton[catalogItem="true"]:hover{background:#272F3E;border-color:#637EA9;}
QPushButton[catalogItem="true"]:pressed{background:#303E56;}
QWidget#selectedProduct{background:#1B1E25;border:1px solid #353C49;border-radius:13px;}
QPushButton#quantityControl{min-width:30px;max-width:30px;min-height:30px;max-height:30px;padding:0;border-radius:9px;font-size:17px;}
QPushButton:disabled{color:#656E80;background:rgba(255,255,255,3);border-color:rgba(255,255,255,7);}
QLineEdit,QComboBox,QDateEdit,QDateTimeEdit,QSpinBox,QDoubleSpinBox{background:#191C23;border:1px solid #383E4B;border-radius:9px;padding:8px 11px;color:#EFF2F8;selection-background-color:#385EA6;selection-color:white;min-height:19px;}
QLineEdit:hover,QComboBox:hover,QSpinBox:hover,QDoubleSpinBox:hover{border-color:#4C566A;}
QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus,QDateEdit:focus,QDateTimeEdit:focus{border:1px solid #809FDA;background:#1D222D;}
QLineEdit:disabled,QDoubleSpinBox:disabled{color:#6E7788;background:#1E222A;border-color:#2D333E;}
QComboBox::drop-down,QDateEdit::drop-down,QDateTimeEdit::drop-down{border:none;background:transparent;width:28px;}
QComboBox::down-arrow,QDateEdit::down-arrow,QDateTimeEdit::down-arrow{image:url(:/settings/chevron-down.svg);width:12px;height:12px;}
QComboBox QAbstractItemView,QMenu{background:#292E38;color:#EDF1F9;border:1px solid #475166;border-radius:10px;padding:6px;selection-background-color:#3B5279;selection-color:white;}
QMenu::item{padding:8px 24px 8px 12px;border-radius:5px;} QMenu::item:selected{background:#3B5279;}
QCheckBox{spacing:10px;color:#D2D9E7;min-height:24px;background:transparent;}
QCheckBox::indicator{width:18px;height:18px;border:1px solid #59657B;border-radius:6px;background:#191C23;}
QCheckBox::indicator:checked{background:#3B6BD2;border-color:#6388DC;image:url(:/settings/checkmark.svg);}
QListWidget{background:#191C23;border:1px solid #383E4B;border-radius:10px;padding:5px;outline:none;}
QListWidget::item{padding:11px 12px;border-radius:8px;} QListWidget::item:selected{background:#35445F;color:#F1F5FF;}
QListWidget#navigation{background:transparent;border:none;padding:0;}
QListWidget#navigation::item{padding:12px 14px;margin:3px 0;border-radius:11px;color:#B2BBCC;font-size:13px;}
QListWidget#navigation::item:selected{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #39455E,stop:1 #303B50);color:#EFF4FF;font-weight:600;}
QListWidget#navigation::item:hover:!selected{background:rgba(255,255,255,8);}
QTabWidget::pane{border:none;background:transparent;} QTabBar{background:transparent;}
QTabBar::tab{background:transparent;border:none;border-bottom:2px solid transparent;color:#959FB2;padding:12px 17px;margin-right:4px;font-weight:500;}
QTabBar::tab:selected{color:#DDE9FF;border-bottom:2px solid #88A9EF;} QTabBar::tab:hover{color:white;background:rgba(255,255,255,5);}
QScrollArea,QScrollArea>QWidget,QScrollArea>QWidget>QWidget{background:transparent;border:none;}
QScrollBar:vertical{background:transparent;width:7px;margin:4px 0;}QScrollBar:horizontal{background:transparent;height:7px;margin:0 4px;}
QScrollBar::handle:vertical,QScrollBar::handle:horizontal{background:#4A5262;border-radius:3px;min-height:26px;min-width:26px;}
QScrollBar::add-line,QScrollBar::sub-line,QScrollBar::add-page,QScrollBar::sub-page{background:transparent;border:none;height:0;width:0;}
QTableWidget{background:transparent;border:none;alternate-background-color:rgba(255,255,255,3);gridline-color:#343B48;selection-background-color:#33486A;selection-color:white;outline:0;}
QHeaderView{background:#232730;border:none;padding:0;} QHeaderView::section{background:#232730;color:#9FAABD;border:none;font-weight:500;padding:10px 12px;}
QTableWidget::item{padding:12px 10px;border-bottom:1px solid rgba(255,255,255,8);}QTableWidget::item:selected{background:#33486A;color:#F1F5FF;}
QTableCornerButton::section{background:#232730;border:none;}
QTextEdit,QTextBrowser{background:#191C23;border:1px solid #383E4B;border-radius:12px;padding:12px;selection-background-color:#385EA6;}
QGroupBox{border:none;margin:0;background:transparent;} QToolTip{background:#303A4C;color:white;padding:8px;border:1px solid #4A5972;border-radius:7px;}
)");}
