#include "trophy_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QTabWidget>
#include <QGroupBox>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QHeaderView>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QClipboard>
#include <QApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPainter>
#include <QFile>

TrophyWindow::TrophyWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_mainSplitter(nullptr)
    , m_rightSplitter(nullptr)
    , m_gameTree(nullptr)
    , m_searchEdit(nullptr)
    , m_filterCombo(nullptr)
    , m_showHiddenButton(nullptr)
    , m_trophyList(nullptr)
    , m_rightTabs(nullptr)
    , m_detailsWidget(nullptr)
    , m_statsWidget(nullptr)
    , m_statusLabel(nullptr)
    , m_syncProgress(nullptr)
    , m_currentTrophySet(nullptr)
    , m_currentTrophy(nullptr)
    , m_showHiddenTrophies(false)
    , m_psnManager(nullptr)
    , m_networkManager(new QNetworkAccessManager(this))
{
    setWindowTitle("PSX5 Trophy Manager");
    setMinimumSize(1000, 700);
    resize(1200, 800);
    
    setupUI();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    
    loadTrophyData();
    updateGameList();
}

TrophyWindow::~TrophyWindow()
{
    saveTrophyData();
}

void TrophyWindow::setupUI()
{
    QWidget *centralWidget = new QWidget;
    setCentralWidget(centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    // Main splitter (3-panel layout)
    m_mainSplitter = new QSplitter(Qt::Horizontal);
    
    // Left panel - Game list with search/filter
    QWidget *leftPanel = new QWidget;
    leftPanel->setMinimumWidth(250);
    leftPanel->setMaximumWidth(350);
    
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    
    // Search and filter controls
    QHBoxLayout *searchLayout = new QHBoxLayout;
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("Search games...");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &TrophyWindow::onSearchTextChanged);
    searchLayout->addWidget(m_searchEdit);
    
    leftLayout->addLayout(searchLayout);
    
    QHBoxLayout *filterLayout = new QHBoxLayout;
    m_filterCombo = new QComboBox;
    m_filterCombo->addItems({"All Trophies", "Unlocked", "Locked", "Platinum Only", "Gold+", "Incomplete Games"});
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &TrophyWindow::onFilterChanged);
    
    m_showHiddenButton = new QPushButton("Show Hidden");
    m_showHiddenButton->setCheckable(true);
    connect(m_showHiddenButton, &QPushButton::toggled, this, &TrophyWindow::onShowHiddenToggled);
    
    filterLayout->addWidget(m_filterCombo);
    filterLayout->addWidget(m_showHiddenButton);
    leftLayout->addLayout(filterLayout);
    
    // Game tree
    m_gameTree = new QTreeWidget;
    m_gameTree->setHeaderLabels({"Game", "Progress"});
    m_gameTree->setRootIsDecorated(false);
    m_gameTree->setAlternatingRowColors(true);
    connect(m_gameTree, &QTreeWidget::currentItemChanged, this, &TrophyWindow::onGameSelectionChanged);
    
    leftLayout->addWidget(m_gameTree);
    
    // Center and right panels in splitter
    m_rightSplitter = new QSplitter(Qt::Horizontal);
    
    // Center panel - Trophy list
    m_trophyList = new TrophyListWidget;
    connect(m_trophyList, &QTreeWidget::currentItemChanged, this, &TrophyWindow::onTrophySelectionChanged);
    
    // Right panel - Details and stats tabs
    m_rightTabs = new QTabWidget;
    m_rightTabs->setMinimumWidth(300);
    m_rightTabs->setMaximumWidth(400);
    
    m_detailsWidget = new TrophyDetailsWidget;
    m_statsWidget = new TrophyStatsWidget;
    
    m_rightTabs->addTab(m_detailsWidget, "Details");
    m_rightTabs->addTab(m_statsWidget, "Statistics");
    
    m_rightSplitter->addWidget(m_trophyList);
    m_rightSplitter->addWidget(m_rightTabs);
    m_rightSplitter->setSizes({400, 300});
    
    m_mainSplitter->addWidget(leftPanel);
    m_mainSplitter->addWidget(m_rightSplitter);
    m_mainSplitter->setSizes({250, 750});
    
    mainLayout->addWidget(m_mainSplitter);
}

void TrophyWindow::setupMenuBar()
{
    // File menu
    QMenu *fileMenu = menuBar()->addMenu("&File");
    
    QAction *exportAction = fileMenu->addAction("&Export Trophies...");
    connect(exportAction, &QAction::triggered, this, &TrophyWindow::onExportTrophies);
    
    QAction *importAction = fileMenu->addAction("&Import Trophies...");
    connect(importAction, &QAction::triggered, this, &TrophyWindow::onImportTrophies);
    
    fileMenu->addSeparator();
    
    QAction *closeAction = fileMenu->addAction("&Close");
    closeAction->setShortcut(QKeySequence::Close);
    connect(closeAction, &QAction::triggered, this, &QWidget::close);
    
    // Trophy menu
    QMenu *trophyMenu = menuBar()->addMenu("&Trophy");
    
    QAction *unlockAction = trophyMenu->addAction("&Unlock Selected Trophy");
    unlockAction->setShortcut(Qt::Key_Space);
    connect(unlockAction, &QAction::triggered, this, &TrophyWindow::onUnlockTrophy);
    
    QAction *syncAction = trophyMenu->addAction("&Sync with PSN");
    syncAction->setShortcut(Qt::Key_F5);
    connect(syncAction, &QAction::triggered, this, &TrophyWindow::onSyncTrophies);
    
    trophyMenu->addSeparator();
    
    QAction *refreshAction = trophyMenu->addAction("&Refresh");
    refreshAction->setShortcut(Qt::Key_F5);
    connect(refreshAction, &QAction::triggered, this, &TrophyWindow::refreshTrophyData);
}

