#include "psn_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QListWidget>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>

PSNLoginDialog::PSNLoginDialog(QWidget *parent)
    : QDialog(parent)
    , m_usernameEdit(nullptr)
    , m_passwordEdit(nullptr)
    , m_regionCombo(nullptr)
    , m_rememberCheck(nullptr)
    , m_loginButton(nullptr)
    , m_cancelButton(nullptr)
    , m_statusLabel(nullptr)
    , m_progressBar(nullptr)
{
    setWindowTitle("PSN Login");
    setModal(true);
    setFixedSize(400, 300);
    setupUI();
}

void PSNLoginDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // PSN Logo/Header
    QLabel *headerLabel = new QLabel("PlayStation Network Login");
    headerLabel->setAlignment(Qt::AlignCenter);
    headerLabel->setStyleSheet("font-size: 16px; font-weight: bold; margin: 10px;");
    mainLayout->addWidget(headerLabel);
    
    // Login form
    QGroupBox *loginGroup = new QGroupBox("Account Information");
    QFormLayout *formLayout = new QFormLayout(loginGroup);
    
    m_usernameEdit = new QLineEdit;
    m_usernameEdit->setPlaceholderText("Enter your PSN username or email");
    formLayout->addRow("Username/Email:", m_usernameEdit);
    
    m_passwordEdit = new QLineEdit;
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("Enter your password");
    formLayout->addRow("Password:", m_passwordEdit);
    
    m_regionCombo = new QComboBox;
    m_regionCombo->addItems({"US (United States)", "EU (Europe)", "JP (Japan)", "Asia"});
    formLayout->addRow("Region:", m_regionCombo);
    
    m_rememberCheck = new QCheckBox("Remember login credentials");
    formLayout->addRow(m_rememberCheck);
    
    mainLayout->addWidget(loginGroup);
    
    // Status and progress
    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    mainLayout->addWidget(m_statusLabel);
    
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 0); // Indeterminate progress
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    
    m_loginButton = new QPushButton("Login");
    m_loginButton->setDefault(true);
    m_loginButton->setEnabled(false);
    
    m_cancelButton = new QPushButton("Cancel");
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_loginButton);
    buttonLayout->addWidget(m_cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect signals
    connect(m_usernameEdit, &QLineEdit::textChanged, this, &PSNLoginDialog::validateInput);
    connect(m_passwordEdit, &QLineEdit::textChanged, this, &PSNLoginDialog::validateInput);
    connect(m_loginButton, &QPushButton::clicked, this, &PSNLoginDialog::onLoginClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &PSNLoginDialog::onCancelClicked);
}

void PSNLoginDialog::validateInput()
{
    bool valid = !m_usernameEdit->text().isEmpty() && !m_passwordEdit->text().isEmpty();
    m_loginButton->setEnabled(valid);
}

void PSNLoginDialog::onLoginClicked()
{
    m_statusLabel->setText("Connecting to PlayStation Network...");
    m_statusLabel->show();
    m_progressBar->show();
    m_loginButton->setEnabled(false);
    
    // TODO: Implement actual PSN authentication
    // For now, we simulate login process
    QTimer::singleShot(2000, [this]() {
        if (!m_usernameEdit->text().isEmpty()) {
            accept();
        } else {
            m_statusLabel->setText("Login failed. Please check your credentials.");
            m_progressBar->hide();
            m_loginButton->setEnabled(true);
        }
    });
}

void PSNLoginDialog::onCancelClicked()
{
    reject();
}

QString PSNLoginDialog::getUsername() const
{
    return m_usernameEdit->text();
}

QString PSNLoginDialog::getPassword() const
{
    return m_passwordEdit->text();
}

QString PSNLoginDialog::getRegion() const
{
    return m_regionCombo->currentText().left(2); // Extract region code
}

bool PSNLoginDialog::shouldRememberLogin() const
{
    return m_rememberCheck->isChecked();
}

