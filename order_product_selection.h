#pragma once
#include "domain.h"
#include <limits>

class OrderProductSelection {
    QVector<OrderItem> selected;
public:
    const QVector<OrderItem>& items() const { return selected; }
    int quantity(const QString& key) const { for(const auto& item:selected)if(item.productKey==key)return item.quantity;return 0; }
    void setQuantity(const QString& key,int quantity) {
        for(int i=0;i<selected.size();++i)if(selected[i].productKey==key){if(quantity<=0)selected.removeAt(i);else selected[i].quantity=quantity;return;}
        if(!key.isEmpty() && quantity>0)selected.push_back({key,quantity});
    }
    void add(const QString& key,int count=1) {
        if(key.isEmpty() || count<=0)return;
        setQuantity(key,static_cast<int>(qMin<qint64>(std::numeric_limits<int>::max(),qint64(quantity(key))+count)));
    }
    void setItems(const QVector<OrderItem>& items) { if(&items==&selected)return;selected.clear();for(const auto& item:items)add(item.productKey,item.quantity); }
};
