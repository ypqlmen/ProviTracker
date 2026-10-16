#pragma once
#include "commission.h"

struct HistoryTaxEstimate {
    double ratePercent = 0;
    double annualGross = 0;
    int months = 0;
};

// A planning estimate, not a tax-card rate. Only verified 2026 thresholds are
// enabled. Municipal/church rates and the monthly allowance stay user-editable.
static std::optional<HistoryTaxEstimate> historyTaxEstimate(
    const Repository& repo, const QString& sellerId, const QDate& today) {
    if (today.year() != 2026) return std::nullopt;
    double gross = 0;
    int periods = 0;
    for (int offset = 1; offset <= 3; ++offset) {
        const QDate month = QDate(today.year(), today.month(), 1).addMonths(-offset);
        const auto period = payrollRangeEndingInMonth(month);
        const QString key = period.first.date().toString("dd-MM-yyyy") + "_" + period.second.date().toString("dd-MM-yyyy");
        if (!repo.settings.intramanagerHoursCache.contains(key)) continue;
        const auto hours = repo.settings.intramanagerHoursCache.value(key);
        if (!intramanagerHoursEntryCompletesRange(hours, period.second)) continue;
        const auto previous = monthRange(month.addMonths(-1));
        const auto current = CommissionEngine::calculate(repo,sellerId,period.first,period.second,period);
        const auto delayed = CommissionEngine::calculate(repo,sellerId,previous.first,previous.second);
        const double salary = hours.hours*repo.settings.hourlyRate + hours.sickPay + hours.cancellations
            + current.dayBonus + delayed.monthlyBonus + delayed.simoBonus + delayed.voiceBonus;
        if (!std::isfinite(salary) || salary <= 0) continue;
        gross += salary;
        ++periods;
    }
    if (!periods) return std::nullopt;
    HistoryTaxEstimate estimate;
    estimate.months = periods;
    estimate.annualGross = gross / periods * 12;
    const double afterAm = estimate.annualGross * 0.92;
    const double taxable = qMax(0.0,afterAm - repo.settings.taxDeduction*12);
    const double municipal = qBound(0.0,repo.settings.municipalTaxPercent,35.0);
    const double church = qBound(0.0,repo.settings.churchTaxPercent,3.0);
    const double middleRate = qMin(7.5,qMax(0.0,44.57-12.01-municipal));
    const double progressive = qMax(0.0,afterAm-641200)*middleRate/100
        + qMax(0.0,afterAm-777900)*0.075 + qMax(0.0,afterAm-2592700)*0.05;
    estimate.ratePercent = taxable > 0 ? qBound(0.0,12.01+municipal+church+progressive/taxable*100,100.0) : 0;
    return estimate;
}

static double effectiveTaxRate(const Repository& repo, const QString& sellerId, const QDate& today) {
    if (!repo.settings.automaticTaxEstimate) return repo.settings.taxRatePercent;
    const auto estimate = historyTaxEstimate(repo,sellerId,today);
    return estimate ? estimate->ratePercent : repo.settings.taxRatePercent;
}
