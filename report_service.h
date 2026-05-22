#pragma once

#include <QtWidgets>
#include <QtPrintSupport>
#include "commission.h"

// ============================================================
// Reports / snapshots
// ============================================================

class ReportService {
public:

    static QString buildHtmlReport(
        const Repository& repo,
        const Salesperson& s,
        const QString& label,
        const Metrics& m,
        double workedHours = 0.0,
        double hourlyRate = 0.0,
        const QString& hoursPeriod = QString(),
        ReportSalaryBreakdown salary = ReportSalaryBreakdown()
        ) {
        const double rate = CommissionEngine::monthlyRatePerPoint(m.totalPoints, repo.settings.bonus);
        const int activeDays = activeDayCount(m);
        const double avgPointsPerActiveDay = activeDays > 0 ? (m.totalPoints / activeDays) : 0.0;
        const double avgCommissionPerActiveDay = activeDays > 0 ? (m.totalCommission / activeDays) : 0.0;
        const auto bestDay = bestDayByPoints(m);
        if (!salary.usesPaymentMonthRules) {
            salary.baseSalary = workedHours * hourlyRate;
            salary.totalProvision = m.totalCommission;
            salary.totalSalary = salary.baseSalary + salary.totalProvision;
        }
        salary.taxConfigured = repo.settings.taxRatePercent > 0.0;
        salary.taxDeduction = repo.settings.taxDeduction;
        salary.taxRatePercent = repo.settings.taxRatePercent;
        const SalaryTaxEstimate taxEstimate = estimateSalaryTax(
            salary.totalSalary,
            salary.taxDeduction,
            salary.taxRatePercent
            );
        salary.amBidrag = taxEstimate.amBidrag;
        salary.aTax = taxEstimate.aTax;
        salary.taxAmount = taxEstimate.totalTax;
        salary.netSalary = taxEstimate.netSalary;

        QString html;
        QTextStream ts(&html);
        ts << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
           << "<title>Salgsrapport - " << label.toHtmlEscaped() << "</title>"
           << "<style>"
           << "body{font-family:Segoe UI,Arial,sans-serif;margin:18px;color:#E6EEF8;line-height:1.45;background:#0F172A;}"
           << "h1,h2{margin-bottom:8px;color:#F8FBFF;}"
           << "h1{font-size:22px;margin-top:0;}h2{font-size:15px;margin-top:18px;color:#9CC7E8;text-transform:none;}"
           << "table{border-collapse:collapse;width:100%;margin-top:12px;background:#0F1A2E;border-radius:14px;overflow:hidden;}"
           << "th,td{border:1px solid #22304A;padding:10px 12px;text-align:left;vertical-align:top;}"
           << "th{background:#13203A;color:#DDEBFB;}"
           << ".meta{margin-bottom:18px;color:#BFD7EE;}"
           << ".card-table{width:100%;border-collapse:separate;border-spacing:12px;margin:12px -12px;background:transparent;}"
           << ".card-table td{width:33%;border:1px solid #223556;border-radius:18px;padding:14px 16px;background:#111B2E;vertical-align:top;}"
           << ".card-label{color:#9CC7E8;font-size:12px;font-weight:800;margin-bottom:8px;}"
           << ".card-value{font-size:20px;font-weight:900;color:#FFFFFF;}"
           << ".section{border:1px solid #223556;border-radius:18px;padding:16px;background:#111B2E;box-shadow:0 10px 24px rgba(0,0,0,0.18);}"
           << ".section-title{display:block;color:#9CC7E8;font-size:12px;font-weight:800;margin-bottom:6px;}"
           << ".section{margin-top:14px;font-size:13px;font-weight:400;}"
           << ".hint{margin-top:10px;padding:12px 14px;background:#10233B;border:1px solid #24527A;border-radius:14px;color:#D8F5FF;}"
           << "</style></head><body>";

        ts << R"(
<h1>Salgsrapport</h1>
<div class="hint">Her får du det korte overblik: hvor du står nu, og hvad der skal til for næste løft.</div>
)";
        ts << "<div class=\"meta\"><strong>Sælger:</strong> " << s.name.toHtmlEscaped()
           << "<br><strong>Periode:</strong> " << label.toHtmlEscaped()
           << "<br><strong>Eksporteret:</strong> " << QDateTime::currentDateTime().toString("dd-MM-yyyy HH:mm").toHtmlEscaped()
           << "</div>";