void TrophyWindow::setupToolBar()
{
    QToolBar *toolBar = addToolBar("Main");
    
    QAction *syncAction = toolBar->addAction("Sync PSN");
    connect(syncAction, &QAction::triggered, this, &TrophyWindow::onSyncTrophies);
    
    toolBar->addSeparator();
    
    QAction *unlockAction = toolBar->addAction("Unlock Trophy");
    connect(unlockAction, &QAction::triggered, this, &TrophyWindow::onUnlockTrophy);
    
    QAction *refreshAction = toolBar->addAction("Refresh");
    connect(refreshAction, &QAction::triggered, this, &TrophyWindow::refreshTrophyData);
}

void TrophyWindow::setupStatusBar()
{
    m_statusLabel = new QLabel("Ready");
    m_syncProgress = new QProgressBar;
    m_syncProgress->setVisible(false);
    
    statusBar()->addWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_syncProgress);
}

void TrophyWindow::loadTrophyData()
{
    QString trophyPath = getTrophyStoragePath();
    QDir trophyDir(trophyPath);
    
    if (!trophyDir.exists()) {
        trophyDir.mkpath(trophyPath);
        return;
    }
    
    QStringList gameIds = trophyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString &gameId : gameIds) {
        QString gameDir = trophyDir.absoluteFilePath(gameId);
        TrophySet trophySet = loadTrophySetFromDirectory(gameDir, gameId);
        
        if (trophySet.totalTrophies > 0) {
            m_trophySets.append(trophySet);
        }
    }
    
    if (m_trophySets.isEmpty()) {
        createInitialTrophyStructure();
    }
}

TrophySet TrophyWindow::loadTrophySetFromDirectory(const QString &directory, const QString &gameId)
{
    TrophySet trophySet;
    trophySet.gameId = gameId;
    
    QString metadataPath = directory + "/trophy_metadata.json";
    QFile metadataFile(metadataPath);
    
    if (metadataFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll());
        QJsonObject metadata = doc.object();
        
        trophySet.gameName = metadata["gameName"].toString();
        trophySet.totalTrophies = metadata["totalTrophies"].toInt();
        trophySet.unlockedTrophies = metadata["unlockedTrophies"].toInt();
        trophySet.bronzeCount = metadata["bronzeCount"].toInt();
        trophySet.silverCount = metadata["silverCount"].toInt();
        trophySet.goldCount = metadata["goldCount"].toInt();
        trophySet.platinumCount = metadata["platinumCount"].toInt();
        trophySet.completionPercentage = metadata["completionPercentage"].toDouble();
        trophySet.lastUpdated = QDateTime::fromString(metadata["lastUpdated"].toString(), Qt::ISODate);
        
        QJsonArray trophiesArray = metadata["trophies"].toArray();
        for (const QJsonValue &value : trophiesArray) {
            QJsonObject trophyObj = value.toObject();
            
            Trophy trophy;
            trophy.id = trophyObj["id"].toString();
            trophy.name = trophyObj["name"].toString();
            trophy.description = trophyObj["description"].toString();
            trophy.gameId = gameId;
            trophy.gameName = trophySet.gameName;
            trophy.type = static_cast<TrophyType>(trophyObj["type"].toInt());
            trophy.grade = static_cast<TrophyGrade>(trophyObj["grade"].toInt());
            trophy.unlocked = trophyObj["unlocked"].toBool();
            trophy.hidden = trophyObj["hidden"].toBool();
            trophy.progressPercentage = trophyObj["progressPercentage"].toDouble();
            
            if (trophy.unlocked) {
                trophy.unlockedDate = QDateTime::fromString(trophyObj["unlockedDate"].toString(), Qt::ISODate);
            }
            
            trophySet.trophies.append(trophy);
        }
    }
    
    return trophySet;
}

void TrophyWindow::saveTrophyData()
{
    QString trophyPath = getTrophyStoragePath();
    QDir().mkpath(trophyPath);
    
    for (const TrophySet &trophySet : m_trophySets) {
        QString gameDir = trophyPath + "/" + trophySet.gameId;
        QDir().mkpath(gameDir);
        
        QJsonObject metadata;
        metadata["gameName"] = trophySet.gameName;
        metadata["totalTrophies"] = trophySet.totalTrophies;
        metadata["unlockedTrophies"] = trophySet.unlockedTrophies;
        metadata["bronzeCount"] = trophySet.bronzeCount;
        metadata["silverCount"] = trophySet.silverCount;
        metadata["goldCount"] = trophySet.goldCount;
        metadata["platinumCount"] = trophySet.platinumCount;
        metadata["completionPercentage"] = trophySet.completionPercentage;
        metadata["lastUpdated"] = trophySet.lastUpdated.toString(Qt::ISODate);
        
        QJsonArray trophiesArray;
        for (const Trophy &trophy : trophySet.trophies) {
            QJsonObject trophyObj;
            trophyObj["id"] = trophy.id;
            trophyObj["name"] = trophy.name;
            trophyObj["description"] = trophy.description;
            trophyObj["type"] = static_cast<int>(trophy.type);
            trophyObj["grade"] = static_cast<int>(trophy.grade);
            trophyObj["unlocked"] = trophy.unlocked;
            trophyObj["hidden"] = trophy.hidden;
            trophyObj["progressPercentage"] = trophy.progressPercentage;
            
            if (trophy.unlocked) {
                trophyObj["unlockedDate"] = trophy.unlockedDate.toString(Qt::ISODate);
            }
            
            trophiesArray.append(trophyObj);
        }
        
        metadata["trophies"] = trophiesArray;
        
        QString metadataPath = gameDir + "/trophy_metadata.json";
        QFile metadataFile(metadataPath);
        if (metadataFile.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(metadata);
            metadataFile.write(doc.toJson());
        }
    }
}

