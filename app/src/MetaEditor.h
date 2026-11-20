// The editor: the list of files, which are selected, and the changes waiting
// for Apply. Nothing is written to a file until apply().
//
// The fields shown are every field the selected files' formats can hold,
// filled in or not, plus any other field one of them has; with several
// selected, where the values differ the row says so. A
// change is held for each selected file whose format can hold that field,
// and the others are left alone.

#pragma once

#include "Fields.h"
#include "MetaIO.h"

#include <QAbstractListModel>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariant>

#include <optional>

class QThreadPool;

struct MetaFile {
    enum State { Loading, Ready, Failed, Saving };

    QString path, name, folder, badge, ext;   // ext: lower case
    meta::Kind kind = meta::Kind::Unknown;
    State state = Loading;
    QString error;     // couldn't be read, or the last save failed
    QString warning;   // saved, but some fields weren't kept
    meta::Pairs tags;  // as on disk
    meta::Pairs info;
    QHash<QString, std::optional<QString>> pending;   // waiting for Apply; nullopt removes
    QVariantMap credentials;        // Content Credentials, as read (C2pa.h)
    bool dropCredentials = false;   // remove them on Apply
    bool selected = false;

    std::optional<QString> original(const QString& key) const;
    /// With the pending change, if any.
    std::optional<QString> value(const QString& key) const;
    /// The fields it has, pending changes included, in display order.
    QStringList keys() const;
    bool modified() const { return !pending.isEmpty() || dropCredentials; }
    bool hasCredentials() const { return credentials.value("present").toBool(); }
};

class MetaEditor;

class FileListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { NameRole = Qt::UserRole + 1, FolderRole, PathRole, BadgeRole, KindRole, SelectedRole, ModifiedRole,
                StateRole, ErrorRole, WarningRole, CredentialsRole };
    explicit FileListModel(MetaEditor* editor);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    friend class MetaEditor;
    void rowChanged(int row);
    void allChanged();
    MetaEditor* editor_;
};

class FieldListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { KeyRole = Qt::UserRole + 1, LabelRole, ValueRole, MixedRole, EditedRole, MultilineRole, AppliesToRole,
                TotalRole };
    explicit FieldListModel(MetaEditor* editor);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    friend class MetaEditor;
    struct Row {
        QString key, label, value;
        bool mixed = false, edited = false, multiline = false;
        int appliesTo = 0, total = 0;   // selected files that can hold it, of all selected
    };
    Row compute(const QString& key, const QList<int>& sel) const;
    Row compute(const QString& key) const;
    void rebuild();
    void refresh(const QString& key);
    MetaEditor* editor_;
    QList<Row> rows_;
};

class MetaEditor : public QObject {
    Q_OBJECT
    Q_PROPERTY(FileListModel* files READ files CONSTANT)
    Q_PROPERTY(FieldListModel* fields READ fields CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString selectionSummary READ selectionSummary NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList addableFields READ addableFields NOTIFY selectionChanged)
    Q_PROPERTY(int modifiedCount READ modifiedCount NOTIFY modifiedChanged)
    // Selected files with Content Credentials, and how many of them are set to lose them.
    Q_PROPERTY(int credentialsCount READ credentialsCount NOTIFY currentChanged)
    Q_PROPERTY(int credentialsDropping READ credentialsDropping NOTIFY currentChanged)
    Q_PROPERTY(int loadingCount READ loadingCount NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)   // looking through added folders
    // Whether an added folder's subfolders are searched too (Add folder and
    // folders dropped on the window). Off by default; the app remembers it.
    Q_PROPERTY(bool includeSubfolders READ includeSubfolders WRITE setIncludeSubfolders NOTIFY includeSubfoldersChanged)
    // Where files were last added from, for the next Add files / Add folder
    // (the app remembers it). The folder's parent too: the folder picker
    // opens there, so the last folder sits among its neighbours.
    Q_PROPERTY(QString lastFolder READ lastFolder WRITE setLastFolder NOTIFY lastFolderChanged)
    Q_PROPERTY(QUrl lastFolderUrl READ lastFolderUrl NOTIFY lastFolderChanged)
    Q_PROPERTY(QUrl lastFolderParentUrl READ lastFolderParentUrl NOTIFY lastFolderChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString messageKind READ messageKind NOTIFY messageChanged)

public:
    explicit MetaEditor(QObject* parent = nullptr);
    ~MetaEditor() override;