        QVector<QPair<QString, QString>> cards;
        auto addCard = [&](const QString& title, const QString& value) {
            cards.append(qMakePair(title, value));
        };
        addCard("Ordrer", QString::number(m.totalOrders));
        addCard("Salg", QString::number(m.salesCount));
        addCard("Tillæg", QString::number(m.addOnCount));
        addCard("SIMO / VOICE", QString("%1 / %2").arg(m.simoCount).arg(m.voiceCount));
        addCard("Point nu", money(m.totalPoints));
        addCard("Løntimer", money(workedHours) + " timer");
        addCard("Timeløn", money(hourlyRate) + " kr/t");
        addCard("Timer", money(salary.baseSalary) + " kr");
        addCard("Provision", money(salary.totalProvision) + " kr");
        addCard("Løn", money(salary.totalSalary) + " kr");
        addCard("Udbetalt", salary.taxConfigured ? money(salary.netSalary) + " kr" : QString("Indstil skat"));
        addCard("Aktive salgsdage", QString::number(activeDays));
        addCard("Snit point pr aktiv dag", money(avgPointsPerActiveDay));
        addCard("Snit provision pr aktiv dag", money(avgCommissionPerActiveDay) + " kr");

        ts << "<table class=\"card-table\">";
        for (int i = 0; i < cards.size(); ++i) {
            if (i % 3 == 0) ts << "<tr>";
            ts << "<td><div class=\"card-label\">" << cards.at(i).first.toHtmlEscaped()
               << "</div><div class=\"card-value\">" << cards.at(i).second.toHtmlEscaped()
               << "</div></td>";
            if (i % 3 == 2) ts << "</tr>";
        }
        const int remainder = cards.size() % 3;
        if (remainder != 0) {
            for (int i = remainder; i < 3; ++i) ts << "<td></td>";
            ts << "</tr>";
        }
        ts << "</table>";

        ts << "<div class=\"hint\">" << nextMonthlyTierHint(m.totalPoints, repo.settings.bonus).toHtmlEscaped() << "</div>";
        ts << "<h2>Løn</h2><div class=\"hint\">Her kombineres timer fra Intramanager med provision efter samme regler som dashboardet.</div><table>";
        ts << "<tr><th>Felt</th><th>Værdi</th></tr>";
        ts << "<tr><td>Lønperiode</td><td>" << hoursPeriod.toHtmlEscaped() << "</td></tr>";
        ts << "<tr><td>Løntimer</td><td>" << money(workedHours) << " timer</td></tr>";
        ts << "<tr><td>Timeløn</td><td>" << money(hourlyRate) << " kr/t</td></tr>";
        ts << "<tr><td>Timer</td><td>" << money(salary.baseSalary) << " kr</td></tr>";
        ts << "<tr><td>Provision</td><td>" << money(salary.totalProvision) << " kr</td></tr>";
        if (salary.usesPaymentMonthRules) {
            ts << "<tr><td>Provision i lønperioden</td><td>" << money(salary.periodProvision) << " kr";
            if (!salary.salaryPeriod.isEmpty()) ts << " (" << salary.salaryPeriod.toHtmlEscaped() << ")";
            ts << "</td></tr>";
            ts << "<tr><td>Bagbetalt provision</td><td>" << money(salary.delayedProvision) << " kr";
            if (!salary.delayedPeriod.isEmpty()) ts << " (" << salary.delayedPeriod.toHtmlEscaped() << ")";
            ts << "</td></tr>";
        }
        ts << "<tr><td><strong>Løn</strong></td><td><strong>" << money(salary.totalSalary) << " kr</strong></td></tr>";
        if (salary.taxConfigured) {
            ts << "<tr><td>Skattefradrag</td><td>" << money(salary.taxDeduction) << " kr</td></tr>";
            ts << "<tr><td>Trækprocent</td><td>" << money(salary.taxRatePercent) << " %</td></tr>";
            ts << "<tr><td>AM-bidrag (8%)</td><td>" << money(salary.amBidrag) << " kr</td></tr>";
            ts << "<tr><td>A-skat</td><td>" << money(salary.aTax) << " kr</td></tr>";
            ts << "<tr><td>Estimeret skat inkl. AM-bidrag</td><td>" << money(salary.taxAmount) << " kr</td></tr>";
            ts << "<tr><td><strong>Udbetalt</strong></td><td><strong>" << money(salary.netSalary) << " kr</strong></td></tr>";
        } else {
            ts << "<tr><td>Udbetalt</td><td>Indstil skattefradrag og trækprocent i Indstillinger</td></tr>";
        }
        ts << "</table>";

