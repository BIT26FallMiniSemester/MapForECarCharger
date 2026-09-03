#include "rechargedialog.h"
#include "ui_rechargedialog.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QtGlobal>

RechargeDialog::RechargeDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RechargeDialog)
{
    ui->setupUi(this);
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->hintLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->confirmButton->setCursor(Qt::PointingHandCursor);
    ui->minusButton->setObjectName(QStringLiteral("amountStepButton"));
    ui->plusButton->setObjectName(QStringLiteral("amountStepButton"));
    ui->minusButton->setCursor(Qt::PointingHandCursor);
    ui->plusButton->setCursor(Qt::PointingHandCursor);
    ui->minusButton->style()->unpolish(ui->minusButton);
    ui->minusButton->style()->polish(ui->minusButton);
    ui->plusButton->style()->unpolish(ui->plusButton);
    ui->plusButton->style()->polish(ui->plusButton);

    ui->amountSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ui->amountSpin->setSingleStep(1.0);
    ui->amountSpin->setKeyboardTracking(true);

    const QList<QPair<QPushButton *, int>> chips = {
        {ui->amount5, 5},
        {ui->amount10, 10},
        {ui->amount20, 20},
        {ui->amount50, 50},
        {ui->amount100, 100},
    };
    m_presets = new QButtonGroup(this);
    m_presets->setExclusive(true);
    for (const auto &chip : chips) {
        chip.first->setObjectName(QStringLiteral("amountChip"));
        chip.first->setCheckable(true);
        chip.first->setCursor(Qt::PointingHandCursor);
        chip.first->style()->unpolish(chip.first);
        chip.first->style()->polish(chip.first);
        m_presets->addButton(chip.first, chip.second);
    }

    connect(m_presets, &QButtonGroup::idClicked, this, [this](int yuan) {
        ui->amountSpin->setValue(yuan);
    });
    connect(ui->amountSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &RechargeDialog::syncPresetButtons);
    connect(ui->minusButton, &QToolButton::clicked, this, [this]() {
        ui->amountSpin->stepBy(-1);
    });
    connect(ui->plusButton, &QToolButton::clicked, this, [this]() {
        ui->amountSpin->stepBy(1);
    });
    connect(ui->confirmButton, &QPushButton::clicked, this, &RechargeDialog::accept);

    ui->amountSpin->setValue(10.0);
    syncPresetButtons(10.0);
}

RechargeDialog::~RechargeDialog()
{
    delete ui;
}

double RechargeDialog::amountYuan() const
{
    return ui->amountSpin->value();
}

void RechargeDialog::syncPresetButtons(double yuan)
{
    const int whole = qRound(yuan);
    QAbstractButton *match = (qAbs(yuan - whole) < 0.005) ? m_presets->button(whole) : nullptr;
    if (match) {
        match->setChecked(true);
        return;
    }
    m_presets->setExclusive(false);
    if (QAbstractButton *checked = m_presets->checkedButton())
        checked->setChecked(false);
    m_presets->setExclusive(true);
}
