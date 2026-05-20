#pragma once

#include <QtCore>
#include <algorithm>
#include <cmath>
#include <optional>
#include "repository.h"

// ============================================================
// Commission engine
// ============================================================

class CommissionEngine {
public:
    static double monthlyRatePerPoint(double points, const BonusSettings& b) {
        double best = 0.0;
        for (const auto& tier : b.monthlyRateTiers) {
            if (points >= tier.threshold) best = tier.ratePerPoint;
        }
        return best;
    }

    static double lockedStepBonus(int count, int minEligible, int stepSize, double payoutPerUnit) {
        if (count < minEligible) return 0.0;
        const int safeStep = qMax(1, stepSize);
        int lockedUnits = (count / safeStep) * safeStep;
        return lockedUnits * payoutPerUnit;
    }

    static double lockedFiveBonus(int count, int minEligible, double payoutPerUnit) {
        return lockedStepBonus(count, minEligible, 5, payoutPerUnit);
    }

    static double pointsOnly(
        const Repository& repo,
        const QString& salespersonId,
        const QDateTime& from,
        const QDateTime& to
        ) {
        double points = 0.0;

        QHash<QString, Product> productMap;
        for (const auto& p : repo.products) {
            productMap.insert(p.key, p);
        }

        for (const auto& order : repo.orders) {
            if (order.salespersonId != salespersonId) continue;
            if (order.createdAt < from || order.createdAt > to) continue;

            for (const auto& item : order.items) {
                if (!productMap.contains(item.productKey)) continue;
                const auto& p = productMap[item.productKey];
                points += p.points * item.quantity;
            }
        }

        return points;
    }

    static Metrics calculate(
        const Repository& repo,
        const QString& salespersonId,
        const QDateTime& from,
        const QDateTime& to,
        std::optional<QPair<QDateTime, QDateTime>> dayBonusRange = std::nullopt
        ) {
        Metrics m;
        QHash<QString, Product> productMap;
        for (const auto& p : repo.products) productMap.insert(p.key, p);

        for (const auto& order : repo.orders) {
            if (order.salespersonId != salespersonId) continue;
            if (order.createdAt < from || order.createdAt > to) continue;
            m.totalOrders++;
            const QString day = order.createdAt.date().toString("yyyy-MM-dd");

            for (const auto& item : order.items) {
                if (!productMap.contains(item.productKey)) continue;
                const auto& p = productMap[item.productKey];
                m.totalUnits += item.quantity;
                const double pts = p.points * item.quantity;
                m.totalPoints += pts;
                m.quantityByProduct[p.displayName] += item.quantity;
                m.quantityByCategory[p.category] += item.quantity;
                m.pointsByProduct[p.displayName] += pts;
                m.pointsByDay[day] += pts;

                if (isAddOnCategory(p.category)) m.addOnCount += item.quantity;
                else m.coreProductCount += item.quantity;

                if (p.countsAsSale) m.salesCount += item.quantity;
                if (p.countMode == CountMode::Simo || p.countMode == CountMode::Both) m.simoCount += item.quantity;
                if (p.countMode == CountMode::Voice || p.countMode == CountMode::Both) m.voiceCount += item.quantity;
            }
        }

        const auto& bonus = repo.settings.bonus;
        const double dayBonusPoints = dayBonusRange.has_value()
                                          ? pointsOnly(repo, salespersonId, dayBonusRange->first, dayBonusRange->second)
                                          : m.totalPoints;

        m.dayBonus = dayBonusPoints * bonus.dayBonusPerPoint;
        const double monthlyRate = monthlyRatePerPoint(m.totalPoints, bonus);
        m.monthlyBonus = m.totalPoints * monthlyRate;
        m.simoBonus = lockedStepBonus(m.simoCount, bonus.simoMinEligible, 5, bonus.simoPayoutPerUnit);
        m.voiceBonus = lockedStepBonus(m.voiceCount, bonus.voiceMinEligible, 10, bonus.voicePayoutPerUnit);
        m.totalCommission = m.dayBonus + m.monthlyBonus + m.simoBonus + m.voiceBonus;

        for (auto it = m.pointsByDay.begin(); it != m.pointsByDay.end(); ++it) {
            m.commissionByDay[it.key()] = it.value() * (bonus.dayBonusPerPoint + monthlyRate);
        }

        return m;
    }
};

static QString money(double v) {
    return QLocale(QLocale::Danish, QLocale::Denmark).toString(v, 'f', 2);
}

static QString monthKey(const QDate& d) {
    return QString("%1-%2").arg(d.year()).arg(d.month(), 2, 10, QLatin1Char('0'));
}

static QPair<QDateTime, QDateTime> monthRange(const QDate& date) {
    QDate first(date.year(), date.month(), 1);
    QDate last(date.year(), date.month(), date.daysInMonth());
    return {
        QDateTime(first, QTime(0,0,0)),
        QDateTime(last, QTime(23,59,59))
    };
}

static QPair<QDateTime, QDateTime> payrollBonusRange(const QDate& date) {
    QDate start;
    QDate end;

    if (date.day() <= 20) {
        start = QDate(date.year(), date.month(), 21).addMonths(-1);
        end = QDate(date.year(), date.month(), 20);
    } else {
        start = QDate(date.year(), date.month(), 21);
        end = QDate(date.year(), date.month(), 20).addMonths(1);
    }

    return {
        QDateTime(start, QTime(0, 0, 0)),
        QDateTime(end, QTime(23, 59, 59))
    };
}

