#pragma once

#include "models.h"

#include <QDialog>

class QComboBox;
class QUrl;
class QVBoxLayout;

class NavigationDialog : public QDialog
{
    Q_OBJECT
public:
    NavigationDialog(double fromLatitude, double fromLongitude,
                     const StationSummary &station, QWidget *parent = nullptr);

private:
    QUrl routeUrl() const;
    void loadRoute();

    double m_fromLatitude;
    double m_fromLongitude;
    StationSummary m_station;
    QComboBox *m_mode = nullptr;
    QVBoxLayout *m_viewLayout = nullptr;
};
