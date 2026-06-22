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

    return 0;
}
