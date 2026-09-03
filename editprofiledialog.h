#ifndef EDITPROFILEDIALOG_H
#define EDITPROFILEDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class EditProfileDialog;
}
QT_END_NAMESPACE

class EditProfileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditProfileDialog(QWidget *parent = nullptr);
    ~EditProfileDialog();

    void setNickname(const QString &nickname);
    void setAvatarPath(const QString &path);

    QString nickname() const;
    QString selectedAvatarPath() const;
    bool avatarChanged() const { return m_avatarChanged; }

private slots:
    void onPickAvatar();
    void onSave();

private:
    void applyAvatarPreview(const QString &path);

    Ui::EditProfileDialog *ui;
    QString m_avatarPath;
    bool m_avatarChanged = false;
};

#endif
