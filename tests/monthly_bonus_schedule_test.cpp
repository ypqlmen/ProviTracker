#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../commission.h"
#include <cassert>

int main() {
    const QDate october(2026, 10, 1), november(2026, 11, 1), december(2026, 12, 1);
    QJsonArray tiers;
    const BonusSettings standard;
    for (const auto& tier : standard.monthlyRateTiers) {
        tiers.append(toJson(MonthlyRateTier{tier.threshold, tier.threshold == 200 ? 70.0
            : tier.threshold == 250 ? 80.0 : tier.threshold == 300 ? 110.0 : tier.ratePerPoint}));
    }
    const QJsonArray schedule{
        QJsonObject{{"effectiveMonth", "2026-11"}, {"tiers", tiers}},
        QJsonObject{{"effectiveMonth", "2026-13"}, {"tiers", tiers}},
        QJsonObject{{"effectiveMonth", "2026-10"}, {"tiers", QJsonArray{tiers[1], tiers[0]}}}
    };
    Repository repo;
    repo.applyCloudPayload(QJsonObject{{"monthlyBonusSchedule", schedule}}, "Trial");
    const auto& b = repo.settings.bonus;
    assert(b.monthlyRateSchedule.size() == 1);
    assert(!repo.cloudPayload().contains("monthlyBonusSchedule"));
    assert(!toJson(b).contains("monthlyRateSchedule"));

    const auto rate = [&](double points, QDate date) {
        return CommissionEngine::monthlyRatePerPoint(points, b, date);
    };
    assert(rate(199.99, november) == 55);
    assert(rate(200, october) == 60 && rate(250, october) == 70 && rate(300, october) == 80);
    assert(rate(200, november) == 70 && rate(249.99, november) == 70 && rate(250, november) == 80);
    assert(rate(299.99, november) == 80 && rate(300, november) == 110 && rate(350, december) == 110);
    assert(CommissionEngine::monthlyRatePerPoint(300, standard, november) == 80);

    Product p;
    p.key = "test-product"; p.displayName = "Test"; p.points = 1;
    repo.products = {p};
    const auto sale = [&](QDate date, int points) {
        Order o;
        o.salespersonId = "seller";
        o.createdAt = QDateTime(date, QTime(12, 0)).toUTC();
        o.items = {OrderItem{p.key, points}};
        repo.orders.push_back(o);
    };
    sale(october.addDays(30), 250);
    sale(november, 300);
    const auto oct = monthRange(october), nov = monthRange(november);
    const auto old = CommissionEngine::calculate(repo, "seller", oct.first, oct.second);
    const auto fresh = CommissionEngine::calculate(repo, "seller", nov.first, nov.second);
    const auto mixed = CommissionEngine::calculate(repo, "seller", oct.first, nov.second);
    assert(old.monthlyBonus == 17500 && old.monthlyRate == 70);
    assert(fresh.monthlyBonus == 33000 && fresh.monthlyRate == 110);
    assert(mixed.mixedMonthlyRates && mixed.monthlyBonus == old.monthlyBonus + fresh.monthlyBonus);
    assert(mixed.commissionByDay.value("2026-10-31") == 30000);
    assert(mixed.commissionByDay.value("2026-11-01") == 48000);
    assert(nextMonthlyTierHint(old, b).contains(money(80) + " kr/point"));
    repo.applyCloudPayload({}, "Another account");
    assert(repo.settings.bonus.monthlyRateSchedule.isEmpty());
}
