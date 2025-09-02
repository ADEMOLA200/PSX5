#include "game_list_model.h"
#include <QStandardPaths>
#include <QDirIterator>
#include <QImageReader>
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QHash>
#include <QPainter>
#include <QPen>
#include <QFont>

GameListModel::GameListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    // Set default games directory
    QString gamesPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/PSX5/Games";
    m_gamesDirectory.setPath(gamesPath);
    
    refreshGameList();
}

int GameListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_games.size();
}

int GameListModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return ColumnCount;
}

QVariant GameListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_games.size())
        return QVariant();

    const GameInfo &game = m_games.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColumnCover:
            return game.coverArt.isNull() ? QVariant() : game.coverArt;
        case ColumnTitle:
            return game.title;
        case ColumnSerial:
            return game.serial;
        case ColumnFirmware:
            return game.firmware;
        case ColumnPath:
            return game.path;
        default:
            return QVariant();
        }
        
    case Qt::ToolTipRole:
        return QString("Title: %1\nSerial: %2\nPath: %3")
               .arg(game.title, game.serial, game.path);
               
    default:
        return QVariant();
    }
}

QVariant GameListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (section) {
    case ColumnCover:
        return "Cover";
    case ColumnTitle:
        return "Title";
    case ColumnSerial:
        return "Serial";
    case ColumnFirmware:
        return "Firmware";
    case ColumnPath:
        return "Path";
    default:
        return QVariant();
    }
}

void GameListModel::addGame(const QString &path)
{
    GameInfo game = parseGameInfo(path);
    if (game.isValid) {
        beginInsertRows(QModelIndex(), m_games.size(), m_games.size());
        m_games.append(game);
        endInsertRows();
    }
}

void GameListModel::removeGame(int row)
{
    if (row >= 0 && row < m_games.size()) {
        beginRemoveRows(QModelIndex(), row, row);
        m_games.removeAt(row);
        endRemoveRows();
    }
}

void GameListModel::refreshGameList()
{
    beginResetModel();
    m_games.clear();
    
    if (m_gamesDirectory.exists()) {
        scanDirectory(m_gamesDirectory.absolutePath());
    }
    
    endResetModel();
}

void GameListModel::scanDirectory(const QString &directory)
{
    QDirIterator it(directory, QStringList() << "*.elf" << "*.pkg", 
                    QDir::Files, QDirIterator::Subdirectories);
    
    while (it.hasNext()) {
        QString filePath = it.next();
        GameInfo game = parseGameInfo(filePath);
        if (game.isValid) {
            m_games.append(game);
        }
    }
}

GameInfo GameListModel::parseGameInfo(const QString &path)
{
    GameInfo game;
    QFileInfo fileInfo(path);
    
    if (!fileInfo.exists()) {
        return game;
    }
    
    game.path = path;
    game.isValid = true;
    
    if (path.endsWith(".pkg")) {
        game = parsePKGFile(path);
    } else if (path.endsWith(".elf")) {
        game = parseELFFile(path);
    } else {
        // Fallback for unknown formats
        game.title = fileInfo.baseName();
        game.serial = "UNKNOWN";
        game.firmware = "Unknown";
    }
    
    game.coverArt = loadCoverArt(path);
    return game;
}

GameInfo GameListModel::parsePKGFile(const QString &path)
{
    GameInfo game;
    game.path = path;
    
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return game;
    }
    
    QByteArray header = file.read(0x80);
    if (header.size() < 0x80) {
        return game;
    }
    
    if (header.left(4) != QByteArray("\x7F\x43\x4E\x54")) {
        return game;
    }
    
    // Extract content ID from offset 0x30
    QByteArray contentId = header.mid(0x30, 36);
    contentId = contentId.left(contentId.indexOf('\0'));
    
    if (contentId.size() >= 9) {
        game.serial = QString::fromLatin1(contentId.left(9));
        
        QString titleId = QString::fromLatin1(contentId);
        game.title = extractTitleFromContentId(titleId);
        
        // Determine firmware version from content type
        char contentType = contentId.at(7);
        switch (contentType) {
            case '0': game.firmware = "1.00+"; break;
            case '1': game.firmware = "4.00+"; break;
            case '2': game.firmware = "7.00+"; break;
            case '3': game.firmware = "9.00+"; break;
            default: game.firmware = "Unknown"; break;
        }
    } else {
        game.serial = "INVALID_PKG";
        game.title = QFileInfo(path).baseName();
        game.firmware = "Unknown";
    }
    
    game.isValid = true;
    return game;
}

