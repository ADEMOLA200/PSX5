#pragma once

#include <QAbstractTableModel>
#include <QPixmap>
#include <QDir>
#include <QFileInfo>

struct GameInfo {
    QString title;
    QString serial;
    QString path;
    QString firmware;
    QPixmap coverArt;
    bool isValid;
    
    GameInfo() : isValid(false) {}
};

class GameListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        ColumnCover = 0,
        ColumnTitle,
        ColumnSerial,
        ColumnFirmware,
        ColumnPath,
        ColumnCount
    };

    explicit GameListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addGame(const QString &path);
    void removeGame(int row);
    void refreshGameList();
    void scanDirectory(const QString &directory);

private:
    GameInfo parseGameInfo(const QString &path);
    QPixmap loadCoverArt(const QString &gamePath);
    
    QList<GameInfo> m_games;
    QDir m_gamesDirectory;
};
