#pragma once
#include "premium_ui.h"

class OrderCardDelegate : public QStyledItemDelegate {
    struct CardLayout { int productsHeight,noteHeight,mailHeight,height; };
    static QFont textFont(int pixels,QFont::Weight weight=QFont::Normal){QFont font=qApp->font();font.setPixelSize(pixels);font.setWeight(weight);return font;}
    static CardLayout measure(const QVariantMap& data,int width){
        const int available=qMax(160,width-54);
        const int products=QFontMetrics(textFont(13)).boundingRect(QRect(0,0,available,20000),Qt::TextWordWrap,data.value("products").toString()).height();
        const QString note=data.value("note").toString();
        const int notes=note.isEmpty()?0:QFontMetrics(textFont(12)).boundingRect(QRect(0,0,available,20000),Qt::TextWordWrap,note).height()+12;
        const int mail=data.value("mailStatus").toString().isEmpty()?0:28;
        return {qMax(18,products),notes,mail,96+qMax(18,products)+notes+mail};
    }
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        const auto* list=qobject_cast<const QAbstractItemView*>(parent());const int width=list?list->viewport()->width():option.rect.width();
        return QSize(width,measure(index.data(Qt::UserRole).toMap(),width).height+12);
    }
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        painter->save();painter->setRenderHint(QPainter::Antialiasing);
        const auto data=index.data(Qt::UserRole).toMap();const bool selected=option.state&QStyle::State_Selected,hover=option.state&QStyle::State_MouseOver;
        const QRectF rect=QRectF(option.rect).adjusted(1,1,-7,-11);const auto path=continuousCardPath(rect,19);
        QLinearGradient fill(rect.topLeft(),rect.bottomRight());fill.setColorAt(0,QColor(selected?"#2E3C55":(hover?"#2A303C":"#252A33")));fill.setColorAt(1,QColor(selected?"#26344B":"#20242C"));painter->fillPath(path,fill);
        painter->setPen(QPen(QColor(selected?"#7297D5":"#373E4B"),1));painter->drawPath(path);
        const int left=qRound(rect.left())+22,right=qRound(rect.right())-22;
        const QString state=data.value("state").toString(),status=data.value("status").toString();
        const QColor accent(state=="registered"?"#8BD8B4":state=="error"?"#E9B67C":"#ACC3EF");
        const auto badgeFont=textFont(11,QFont::Medium);const int badgeWidth=qMin(245,QFontMetrics(badgeFont).horizontalAdvance(status)+24);
        const QRectF badge(right-badgeWidth,rect.top()+18,badgeWidth,25);painter->setPen(Qt::NoPen);painter->setBrush(QColor(accent.red(),accent.green(),accent.blue(),22));painter->drawRoundedRect(badge,9,9);painter->setPen(accent);painter->setFont(badgeFont);painter->drawText(badge.adjusted(10,0,-10,0),Qt::AlignCenter,QFontMetrics(badgeFont).elidedText(status,Qt::ElideRight,badgeWidth-20));
        painter->setFont(textFont(15,QFont::DemiBold));painter->setPen(QColor("#EDF1F8"));const QString company=data.value("company").toString();
        painter->drawText(QRect(left,qRound(rect.top())+18,qMax(30,right-left-badgeWidth-18),26),Qt::AlignVCenter,painter->fontMetrics().elidedText(company,Qt::ElideRight,qMax(30,right-left-badgeWidth-18)));
        painter->setFont(textFont(11));painter->setPen(QColor("#A1ADBF"));const QString meta=data.value("id").toString()+"  ·  "+data.value("date").toString();
        const QString points=data.value("pointsText").toString()+" point";const int pointWidth=QFontMetrics(textFont(13,QFont::Medium)).horizontalAdvance(points)+14;
        painter->drawText(QRect(left,qRound(rect.top())+49,qMax(30,right-left-pointWidth),20),Qt::AlignVCenter,painter->fontMetrics().elidedText(meta,Qt::ElideRight,qMax(30,right-left-pointWidth)));
        painter->setFont(textFont(13,QFont::Medium));painter->setPen(QColor("#D8E5FF"));painter->drawText(QRect(right-pointWidth,qRound(rect.top())+48,pointWidth,22),Qt::AlignRight|Qt::AlignVCenter,points);
        const auto layout=measure(data,option.rect.width());const int productTop=qRound(rect.top())+80;
        painter->setFont(textFont(13));painter->setPen(QColor("#D3DAE6"));painter->drawText(QRect(left,productTop,right-left,layout.productsHeight),Qt::TextWordWrap,data.value("products").toString());
        if(layout.noteHeight){painter->setFont(textFont(12));painter->setPen(QColor("#9AA7BB"));painter->drawText(QRect(left,productTop+layout.productsHeight+12,right-left,layout.noteHeight-12),Qt::TextWordWrap,data.value("note").toString());}
        if(layout.mailHeight){
            const QString state=data.value("mailState").toString();const QColor colour(state=="sent"?"#8BD8B4":(state=="error" || state=="uncertain" || state=="changed")?"#E9B67C":"#A6BDE4");
            const int y=productTop+layout.productsHeight+layout.noteHeight+12;painter->setPen(Qt::NoPen);painter->setBrush(colour);painter->drawEllipse(QPointF(left+3,y+9),3,3);
            painter->setPen(colour);painter->setFont(textFont(11,QFont::Medium));const QString mail=data.value("mailStatus").toString()+" · "+data.value("mailRecipient").toString();
            painter->drawText(QRect(left+15,y,right-left-15,20),Qt::AlignVCenter,painter->fontMetrics().elidedText(mail,Qt::ElideRight,right-left-15));
        }
        painter->restore();
    }
};

