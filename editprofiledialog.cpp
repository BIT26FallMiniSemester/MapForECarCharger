#include "editprofiledialog.h"
#include "ui_editprofiledialog.h"

#include <QFileDialog>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>

EditProfileDialog::EditProfileDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EditProfileDialog)
{
    ui->setupUi(this);
    ui->titleLabel->setObjectName(QStringLiteral("titleLabel"));
    ui->hintLabel->setObjectName(QStringLiteral("subtitleLabel"));
    ui->nicknameHint->setObjectName(QStringLiteral("subtitleLabel"));
    ui->statusLabel->setObjectName(QStringLiteral("statusLabel"));
    ui->avatarLabel->setObjectName(QStringLiteral("avatar"));
    ui->pickAvatarButton->setObjectName(QStringLiteral("secondaryButton"));
    ui->pickAvatarButton->setCursor(Qt::PointingHandCursor);
    ui->saveButton->setCursor(Qt::PointingHandCursor);
    applyAvatarPreview(QString());

    connect(ui->pickAvatarButton, &QPushButton::clicked, this, &EditProfileDialog::onPickAvatar);
    connect(ui->saveButton, &QPushButton::clicked, this, &EditProfileDialog::onSave);
    connect(ui->nicknameEdit, &QLineEdit::returnPressed, this, &EditProfileDialog::onSave);
}

EditProfileDialog::~EditProfileDialog()
{
    delete ui;
}

void EditProfileDialog::setNickname(const QString &nickname)
{
    ui->nicknameEdit->setText(nickname);
}

void EditProfileDialog::setAvatarPath(const QString &path)
{
    m_avatarPath = path;
    m_avatarChanged = false;
    applyAvatarPreview(path);
}

QString EditProfileDialog::nickname() const
{
    return ui->nicknameEdit->text().trimmed();
}

QString EditProfileDialog::selectedAvatarPath() const
{
    return m_avatarPath;
}

void EditProfileDialog::onPickAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择头像"),
        QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp)"));
    if (path.isEmpty())
        return;
    QPixmap pix(path);
    if (pix.isNull()) {
        ui->statusLabel->setText(QStringLiteral("无法打开该图片，请换一张再试"));
        return;
    }
    m_avatarPath = path;
    m_avatarChanged = true;
    applyAvatarPreview(path);
    ui->statusLabel->setText(QString());
}

void EditProfileDialog::onSave()
{
    const QString nick = nickname();
    if (nick.isEmpty() || nick.size() > 32) {
        ui->statusLabel->setText(QStringLiteral("昵称长度为 1～32 个字符"));
        return;
    }
    accept();
}

void EditProfileDialog::applyAvatarPreview(const QString &path)
{
    if (path.isEmpty()) {
        ui->avatarLabel->clear();
        ui->avatarLabel->setText(QStringLiteral("默认"));
        ui->avatarLabel->setStyleSheet(QStringLiteral("background:#64748b;border-radius:36px;color:white;"));
        return;
    }
    QPixmap pix(path);
    if (pix.isNull()) {
        applyAvatarPreview(QString());
        return;
    }
    ui->avatarLabel->setText(QString());
    ui->avatarLabel->setPixmap(pix.scaled(72, 72, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    ui->avatarLabel->setStyleSheet(QStringLiteral("border-radius:36px;"));
}