GameInfo GameListModel::parseELFFile(const QString &path)
{
    GameInfo game;
    game.path = path;
    
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return game;
    }
    
    // Read ELF header
    QByteArray header = file.read(64);
    if (header.size() < 64) {
        return game;
    }
    
    // Check ELF magic
    if (header.left(4) != QByteArray("\x7F\x45\x4C\x46")) {
        return game;
    }
    
    // Check for PS5 ELF (64-bit, little endian)
    if (header.at(4) != 2 || header.at(5) != 1) {
        return game;
    }
    
    // Extract basic info
    game.title = QFileInfo(path).baseName();
    game.serial = "ELF_" + QString::number(QFileInfo(path).size(), 16).toUpper();
    game.firmware = "Dev/Homebrew";
    game.isValid = true;
    
    // Try to find embedded title string
    file.seek(0);
    QByteArray content = file.read(1024 * 1024); // Read first 1MB
    
    // Look for common title patterns
    QRegularExpression titleRegex("([A-Za-z0-9 ]{4,50})\\x00");
    QRegularExpressionMatchIterator matches = titleRegex.globalMatch(QString::fromLatin1(content));
    
    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        QString candidate = match.captured(1).trimmed();
        
        // Filter out common false positives
        if (candidate.length() > 8 && 
            !candidate.contains(QRegularExpression("^[0-9A-F]+$")) &&
            !candidate.startsWith("lib") &&
            !candidate.contains("gcc") &&
            !candidate.contains("GNU")) {
            game.title = candidate;
            break;
        }
    }
    
    return game;
}

QString GameListModel::extractTitleFromContentId(const QString &contentId)
{
    // Map common content IDs to game titles
    static QHash<QString, QString> titleMap = {
        {"CUSA00001", "Knack"},
        {"CUSA00002", "Killzone Shadow Fall"},
        {"CUSA00003", "Assassin's Creed IV: Black Flag"},
        {"CUSA00419", "The Last of Us Remastered"},
        {"CUSA00744", "Grand Theft Auto V"},
        {"CUSA01047", "Bloodborne"},
        {"CUSA02299", "The Witcher 3: Wild Hunt"},
        {"CUSA03041", "Horizon Zero Dawn"},
        {"CUSA07408", "God of War"},
        {"CUSA08966", "Marvel's Spider-Man"},
        {"CUSA13795", "The Last of Us Part II"}
    };
    
    QString titleId = contentId.left(9);
    if (titleMap.contains(titleId)) {
        return titleMap[titleId];
    }
    
    // Generate title from content ID pattern
    if (contentId.startsWith("CUSA")) {
        return QString("Game %1").arg(titleId);
    } else if (contentId.startsWith("NPWR")) {
        return QString("Network Game %1").arg(titleId);
    }
    
    return QString("Unknown Game (%1)").arg(titleId);
}

QPixmap GameListModel::loadCoverArt(const QString &gamePath)
{
    QFileInfo gameFile(gamePath);
    QString gameDir = gameFile.absoluteDir().absolutePath();
    
    QStringList coverNames = {"cover.jpg", "cover.png", "icon0.png", "pic1.png", "icon.jpg"};
    
    for (const QString &coverName : coverNames) {
        QString coverPath = gameDir + "/" + coverName;
        if (QFile::exists(coverPath)) {
            QPixmap pixmap(coverPath);
            if (!pixmap.isNull()) {
                return pixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        }
    }
    
    QPixmap placeholder(64, 64);
    
    QString serial = QFileInfo(gamePath).baseName();
    uint hash = qHash(serial);
    
    QColor baseColor;
    baseColor.setHsv((hash % 360), 100, 180); // Varied hue, consistent saturation/value
    
    placeholder.fill(baseColor);
    
    QPainter painter(&placeholder);
    painter.setRenderHint(QPainter::Antialiasing);
    
    painter.setPen(QPen(Qt::white, 2));
    painter.drawRect(1, 1, 62, 62);
    
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 24, QFont::Bold));
    
    QString initial = serial.isEmpty() ? "?" : serial.left(1).toUpper();
    painter.drawText(placeholder.rect(), Qt::AlignCenter, initial);
    
    return placeholder;
}
