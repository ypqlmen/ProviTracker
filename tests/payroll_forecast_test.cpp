#include "../payroll_forecast.h"
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* label) {
        if (!condition) { std::cerr << label << '\n'; ++failures; }
    };
    const QDate start(2026, 8, 21), end(2026, 9, 20), today(2026, 9, 11);
    const auto partial = forecastHourlyPay(16000.0, start, end, today, today);
    check(partial.has_value(), "Partial period has a forecast");
    if (partial) {
        check(partial->elapsedWorkingDays == 16, "16 weekdays elapsed across month boundary");
        check(partial->totalWorkingDays == 21, "21 weekdays in the payroll period");
        check(std::abs(partial->amount - 21000.0) < 0.001, "Projects earned pay over entire period");
    }
    const auto stale = forecastHourlyPay(10000.0, start, end, QDate(2026, 9, 3), today);
    check(stale && std::abs(stale->amount - 21000.0) < 0.001,
          "Stale hours use their snapshot date, not today's denominator");
    const auto weekend = forecastHourlyPay(16000.0, start, end, QDate(2026, 9, 13), QDate(2026, 9, 13));
    check(weekend && std::abs(weekend->amount - 21000.0) < 0.001, "Weekend does not lower forecast");
    const auto ended = forecastHourlyPay(21000.0, start, end, QDate(2026, 9, 22), QDate(2026, 9, 22));
    check(ended && ended->amount == 21000.0, "Ended period equals final earned pay");
    check(!forecastHourlyPay(0, QDate(2026, 9, 21), QDate(2026, 10, 20), today, today),
          "Future period has no forecast");
    check(!forecastHourlyPay(0, start, end, QDate(), today), "Missing timestamp has no forecast");
    check(!forecastHourlyPay(0, start, end, today.addDays(1), today), "Future timestamp is rejected");
    check(!forecastHourlyPay(0, QDate(2026, 8, 22), QDate(2026, 9, 20),
                             QDate(2026, 8, 23), QDate(2026, 8, 23)),
          "Period starting on weekend waits for a weekday");
    const auto zero = forecastHourlyPay(0, start, end, today, today);
    check(zero && zero->amount == 0, "Known zero hours differs from missing hours");
    check(!forecastHourlyPay(100, end, start, today, today), "Invalid period rejected");
    check(payrollWorkingDays(QDate(2025, 12, 21), QDate(2026, 1, 20)) == 22,
          "Year boundary weekdays");
    check(payrollWorkingDays(QDate(2024, 2, 21), QDate(2024, 3, 20)) == 21,
          "Leap year weekdays");
    return failures ? 1 : 0;
}
