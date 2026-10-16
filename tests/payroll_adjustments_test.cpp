#include "../payroll_tax_estimate.h"
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
    Repository repo;repo.settings.hourlyRate=100;repo.settings.taxDeduction=4500;repo.settings.taxRatePercent=38;
    if(historyTaxEstimate(repo,"seller",QDate(2026,10,17)))return 6;
    if(effectiveTaxRate(repo,"seller",QDate(2026,10,17))!=38)return 7;
    for(int offset=1;offset<=3;++offset){
        const auto range=payrollRangeEndingInMonth(QDate(2026,10,1).addMonths(-offset));
        IntramanagerHoursEntry entry;entry.hours=350;entry.sickPay=0;entry.cancellations=-100;
        entry.syncedAt=range.second.addDays(1).toString(Qt::ISODate);
        repo.settings.intramanagerHoursCache[range.first.date().toString("dd-MM-yyyy")+"_"+range.second.date().toString("dd-MM-yyyy")]=entry;
    }
    auto estimate=historyTaxEstimate(repo,"seller",QDate(2026,10,17));
    if(!estimate || estimate->months!=3 || !near(estimate->annualGross,418800) || !near(estimate->ratePercent,37.01)) return 8;
    repo.settings.automaticTaxEstimate=false;
    if(effectiveTaxRate(repo,"seller",QDate(2026,10,17))!=38)return 9;
    if(historyTaxEstimate(repo,"seller",QDate(2027,10,17))) return 10;
    return 0;
}
