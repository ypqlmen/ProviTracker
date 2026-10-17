#include "../commission.h"
#include <cmath>

static bool near(double a,double b){return std::abs(a-b)<0.00001;}
int main(){
    QJsonObject legacy{{"sickPay",584.92},{"hours",120},{"syncedAt","2026-09-21T12:00:00"}};
    const auto migrated=fromIntramanagerHoursJson(legacy);
    if(!near(migrated.sickPay,1169.84) || migrated.payrollSchema!=1) return 1;
    const auto again=fromIntramanagerHoursJson(toJson(migrated));
    if(!near(again.sickPay,1169.84)) return 2;
    auto current=migrated;current.payrollSchema=2;current.cancellations=-380.40;
    const auto encoded=toJson(current);
    const auto decoded=fromIntramanagerHoursJson(encoded);
    if(!near(encoded["sickPay"].toDouble(),584.92) || !near(decoded.sickPay,1169.84) || !near(decoded.cancellations,-380.40)) return 3;
    // Older clients preserve the old half-value; reading that must not double a full-value twice.
    if(!near(fromIntramanagerHoursJson(QJsonObject{{"sickPay",encoded["sickPay"]}}).sickPay,1169.84)) return 4;
    if(intramanagerHoursEntryCompletesRange(migrated,QDateTime(QDate(2026,9,20),QTime(23,59,59)))) return 5;
    // Stored manual tax settings remain authoritative even if old payloads
    // still contain the retired automatic-estimate options.
    QJsonObject oldSettings{{"taxRatePercent",38.0},{"taxDeduction",4500.0},
        {"automaticTaxEstimate",true},{"municipalTaxPercent",25.0}};
    const auto manual = fromSettingsJson(oldSettings);
    if(!near(manual.taxRatePercent,38.0) || !near(manual.taxDeduction,4500.0))return 6;
    if(toJson(manual).contains("automaticTaxEstimate"))return 7;
    return 0;
}