class PremiumOrdersList : public QListWidget {
public:
    explicit PremiumOrdersList(QWidget* parent=nullptr):QListWidget(parent){
        setObjectName("ordersList");setAccessibleName("Alle ordrer");setItemDelegate(new OrderCardDelegate(this));setSelectionMode(QAbstractItemView::SingleSelection);setEditTriggers(QAbstractItemView::NoEditTriggers);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);setResizeMode(QListView::Adjust);setMouseTracking(true);setFrameShape(QFrame::NoFrame);setSpacing(0);
        setStyleSheet("QListWidget#ordersList{background:transparent;border:none;padding:0;outline:0;}QListWidget#ordersList::item{background:transparent;border:none;padding:0;margin:0;}");
    }
protected:
    void resizeEvent(QResizeEvent* event)override{QListWidget::resizeEvent(event);doItemsLayout();}
    void mousePressEvent(QMouseEvent* event)override{if(!itemAt(event->position().toPoint())){clearSelection();setCurrentRow(-1);}QListWidget::mousePressEvent(event);}
    void paintEvent(QPaintEvent* event)override{
        QListWidget::paintEvent(event);bool hasVisible=false;for(int i=0;i<count();++i)if(!item(i)->isHidden()){hasVisible=true;break;}if(hasVisible)return;
        QPainter painter(viewport());painter.setRenderHint(QPainter::Antialiasing);const int y=qMax(24,viewport()->height()/2-75);premiumIcon("orders",QColor("#8197BB")).paint(&painter,QRect(viewport()->width()/2-20,y,40,40));
        QFont font=this->font();font.setPixelSize(17);font.setWeight(QFont::DemiBold);painter.setFont(font);painter.setPen(QColor("#DFE7F5"));painter.drawText(QRect(20,y+58,viewport()->width()-40,28),Qt::AlignCenter,count()?"Ingen ordrer matcher":"Dine ordrer samles her");
        font.setPixelSize(12);font.setWeight(QFont::Normal);painter.setFont(font);painter.setPen(QColor("#96A5BC"));painter.drawText(QRect(20,y+95,viewport()->width()-40,42),Qt::AlignCenter|Qt::TextWordWrap,count()?"Prøv en anden søgning eller status.":"Opret dit første salg med Ny ordre øverst.");
    }
};
