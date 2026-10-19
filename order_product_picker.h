#pragma once
#include "premium_ui.h"
#include "repository.h"
#include "commission.h"
#include <algorithm>
#include "order_product_selection.h"
#include <functional>

class ProductCatalogButton : public QAbstractButton {
    QString detail;
    int count=0;
public:
    ProductCatalogButton(const Product& product,QWidget* parent=nullptr):QAbstractButton(parent),detail(product.category+" · "+money(product.points)+" point / stk."){
        setText(product.displayName);setAccessibleName("Tilføj "+product.displayName);setFocusPolicy(Qt::StrongFocus);setCursor(Qt::PointingHandCursor);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setMinimumHeight(80);
    }
    QSize sizeHint()const override{return QSize(360,80);}
    QSize minimumSizeHint()const override{return QSize(220,80);}
    void setPickedCount(int value){if(count==value)return;count=value;setAccessibleDescription(QString::number(count)+" valgt");update();}
protected:
    void enterEvent(QEnterEvent* event)override{QAbstractButton::enterEvent(event);update();}
    void leaveEvent(QEvent* event)override{QAbstractButton::leaveEvent(event);update();}
    void keyPressEvent(QKeyEvent* event)override{if(event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter){click();event->accept();return;}QAbstractButton::keyPressEvent(event);}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const auto path=continuousCardPath(QRectF(rect()).adjusted(.5,.5,-.5,-.5),14);
        p.fillPath(path,QColor(isDown()?"#303E56":(underMouse()?"#272F3E":(count?"#222C3C":"#1C2028"))));p.setPen(QPen(QColor(hasFocus()?"#A4BCEE":(count?"#59729A":"#353C49")),1));p.drawPath(path);
        QFont font=this->font();font.setPixelSize(13);font.setWeight(QFont::Medium);p.setFont(font);p.setPen(QColor("#E3E8F2"));p.drawText(QRect(16,10,width()-112,38),Qt::AlignVCenter|Qt::TextWordWrap,text());
        font.setPixelSize(11);font.setWeight(QFont::Normal);p.setFont(font);p.setPen(QColor("#969FAF"));p.drawText(QRect(16,height()-26,width()-105,18),Qt::AlignVCenter,detail);
        font.setPixelSize(count?12:23);font.setWeight(QFont::Medium);p.setFont(font);p.setPen(QColor(count?"#B8CDF6":"#AABAD5"));const auto badge=count?QString::number(count)+" valgt":QString("+");p.drawText(QRect(width()-91,15,74,height()-30),Qt::AlignRight|Qt::AlignVCenter,p.fontMetrics().elidedText(badge,Qt::ElideRight,74));
    }
};

class OrderProductPicker : public QWidget {
    const Repository& repo;
    QString salespersonId;
    OrderProductSelection selection;
    QLineEdit* search;
    QComboBox* category;
    QTabBar* source;
    QVBoxLayout *catalogLayout,*selectedLayout;
    QLabel *total,*selectionCount;
    struct SelectedRow { QSpinBox* quantity;QLabel* points; };
    QMap<QString,SelectedRow> selectedRows;
    QMap<QString,ProductCatalogButton*> catalogButtons;
    QStringList visibleKeys;

