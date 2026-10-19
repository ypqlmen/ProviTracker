#pragma once
#include "premium_ui.h"
#include "commission.h"

// Uses the same native cards and typography as Overblik; no document preview.
class PremiumReportView : public QWidget {
    QWidget *content;
    PremiumCard *statusCard;
    QLabel *statusTitle,*statusMessage,*period,*tierHint,*bestDay;
    PremiumSalaryCard* salaryCard;
    QVector<QLabel*> statistics,payrollValues,bonusValues;
    QTableWidget *products,*days;
    QLabel *productsEmpty,*daysEmpty;

    static void detailRows(QVBoxLayout* layout,const QStringList& names,QVector<QLabel*>& values) {
        for(int i=0;i<names.size();++i){
            if(i)layout->addWidget(premiumDivider());
            auto* row=new QHBoxLayout;row->setSpacing(20);
            auto* label=premiumLabel(names[i],"muted");label->setWordWrap(true);row->addWidget(label,1);
            auto* value=premiumLabel("—","detailValue");value->setWordWrap(true);value->setAlignment(Qt::AlignRight|Qt::AlignVCenter);value->setMaximumWidth(245);row->addWidget(value,1);values<<value;layout->addLayout(row);
        }
    }
    static QTableWidget* table(const QStringList& columns) {
        auto* widget=new QTableWidget(0,columns.size());widget->setHorizontalHeaderLabels(columns);
        widget->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
        for(int i=1;i<columns.size();++i)widget->horizontalHeader()->setSectionResizeMode(i,QHeaderView::ResizeToContents);
        widget->verticalHeader()->hide();widget->verticalHeader()->setDefaultSectionSize(43);
        widget->setEditTriggers(QAbstractItemView::NoEditTriggers);widget->setSelectionMode(QAbstractItemView::NoSelection);
        widget->setFocusPolicy(Qt::NoFocus);widget->setShowGrid(false);widget->setAlternatingRowColors(true);
        widget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);widget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        return widget;
    }
    static void setCell(QTableWidget* table,int row,int column,const QString& text) {
        auto* cell=new QTableWidgetItem(text);if(column)cell->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);table->setItem(row,column,cell);
    }
    static void finishTable(QTableWidget* table,QLabel* empty) {
        const bool hasRows=table->rowCount()>0;table->setVisible(hasRows);empty->setVisible(!hasRows);
        table->resizeRowsToContents();int height=table->horizontalHeader()->sizeHint().height()+4;
        for(int row=0;row<table->rowCount();++row){table->setRowHeight(row,qMax(43,table->rowHeight(row)));height+=table->rowHeight(row);}
        table->setFixedHeight(height);
    }