QString TrophyWindow::getTrophyStoragePath()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dataPath + "/trophies";
}

void TrophyWindow::onSyncTrophies()
{
    if (!m_psnManager || !m_psnManager->isLoggedIn()) {
        QMessageBox::information(this, "PSN Sync", "Please log in to PSN first.");
        return;
    }
    
    m_syncProgress->setVisible(true);
    m_syncProgress->setRange(0, 0); // Indeterminate
    m_statusLabel->setText("Syncing with PSN...");
    
    performPSNSync();
}

void TrophyWindow::performPSNSync()
{
    PSNAccount account = m_psnManager->getCurrentAccount();
    
    int totalSets = m_trophySets.size();
    int currentSet = 0;
    
    for (TrophySet &trophySet : m_trophySets) {
        m_statusLabel->setText(QString("Syncing %1...").arg(trophySet.gameName));
        
        QNetworkRequest request;
        request.setUrl(QUrl(QString("https://m.np.playstation.com/api/trophy/v1/users/%1/npCommunicationIds/%2/trophyGroups/all/trophies")
                           .arg(account.username, trophySet.gameId)));
        request.setRawHeader("Authorization", QString("Bearer %1").arg(m_psnManager->getSessionToken()).toUtf8());
        
        QNetworkReply *reply = m_networkManager->get(request);
        
        connect(reply, &QNetworkReply::finished, [this, reply, &trophySet, currentSet, totalSets]() {
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                updateTrophySetFromPSN(trophySet, doc.object());
            }
            
            reply->deleteLater();
            
            currentSet++;
            if (currentSet >= totalSets) {
                m_syncProgress->setVisible(false);
                m_statusLabel->setText("PSN sync completed");
                refreshTrophyData();
                saveTrophyData();
            }
        });
    }
}

void TrophyWindow::updateTrophySetFromPSN(TrophySet &trophySet, const QJsonObject &psnData)
{
    QJsonArray trophies = psnData["trophies"].toArray();
    
    for (const QJsonValue &value : trophies) {
        QJsonObject trophyData = value.toObject();
        QString trophyId = trophyData["trophyId"].toString();
        
        for (Trophy &trophy : trophySet.trophies) {
            if (trophy.id.endsWith(trophyId)) {
                bool wasUnlocked = trophy.unlocked;
                trophy.unlocked = trophyData["earned"].toBool();
                
                if (trophy.unlocked && !wasUnlocked) {
                    trophy.unlockedDate = QDateTime::fromString(
                        trophyData["earnedDate"].toString(), Qt::ISODate);
                    trophySet.unlockedTrophies++;
                }
                
                trophy.progressPercentage = trophyData["progress"].toDouble();
                break;
            }
        }
    }
    
    trophySet.completionPercentage = (static_cast<double>(trophySet.unlockedTrophies) / trophySet.totalTrophies) * 100.0;
    trophySet.lastUpdated = QDateTime::currentDateTime();
}

void TrophyWindow::createInitialTrophyStructure()
{
    // Placeholder for creating initial trophy structure
    // TODO: Implement trophy structure creation
}

QPixmap TrophyWindow::getTrophyTypeIcon(TrophyType type)
{
    QString iconPath = getTrophyIconPath(type);
    
    if (QFile::exists(iconPath)) {
        return QPixmap(iconPath).scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QColor trophyColor = getTrophyTypeColor(type);
    painter.setBrush(QBrush(trophyColor));
    painter.setPen(QPen(trophyColor.darker(150), 2));
    
    painter.drawEllipse(6, 8, 20, 16);
    painter.drawRect(8, 16, 16, 8);
    painter.drawRect(14, 24, 4, 6);
    painter.drawRect(10, 28, 12, 2);
    
    painter.drawEllipse(2, 10, 6, 8);
    painter.drawEllipse(24, 10, 6, 8);
    
    return pixmap;
}

QString TrophyWindow::getTrophyIconPath(TrophyType type)
{
    QString iconDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/icons";
    
    switch (type) {
        case TrophyType::Bronze:
            return iconDir + "/trophy_bronze.png";
        case TrophyType::Silver:
            return iconDir + "/trophy_silver.png";
        case TrophyType::Gold:
            return iconDir + "/trophy_gold.png";
        case TrophyType::Platinum:
            return iconDir + "/trophy_platinum.png";
        default:
            return iconDir + "/trophy_bronze.png";
    }
}

void TrophyWindow::onExportTrophies()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export Trophy Data",
        QString("trophies_%1.json").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        "JSON Files (*.json);;All Files (*)"
    );
    
    if (!fileName.isEmpty()) {
        QJsonObject exportData;
        exportData["exportDate"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        exportData["version"] = "1.0";
        
        QJsonArray trophySetsArray;
        for (const TrophySet &trophySet : m_trophySets) {
            QJsonObject setObj;
            setObj["gameId"] = trophySet.gameId;
            setObj["gameName"] = trophySet.gameName;
            setObj["totalTrophies"] = trophySet.totalTrophies;
            setObj["unlockedTrophies"] = trophySet.unlockedTrophies;
            setObj["completionPercentage"] = trophySet.completionPercentage;
            
            QJsonArray trophiesArray;
            for (const Trophy &trophy : trophySet.trophies) {
                if (trophy.unlocked) {
                    QJsonObject trophyObj;
                    trophyObj["id"] = trophy.id;
                    trophyObj["name"] = trophy.name;
                    trophyObj["type"] = static_cast<int>(trophy.type);
                    trophyObj["unlockedDate"] = trophy.unlockedDate.toString(Qt::ISODate);
                    trophiesArray.append(trophyObj);
                }
            }
            
            setObj["trophies"] = trophiesArray;
            trophySetsArray.append(setObj);
        }
        
        exportData["trophySets"] = trophySetsArray;
        
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(exportData);
            file.write(doc.toJson());
            QMessageBox::information(this, "Export Complete", 
                QString("Trophy data exported successfully to:\n%1").arg(fileName));
        } else {
            QMessageBox::warning(this, "Export Failed", "Failed to write trophy data to file.");
        }
    }
}

