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
#include <QObject>
#include <QUrl>
#include <QVariant>

#include <optional>

class QThreadPool;

struct MetaFile {
    enum State { Loading, Ready, Failed, Saving };

    QString path, name, folder, badge;
    meta::Kind kind = meta::Kind::Unknown;
    State state = Loading;
    QString error;     // couldn't be read, or the last save failed
    QString warning;   // saved, but some fields weren't kept
    meta::Pairs tags;  // as on disk
    meta::Pairs info;
    QHash<QString, std::optional<QString>> pending;   // waiting for Apply; nullopt removes
    bool selected = false;

    std::optional<QString> original(const QString& key) const;
    /// With the pending change, if any.
    std::optional<QString> value(const QString& key) const;
    /// The fields it has, pending changes included, in display order.
    QStringList keys() const;
    bool modified() const { return !pending.isEmpty(); }
};

class MetaEditor;

class FileListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { NameRole = Qt::UserRole + 1, FolderRole, PathRole, BadgeRole, KindRole, SelectedRole, ModifiedRole,
                StateRole, ErrorRole, WarningRole };
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
    Q_PROPERTY(int loadingCount READ loadingCount NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
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
    int loadingCount() const;
    bool busy() const { return busy_; }
    double progress() const { return progress_; }
    QString message() const { return message_; }
    QString messageKind() const { return messageKind_; }

    /// Files and folders (folders are searched, subfolders too). Unsupported
    /// files and ones already in the list are skipped.
    Q_INVOKABLE void addUrls(const QVariantList& urls);
    void addPaths(const QStringList& paths);

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
    void progressChanged();
    void messageChanged();

private:
    friend class FileListModel;
    friend class FieldListModel;

    int indexOf(const QString& path) const;
    QList<int> selectedRows() const;   // ready ones only, when readyOnly
    QList<int> readySelection() const;
    void selectionDidChange();
    void say(const QString& text, const QString& kind = "info");
    void loaded(const QString& path, const meta::ReadResult& r);
    void saved(const QString& path, const meta::WriteResult& w);
    void finishApply();

    QList<MetaFile> list_;
    FileListModel files_;
    FieldListModel fields_;
    QStringList extraKeys_;   // fields added with "Add field" for the current selection
    int anchor_ = -1, current_ = -1;
    bool busy_ = false;
    double progress_ = 0;
    int applyTotal_ = 0, applyDone_ = 0, applyFailed_ = 0, applyPartial_ = 0;
    QString message_, messageKind_ = "info";
    QThreadPool* pool_;
};
