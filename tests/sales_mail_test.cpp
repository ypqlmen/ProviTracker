#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../sales_mail.h"
#include <cassert>
int main(){
    AppSettings s;s.salesMailEnabled=true;s.salesMailRecipient="sales+test@example.com";s.salesMailSender="seller@example.com";
    Order o;o.masterRegistration=QJsonObject{{"registrationId","sale-identity"}};o.masterRegistrationState="registered";
    prepareSalesMail(o,s,false);assert(o.salesMail.isEmpty());
    prepareSalesMail(o,s,true);assert(o.salesMailState=="pending");assert(o.masterRegistration.value("includeMail").toBool());
    auto restored=fromOrderJson(toJson(o));assert(restored.salesMail==o.salesMail && restored.salesMailState=="pending");
    assert(!isSalesMailAddress("person@example.com\nBcc:other@example.com"));assert(!isSalesMailAddress("a@example.com,b@example.com"));
    s.salesMailRecipient="changed@example.com";prepareSalesMail(o,s,false);assert(o.salesMail.value("recipient").toString()=="sales+test@example.com");
    QJsonObject r{{"success",true},{"requestId","attempt"},{"status","mail-sent"},{"registrationId","sale-identity"},{"recipient","sales+test@example.com"},{"senderEmail","seller@example.com"}};
    Order missing;assert(!applySalesMailResult(missing,r,"attempt"));o.salesMail["html"]="queued table";o.salesMail["text"]="queued text";
    assert(applySalesMailResult(o,r,"attempt") && o.salesMailState=="sent" && o.masterRegistrationState=="registered");assert(!o.salesMail.contains("html") && !o.salesMail.contains("text"));
    r["requestId"]="old";assert(!applySalesMailResult(o,r,"attempt") && o.salesMailState=="uncertain");
    r=QJsonObject{{"requestId","attempt"},{"success",false},{"notSent",true},{"status","mail-login-required"}};
    assert(!applySalesMailResult(o,r,"attempt") && o.salesMailState=="waiting-browser" && o.masterRegistrationState=="registered");
    r["status"]="mail-uncertain";assert(!applySalesMailResult(o,r,"attempt") && o.salesMailState=="uncertain");
    r["status"]="mail-not-sent";assert(!applySalesMailResult(o,r,"attempt") && o.salesMailState=="error");
    r.remove("notSent");assert(!applySalesMailResult(o,r,"attempt") && o.salesMailState=="uncertain");
    const auto restoredSettings=fromSettingsJson(toJson(s));assert(restoredSettings.salesMailEnabled && restoredSettings.salesMailRecipient==s.salesMailRecipient);
    assert(!fromSettingsJson(QJsonObject()).salesMailEnabled);
}