    FileListModel* files() { return &files_; }
    FieldListModel* fields() { return &fields_; }
    const QList<MetaFile>& list() const { return list_; }

    int count() const { return int(list_.size()); }
    int selectedCount() const;
    int currentRow() const { return current_; }
    QVariantMap current() const;
    QString selectionSummary() const;
    QVariantList addableFields() const;
    int modifiedCount() const;
    int credentialsCount() const;
    int credentialsDropping() const;
    int loadingCount() const;
    bool busy() const { return busy_; }
    bool scanning() const { return scanning_ > 0; }
    struct Scan;
    bool includeSubfolders() const { return includeSubfolders_; }
    void setIncludeSubfolders(bool on);
    QString lastFolder() const { return lastFolder_; }
    void setLastFolder(const QString& folder);
    QUrl lastFolderUrl() const;
    QUrl lastFolderParentUrl() const;
    double progress() const { return progress_; }
    QString message() const { return message_; }
    QString messageKind() const { return messageKind_; }

    /// Files and folders (a folder's subfolders too, with includeSubfolders).
    /// Unsupported files and ones already in the list are skipped. addUrls
    /// searches folders on another thread (see scanning); addPaths at once.
    Q_INVOKABLE void addUrls(const QVariantList& urls);
    void addPaths(const QStringList& paths);
    bool waitForScans(int ms);   // for tests

    // Selection, as in Explorer: Ctrl toggles, Shift extends from the last click.
    Q_INVOKABLE void click(int row, int modifiers);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void step(int delta, bool extend);   // arrow keys
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE bool selectionHasChanges() const;

    // Field edits, held until apply(). They go to every selected file.
    Q_INVOKABLE void setField(const QString& key, const QString& value);
    Q_INVOKABLE void revertField(const QString& key);
    Q_INVOKABLE void removeField(const QString& key);
    Q_INVOKABLE void addField(const QString& key);

    // Content Credentials: shown, and removable from the selected files on
    // Apply. Never edited (they are signed by whoever made the file).
    Q_INVOKABLE void removeCredentials();
    Q_INVOKABLE void keepCredentials();

    Q_INVOKABLE void revertAll();
    /// Writes every pending change, in the background.
    Q_INVOKABLE void apply();
    /// Blocks until a running apply() is done (for tests).
    bool waitForApply(int ms);
    bool waitForLoads(int ms);

    Q_INVOKABLE void showInFolder(const QString& path) const;

signals:
    void countChanged();
    void selectionChanged();
    void currentChanged();
    void modifiedChanged();
    void busyChanged();
    void scanningChanged();
    void includeSubfoldersChanged();
    void lastFolderChanged();
    void progressChanged();
    void messageChanged();

private:
    friend class FileListModel;
    friend class FieldListModel;

    int indexOf(const QString& path) const;
    void rebuildIndex();
    void addScanned(const QStringList& paths, Scan scan);
    void startRead(const QString& path);
    void takeReads();           // the reads finished so far, into the list
    void checkShownCredentials();   // the hash check for the one file on show
    QList<int> selectedRows() const;   // ready ones only, when readyOnly
    QList<int> readySelection() const;
    void selectionDidChange();
    void say(const QString& text, const QString& kind = "info");
    void saved(const QString& path, const meta::WriteResult& w);
    void finishApply();

    QList<MetaFile> list_;
    FileListModel files_;
    FieldListModel fields_;
    QStringList extraKeys_;   // fields added with "Add field" for the current selection
    int anchor_ = -1, current_ = -1;
    bool busy_ = false;
    bool includeSubfolders_ = false;
    double progress_ = 0;
    int applyTotal_ = 0, applyDone_ = 0, applyFailed_ = 0, applyPartial_ = 0;
    QString message_, messageKind_ = "info";
    QThreadPool* pool_;
    QThreadPool* applyPool_;   // saves, a few at a time

    // With thousands of files: rows found by path without a search, the
    // number still loading kept as a count, and finished reads handed to the
    // UI thread in batches rather than one event (and one redraw) each.
    QHash<QString, int> rowOf_;   // lower-case path -> row
    int loading_ = 0;
    int scanning_ = 0;
    QMutex readsMutex_;
    QList<QPair<QString, meta::ReadResult>> reads_;   // finished, not yet taken (readsMutex_)
    bool takeQueued_ = false;                         // (readsMutex_)
    QSet<QString> checking_;   // files whose credentials are being checked
    QString lastFolder_;
};
