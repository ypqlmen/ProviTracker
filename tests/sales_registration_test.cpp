#include "../sales_registration.h"
#include <iostream>
#include <cstdlib>
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    Order order;
    order.id = "00123456789012345678";
    order.salespersonId = "seller-a";
    order.createdAt = QDateTime(QDate(2026,9,11), QTime(12,30));
    order.sellerInitials = "TEST";
    order.cvrNumber = "00123456";
    order.companyName = "ÆØÅ Test";
    order.phoneNumber = "00112233";
    order.note = "Bemærkning";
    order.items = {{"mobil_200gb_36_term",2},{"new_unknown_key",1}};
    const QVector<Product> products = {{"mobil_200gb_36_term","Mobil 200GB 36mdr + term","Mobil",3.25,CountMode::Voice,true}};
    order.masterRegistration = makeSalesRegistration(order, products);
    order.masterRegistrationState = "pending";
    order.masterWorkbookUrl = "https://example.sharepoint.com/personal/test/Documents/master.xlsx";
    const auto restored = fromOrderJson(toJson(order));
    check(restored.masterRegistration == order.masterRegistration);
    check(restored.masterWorkbookUrl == order.masterWorkbookUrl);
    check(restored.masterRegistrationState == "pending");
    check(restored.masterRegistration["orderNumber"].toString() == order.id);
    check(restored.masterRegistration["date"].toString() == "11.09.2026");
    check(restored.masterRegistration["items"].toArray().size() == 2); // Never drop unknown products.
    check(!restored.masterRegistration.contains("recipient"));
    check(isMasterWorkbookUrl(order.masterWorkbookUrl));
    check(!isMasterWorkbookUrl("https://example.sharepoint.com.evil.test/file"));
    check(!isMasterWorkbookUrl("http://example.sharepoint.com/file"));
    check(!isMasterWorkbookUrl("https://user:password@example.sharepoint.com/file"));
    check(fromOrderJson(QJsonObject{{"id","legacy"}}).masterRegistration.isEmpty());
    AppSettings settings;
    settings.masterRegistrationEnabled = true;
    settings.masterWorkbookUrl = order.masterWorkbookUrl;
    check(fromSettingsJson(toJson(settings)).masterWorkbookUrl == settings.masterWorkbookUrl);
    check(fromSettingsJson(toJson(settings)).masterRegistrationEnabled);
    check(!fromSettingsJson(QJsonObject{{"salesRegistrationEnabled",true}}).masterRegistrationEnabled);
    std::cout << "Sales registration snapshots and settings passed\n";
}
