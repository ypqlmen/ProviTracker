#pragma once

#include <QtCore>
#include <cmath>
#include <optional>

// Monday through Sunday, in hours. A regular week totals 34.25 hours.
static constexpr double payrollWeekSchedule[] = {7.5, 7.5, 7.0, 7.5, 4.75, 0.0, 0.0};

static std::optional<double> forecastHourlyPay(
    double hourlyRate, const QDate& start, const QDate& end,
    const QDate& today, double paidHours = 0.0,
    const QDate& snapshotDate = {}, const QJsonValue& dailyHours = {}
) {
    if (!start.isValid() || !end.isValid() || end < start || !today.isValid()
        || !std::isfinite(hourlyRate) || hourlyRate < 0.0) return std::nullopt;

    const bool hasSnapshot = snapshotDate.isValid() && snapshotDate <= today
        && std::isfinite(paidHours) && paidHours >= 0.0;
    // Completed days are already accounted for in the imported total, even on days
    // with no hours. A same-day import may only contain part of today's shift.
    const QJsonObject breakdown = hasSnapshot ? dailyHours.toObject() : QJsonObject();
    const QDate firstProjectedDay = hasSnapshot
        ? qMax(start, dailyHours.isObject() ? snapshotDate : snapshotDate.addDays(1)) : start;
    double expectedHours = hasSnapshot ? paidHours : 0.0;
    for (QDate day = firstProjectedDay; day <= end; day = day.addDays(1)) {
        const double scheduledHours = payrollWeekSchedule[day.dayOfWeek() - 1];
        if (scheduledHours == 0) continue;
        const auto entry = breakdown.value(day.toString(Qt::ISODate)).toObject();
        if (entry.value("dayOff").toBool()) continue;
        const double recordedHours = qMax(0.0, entry.value("hours").toDouble());
        const double absenceHours = qMax(0.0, entry.value("absenceHours").toDouble());
        expectedHours += qMax(0.0, scheduledHours - recordedHours - absenceHours);
    }
    return expectedHours * hourlyRate;
}
