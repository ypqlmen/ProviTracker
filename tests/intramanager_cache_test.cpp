#include "../domain.h"

int main() {
    const QDateTime periodEnd(
        QDate(2026, 6, 20),
        QTime(23, 59, 59),
        QTimeZone::fromSecondsAheadOfUtc(0)
        );

    IntramanagerHoursEntry partialEntry;
    partialEntry.syncedAt = "2026-06-14T12:00:00Z";
    if (intramanagerHoursEntryCompletesRange(partialEntry, periodEnd)) return 1;

    IntramanagerHoursEntry completedEntry;
    completedEntry.syncedAt = "2026-06-21T00:01:00Z";
    if (!intramanagerHoursEntryCompletesRange(completedEntry, periodEnd)) return 2;

    IntramanagerHoursEntry invalidEntry;
    invalidEntry.syncedAt = "invalid";
    if (intramanagerHoursEntryCompletesRange(invalidEntry, periodEnd)) return 3;

    auto older = fromIntramanagerHoursJson(QJsonObject{{"hours", 6.0}});
    if (!older.dailyHours.isUndefined() || toJson(older).contains("dailyHours")) return 4;
    completedEntry.dailyHours = QJsonObject{{"2026-06-19", QJsonObject{{"hours", 4.75}}}};
    if (fromIntramanagerHoursJson(toJson(completedEntry)).dailyHours != completedEntry.dailyHours) return 5;

    return 0;
}
