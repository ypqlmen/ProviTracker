#pragma once
#include "commission.h"

class ReportService {
public:
    static void autoClosePreviousMonths(Repository& repo) {
        if (repo.salespeople.isEmpty()) return;
        const QDate previousMonthDate = QDate::currentDate().addMonths(-1);
        const QString previousKey = monthKey(previousMonthDate);

        QStringList keysToClose;
        if (repo.settings.lastClosedMonthKey.isEmpty()) {
            keysToClose << previousKey;
        } else {
            QDate d = QDate::fromString(repo.settings.lastClosedMonthKey + "-01", "yyyy-MM-dd").addMonths(1);
            while (monthKey(d) <= previousKey) {
                keysToClose << monthKey(d);
                d = d.addMonths(1);
            }
        }

        for (const QString& key : keysToClose) {
            const QDate d = QDate::fromString(key + "-01", "yyyy-MM-dd");
            const auto range = monthRange(d);
            const auto dayBonusPeriod = payrollBonusRange(d);

            for (const auto& s : repo.salespeople) {
                const auto m = CommissionEngine::calculate(
                    repo,
                    s.id,
                    range.first,
                    range.second,
                    dayBonusPeriod
                    );
                const QString jsonPath = repo.snapshotDir() + "/" + s.id + "_" + key + ".json";
                QFile jf(jsonPath);
                if (jf.open(QIODevice::WriteOnly)) {
                    QJsonObject snap{{"salespersonId", s.id}, {"salespersonName", s.name}, {"monthKey", key}, {"points", m.totalPoints}, {"totalCommission", m.totalCommission}};
                    jf.write(QJsonDocument(snap).toJson(QJsonDocument::Indented));
                }
            }
            repo.settings.lastClosedMonthKey = key;
        }

        repo.saveSettings();
    }
};
