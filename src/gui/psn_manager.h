#pragma once

#include <QObject>
#include <QDialog>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QVBoxLayout;
class QHBoxLayout;
class QFormLayout;
class QGroupBox;
class QCheckBox;
class QProgressBar;

struct PSNAccount {
    QString username;
    QString email;
    QString region;
    QString avatarUrl;
    bool rememberLogin;
    QDateTime lastLogin;
    QString accountId;
    
    PSNAccount() : rememberLogin(false) {}
};

class PSNLoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PSNLoginDialog(QWidget *parent = nullptr);
    
    QString getUsername() const;
    QString getPassword() const;
    QString getRegion() const;
    bool shouldRememberLogin() const;

private slots:
    void onLoginClicked();
    void onCancelClicked();
    void validateInput();

private:
    void setupUI();
    
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QComboBox *m_regionCombo;
    QCheckBox *m_rememberCheck;
    QPushButton *m_loginButton;
    QPushButton *m_cancelButton;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
};

class PSNAccountManager : public QDialog
{
    Q_OBJECT

public:
    explicit PSNAccountManager(QWidget *parent = nullptr);
    
    void refreshAccountList();

private slots:
    void onAddAccount();
    void onRemoveAccount();
    void onSetDefault();
    void onAccountSelected();
    void onEditAccount();

private:
    void setupUI();
    void loadAccounts();
    void saveAccounts();
    PSNAccount getSelectedAccount();
    
    QListWidget *m_accountList;
    QPushButton *m_addButton;
    QPushButton *m_removeButton;
    QPushButton *m_setDefaultButton;
    QPushButton *m_editButton;
    QLabel *m_accountDetails;
    
    QList<PSNAccount> m_accounts;
    QString m_defaultAccountId;
};

class PSNManager : public QObject
{
    Q_OBJECT

public:
    explicit PSNManager(QObject *parent = nullptr);
    ~PSNManager();
    
    bool showLoginDialog();
    void showAccountManager();
    void logout();
    
    bool isLoggedIn() const;
    QString getCurrentUsername() const;
    QString getCurrentRegion() const;
    PSNAccount getCurrentAccount() const;
    
    // PSN API methods (stubs for now)
    void syncTrophies();
    void uploadSaveData();
    void downloadSaveData();
    void updateFriendsList();

signals:
    void loginStatusChanged(bool loggedIn);
    void accountChanged(const PSNAccount &account);
    void trophySyncCompleted();
    void saveDataSyncCompleted();

private slots:
    void onLoginReply();
    void onSyncReply();
    void onConnectionTimeout();

private:
    void setupNetworking();
    void loadStoredAccounts();
    void saveStoredAccounts();
    void performLogin(const QString &username, const QString &password, const QString &region);
    void performLogout();
    QString getAccountsFilePath();
    
    PSNLoginDialog *m_loginDialog;
    PSNAccountManager *m_accountManager;
    QNetworkAccessManager *m_networkManager;
    QTimer *m_connectionTimer;
    
    PSNAccount m_currentAccount;
    QList<PSNAccount> m_storedAccounts;
    bool m_isLoggedIn;
    QString m_sessionToken;
};