public:
    explicit PremiumReportView(QWidget* parent=nullptr):QWidget(parent) {
        auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,8,8);root->setSpacing(18);
        statusCard=new PremiumCard;auto* status=new QVBoxLayout(statusCard);status->setContentsMargins(26,24,26,24);status->setSpacing(10);
        statusTitle=premiumLabel("Henter rapport","sectionTitle");statusMessage=premiumLabel("","muted");statusMessage->setWordWrap(true);status->addWidget(statusTitle);status->addWidget(statusMessage);root->addWidget(statusCard);statusCard->hide();
        content=new QWidget;auto* body=new QVBoxLayout(content);body->setContentsMargins(0,0,0,0);body->setSpacing(18);
        period=premiumLabel("","muted");period->setWordWrap(true);body->addWidget(period);
        auto* hero=new QHBoxLayout;hero->setSpacing(18);salaryCard=new PremiumSalaryCard("Løn i perioden",true);hero->addWidget(salaryCard,1);
        auto* performance=new PremiumCard;auto* performanceBody=new QVBoxLayout(performance);performanceBody->setContentsMargins(26,24,26,22);performanceBody->setSpacing(16);
        performanceBody->addWidget(premiumLabel("Salg i perioden","cardTitle"));
        auto* grid=new QGridLayout;grid->setHorizontalSpacing(24);grid->setVerticalSpacing(22);
        const QStringList stats={"Point","Salg","Løntimer","Point pr. salgsdag"};
        for(int i=0;i<stats.size();++i){auto* cell=new QVBoxLayout;cell->setSpacing(8);cell->addWidget(premiumLabel(stats[i],"caption"));auto* value=premiumLabel("—","statValue");statistics<<value;cell->addWidget(value);grid->addLayout(cell,i/2,i%2);}
        performanceBody->addLayout(grid);performanceBody->addStretch();performanceBody->addWidget(premiumDivider());
        tierHint=premiumLabel("","forecast");tierHint->setWordWrap(true);performanceBody->addWidget(tierHint);
        bestDay=premiumLabel("","forecast");bestDay->setWordWrap(true);performanceBody->addWidget(bestDay);hero->addWidget(performance,1);body->addLayout(hero);
        auto* detail=new QHBoxLayout;detail->setSpacing(18);
        auto* payroll=new PremiumCard;auto* payrollBody=new QVBoxLayout(payroll);payrollBody->setContentsMargins(24,22,24,22);payrollBody->setSpacing(12);payrollBody->addWidget(premiumLabel("Løn & skat","sectionTitle"));payrollBody->addSpacing(4);
        detailRows(payrollBody,{"Lønperiode","Timeløn","Provision i lønperioden","Bagbetalt provision","Månedsfradrag","Trækprocent","AM-bidrag · 8 %","A-skat"},payrollValues);payrollBody->addStretch();detail->addWidget(payroll,1);
        auto* bonus=new PremiumCard;auto* bonusBody=new QVBoxLayout(bonus);bonusBody->setContentsMargins(24,22,24,22);bonusBody->setSpacing(12);bonusBody->addWidget(premiumLabel("Provision & mål","sectionTitle"));bonusBody->addSpacing(4);
        detailRows(bonusBody,{"Dags- / pointprovision","Månedsprovision","SIMO-bonus","VOICE-bonus","Rate pr. point","Pointmål","Afstand til pointmål","Salgsmål"},bonusValues);bonusBody->addStretch();detail->addWidget(bonus,1);body->addLayout(detail);
        auto* tables=new QHBoxLayout;tables->setSpacing(18);
        auto* productCard=new PremiumCard;auto* productBody=new QVBoxLayout(productCard);productBody->setContentsMargins(24,22,24,22);productBody->setSpacing(16);productBody->addWidget(premiumLabel("Solgte produkter","sectionTitle"));products=table({"Produkt","Antal","Point"});productsEmpty=premiumLabel("Ingen solgte produkter i perioden.","muted");productBody->addWidget(products);productBody->addWidget(productsEmpty);productBody->addStretch();tables->addWidget(productCard,1);
        auto* dayCard=new PremiumCard;auto* dayBody=new QVBoxLayout(dayCard);dayBody->setContentsMargins(24,22,24,22);dayBody->setSpacing(16);dayBody->addWidget(premiumLabel("Dag for dag","sectionTitle"));days=table({"Dato","Point","Provision"});daysEmpty=premiumLabel("Ingen salgsaktivitet i perioden.","muted");dayBody->addWidget(days);dayBody->addWidget(daysEmpty);dayBody->addStretch();tables->addWidget(dayCard,1);body->addLayout(tables);
        root->addWidget(content);root->addStretch();
    }
    void showStatus(const QString& title,const QString& message,const QString& detail=QString()) {
        statusTitle->setText(title);statusMessage->setText(message+(detail.isEmpty()?QString():"\n"+detail));statusCard->show();content->hide();
    }
    void present(const Repository& repo,const Salesperson& salesperson,const QString& label,const Metrics& metrics,double hours,const QString& hoursPeriod,ReportSalaryBreakdown salary,const QString& payout) {
        statusCard->hide();content->show();
        if(!salary.usesPaymentMonthRules){salary.baseSalary=hours*repo.settings.hourlyRate;salary.totalProvision=metrics.totalCommission;salary.totalSalary=salary.baseSalary+salary.sickPay+salary.totalProvision+salary.cancellations;}
        const bool taxConfigured=repo.settings.taxRatePercent>0;
        const auto tax=estimateSalaryTax(salary.totalSalary,repo.settings.taxDeduction,repo.settings.taxRatePercent);
        const auto cash=[](double value){return money(value)+" kr";};
        period->setText(salesperson.name+" · "+label+" · Timer: "+hoursPeriod);
        salaryCard->present(cash(salary.totalSalary),taxConfigured?"Efter skat · "+cash(tax.netSalary)+" (estimat)":"Angiv fradrag og trækprocent i Indstillinger → Løn",payout,
            {cash(salary.baseSalary),cash(salary.sickPay),cash(salary.totalProvision),salary.cancellationsKnown?cash(salary.cancellations):QString("Afventer Intramanager")},QString());
        const int activeDays=activeDayCount(metrics);const auto best=bestDayByPoints(metrics);
        const QStringList statValues={money(metrics.totalPoints),QString::number(metrics.salesCount),money(hours),money(activeDays?metrics.totalPoints/activeDays:0)};
        for(int i=0;i<statistics.size();++i)statistics[i]->setText(statValues[i]);
        tierHint->setText(nextMonthlyTierHint(metrics.totalPoints,repo.settings.bonus));
        bestDay->setText(best.first.isEmpty()?"Ingen salgsaktivitet i perioden":"Bedste salgsdag · "+QDate::fromString(best.first,"yyyy-MM-dd").toString("dd-MM-yyyy")+" · "+money(best.second)+" point");
        const QStringList payroll={hoursPeriod,money(repo.settings.hourlyRate)+" kr/t",salary.usesPaymentMonthRules?cash(salary.periodProvision):cash(salary.totalProvision),salary.usesPaymentMonthRules?cash(salary.delayedProvision):QString("—"),cash(repo.settings.taxDeduction),taxConfigured?money(repo.settings.taxRatePercent)+" %":QString("Ikke angivet"),cash(tax.amBidrag),taxConfigured?cash(tax.aTax):QString("—")};
        for(int i=0;i<payrollValues.size();++i)payrollValues[i]->setText(payroll[i]);
        const double target=repo.settings.bonus.monthlyTargetPoints,delta=metrics.totalPoints-target;
        const QStringList bonuses={cash(metrics.dayBonus),cash(metrics.monthlyBonus),cash(metrics.simoBonus),cash(metrics.voiceBonus),money(CommissionEngine::monthlyRatePerPoint(metrics.totalPoints,repo.settings.bonus))+" kr/point",target>0?money(target)+" point":QString("Ikke angivet"),target>0?money(std::abs(delta))+(delta<0?" point tilbage":" point over mål"):QString("—"),repo.settings.monthlySalesTarget>0?QString::number(metrics.salesCount)+" / "+QString::number(repo.settings.monthlySalesTarget)+" salg":QString("Ikke angivet")};
        for(int i=0;i<bonusValues.size();++i)bonusValues[i]->setText(bonuses[i]);
        products->setRowCount(metrics.quantityByProduct.size());int row=0;
        for(auto it=metrics.quantityByProduct.cbegin();it!=metrics.quantityByProduct.cend();++it,++row){setCell(products,row,0,it.key());setCell(products,row,1,QString::number(it.value()));setCell(products,row,2,money(metrics.pointsByProduct.value(it.key())));}
        days->setRowCount(metrics.pointsByDay.size());row=0;
        for(auto it=metrics.pointsByDay.cbegin();it!=metrics.pointsByDay.cend();++it,++row){setCell(days,row,0,QDate::fromString(it.key(),"yyyy-MM-dd").toString("dd-MM-yyyy"));setCell(days,row,1,money(it.value()));setCell(days,row,2,cash(metrics.commissionByDay.value(it.key())));}
        finishTable(products,productsEmpty);finishTable(days,daysEmpty);
    }
};