        ts << "<h2>Sådan ligger du lige nu</h2><div class=\"hint\">Her kan du se om du er foran, bagud eller lige på kanten af næste løft.</div><table>";
        ts << "<tr><th>Felt</th><th>Værdi</th></tr>";
        if (!bestDay.first.isEmpty()) {
            ts << "<tr><td>Bedste dag</td><td>" << bestDay.first.toHtmlEscaped() << " (" << money(bestDay.second) << " point)</td></tr>";
        }
        ts << "<tr><td>Aktuel månedsrate</td><td>" << money(rate) << " kr/point</td></tr>";
        ts << "<tr><td>Mål for måneden</td><td>" << money(repo.settings.bonus.monthlyTargetPoints) << " point</td></tr>";
        if (repo.settings.monthlySalesTarget > 0) {
            ts << "<tr><td>Mål for måneden (salg)</td><td>" << repo.settings.monthlySalesTarget << " salg</td></tr>";
        }
        ts << "<tr><td>Mangler til / over mål</td><td>";
        if (m.totalPoints < repo.settings.bonus.monthlyTargetPoints) {
            ts << money(repo.settings.bonus.monthlyTargetPoints - m.totalPoints) << " point mangler";
        } else {
            ts << money(m.totalPoints - repo.settings.bonus.monthlyTargetPoints) << " point over mål";
        }
        ts << "</td></tr></table>";

        ts << "<h2>Provision og næste løft</h2><div class=\"hint\">Når du er tæt på provision, bliver de næste pengehop fremhævet tydeligt.</div><table>";
        ts << "<tr><th>Felt</th><th>Værdi</th></tr>";
        ts << "<tr><td>Dagsprovision / pointprovision (21.–20.)</td><td>" << money(m.dayBonus) << " kr</td></tr>";
        ts << "<tr><td>Månedsprovision</td><td>" << money(m.monthlyBonus) << " kr</td></tr>";
        ts << "<tr><td>SIMO provision</td><td>" << money(m.simoBonus) << " kr</td></tr>";
        ts << "<tr><td>VOICE provision</td><td>" << money(m.voiceBonus) << " kr</td></tr>";
        ts << "</table>";

        ts << "<h2>Det du har lukket</h2><table><tr><th>Produkt</th><th>Antal</th><th>Point</th></tr>";
        for (auto it = m.quantityByProduct.begin(); it != m.quantityByProduct.end(); ++it) {
            ts << "<tr><td>" << it.key().toHtmlEscaped() << "</td><td>" << it.value() << "</td><td>"
               << money(m.pointsByProduct.value(it.key())) << "</td></tr>";
        }
        ts << "</table>";

        ts << "<h2>Udvikling dag for dag</h2><table><tr><th>Dag</th><th>Point</th><th>Estimeret provision</th></tr>";
        for (auto it = m.pointsByDay.begin(); it != m.pointsByDay.end(); ++it) {
            ts << "<tr><td>" << it.key().toHtmlEscaped() << "</td><td>" << money(it.value()) << "</td><td>"
               << money(m.commissionByDay.value(it.key())) << " kr</td></tr>";
        }
        ts << "</table>";

        ts << "</body></html>";
        return html;
    }

    static bool exportPdf(
        const QString& path,
        const Repository& repo,
        const Salesperson& s,
        const QString& label,
        const Metrics& m,
        double workedHours = 0.0,
        double hourlyRate = 0.0,
        const QString& hoursPeriod = QString(),
        ReportSalaryBreakdown salary = ReportSalaryBreakdown()
        ) {
        QTextDocument doc;
        doc.setHtml(buildHtmlReport(repo, s, label, m, workedHours, hourlyRate, hoursPeriod, salary));
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        printer.setPageMargins(QMarginsF(14, 14, 14, 14), QPageLayout::Millimeter);
        doc.print(&printer);
        return QFileInfo::exists(path);
    }

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
                const QString pdfPath = repo.reportDir() + "/" + s.id + "_" + key + ".pdf";
                exportPdf(pdfPath, repo, s, key, m);
            }
            repo.settings.lastClosedMonthKey = key;
        }

        repo.saveSettings();
    }
};