// PSNAccountManager Implementation
PSNAccountManager::PSNAccountManager(QWidget *parent)
    : QDialog(parent)
    , m_accountList(nullptr)
    , m_addButton(nullptr)
    , m_removeButton(nullptr)
    , m_setDefaultButton(nullptr)
    , m_editButton(nullptr)
    , m_accountDetails(nullptr)
{
    setWindowTitle("PSN Account Manager");
    setModal(true);
    resize(600, 400);
    setupUI();
    loadAccounts();
}

void PSNAccountManager::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    
    // Left side - Account list
    QVBoxLayout *leftLayout = new QVBoxLayout;
    
    QLabel *listLabel = new QLabel("Stored Accounts:");
    leftLayout->addWidget(listLabel);
    
    m_accountList = new QListWidget;
    m_accountList->setMinimumWidth(200);
    connect(m_accountList, &QListWidget::itemSelectionChanged, 
            this, &PSNAccountManager::onAccountSelected);
    leftLayout->addWidget(m_accountList);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    
    m_addButton = new QPushButton("Add");
    m_removeButton = new QPushButton("Remove");
    m_setDefaultButton = new QPushButton("Set Default");
    m_editButton = new QPushButton("Edit");
    
    m_removeButton->setEnabled(false);
    m_setDefaultButton->setEnabled(false);
    m_editButton->setEnabled(false);
    
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addWidget(m_setDefaultButton);
    buttonLayout->addWidget(m_editButton);
    
    leftLayout->addLayout(buttonLayout);
    
    // Right side - Account details
    QVBoxLayout *rightLayout = new QVBoxLayout;
    
    QLabel *detailsLabel = new QLabel("Account Details:");
    rightLayout->addWidget(detailsLabel);
    
    m_accountDetails = new QLabel("Select an account to view details.");
    m_accountDetails->setAlignment(Qt::AlignTop);
    m_accountDetails->setWordWrap(true);
    m_accountDetails->setStyleSheet("border: 1px solid gray; padding: 10px; background-color: #f5f5f5;");
    rightLayout->addWidget(m_accountDetails);
    
    // Close button
    QPushButton *closeButton = new QPushButton("Close");
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    rightLayout->addWidget(closeButton);
    
    mainLayout->addLayout(leftLayout);
    mainLayout->addLayout(rightLayout);
    
    // Connect button signals
    connect(m_addButton, &QPushButton::clicked, this, &PSNAccountManager::onAddAccount);
    connect(m_removeButton, &QPushButton::clicked, this, &PSNAccountManager::onRemoveAccount);
    connect(m_setDefaultButton, &QPushButton::clicked, this, &PSNAccountManager::onSetDefault);
    connect(m_editButton, &QPushButton::clicked, this, &PSNAccountManager::onEditAccount);
}

void PSNAccountManager::refreshAccountList()
{
    loadAccounts();
}

void PSNAccountManager::loadAccounts()
{
    m_accountList->clear();
    
    // TODO: Load from actual storage
    // For now, add some sample accounts
    PSNAccount account1;
    account1.username = "PSX5User1";
    account1.email = "PSX5User1@psx5.com";
    account1.region = "US";
    account1.lastLogin = QDateTime::currentDateTime().addDays(-1);
    account1.accountId = "PSX5User1";
    m_accounts.append(account1);
    
    PSNAccount account2;
    account2.username = "PSX5User2";
    account2.email = "PSX5User2@psx5.com";
    account2.region = "EU";
    account2.lastLogin = QDateTime::currentDateTime().addDays(-7);
    account2.accountId = "PSX5User2";
    m_accounts.append(account2);
    
    for (const PSNAccount &account : m_accounts) {
        QListWidgetItem *item = new QListWidgetItem(account.username);
        item->setData(Qt::UserRole, account.accountId);
        
        if (account.accountId == m_defaultAccountId) {
            item->setText(account.username + " (Default)");
            item->setIcon(QIcon(":/icons/star")); // Would need actual icon, TODO: implement
        }
        
        m_accountList->addItem(item);
    }
}

