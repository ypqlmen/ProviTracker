#include "../payroll_forecast.h"
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* label) {
        if (!condition) { std::cerr << label << '\n'; ++failures; }
    };
    const auto exact = [&](std::optional<double> amount, double wanted, const char* label) {
        check(amount && std::abs(*amount - wanted) < 0.001, label);
    };
    const QDate monday(2026, 11, 23), friday(2026, 11, 27);
    const QDate tuesday = monday.addDays(1), saturday = friday.addDays(1);
    const QJsonObject partialDay{{"2026-11-24", QJsonObject{{"hours", 1.0}}}};
    const QJsonObject absence{{"2026-11-24", QJsonObject{{"absenceHours", 7.5}}},
                              {"2026-11-26", QJsonObject{{"absenceHours", 3.0}}},
                              {"2026-11-27", QJsonObject{{"dayOff", true}}}};

    exact(forecastHourlyPay(100, monday, friday, monday.addDays(-1)), 3425,
          "A normal week before first sync is 34.25 hours at the user's rate");
    exact(forecastHourlyPay(200, monday, friday, monday, 0, monday, QJsonObject{}), 6850,
          "The morning sync includes the unfinished first day");
    exact(forecastHourlyPay(100, monday, friday, tuesday, 1, tuesday, partialDay), 2675,
          "A missed completed weekday counts zero and partially recorded today is counted once");
    exact(forecastHourlyPay(100, monday, friday, tuesday, 7.5, tuesday, absence), 1900,
          "Current sickness, partial absence and a planned day off lower projected regular hours");
    exact(forecastHourlyPay(100, monday, friday, friday, 1, tuesday, partialDay), 2675,
          "No sync since Tuesday means later days remain scheduled");
    exact(forecastHourlyPay(100, monday, friday, saturday, 28.5, saturday, QJsonObject{}), 2850,
          "Finished and synced period uses paid hours exactly");
    exact(forecastHourlyPay(100, monday, friday, saturday, 0, saturday, QJsonObject{}), 0,
          "Finished period with confirmed zero hours counts no pay");
    exact(forecastHourlyPay(100, monday, friday, tuesday, 11, tuesday), 3025,
          "Legacy aggregate does not add a second copy of Tuesday");
    const QDate from(2026, 10, 21), to(2026, 11, 20);
    exact(forecastHourlyPay(100, from, to, to, 0, from, QJsonObject{}), 15625,
          "The 21st through the 20th includes both boundary weekdays");
    exact(forecastHourlyPay(100, friday, saturday, friday), 475, "Friday is 4.75 hours");
    exact(forecastHourlyPay(100, monday.addDays(2), monday.addDays(2), monday), 700, "Wednesday is seven hours");
    check(!forecastHourlyPay(100, friday, monday, friday), "Invalid period rejected");
    exact(forecastHourlyPay(100, QDate(2025, 12, 21), QDate(2026, 1, 20), monday),
          15200, "Year boundary weekdays");
    exact(forecastHourlyPay(100, QDate(2024, 2, 21), QDate(2024, 3, 20), monday),
          14400, "Leap year weekdays");
    return failures ? 1 : 0;
}