    static void clear(QVBoxLayout* layout) {
        while(auto* item=layout->takeAt(0)){if(auto* widget=item->widget()){widget->hide();widget->deleteLater();}delete item;}
    }
    QStringList frequentKeys(bool recent) const {
        QStringList keys;
        if(recent){
            QVector<const Order*> orders;for(const auto& order:repo.orders)if(order.salespersonId==salespersonId)orders<<&order;
            std::sort(orders.begin(),orders.end(),[](const Order* a,const Order* b){return a->createdAt>b->createdAt;});
            for(const auto* order:orders)for(const auto& item:order->items)if(!keys.contains(item.productKey))keys<<item.productKey;
        }else{
            QMap<QString,qint64> counts;for(const auto& order:repo.orders)if(order.salespersonId==salespersonId)for(const auto& item:order.items)counts[item.productKey]+=item.quantity;
            QVector<QPair<QString,qint64>> sorted;for(auto it=counts.cbegin();it!=counts.cend();++it)sorted<<qMakePair(it.key(),it.value());
            std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.second==b.second?a.first<b.first:a.second>b.second;});for(const auto& item:sorted)keys<<item.first;
        }
        return keys.mid(0,12);
    }
    void notify() { refreshTotals();if(selectionChanged)selectionChanged(); }
    void refreshTotals() {
        double points=0;qint64 units=0;
        for(const auto& item:selection.items()){
            units+=item.quantity;const auto* product=repo.findProduct(item.productKey);const double rowPoints=product?product->points*item.quantity:0;points+=rowPoints;
            if(selectedRows.contains(item.productKey)){const auto row=selectedRows.value(item.productKey);const QSignalBlocker blocker(row.quantity);row.quantity->setValue(item.quantity);row.points->setText(money(rowPoints)+" point");}
        }
        for(auto it=catalogButtons.cbegin();it!=catalogButtons.cend();++it)it.value()->setPickedCount(selection.quantity(it.key()));
        selectionCount->setText(QString::number(units)+" produkter");total->setText(money(points)+" point");
    }
    void refreshSelected() {
        clear(selectedLayout);selectedRows.clear();
        if(selection.items().isEmpty()){
            auto* empty=new QWidget;auto* body=new QVBoxLayout(empty);body->setContentsMargins(12,40,12,40);body->setSpacing(12);
            auto* icon=new QLabel;icon->setAlignment(Qt::AlignCenter);icon->setPixmap(premiumIcon("orders").pixmap(30,30));body->addWidget(icon);
            auto* title=premiumLabel("Ingen produkter valgt","cardTitle");title->setAlignment(Qt::AlignCenter);body->addWidget(title);
            auto* hint=premiumLabel("Tilføj produkter fra kataloget.","muted");hint->setAlignment(Qt::AlignCenter);body->addWidget(hint);selectedLayout->addWidget(empty);
        }
        for(const auto& item:selection.items()){
            const QString key=item.productKey;const auto* product=repo.findProduct(key);const QString name=product?product->displayName:key;
            auto* row=new QWidget;row->setObjectName("selectedProduct");auto* body=new QVBoxLayout(row);body->setContentsMargins(14,14,14,14);body->setSpacing(10);
            auto* heading=new QHBoxLayout;auto* title=premiumLabel(name,"detailValue");title->setWordWrap(true);heading->addWidget(title,1);
            auto* remove=new QPushButton("×");remove->setObjectName("quantityControl");remove->setAccessibleName("Fjern "+name);remove->setToolTip("Fjern produkt");heading->addWidget(remove);body->addLayout(heading);
            auto* controls=new QHBoxLayout;controls->setSpacing(6);
            auto* minus=new QPushButton("−");minus->setObjectName("quantityControl");minus->setAccessibleName("Ét færre "+name);
            auto* quantity=new QSpinBox;quantity->setObjectName("quantity_"+key);quantity->setAccessibleName("Antal "+name);quantity->setRange(1,std::numeric_limits<int>::max());quantity->setButtonSymbols(QAbstractSpinBox::NoButtons);quantity->setAlignment(Qt::AlignCenter);quantity->setFixedWidth(70);quantity->setValue(item.quantity);
            auto* plus=new QPushButton("+");plus->setObjectName("quantityControl");plus->setAccessibleName("Ét mere "+name);
            controls->addWidget(minus);controls->addWidget(quantity);controls->addWidget(plus);controls->addStretch();
            auto* points=premiumLabel("","caption");controls->addWidget(points);body->addLayout(controls);selectedLayout->addWidget(row);selectedRows[key]={quantity,points};
            connect(remove,&QPushButton::clicked,this,[this,key](){selection.setQuantity(key,0);refreshSelected();notify();});
            connect(minus,&QPushButton::clicked,this,[this,key](){const int count=selection.quantity(key);selection.setQuantity(key,count-1);if(count==1)refreshSelected();notify();});
            connect(plus,&QPushButton::clicked,this,[this,key](){selection.add(key);notify();});
            connect(quantity,&QSpinBox::valueChanged,this,[this,key](int value){selection.setQuantity(key,value);notify();});
        }
        selectedLayout->addStretch();refreshTotals();
    }
    void refreshCatalog() {
        clear(catalogLayout);catalogButtons.clear();visibleKeys.clear();
        const QString query=search->text().simplified();const auto preferred=source->currentIndex()?frequentKeys(source->currentIndex()==2):QStringList();
        QVector<const Product*> products;
        for(const auto& product:repo.products){
            if(!category->currentData().toString().isEmpty() && category->currentData().toString()!=product.category)continue;
            if(source->currentIndex() && !preferred.contains(product.key))continue;
            bool match=true;for(const auto& word:query.split(' ',Qt::SkipEmptyParts))if(!(product.displayName+" "+product.category).contains(word,Qt::CaseInsensitive)){match=false;break;}if(match)products<<&product;
        }
        if(source->currentIndex())std::sort(products.begin(),products.end(),[&preferred](const Product* a,const Product* b){return preferred.indexOf(a->key)<preferred.indexOf(b->key);});
        for(const auto* product:products){
            const QString key=product->key;visibleKeys<<key;
            auto* button=new ProductCatalogButton(*product);button->setObjectName("catalog_"+key);catalogButtons[key]=button;catalogLayout->addWidget(button);
            connect(button,&QAbstractButton::clicked,this,[this,key](){addProduct(key);});
        }
        if(products.isEmpty()){auto* empty=premiumLabel("Ingen produkter fundet","muted");empty->setAlignment(Qt::AlignCenter);empty->setContentsMargins(8,35,8,35);catalogLayout->addWidget(empty);}
        catalogLayout->addStretch();refreshTotals();
    }