void TrophyWindow::onImportTrophies()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Import Trophy Data",
        QString(),
        "JSON Files (*.json);;All Files (*)"
    );
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "Import Failed", "Failed to open trophy data file.");
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonObject importData = doc.object();
        
        if (!importData.contains("trophySets")) {
            QMessageBox::warning(this, "Import Failed", "Invalid trophy data format.");
            return;
        }
        
        int importedSets = 0;
        int importedTrophies = 0;
        
        QJsonArray trophySetsArray = importData["trophySets"].toArray();
        for (const QJsonValue &value : trophySetsArray) {
            QJsonObject setObj = value.toObject();
            QString gameId = setObj["gameId"].toString();
            
            TrophySet *existingSet = nullptr;
            for (TrophySet &set : m_trophySets) {
                if (set.gameId == gameId) {
                    existingSet = &set;
                    break;
                }
            }
            
            if (existingSet) {
                QJsonArray trophiesArray = setObj["trophies"].toArray();
                for (const QJsonValue &trophyValue : trophiesArray) {
                    QJsonObject trophyObj = trophyValue.toObject();
                    QString trophyId = trophyObj["id"].toString();
                    
                    for (Trophy &trophy : existingSet->trophies) {
                        if (trophy.id == trophyId && !trophy.unlocked) {
                            trophy.unlocked = true;
                            trophy.unlockedDate = QDateTime::fromString(
                                trophyObj["unlockedDate"].toString(), Qt::ISODate);
                            existingSet->unlockedTrophies++;
                            importedTrophies++;
                            break;
                        }
                    }
                }
                
                existingSet->completionPercentage = 
                    (static_cast<double>(existingSet->unlockedTrophies) / existingSet->totalTrophies) * 100.0;
                existingSet->lastUpdated = QDateTime::currentDateTime();
                importedSets++;
            }
        }
        
        if (importedSets > 0) {
            saveTrophyData();
            refreshTrophyData();
            QMessageBox::information(this, "Import Complete", 
                QString("Successfully imported %1 trophies from %2 games.")
                .arg(importedTrophies).arg(importedSets));
        } else {
            QMessageBox::information(this, "Import Complete", "No new trophy data was imported.");
        }
    }
}

void TrophyWindow::updateGameList()
{
    m_gameTree->clear();
    
    for (const TrophySet &trophySet : m_trophySets) {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_gameTree);
        item->setText(0, trophySet.gameName);
        item->setText(1, QString("%1%").arg(trophySet.completionPercentage, 0, 'f', 1));
        item->setData(0, Qt::UserRole, trophySet.gameId);
        
        if (trophySet.completionPercentage == 100.0) {
            item->setBackground(1, QColor(144, 238, 144)); // Light green
        } else if (trophySet.completionPercentage >= 75.0) {
            item->setBackground(1, QColor(255, 255, 224)); // Light yellow
        } else if (trophySet.completionPercentage >= 50.0) {
            item->setBackground(1, QColor(255, 228, 196)); // Light orange
        }
    }
    
    m_gameTree->resizeColumnToContents(0);
}

void TrophyWindow::updateTrophyList()
{
    if (m_currentTrophySet) {
        m_trophyList->setTrophySet(m_currentTrophySet);
        filterTrophies();
    } else {
        m_trophyList->setTrophySet(nullptr);
    }
}

void TrophyWindow::updateTrophyDetails()
{
    m_detailsWidget->setTrophy(m_currentTrophy);
}

void TrophyWindow::updateStats()
{
    m_statsWidget->setTrophySet(m_currentTrophySet);
    m_statsWidget->setOverallStats(m_trophySets);
}

void TrophyWindow::filterTrophies()
{
    if (m_trophyList) {
        m_trophyList->applyFilter(m_currentFilter, m_searchText, m_showHiddenTrophies);
    }
}

void TrophyWindow::refreshTrophyData()
{
    loadTrophyData();
    updateGameList();
    updateTrophyList();
    updateStats();
    m_statusLabel->setText("Trophy data refreshed");
}

void TrophyWindow::onGameSelectionChanged()
{
    QTreeWidgetItem *current = m_gameTree->currentItem();
    if (current) {
        QString gameId = current->data(0, Qt::UserRole).toString();
        auto it = std::find_if(m_trophySets.begin(), m_trophySets.end(),
                              [gameId](const TrophySet &set) {
                                  return set.gameId == gameId;
                              });
        
        if (it != m_trophySets.end()) {
            m_currentTrophySet = &(*it);
            updateTrophyList();
            updateStats();
        }
    } else {
        m_currentTrophySet = nullptr;
        updateTrophyList();
        updateStats();
    }
}

void TrophyWindow::onTrophySelectionChanged()
{
    m_currentTrophy = m_trophyList->getCurrentTrophy();
    updateTrophyDetails();
}

void TrophyWindow::onUnlockTrophy()
{
    if (m_currentTrophy && !m_currentTrophy->unlocked) {
        m_currentTrophy->unlocked = true;
        m_currentTrophy->unlockedDate = QDateTime::currentDateTime();
        m_currentTrophy->progressPercentage = 100.0;
        
        if (m_currentTrophySet) {
            m_currentTrophySet->unlockedTrophies++;
            m_currentTrophySet->completionPercentage = 
                (double)m_currentTrophySet->unlockedTrophies / m_currentTrophySet->totalTrophies * 100.0;
        }
        
        updateTrophyList();
        updateTrophyDetails();
        updateStats();
        updateGameList();
        
        m_statusLabel->setText(QString("Trophy unlocked: %1").arg(m_currentTrophy->name));
    }
}

