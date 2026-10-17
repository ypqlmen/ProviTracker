#include "../repository.h"

int main() {
    Repository repo;
    repo.localPersistenceEnabled=false;
    repo.salespeople={{"old-a","Person A"},{"old-b","Person B"}};
    repo.settings.activeSalespersonId="old-b";
    Order first;first.id="order-a";first.createdAt=QDateTime(QDate(2026,10,1),QTime(12,0));first.salespersonId="old-a";first.companyName="Original virksomhed";first.masterRegistrationState="registered";
    Order second=first;second.id="order-b";second.salespersonId="old-b";second.masterRegistrationState="pending";
    repo.orders={first,second};
    SickPayEntry sick;sick.id="sick-a";sick.date=QDate(2026,10,2);sick.salespersonId="old-b";sick.amount=1200;repo.sickPayEntries={sick};
    const auto originalFirst=toJson(first),originalSecond=toJson(second),originalSick=toJson(sick);
    repo.normalizeForCloudUser(" VictorTang ");
    const auto owner=Repository::cloudSalespersonId("VictorTang");
    if(repo.salespeople.size()!=1 || repo.salespeople[0].id!=owner || repo.salespeople[0].name!="VictorTang" || repo.settings.activeSalespersonId!=owner)return 1;
    if(repo.orders.size()!=2 || repo.sickPayEntries.size()!=1)return 2;
    auto expectedFirst=originalFirst,expectedSecond=originalSecond,expectedSick=originalSick;
    expectedFirst["salespersonId"]=owner;expectedSecond["salespersonId"]=owner;expectedSick["salespersonId"]=owner;
    if(toJson(repo.orders[0])!=expectedFirst || toJson(repo.orders[1])!=expectedSecond || toJson(repo.sickPayEntries[0])!=expectedSick)return 3;
    const auto payload=repo.cloudPayload();
    repo.normalizeForCloudUser("VictorTang");
    if(repo.cloudPayload()!=payload)return 4;
    if(Repository::cloudSalespersonId(" victortang ")!=owner)return 5;
    // An old backup/cloud payload cannot change the authenticated account identity.
    auto legacy=payload;legacy["salesperson"]=QJsonObject{{"id","old-b"},{"name","Other seller"}};
    Repository restored;restored.localPersistenceEnabled=false;restored.applyCloudPayload(legacy,"VictorTang");
    if(restored.salespeople.size()!=1 || restored.salespeople[0].name!="VictorTang" || restored.settings.activeSalespersonId!=owner)return 6;
    if(restored.orders.size()!=2 || toJson(restored.orders[0])!=expectedFirst || toJson(restored.sickPayEntries[0])!=expectedSick)return 7;
    return 0;
}
