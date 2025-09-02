#pragma once

#include <QMainWindow>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QPixmap>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QProgressBar;
class QPushButton;
class QLineEdit;
class QComboBox;
class QSplitter;
class QGroupBox;
class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;
class QTextEdit;
class QTabWidget;

enum class TrophyType {
    Bronze = 0,
    Silver,
    Gold,
    Platinum
};

enum class TrophyGrade {
    Common = 0,
    Uncommon,
    Rare,
    VeryRare,
    UltraRare
};

struct Trophy {
    QString id;
    QString name;
    QString description;
    TrophyType type;
    TrophyGrade grade;
    QPixmap icon;
    bool unlocked;
    QDateTime unlockedDate;
    QString gameId;
    QString gameName;
    bool hidden;
    double progressPercentage;
    
    Trophy() : type(TrophyType::Bronze), grade(TrophyGrade::Common), 
               unlocked(false), hidden(false), progressPercentage(0.0) {}
};

struct TrophySet {
    QString gameId;
    QString gameName;
    QString gameIcon;
    QList<Trophy> trophies;
    int totalTrophies;
    int unlockedTrophies;
    int bronzeCount;
    int silverCount;
    int goldCount;
    int platinumCount;
    double completionPercentage;
    QDateTime lastUpdated;
    
    TrophySet() : totalTrophies(0), unlockedTrophies(0), 
                  bronzeCount(0), silverCount(0), goldCount(0), platinumCount(0),
                  completionPercentage(0.0) {}
};

class TrophyListWidget;
class TrophyDetailsWidget;
class TrophyStatsWidget;

class TrophyWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TrophyWindow(QWidget *parent = nullptr);
    ~TrophyWindow();

    void refreshTrophyData();
    void syncWithPSN();

private slots:
    void onGameSelectionChanged();
    void onTrophySelectionChanged();
    void onUnlockTrophy();
    void onSyncTrophies();
    void onExportTrophies();
    void onImportTrophies();
    void onFilterChanged();
    void onSearchTextChanged();
    void onShowHiddenToggled(bool show);

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void loadTrophyData();
    void saveTrophyData();
    void updateGameList();
    void updateTrophyList();
    void updateTrophyDetails();
    void updateStats();
    void filterTrophies();
    QString getTrophyDataPath();
    QPixmap getTrophyTypeIcon(TrophyType type);
    QString getTrophyTypeString(TrophyType type);
    QString getTrophyGradeString(TrophyGrade grade);
    QColor getTrophyTypeColor(TrophyType type);

    // UI Components
    QSplitter *m_mainSplitter;
    QSplitter *m_rightSplitter;
    
    // Left panel - Game list
    QTreeWidget *m_gameTree;
    QLineEdit *m_searchEdit;
    QComboBox *m_filterCombo;
    QPushButton *m_showHiddenButton;
    
    // Center panel - Trophy list
    TrophyListWidget *m_trophyList;
    
    // Right panel - Details and stats
    QTabWidget *m_rightTabs;
    TrophyDetailsWidget *m_detailsWidget;
    TrophyStatsWidget *m_statsWidget;
    
    // Status bar
    QLabel *m_statusLabel;
    QProgressBar *m_syncProgress;
    
    // Data
    QList<TrophySet> m_trophySets;
    TrophySet *m_currentTrophySet;
    Trophy *m_currentTrophy;
    
    // Settings
    bool m_showHiddenTrophies;
    QString m_currentFilter;
    QString m_searchText;
};

class TrophyListWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit TrophyListWidget(QWidget *parent = nullptr);
    
    void setTrophySet(const TrophySet *trophySet);
    void applyFilter(const QString &filter, const QString &searchText, bool showHidden);
    Trophy* getCurrentTrophy();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onUnlockTrophy();
    void onCopyTrophyInfo();

private:
    void setupColumns();
    void addTrophyItem(const Trophy &trophy);
    void updateTrophyItem(QTreeWidgetItem *item, const Trophy &trophy);
    
    const TrophySet *m_trophySet;
    QList<QTreeWidgetItem*> m_trophyItems;
};

class TrophyDetailsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrophyDetailsWidget(QWidget *parent = nullptr);
    
    void setTrophy(const Trophy *trophy);
    void clear();

private:
    void setupUI();
    
    QLabel *m_iconLabel;
    QLabel *m_nameLabel;
    QLabel *m_typeLabel;
    QLabel *m_gradeLabel;
    QTextEdit *m_descriptionEdit;
    QLabel *m_statusLabel;
    QLabel *m_unlockedDateLabel;
    QLabel *m_progressLabel;
    QProgressBar *m_progressBar;
    QPushButton *m_unlockButton;
};

class TrophyStatsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrophyStatsWidget(QWidget *parent = nullptr);
    
    void setTrophySet(const TrophySet *trophySet);
    void setOverallStats(const QList<TrophySet> &allSets);
    void clear();

private:
    void setupUI();
    void updateGameStats();
    void updateOverallStats();
    
    // Game stats
    QLabel *m_gameNameLabel;
    QLabel *m_completionLabel;
    QProgressBar *m_completionBar;
    QLabel *m_totalTrophiesLabel;
    QLabel *m_unlockedTrophiesLabel;
    QLabel *m_bronzeLabel;
    QLabel *m_silverLabel;
    QLabel *m_goldLabel;
    QLabel *m_platinumLabel;
    QLabel *m_lastUpdatedLabel;
    
    // Overall stats
    QLabel *m_totalGamesLabel;
    QLabel *m_completedGamesLabel;
    QLabel *m_overallCompletionLabel;
    QProgressBar *m_overallCompletionBar;
    QLabel *m_overallTrophiesLabel;
    QLabel *m_overallBronzeLabel;
    QLabel *m_overallSilverLabel;
    QLabel *m_overallGoldLabel;
    QLabel *m_overallPlatinumLabel;
    
    const TrophySet *m_currentTrophySet;
    const QList<TrophySet> *m_allTrophySets;
};
