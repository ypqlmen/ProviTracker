#pragma once

#include <QtCore>
#include <cmath>
#include <optional>

struct HourlyPayForecast {
    double amount = 0.0;
    int elapsedWorkingDays = 0;
    int totalWorkingDays = 0;
    QDate asOf;
};

static int payrollWorkingDays(const QDate& start, const QDate& end) {
    if (!start.isValid() || !end.isValid() || end < start) return 0;
    int days = 0;
    for (QDate date = start; date <= end; date = date.addDays(1)) {
        if (date.dayOfWeek() <= Qt::Friday) ++days;
    }
    return days;
}

// Hours and the elapsed-day denominator must describe the same snapshot.
// Weekdays include the snapshot date; weekends never add to the denominator.
static std::optional<HourlyPayForecast> forecastHourlyPay(
    double earnedHourlyPay, const QDate& start, const QDate& end,
    const QDate& snapshotDate, const QDate& today
) {
    if (!std::isfinite(earnedHourlyPay) || earnedHourlyPay < 0.0
        || !snapshotDate.isValid() || !today.isValid() || snapshotDate > today)
        return std::nullopt;
    HourlyPayForecast forecast;
    forecast.totalWorkingDays = payrollWorkingDays(start, end);
    if (forecast.totalWorkingDays == 0 || today < start || snapshotDate < start)
        return std::nullopt;
    forecast.asOf = qMin(snapshotDate, end);
    forecast.elapsedWorkingDays = payrollWorkingDays(start, forecast.asOf);
    if (forecast.elapsedWorkingDays == 0) return std::nullopt;
    forecast.amount = earnedHourlyPay / forecast.elapsedWorkingDays * forecast.totalWorkingDays;
    return forecast;
}