static QPair<QDateTime, QDateTime> payrollRangeEndingInMonth(const QDate& date) {
    const QDate end(date.year(), date.month(), 20);
    const QDate start = end.addMonths(-1).addDays(1);

    return {
        QDateTime(start, QTime(0, 0, 0)),
        QDateTime(end, QTime(23, 59, 59))
    };
}

static QDate payoutDateForMonth(const QDate& date) {
    QDate payout(date.year(), date.month(), date.daysInMonth());
    while (payout.dayOfWeek() > Qt::Friday) {
        payout = payout.addDays(-1);
    }
    return payout;
}

static QString payoutDateLabel(const QDate& date) {
    return QLocale(QLocale::Danish, QLocale::Denmark).toString(payoutDateForMonth(date), "dddd dd-MM-yyyy");
}

static QPair<QDateTime, QDateTime> workWeekRange(const QDate& date) {
    const int dayOfWeek = date.dayOfWeek();
    const QDate monday = date.addDays(1 - dayOfWeek);
    const QDate sunday = monday.addDays(6);
    return {
        QDateTime(monday, QTime(0,0,0)),
        QDateTime(sunday, QTime(23,59,59))
    };
}

static QPair<QDateTime, QDateTime> previousAndCurrentWorkWeeksRange(const QDate& date) {
    const auto currentWeek = workWeekRange(date);
    const QDate currentMonday = currentWeek.first.date();
    const QDate previousMonday = currentMonday.addDays(-7);
    const QDate currentSunday = currentWeek.second.date();

    return {
        QDateTime(previousMonday, QTime(0,0,0)),
        QDateTime(currentSunday, QTime(23,59,59))
    };
}

static int nextLockedStep(int count, int minimumToActivate, int stepSize) {
    const int safeStep = qMax(1, stepSize);
    if (count < minimumToActivate) return minimumToActivate;
    return ((count / safeStep) + 1) * safeStep;
}

static int nextLockedFiveStep(int count, int minimumToActivate) {
    return nextLockedStep(count, minimumToActivate, 5);
}

static double potentialLockedStepBonus(int count, int simulatedMinimum, int stepSize, double payoutPerUnit) {
    if (count < simulatedMinimum) return 0.0;
    const int safeStep = qMax(1, stepSize);
    const int lockedUnits = (count / safeStep) * safeStep;
    return lockedUnits * payoutPerUnit;
}

static int activeDayCount(const Metrics& m) {
    return m.pointsByDay.size();
}

static QPair<QString, double> bestDayByPoints(const Metrics& m) {
    QString bestDate;
    double bestPoints = 0.0;
    for (auto it = m.pointsByDay.begin(); it != m.pointsByDay.end(); ++it) {
        if (bestDate.isEmpty() || it.value() > bestPoints) {
            bestDate = it.key();
            bestPoints = it.value();
        }
    }
    return {bestDate, bestPoints};
}


static int workingDaysInMonth(const QDate& date) {
    int count = 0;
    for (QDate d(date.year(), date.month(), 1); d.month() == date.month(); d = d.addDays(1)) {
        if (d.dayOfWeek() <= 5) ++count;
    }
    return count;
}

static int workingDaysElapsedInMonth(const QDate& date) {
    int count = 0;
    for (QDate d(date.year(), date.month(), 1); d <= date; d = d.addDays(1)) {
        if (d.dayOfWeek() <= 5) ++count;
    }
    return count;
}

static int remainingWorkingDaysInMonth(const QDate& date) {
    return qMax(0, workingDaysInMonth(date) - workingDaysElapsedInMonth(date));
}

static double projectedMonthPoints(double currentPoints, int elapsedWorkingDays, int totalWorkingDays) {
    if (elapsedWorkingDays <= 0 || totalWorkingDays <= 0) return 0.0;
    return (currentPoints / elapsedWorkingDays) * totalWorkingDays;
}

static double estimatedSalaryTax(double grossSalary, double taxDeduction, double taxRatePercent) {
    if (grossSalary <= 0.0 || taxRatePercent <= 0.0) return 0.0;
    const double taxableSalary = qMax(0.0, grossSalary - qMax(0.0, taxDeduction));
    return taxableSalary * (qBound(0.0, taxRatePercent, 100.0) / 100.0);
}

static double estimatedNetSalary(double grossSalary, double taxDeduction, double taxRatePercent) {
    return qMax(0.0, grossSalary - estimatedSalaryTax(grossSalary, taxDeduction, taxRatePercent));
}

static QString nextMonthlyTierHint(double points, const BonusSettings& b) {
    for (const auto& tier : b.monthlyRateTiers) {
        if (points < tier.threshold) {
            return QString("N?ste point-tier: %1 point (%2 kr/point), mangler %3 point")
                .arg(tier.threshold)
                .arg(money(tier.ratePerPoint))
                .arg(money(tier.threshold - points));
        }
    }
    return "Du er allerede p? h?jeste point-tier";
}

struct ReportSalaryBreakdown {
    bool usesPaymentMonthRules = false;
    double baseSalary = 0.0;
    double periodProvision = 0.0;
    double delayedProvision = 0.0;
    double totalProvision = 0.0;
    double totalSalary = 0.0;
    bool taxConfigured = false;
    double taxDeduction = 0.0;
    double taxRatePercent = 0.0;
    double taxAmount = 0.0;
    double netSalary = 0.0;
    QString salaryPeriod;
    QString delayedPeriod;
};
