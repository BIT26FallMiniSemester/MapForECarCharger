#ifndef STATIONDETAILDIALOG_H
#define STATIONDETAILDIALOG_H

#include "models.h"

#include <QDialog>
#include <QVector>

QT_BEGIN_NAMESPACE
namespace Ui {
class StationDetailDialog;
}
QT_END_NAMESPACE

class StationDetailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StationDetailDialog(QWidget *parent = nullptr);
    ~StationDetailDialog();

    void setStation(const StationSummary &station);
    StationSummary station() const { return m_station; }

public slots:
    void showPiles(qint64 stationId, const QVector<ChargingPile> &piles);
    void showPilesHint(const QString &text);

signals:
    void navigateClicked();
    void reserveClicked(const ChargingPile &pile);
    void pileClicked(const ChargingPile &pile);

private slots:
    void applyPileFilter();

private:
    void clearPileCards();
    void renderPileCards(const QVector<ChargingPile> &piles);
    QString selectedPileStatus() const;
    QString selectedPileType() const;

    Ui::StationDetailDialog *ui;
    StationSummary m_station;
    QVector<ChargingPile> m_allPiles;
};

#endif