QString TrophyWindow::getTrophyTypeString(TrophyType type)
{
    switch (type) {
    case TrophyType::Bronze:   return "Bronze";
    case TrophyType::Silver:   return "Silver";
    case TrophyType::Gold:     return "Gold";
    case TrophyType::Platinum: return "Platinum";
    default:                   return "Unknown";
    }
}

QString TrophyWindow::getTrophyGradeString(TrophyGrade grade)
{
    switch (grade) {
    case TrophyGrade::Common:    return "Common";
    case TrophyGrade::Uncommon:  return "Uncommon";
    case TrophyGrade::Rare:      return "Rare";
    case TrophyGrade::VeryRare:  return "Very Rare";
    case TrophyGrade::UltraRare: return "Ultra Rare";
    default:                     return "Unknown";
    }
}

QColor TrophyWindow::getTrophyTypeColor(TrophyType type)
{
    switch (type) {
    case TrophyType::Bronze:   return QColor(205, 127, 50);   // Bronze
    case TrophyType::Silver:   return QColor(192, 192, 192);  // Silver
    case TrophyType::Gold:     return QColor(255, 215, 0);    // Gold
    case TrophyType::Platinum: return QColor(229, 228, 226);  // Platinum
    default:                   return QColor(128, 128, 128);  // Gray
    }
}

// TrophyListWidget Implementation
TrophyListWidget::TrophyListWidget(QWidget *parent)
    : QTreeWidget(parent)
    , m_trophySet(nullptr)
{
    setupColumns();
    setAlternatingRowColors(true);
    setRootIsDecorated(false);
    setSortingEnabled(true);
    
    connect(this, &QTreeWidget::itemDoubleClicked, this, &TrophyListWidget::onItemDoubleClicked);
}

void TrophyListWidget::setupColumns()
{
    setHeaderLabels({"Trophy", "Type", "Grade", "Status", "Progress"});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
}

void TrophyListWidget::setTrophySet(const TrophySet *trophySet)
{
    m_trophySet = trophySet;
    clear();
    m_trophyItems.clear();
    
    if (m_trophySet) {
        for (const Trophy &trophy : m_trophySet->trophies) {
            addTrophyItem(trophy);
        }
    }
}

void TrophyListWidget::addTrophyItem(const Trophy &trophy)
{
    QTreeWidgetItem *item = new QTreeWidgetItem(this);
    updateTrophyItem(item, trophy);
    m_trophyItems.append(item);
}

void TrophyListWidget::updateTrophyItem(QTreeWidgetItem *item, const Trophy &trophy)
{
    item->setText(0, trophy.name);
    item->setText(1, getTrophyTypeString(trophy.type));
    item->setText(2, getTrophyGradeString(trophy.grade));
    item->setText(3, trophy.unlocked ? "Unlocked" : "Locked");
    item->setText(4, QString("%1%").arg(trophy.progressPercentage, 0, 'f', 1));
    
    item->setData(0, Qt::UserRole, trophy.id);
    
    QColor typeColor = getTrophyTypeColor(trophy.type);
    item->setForeground(1, typeColor);
    
    if (trophy.unlocked) {
        item->setBackground(3, QColor(144, 238, 144)); // Light green
    } else if (trophy.hidden) {
        item->setForeground(0, QColor(128, 128, 128)); // Gray for hidden
        item->setText(0, "Hidden Trophy");
    }
}

void TrophyListWidget::applyFilter(const QString &filter, const QString &searchText, bool showHidden)
{
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        bool visible = true;
        
        if (!searchText.isEmpty()) {
            visible = item->text(0).contains(searchText, Qt::CaseInsensitive);
        }
        
        if (visible && !filter.isEmpty() && filter != "All Trophies") {
            if (filter == "Unlocked" && item->text(3) != "Unlocked") {
                visible = false;
            } else if (filter == "Locked" && item->text(3) != "Locked") {
                visible = false;
            } else if (filter == "Platinum Only" && item->text(1) != "Platinum") {
                visible = false;
            } else if (filter == "Gold+" && item->text(1) != "Gold" && item->text(1) != "Platinum") {
                visible = false;
            }
        }
        
        if (visible && !showHidden) {
            QString trophyId = item->data(0, Qt::UserRole).toString();
            if (m_trophySet) {
                auto it = std::find_if(const_cast<TrophySet*>(m_trophySet)->trophies.begin(), 
                                      const_cast<TrophySet*>(m_trophySet)->trophies.end(),
                                      [trophyId](const Trophy &t) { return t.id == trophyId; });
                if (it != const_cast<TrophySet*>(m_trophySet)->trophies.end() && it->hidden) {
                    visible = false;
                }
            }
        }
        
        item->setHidden(!visible);
    }
}

Trophy* TrophyListWidget::getCurrentTrophy()
{
    QTreeWidgetItem *current = currentItem();
    if (!current || !m_trophySet) return nullptr;
    
    QString trophyId = current->data(0, Qt::UserRole).toString();
    auto it = std::find_if(const_cast<TrophySet*>(m_trophySet)->trophies.begin(), 
                          const_cast<TrophySet*>(m_trophySet)->trophies.end(),
                          [trophyId](const Trophy &t) { return t.id == trophyId; });
    
    return (it != const_cast<TrophySet*>(m_trophySet)->trophies.end()) ? &(*it) : nullptr;
}

void TrophyListWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QTreeWidgetItem *item = itemAt(event->pos());
    if (!item) return;
    
    QMenu menu(this);
    
    QAction *unlockAction = menu.addAction("Unlock Trophy");
    QAction *copyAction = menu.addAction("Copy Trophy Info");
    
    connect(unlockAction, &QAction::triggered, this, &TrophyListWidget::onUnlockTrophy);
    connect(copyAction, &QAction::triggered, this, &TrophyListWidget::onCopyTrophyInfo);
    
    menu.exec(event->globalPos());
}

void TrophyListWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (item && item->text(3) == "Locked") {
        onUnlockTrophy();
    }
}

void TrophyListWidget::onUnlockTrophy()
{
    Trophy *trophy = getCurrentTrophy();
    if (trophy && !trophy->unlocked) {
        trophy->unlocked = true;
        trophy->unlockedDate = QDateTime::currentDateTime();
        trophy->progressPercentage = 100.0;
        
        QTreeWidgetItem *current = currentItem();
        if (current) {
            updateTrophyItem(current, *trophy);
        }
    }
}

void TrophyListWidget::onCopyTrophyInfo()
{
    Trophy *trophy = getCurrentTrophy();
    if (trophy) {
        QString info = QString("Trophy: %1\nType: %2\nGrade: %3\nDescription: %4\nStatus: %5")
                      .arg(trophy->name, getTrophyTypeString(trophy->type), 
                           getTrophyGradeString(trophy->grade), trophy->description,
                           trophy->unlocked ? "Unlocked" : "Locked");
        
        QApplication::clipboard()->setText(info);
    }
}

QString TrophyListWidget::getTrophyTypeString(TrophyType type)
{
    switch (type) {
    case TrophyType::Bronze:   return "Bronze";
    case TrophyType::Silver:   return "Silver";
    case TrophyType::Gold:     return "Gold";
    case TrophyType::Platinum: return "Platinum";
    default:                   return "Unknown";
    }
}

QString TrophyListWidget::getTrophyGradeString(TrophyGrade grade)
{
    switch (grade) {
    case TrophyGrade::Common:    return "Common";
    case TrophyGrade::Uncommon:  return "Uncommon";
    case TrophyGrade::Rare:      return "Rare";
    case TrophyGrade::VeryRare:  return "Very Rare";
    case TrophyGrade::UltraRare: return "Ultra Rare";
    default:                     return "Unknown";
    }
}

QColor TrophyListWidget::getTrophyTypeColor(TrophyType type)
{
    switch (type) {
    case TrophyType::Bronze:   return QColor(205, 127, 50);
    case TrophyType::Silver:   return QColor(192, 192, 192);
    case TrophyType::Gold:     return QColor(255, 215, 0);
    case TrophyType::Platinum: return QColor(229, 228, 226);
    default:                   return QColor(128, 128, 128);
    }
}

// TrophyDetailsWidget Implementation
TrophyDetailsWidget::TrophyDetailsWidget(QWidget *parent)
    : QWidget(parent)
    , m_iconLabel(nullptr)
    , m_nameLabel(nullptr)
    , m_typeLabel(nullptr)
    , m_gradeLabel(nullptr)
    , m_descriptionEdit(nullptr)
    , m_statusLabel(nullptr)
    , m_unlockedDateLabel(nullptr)
    , m_progressLabel(nullptr)
    , m_progressBar(nullptr)
    , m_unlockButton(nullptr)
{
    setupUI();
}

void TrophyDetailsWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    
    QHBoxLayout *headerLayout = new QHBoxLayout;
    
    m_iconLabel = new QLabel;
    m_iconLabel->setFixedSize(64, 64);
    m_iconLabel->setStyleSheet("border: 1px solid gray;");
    m_iconLabel->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_iconLabel);
    
    QVBoxLayout *infoLayout = new QVBoxLayout;
    
    m_nameLabel = new QLabel;
    m_nameLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    m_nameLabel->setWordWrap(true);
    infoLayout->addWidget(m_nameLabel);
    
    m_typeLabel = new QLabel;
    infoLayout->addWidget(m_typeLabel);
    
    m_gradeLabel = new QLabel;
    infoLayout->addWidget(m_gradeLabel);
    
    infoLayout->addStretch();
    headerLayout->addLayout(infoLayout);
    
    layout->addLayout(headerLayout);
    
    QLabel *descLabel = new QLabel("Description:");
    descLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(descLabel);
    
    m_descriptionEdit = new QTextEdit;
    m_descriptionEdit->setReadOnly(true);
    m_descriptionEdit->setMaximumHeight(80);
    layout->addWidget(m_descriptionEdit);
    
    QGroupBox *statusGroup = new QGroupBox("Status");
    QVBoxLayout *statusLayout = new QVBoxLayout(statusGroup);
    
    m_statusLabel = new QLabel;
    statusLayout->addWidget(m_statusLabel);
    
    m_unlockedDateLabel = new QLabel;
    statusLayout->addWidget(m_unlockedDateLabel);
    
    m_progressLabel = new QLabel;
    statusLayout->addWidget(m_progressLabel);
    
    m_progressBar = new QProgressBar;
    statusLayout->addWidget(m_progressBar);
    
    layout->addWidget(statusGroup);
    
    m_unlockButton = new QPushButton("Unlock Trophy (Test)");
    layout->addWidget(m_unlockButton);
    
    layout->addStretch();
}