void PSNAccountManager::saveAccounts()
{
    // TODO: Implement actual account storage
}

void PSNAccountManager::onAddAccount()
{
    PSNLoginDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        PSNAccount newAccount;
        newAccount.username = dialog.getUsername();
        newAccount.region = dialog.getRegion();
        newAccount.rememberLogin = dialog.shouldRememberLogin();
        newAccount.lastLogin = QDateTime::currentDateTime();
        newAccount.accountId = QString("account_%1").arg(QDateTime::currentMSecsSinceEpoch());
        
        m_accounts.append(newAccount);
        saveAccounts();
        loadAccounts();
    }
}

void PSNAccountManager::onRemoveAccount()
{
    QListWidgetItem *current = m_accountList->currentItem();
    if (!current) return;
    
    QString accountId = current->data(Qt::UserRole).toString();
    
    int ret = QMessageBox::question(this, "Remove Account",
        QString("Are you sure you want to remove account '%1'?").arg(current->text()),
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        m_accounts.removeIf([accountId](const PSNAccount &acc) {
            return acc.accountId == accountId;
        });
        saveAccounts();
        loadAccounts();
    }
}

void PSNAccountManager::onSetDefault()
{
    QListWidgetItem *current = m_accountList->currentItem();
    if (!current) return;
    
    m_defaultAccountId = current->data(Qt::UserRole).toString();
    saveAccounts();
    loadAccounts();
}

void PSNAccountManager::onAccountSelected()
{
    QListWidgetItem *current = m_accountList->currentItem();
    bool hasSelection = (current != nullptr);
    
    m_removeButton->setEnabled(hasSelection);
    m_setDefaultButton->setEnabled(hasSelection);
    m_editButton->setEnabled(hasSelection);
    
    if (hasSelection) {
        PSNAccount account = getSelectedAccount();
        QString details = QString(
            "Username: %1\n"
            "Email: %2\n"
            "Region: %3\n"
            "Last Login: %4\n"
            "Remember Login: %5"
        ).arg(account.username, account.email, account.region,
              account.lastLogin.toString("yyyy-MM-dd hh:mm:ss"),
              account.rememberLogin ? "Yes" : "No");
        
        m_accountDetails->setText(details);
    } else {
        m_accountDetails->setText("Select an account to view details.");
    }
}

void PSNAccountManager::onEditAccount()
{
    // TODO: Implement account editing
    QMessageBox::information(this, "Edit Account", "Account editing not yet implemented.");
}

PSNAccount PSNAccountManager::getSelectedAccount()
{
    QListWidgetItem *current = m_accountList->currentItem();
    if (!current) return PSNAccount();
    
    QString accountId = current->data(Qt::UserRole).toString();
    auto it = std::find_if(m_accounts.begin(), m_accounts.end(),
                          [accountId](const PSNAccount &acc) {
                              return acc.accountId == accountId;
                          });
    
    return (it != m_accounts.end()) ? *it : PSNAccount();
}

// PSNManager Implementation
PSNManager::PSNManager(QObject *parent)
    : QObject(parent)
    , m_loginDialog(nullptr)
    , m_accountManager(nullptr)
    , m_networkManager(nullptr)
    , m_connectionTimer(nullptr)
    , m_isLoggedIn(false)
{
    setupNetworking();
    loadStoredAccounts();
}

PSNManager::~PSNManager()
{
    saveStoredAccounts();
}

void PSNManager::setupNetworking()
{
    m_networkManager = new QNetworkAccessManager(this);
    m_connectionTimer = new QTimer(this);
    m_connectionTimer->setSingleShot(true);
    m_connectionTimer->setInterval(30000); // 30 second timeout
    
    connect(m_connectionTimer, &QTimer::timeout, this, &PSNManager::onConnectionTimeout);
}

bool PSNManager::showLoginDialog()
{
    if (!m_loginDialog) {
        m_loginDialog = new PSNLoginDialog;
    }
    
    if (m_loginDialog->exec() == QDialog::Accepted) {
        QString username = m_loginDialog->getUsername();
        QString password = m_loginDialog->getPassword();
        QString region = m_loginDialog->getRegion();
        
        performLogin(username, password, region);
        return true;
    }
    
    return false;
}