public:
    std::function<void()> selectionChanged;
    OrderProductPicker(const Repository& repository,const QString& owner,QWidget* parent=nullptr):QWidget(parent),repo(repository),salespersonId(owner){
        auto* root=new QHBoxLayout(this);root->setContentsMargins(0,0,0,0);root->setSpacing(18);
        auto* catalog=new PremiumCard;auto* body=new QVBoxLayout(catalog);body->setContentsMargins(20,20,20,16);body->setSpacing(12);body->addWidget(premiumLabel("Produktkatalog","sectionTitle"));
        auto* filters=new QHBoxLayout;filters->setSpacing(9);search=new QLineEdit;search->setObjectName("productSearch");search->setPlaceholderText("Søg efter produkt…");search->addAction(premiumIcon("search"),QLineEdit::LeadingPosition);search->setClearButtonEnabled(true);filters->addWidget(search,1);
        category=new QComboBox;category->setObjectName("productCategory");category->setMaximumWidth(170);category->addItem("Alle kategorier",QString());QStringList categories;for(const auto& product:repo.products)if(!categories.contains(product.category))categories<<product.category;for(const auto& name:categories)category->addItem(name,name);filters->addWidget(category);body->addLayout(filters);
        source=new QTabBar;source->setDrawBase(false);source->addTab("Alle");source->addTab("Favoritter");source->addTab("Senest brugte");body->addWidget(source);
        auto* catalogScroll=new QScrollArea;catalogScroll->setWidgetResizable(true);catalogScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);auto* catalogBody=new QWidget;catalogLayout=new QVBoxLayout(catalogBody);catalogLayout->setSizeConstraint(QLayout::SetMinimumSize);catalogLayout->setContentsMargins(0,0,5,0);catalogLayout->setSpacing(9);catalogScroll->setWidget(catalogBody);body->addWidget(catalogScroll,1);root->addWidget(catalog,3);
        auto* picked=new PremiumCard;picked->setMinimumWidth(305);auto* pickedBody=new QVBoxLayout(picked);pickedBody->setContentsMargins(20,20,20,20);pickedBody->setSpacing(16);
        auto* heading=new QHBoxLayout;heading->addWidget(premiumLabel("Din ordre","sectionTitle"));heading->addStretch();selectionCount=premiumLabel("0 produkter","caption");heading->addWidget(selectionCount);pickedBody->addLayout(heading);
        auto* pickedScroll=new QScrollArea;pickedScroll->setWidgetResizable(true);pickedScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);auto* pickedItems=new QWidget;selectedLayout=new QVBoxLayout(pickedItems);selectedLayout->setSizeConstraint(QLayout::SetMinimumSize);selectedLayout->setContentsMargins(0,0,5,0);selectedLayout->setSpacing(10);pickedScroll->setWidget(pickedItems);pickedBody->addWidget(pickedScroll,1);
        pickedBody->addWidget(premiumDivider());auto* totalRow=new QHBoxLayout;totalRow->addWidget(premiumLabel("I alt","muted"));totalRow->addStretch();total=premiumLabel("0,00 point","sectionTitle");totalRow->addWidget(total);pickedBody->addLayout(totalRow);root->addWidget(picked,2);
        connect(search,&QLineEdit::textChanged,this,[this](){refreshCatalog();});connect(category,&QComboBox::currentIndexChanged,this,[this](){refreshCatalog();});connect(source,&QTabBar::currentChanged,this,[this](){refreshCatalog();});
        connect(search,&QLineEdit::returnPressed,this,[this](){if(visibleKeys.size()==1)addProduct(visibleKeys.first());});refreshSelected();refreshCatalog();
    }
    void setItems(const QVector<OrderItem>& items){selection.setItems(items);refreshSelected();notify();}
    QVector<OrderItem> items()const{return selection.items();}
    void addProduct(const QString& key,int count=1){if(!repo.findProduct(key))return;const bool fresh=!selection.quantity(key);selection.add(key,count);if(fresh)refreshSelected();notify();}
    QString summary()const {double points=0;qint64 count=0;for(const auto& item:selection.items()){count+=item.quantity;if(const auto* p=repo.findProduct(item.productKey))points+=p->points*item.quantity;}return QString::number(count)+" produkter · "+money(points)+" point";}
    void focusSearch(){search->setFocus();search->selectAll();}
};