void TrophyDetailsWidget::setTrophy(const Trophy *trophy)
{
    if (!trophy) {
        clear();
        return;
    }
    
    QPixmap icon(64, 64);
    icon.fill(getTrophyTypeColor(trophy->type));
    m_iconLabel->setPixmap(icon);
    
    m_nameLabel->setText(trophy->hidden && !trophy->unlocked ? "Hidden Trophy" : trophy->name);
    m_typeLabel->setText(QString("Type: %1").arg(getTrophyTypeString(trophy->type)));
    m_gradeLabel->setText(QString("Grade: %1").arg(getTrophyGradeString(trophy->grade)));
    
    QString description = trophy->hidden && !trophy->unlocked ? 
                         "This trophy is hidden until unlocked." : trophy->description;
    m_descriptionEdit->setPlainText(description);
    
    m_statusLabel->setText(QString("Status: %1").arg(trophy->unlocked ? "Unlocked" : "Locked"));
    
    if (trophy->unlocked) {
        m_unlockedDateLabel->setText(QString("Unlocked: %1").arg(
            trophy->unlockedDate.toString("yyyy-MM-dd hh:mm:ss")));
        m_unlockedDateLabel->setVisible(true);
    } else {
        m_unlockedDateLabel->setVisible(false);
    }
    
    m_progressLabel->setText(QString("Progress: %1%").arg(trophy->progressPercentage, 0, 'f', 1));
    m_progressBar->setValue(static_cast<int>(trophy->progressPercentage));
    
    m_unlockButton->setEnabled(!trophy->unlocked);
}

void TrophyDetailsWidget::clear()
{
    m_iconLabel->clear();
    m_nameLabel->clear();
    m_typeLabel->clear();
    m_gradeLabel->clear();
    m_descriptionEdit->clear();
    m_statusLabel->clear();
    m_unlockedDateLabel->clear();
    m_progressLabel->clear();
    m_progressBar->setValue(0);
    m_unlockButton->setEnabled(false);
}

QString TrophyDetailsWidget::getTrophyTypeString(TrophyType type)
{
    switch (type) {
    case TrophyType::Bronze:   return "Bronze";
    case TrophyType::Silver:   return "Silver";
    case TrophyType::Gold:     return "Gold";
    case TrophyType::Platinum: return "Platinum";
    default:                   return "Unknown";
    }
}

QString TrophyDetailsWidget::getTrophyGradeString(TrophyGrade grade)
{
    switch (grade) {
    case TrophyGrade::Common:    return "Common";
    case TrophyGrade::Uncommon:  return "Uncommon";
    case TrophyGrade::Rare:      return "Rare";
    case TrophyGrade::VeryRare:  return "Very Rare";
    case TrophyGrade::UltraRare: return "Ultra Rare";
    default:                     return "Unknown";
    }
}

QColor TrophyDetailsWidget::getTrophyTypeColor(TrophyType type)
{
    switch (type) {
    case TrophyType::Bronze:   return QColor(205, 127, 50);
    case TrophyType::Silver:   return QColor(192, 192, 192);
    case TrophyType::Gold:     return QColor(255, 215, 0);
    case TrophyType::Platinum: return QColor(229, 228, 226);
    default:                   return QColor(128, 128, 128);
    }
}

// TrophyStatsWidget Implementation
TrophyStatsWidget::TrophyStatsWidget(QWidget *parent)
    : QWidget(parent)
    , m_currentTrophySet(nullptr)
    , m_allTrophySets(nullptr)
{
    setupUI();
}

void TrophyStatsWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    
    QGroupBox *gameGroup = new QGroupBox("Current Game");
    QGridLayout *gameLayout = new QGridLayout(gameGroup);
    
    m_gameNameLabel = new QLabel;
    m_gameNameLabel->setStyleSheet("font-weight: bold;");
    gameLayout->addWidget(m_gameNameLabel, 0, 0, 1, 2);
    
    gameLayout->addWidget(new QLabel("Completion:"), 1, 0);
    m_completionLabel = new QLabel;
    gameLayout->addWidget(m_completionLabel, 1, 1);
    
    m_completionBar = new QProgressBar;
    gameLayout->addWidget(m_completionBar, 2, 0, 1, 2);
    
    gameLayout->addWidget(new QLabel("Total Trophies:"), 3, 0);
    m_totalTrophiesLabel = new QLabel;
    gameLayout->addWidget(m_totalTrophiesLabel, 3, 1);
    
    gameLayout->addWidget(new QLabel("Unlocked:"), 4, 0);
    m_unlockedTrophiesLabel = new QLabel;
    gameLayout->addWidget(m_unlockedTrophiesLabel, 4, 1);
    
    gameLayout->addWidget(new QLabel("Bronze:"), 5, 0);
    m_bronzeLabel = new QLabel;
    gameLayout->addWidget(m_bronzeLabel, 5, 1);
    
    gameLayout->addWidget(new QLabel("Silver:"), 6, 0);
    m_silverLabel = new QLabel;
    gameLayout->addWidget(m_silverLabel, 6, 1);
    
    gameLayout->addWidget(new QLabel("Gold:"), 7, 0);
    m_goldLabel = new QLabel;
    gameLayout->addWidget(m_goldLabel, 7, 1);
    
    gameLayout->addWidget(new QLabel("Platinum:"), 8, 0);
    m_platinumLabel = new QLabel;
    gameLayout->addWidget(m_platinumLabel, 8, 1);
    
    gameLayout->addWidget(new QLabel("Last Updated:"), 9, 0);
    m_lastUpdatedLabel = new QLabel;
    gameLayout->addWidget(m_lastUpdatedLabel, 9, 1);
    
    layout->addWidget(gameGroup);
    
    QGroupBox *overallGroup = new QGroupBox("Overall Statistics");
    QGridLayout *overallLayout = new QGridLayout(overallGroup);
    
    overallLayout->addWidget(new QLabel("Total Games:"), 0, 0);
    m_totalGamesLabel = new QLabel;
    overallLayout->addWidget(m_totalGamesLabel, 0, 1);
    
    overallLayout->addWidget(new QLabel("Completed Games:"), 1, 0);
    m_completedGamesLabel = new QLabel;
    overallLayout->addWidget(m_completedGamesLabel, 1, 1);
    
    overallLayout->addWidget(new QLabel("Overall Completion:"), 2, 0);
    m_overallCompletionLabel = new QLabel;
    overallLayout->addWidget(m_overallCompletionLabel, 2, 1);
    
    m_overallCompletionBar = new QProgressBar;
    overallLayout->addWidget(m_overallCompletionBar, 3, 0, 1, 2);
    
    overallLayout->addWidget(new QLabel("Total Trophies:"), 4, 0);
    m_overallTrophiesLabel = new QLabel;
    overallLayout->addWidget(m_overallTrophiesLabel, 4, 1);
    
    overallLayout->addWidget(new QLabel("Bronze:"), 5, 0);
    m_overallBronzeLabel = new QLabel;
    overallLayout->addWidget(m_overallBronzeLabel, 5, 1);
    
    overallLayout->addWidget(new QLabel("Silver:"), 6, 0);
    m_overallSilverLabel = new QLabel;
    overallLayout->addWidget(m_overallSilverLabel, 6, 1);
    
    overallLayout->addWidget(new QLabel("Gold:"), 7, 0);
    m_overallGoldLabel = new QLabel;
    overallLayout->addWidget(m_overallGoldLabel, 7, 1);
    
    overallLayout->addWidget(new QLabel("Platinum:"), 8, 0);
    m_overallPlatinumLabel = new QLabel;
    overallLayout->addWidget(m_overallPlatinumLabel, 8, 1);
    
    layout->addWidget(overallGroup);
    layout->addStretch();
}