void PSNManager::showAccountManager()
{
    if (!m_accountManager) {
        m_accountManager = new PSNAccountManager;
    }
    
    m_accountManager->refreshAccountList();
    m_accountManager->show();
    m_accountManager->raise();
    m_accountManager->activateWindow();
}

void PSNManager::logout()
{
    performLogout();
}

bool PSNManager::isLoggedIn() const
{
    return m_isLoggedIn;
}

QString PSNManager::getCurrentUsername() const
{
    return m_currentAccount.username;
}

QString PSNManager::getCurrentRegion() const
{
    return m_currentAccount.region;
}

PSNAccount PSNManager::getCurrentAccount() const
{
    return m_currentAccount;
}

void PSNManager::performLogin(const QString &username, const QString &password, const QString &region)
{
    // TODO: Implement actual PSN authentication
    // For now, simulate successful login
    m_currentAccount.username = username;
    m_currentAccount.region = region;
    m_currentAccount.lastLogin = QDateTime::currentDateTime();
    m_currentAccount.accountId = QString("login_%1").arg(QDateTime::currentMSecsSinceEpoch());
    
    m_isLoggedIn = true;
    m_sessionToken = "fake_session_token";
    
    emit loginStatusChanged(true);
    emit accountChanged(m_currentAccount);
}

void PSNManager::performLogout()
{
    m_isLoggedIn = false;
    m_sessionToken.clear();
    m_currentAccount = PSNAccount();
    
    emit loginStatusChanged(false);
}

void PSNManager::syncTrophies()
{
    if (!m_isLoggedIn) return;
    
    // TODO: Implement trophy sync
    QTimer::singleShot(1000, [this]() {
        emit trophySyncCompleted();
    });
}

void PSNManager::uploadSaveData()
{
    if (!m_isLoggedIn) return;
    
    // TODO: Implement save data upload
}

void PSNManager::downloadSaveData()
{
    if (!m_isLoggedIn) return;
    
    // TODO: Implement save data download
}

void PSNManager::updateFriendsList()
{
    if (!m_isLoggedIn) return;
    
    // TODO: Implement friends list update
}

void PSNManager::loadStoredAccounts()
{
    QString filePath = getAccountsFilePath();
    QFile file(filePath);
    
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonArray accounts = doc.array();
        
        for (const QJsonValue &value : accounts) {
            QJsonObject obj = value.toObject();
            PSNAccount account;
            account.username = obj["username"].toString();
            account.email = obj["email"].toString();
            account.region = obj["region"].toString();
            account.accountId = obj["accountId"].toString();
            account.rememberLogin = obj["rememberLogin"].toBool();
            account.lastLogin = QDateTime::fromString(obj["lastLogin"].toString(), Qt::ISODate);
            
            m_storedAccounts.append(account);
        }
    }
}

void PSNManager::saveStoredAccounts()
{
    QString filePath = getAccountsFilePath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonArray accounts;
        
        for (const PSNAccount &account : m_storedAccounts) {
            QJsonObject obj;
            obj["username"] = account.username;
            obj["email"] = account.email;
            obj["region"] = account.region;
            obj["accountId"] = account.accountId;
            obj["rememberLogin"] = account.rememberLogin;
            obj["lastLogin"] = account.lastLogin.toString(Qt::ISODate);
            
            accounts.append(obj);
        }
        
        QJsonDocument doc(accounts);
        file.write(doc.toJson());
    }
}

QString PSNManager::getAccountsFilePath()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dataPath + "/psn_accounts.json";
}

void PSNManager::onLoginReply()
{
    // TODO: Handle login response
}

void PSNManager::onSyncReply()
{
    // TODO: Handle sync response
}

void PSNManager::onConnectionTimeout()
{
    // TODO: Handle connection timeout
}

#include "psn_manager.moc"
