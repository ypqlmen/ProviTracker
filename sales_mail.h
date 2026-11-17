#pragma once
#include "domain.h"

static bool isSalesMailAddress(const QString& value) {
    static const QRegularExpression pattern(R"(^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?\.[A-Za-z]{2,}$)");
    return value.size()<=254 && pattern.match(value).hasMatch();
}

static QString salesMailStatusText(const Order& order) {
    if(order.salesMail.isEmpty())return {};
    const auto state=order.salesMailState;
    if(state=="sent")return order.salesMail.value("confirmation").toString()=="manual"?"Mail bekræftet manuelt":"Mail sendt";
    if(state=="pending")return "Mail afventer";
    if(state=="waiting-browser")return "Mail afventer Chrome";
    if(state=="sending")return "Sender mail";
    if(state=="uncertain")return "Kontrollér Sendt post";
    if(state=="changed")return "Mail kræver kontrol";
    return "Mail kræver handling";
}

static void prepareSalesMail(Order& order,const AppSettings& settings,bool newOrder) {
    if(newOrder && settings.salesMailEnabled && isSalesMailAddress(settings.salesMailRecipient) && isSalesMailAddress(settings.salesMailSender)) {
        order.salesMail=QJsonObject{{"registrationId",order.masterRegistration.value("registrationId")},
            {"recipient",settings.salesMailRecipient},{"senderEmail",settings.salesMailSender},{"subject","salgs reg"}};
        order.salesMailState="pending";order.salesMailError.clear();
    }
    if(!order.salesMail.isEmpty())order.masterRegistration["includeMail"]=true;
}

static bool validSalesMailSnapshot(const QJsonObject& value,const QJsonObject& receipt,const Order& order) {
    return value.value("registrationId")==order.masterRegistration.value("registrationId")
        && value.value("worksheetId")==receipt.value("worksheetId") && value.value("row")==receipt.value("row")
        && value.value("html").isString() && value.value("html").toString().startsWith("<table ")
        && value.value("html").toString().size()<=120000 && value.value("text").isString() && value.value("text").toString().size()<=40000;
}

static bool applySalesMailResult(Order& order,const QJsonObject& result,const QString& requestId) {
    const QString status=result.value("status").toString();
    const bool identity=!requestId.isEmpty() && result.value("requestId").toString()==requestId;
    const bool configured=!order.salesMail.isEmpty() && !order.salesMail.value("registrationId").toString().isEmpty()
        && order.salesMail.value("registrationId")==order.masterRegistration.value("registrationId")
        && isSalesMailAddress(order.salesMail.value("recipient").toString()) && isSalesMailAddress(order.salesMail.value("senderEmail").toString());
    const bool ok=result.value("success").toBool() && identity && configured && (status=="mail-sent" || status=="mail-already-sent")
        && result.value("registrationId")==order.salesMail.value("registrationId")
        && result.value("recipient").toString().compare(order.salesMail.value("recipient").toString(),Qt::CaseInsensitive)==0
        && result.value("senderEmail").toString().compare(order.salesMail.value("senderEmail").toString(),Qt::CaseInsensitive)==0;
    if(ok){order.salesMailState="sent";order.salesMailError.clear();order.salesMail.remove("reviewedRetry");order.salesMail.remove("html");order.salesMail.remove("text");order.salesMail["contentHash"]=result.value("contentHash");order.salesMail["sentAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);}
    else{
        const bool safe=identity && result.value("notSent").toBool(false) && status!="mail-uncertain";
        order.salesMailState=!safe?"uncertain":(status=="mail-login-required" || status=="mail-permission-required" || status=="mail-sleeping" || status=="chrome-update-required")?"waiting-browser":"error";
        order.salesMailError=result.value("error").toString("Afsendelsen er ikke sikkert bekræftet. Kontrollér Sendt post i Outlook.");
    }
    return ok;
}