void TrophyStatsWidget::setTrophySet(const TrophySet *trophySet)
{
    m_currentTrophySet = trophySet;
    updateGameStats();
}

void TrophyStatsWidget::setOverallStats(const QList<TrophySet> &allSets)
{
    m_allTrophySets = &allSets;
    updateOverallStats();
}

void TrophyStatsWidget::clear()
{
    m_currentTrophySet = nullptr;
    m_allTrophySets = nullptr;
    updateGameStats();
    updateOverallStats();
}

void TrophyStatsWidget::updateGameStats()
{
    if (!m_currentTrophySet) {
        m_gameNameLabel->setText("No game selected");
        m_completionLabel->clear();
        m_completionBar->setValue(0);
        m_totalTrophiesLabel->clear();
        m_unlockedTrophiesLabel->clear();
        m_bronzeLabel->clear();
        m_silverLabel->clear();
        m_goldLabel->clear();
        m_platinumLabel->clear();
        m_lastUpdatedLabel->clear();
        return;
    }
    
    const TrophySet &set = *m_currentTrophySet;
    
    m_gameNameLabel->setText(set.gameName);
    m_completionLabel->setText(QString("%1%").arg(set.completionPercentage, 0, 'f', 1));
    m_completionBar->setValue(static_cast<int>(set.completionPercentage));
    m_totalTrophiesLabel->setText(QString::number(set.totalTrophies));
    m_unlockedTrophiesLabel->setText(QString::number(set.unlockedTrophies));
    m_bronzeLabel->setText(QString::number(set.bronzeCount));
    m_silverLabel->setText(QString::number(set.silverCount));
    m_goldLabel->setText(QString::number(set.goldCount));
    m_platinumLabel->setText(QString::number(set.platinumCount));
    m_lastUpdatedLabel->setText(set.lastUpdated.toString("yyyy-MM-dd hh:mm"));
}

void TrophyStatsWidget::updateOverallStats()
{
    if (!m_allTrophySets || m_allTrophySets->isEmpty()) {
        m_totalGamesLabel->setText("0");
        m_completedGamesLabel->setText("0");
        m_overallCompletionLabel->setText("0%");
        m_overallCompletionBar->setValue(0);
        m_overallTrophiesLabel->setText("0");
        m_overallBronzeLabel->setText("0");
        m_overallSilverLabel->setText("0");
        m_overallGoldLabel->setText("0");
        m_overallPlatinumLabel->setText("0");
        return;
    }
    
    int totalGames = m_allTrophySets->size();
    int completedGames = 0;
    int totalTrophies = 0;
    int unlockedTrophies = 0;
    int totalBronze = 0;
    int totalSilver = 0;
    int totalGold = 0;
    int totalPlatinum = 0;
    
    for (const TrophySet &set : *m_allTrophySets) {
        if (set.completionPercentage == 100.0) {
            completedGames++;
        }
        totalTrophies += set.totalTrophies;
        unlockedTrophies += set.unlockedTrophies;
        totalBronze += set.bronzeCount;
        totalSilver += set.silverCount;
        totalGold += set.goldCount;
        totalPlatinum += set.platinumCount;
    }
    
    double overallCompletion = totalTrophies > 0 ? 
        (double)unlockedTrophies / totalTrophies * 100.0 : 0.0;
    
    m_totalGamesLabel->setText(QString::number(totalGames));
    m_completedGamesLabel->setText(QString::number(completedGames));
    m_overallCompletionLabel->setText(QString("%1%").arg(overallCompletion, 0, 'f', 1));
    m_overallCompletionBar->setValue(static_cast<int>(overallCompletion));
    m_overallTrophiesLabel->setText(QString("%1 / %2").arg(unlockedTrophies).arg(totalTrophies));
    m_overallBronzeLabel->setText(QString::number(totalBronze));
    m_overallSilverLabel->setText(QString::number(totalSilver));
    m_overallGoldLabel->setText(QString::number(totalGold));
    m_overallPlatinumLabel->setText(QString::number(totalPlatinum));
}

#include "trophy_window.moc"
