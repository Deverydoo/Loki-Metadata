#include "MetaEditor.h"
#include "C2pa.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDirListing>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <climits>

using namespace meta;

// --- MetaFile ----------------------------------------------------------------

std::optional<QString> MetaFile::original(const QString& key) const
{
    for (const auto& [k, v] : tags)
        if (k == key) return v;
    return std::nullopt;
}

std::optional<QString> MetaFile::value(const QString& key) const
{
    if (auto it = pending.find(key); it != pending.end()) return it.value();
    return original(key);
}

static void sortKeys(QStringList& keys)
{
    std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
        const int oa = fieldOrder(a), ob = fieldOrder(b);
        if (oa != ob) return oa < ob;
        return fieldLabel(a).compare(fieldLabel(b), Qt::CaseInsensitive) < 0;
    });
}

QStringList MetaFile::keys() const
{
    QStringList out;
    for (const auto& [k, v] : tags)
        if (value(k).has_value()) out.append(k);
    for (auto it = pending.begin(); it != pending.end(); ++it)
        if (it.value().has_value() && !out.contains(it.key())) out.append(it.key());
    sortKeys(out);
    return out;
}

// Whether an edit to @p key goes to this file.
static bool holds(const MetaFile& f, const QString& key) { return f.original(key).has_value() || canHold(f.path, key); }

static QString kindName(Kind k)
{
    switch (k) {
    case Kind::Audio: return "audio";
    case Kind::Video: return "video";
    case Kind::Image: return "image";
    case Kind::Document: return "document";
    case Kind::Model: return "model";
    default: return "unknown";
    }
}

static QString stateName(MetaFile::State s)
{
    switch (s) {
    case MetaFile::Loading: return "loading";
    case MetaFile::Ready: return "ready";
    case MetaFile::Failed: return "failed";
    case MetaFile::Saving: return "saving";
    }
    return {};
}

// --- FileListModel -----------------------------------------------------------

FileListModel::FileListModel(MetaEditor* editor) : editor_(editor) {}

int FileListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : editor_->count(); }

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= editor_->count()) return {};
    const MetaFile& f = editor_->list_[index.row()];
    switch (role) {
    case NameRole: return f.name;
    case FolderRole: return f.folder;
    case PathRole: return f.path;
    case BadgeRole: return f.badge;
    case KindRole: return kindName(f.kind);
    case SelectedRole: return f.selected;
    case ModifiedRole: return f.modified();
    case StateRole: return stateName(f.state);
    case ErrorRole: return f.error;
    case WarningRole: return f.warning;
    case CredentialsRole: return !f.hasCredentials() ? QString() : f.credentials.value("ai").toBool() ? "ai" : "cr";
    }
    return {};
}

QHash<int, QByteArray> FileListModel::roleNames() const
{
    return {{NameRole, "name"},         {FolderRole, "folder"},     {PathRole, "path"},   {BadgeRole, "badge"},
            {KindRole, "kind"},         {SelectedRole, "selected"}, {ModifiedRole, "modified"},
            {StateRole, "state"},       {ErrorRole, "error"},       {WarningRole, "warning"},
            {CredentialsRole, "credentials"}};
}

void FileListModel::rowChanged(int row)
{
    if (row >= 0 && row < rowCount()) emit dataChanged(index(row), index(row));
}

void FileListModel::allChanged()
{
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
}

// --- FieldListModel ----------------------------------------------------------

FieldListModel::FieldListModel(MetaEditor* editor) : editor_(editor) {}

int FieldListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(rows_.size()); }

QVariant FieldListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rows_.size()) return {};
    const Row& r = rows_[index.row()];
    switch (role) {
    case KeyRole: return r.key;
    case LabelRole: return r.label;
    case ValueRole: return r.value;
    case MixedRole: return r.mixed;
    case EditedRole: return r.edited;
    case MultilineRole: return r.multiline;
    case AppliesToRole: return r.appliesTo;
    case TotalRole: return r.total;
    }
    return {};
}

QHash<int, QByteArray> FieldListModel::roleNames() const
{
    return {{KeyRole, "key"},     {LabelRole, "label"},   {ValueRole, "value"},
            {MixedRole, "mixed"}, {EditedRole, "edited"}, {MultilineRole, "multiline"},
            {AppliesToRole, "appliesTo"}, {TotalRole, "total"}};
}

FieldListModel::Row FieldListModel::compute(const QString& key) const { return compute(key, editor_->readySelection()); }

FieldListModel::Row FieldListModel::compute(const QString& key, const QList<int>& sel) const
{
    Row r{key, fieldLabel(key), {}, false, false, fieldMultiline(key)};
    bool first = true;
    r.total = int(sel.size());
    QHash<QString, bool> typeHolds;   // per extension: asked once, not once per file
    for (int i : sel) {
        const MetaFile& f = editor_->list_[i];
        auto known = typeHolds.constFind(f.ext);
        if (known == typeHolds.constEnd()) known = typeHolds.insert(f.ext, canHold(f.path, key));
        if (!known.value() && !f.original(key)) continue;   // not its kind of field: it doesn't count
        ++r.appliesTo;
        const QString v = f.value(key).value_or(QString());
        if (first) r.value = v;
        else if (v != r.value) r.mixed = true;
        first = false;
        if (f.pending.contains(key)) r.edited = true;
    }
    if (r.mixed) r.value.clear();
    return r;
}

void FieldListModel::rebuild()
{
    beginResetModel();
    rows_.clear();
    const QList<int> sel = editor_->readySelection();
    if (!sel.isEmpty()) {
        // Every field the selected files' formats hold (empty or not), any
        // other field one of them has, and any custom one just added.
        QSet<QString> keySet, typesSeen;
        for (int i : sel) {
            const MetaFile& f = editor_->list_[i];
            if (!typesSeen.contains(f.ext)) {   // the fields of each type, once
            typesSeen.insert(f.ext);
            for (const QString& k : fieldsFor(f.path)) keySet.insert(k);
        }
            for (const auto& [k, v] : f.tags)
                if (f.value(k).has_value()) keySet.insert(k);
            for (auto it = f.pending.begin(); it != f.pending.end(); ++it)
                if (it.value().has_value()) keySet.insert(it.key());
        }
        for (const QString& k : editor_->extraKeys_) keySet.insert(k);
        QStringList keys(keySet.begin(), keySet.end());
        sortKeys(keys);
        for (const QString& k : keys) rows_.append(compute(k, sel));
    }
    endResetModel();
}

void FieldListModel::refresh(const QString& key)
{
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i].key == key) {
            rows_[i] = compute(key);
            emit dataChanged(index(i), index(i));
        }
}

// --- MetaEditor --------------------------------------------------------------

MetaEditor::MetaEditor(QObject* parent)
    : QObject(parent), files_(this), fields_(this), pool_(new QThreadPool(this)), applyPool_(new QThreadPool(this))
{
    pool_->setMaxThreadCount(std::max(2, QThread::idealThreadCount() / 2));
    applyPool_->setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 4, 1, 4));
}

MetaEditor::~MetaEditor()
{
    pool_->clear();
    applyPool_->clear();
    pool_->waitForDone();
    applyPool_->waitForDone();
}

int MetaEditor::indexOf(const QString& path) const { return rowOf_.value(path.toLower(), -1); }

void MetaEditor::rebuildIndex()
{
    rowOf_.clear();
    rowOf_.reserve(list_.size());
    for (int i = 0; i < list_.size(); ++i) rowOf_.insert(list_[i].path.toLower(), i);
}

QList<int> MetaEditor::selectedRows() const
{
    QList<int> out;
    for (int i = 0; i < list_.size(); ++i)
        if (list_[i].selected) out.append(i);
    return out;
}

QList<int> MetaEditor::readySelection() const
{
    QList<int> out;
    for (int i = 0; i < list_.size(); ++i)
        if (list_[i].selected && list_[i].state == MetaFile::Ready) out.append(i);
    return out;
}

int MetaEditor::selectedCount() const { return int(selectedRows().size()); }

int MetaEditor::modifiedCount() const
{
    return int(std::count_if(list_.begin(), list_.end(), [](const MetaFile& f) { return f.modified(); }));
}

int MetaEditor::loadingCount() const { return loading_; }

QVariantMap MetaEditor::current() const
{
    const QList<int> sel = selectedRows();
    if (sel.size() != 1) return {};
    const MetaFile& f = list_[sel.first()];
    QVariantList info;
    for (const auto& [label, text] : f.info) info.append(QVariantMap{{"label", label}, {"text", text}});
    return {{"name", f.name},        {"path", f.path},   {"folder", f.folder},          {"badge", f.badge},
            {"kind", kindName(f.kind)}, {"info", info},   {"state", stateName(f.state)}, {"error", f.error},
            {"warning", f.warning},   {"credentials", f.credentials}, {"dropCredentials", f.dropCredentials}};
}

int MetaEditor::credentialsCount() const
{
    const QList<int> sel = readySelection();
    return int(std::count_if(sel.begin(), sel.end(), [&](int i) { return list_[i].hasCredentials(); }));
}

int MetaEditor::credentialsDropping() const
{
    const QList<int> sel = readySelection();
    return int(std::count_if(sel.begin(), sel.end(), [&](int i) { return list_[i].hasCredentials() && list_[i].dropCredentials; }));
}

QString MetaEditor::selectionSummary() const
{
    const QList<int> sel = selectedRows();
    if (sel.size() < 2) return {};
    QMap<QString, int> byBadge;
    for (int i : sel) ++byBadge[list_[i].badge];
    QStringList parts;
    for (auto it = byBadge.begin(); it != byBadge.end(); ++it) parts.append(QString("%1 %2").arg(it.value()).arg(it.key()));
    return parts.join(", ");
}

QVariantList MetaEditor::addableFields() const
{
    const QList<int> sel = readySelection();
    if (sel.isEmpty()) return {};
    // Fields any selected file's format can hold, not already shown. (Each
    // goes only to the files that can hold it.)
    QStringList keys;
    for (int i : sel)
        for (const QString& k : fieldsFor(list_[i].path))
            if (!keys.contains(k)) keys.append(k);
    QSet<QString> shown;
    for (const auto& row : fields_.rows_) shown.insert(row.key);
    QVariantList out;
    sortKeys(keys);
    for (const QString& k : keys)
        if (!shown.contains(k)) out.append(QVariantMap{{"key", k}, {"label", fieldLabel(k)}});
    return out;
}

void MetaEditor::say(const QString& text, const QString& kind)
{
    message_ = text;
    messageKind_ = kind;
    emit messageChanged();
}

// --- Adding and loading ----------------------------------------------------------

// What adding some paths would bring: the files, ready to insert. Pure, so
// a big folder can be searched on another thread.
struct MetaEditor::Scan {
    QList<MetaFile> files;   // sorted by path, duplicates and all
    int skipped = 0;         // named files of a type it can't edit
    bool deeper = false;     // a folder with nothing at its top but files below
};

static MetaEditor::Scan scanPaths(const QStringList& paths, bool includeSubfolders)
{
    MetaEditor::Scan scan;
    QStringList found;
    const QStringList exts = supportedExtensions();
    const QSet<QString> wanted(exts.begin(), exts.end());
    // A file worth listing: a supported type, and not one of the "._" files
    // macOS leaves in zip archives (resource forks, never the real thing).
    auto wantedName = [&](const QString& name) {
        if (name.startsWith("._")) return false;
        const qsizetype dot = name.lastIndexOf('.');
        return dot > 0 && wanted.contains(name.mid(dot + 1).toLower());
    };
    // QDirListing: no extra disk lookup per file, which matters over 100,000.
    using F = QDirListing::IteratorFlag;
    auto list = [&](const QString& dir, bool deep, bool firstOnly) {
        QStringList out;
        for (const auto& entry : QDirListing(dir, deep ? F::FilesOnly | F::Recursive : F::FilesOnly)) {
            if (!wantedName(entry.fileName())) continue;
            out.append(entry.filePath());
            if (firstOnly) break;
        }
        return out;
    };
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            const QStringList here = list(fi.absoluteFilePath(), includeSubfolders, false);
            found += here;
            if (!includeSubfolders && here.isEmpty()) scan.deeper = scan.deeper || !list(fi.absoluteFilePath(), true, true).isEmpty();
        } else if (isSupported(p)) {
            found.append(QDir::fromNativeSeparators(fi.absoluteFilePath()));
        } else if (fi.exists()) {
            ++scan.skipped;
        }
    }
    std::sort(found.begin(), found.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    scan.files.reserve(found.size());
    for (const QString& p : found) {
        if (p.contains(".runa-tmp.")) continue;
        MetaFile f;
        f.path = p;
        const qsizetype slash = p.lastIndexOf('/');
        f.name = p.mid(slash + 1);
        f.folder = QDir::toNativeSeparators(p.left(slash));
        f.badge = formatBadge(p);
        f.kind = kindOf(p);
        f.ext = f.name.mid(f.name.lastIndexOf('.') + 1).toLower();
        scan.files.append(std::move(f));
    }
    return scan;
}

void MetaEditor::addUrls(const QVariantList& urls)
{
    QStringList paths;
    bool folders = false;
    for (const QVariant& u : urls) {
        const QUrl url = u.toUrl();
        paths.append(url.isLocalFile() ? url.toLocalFile() : u.toString());
        folders = folders || QFileInfo(paths.last()).isDir();
    }
    if (!folders) {   // files named one by one: nothing to search
        addPaths(paths);
        return;
    }
    // Folders are searched on another thread: a big tree takes seconds, and
    // the window keeps going meanwhile.
    ++scanning_;
    emit scanningChanged();
    say("Looking for files...");
    QPointer<MetaEditor> self(this);
    const bool deep = includeSubfolders_;
    QThread* worker = QThread::create([self, paths, deep] {
        Scan scan = scanPaths(paths, deep);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, paths, scan = std::move(scan)]() mutable {
            if (!self) return;
            --self->scanning_;
            emit self->scanningChanged();
            self->addScanned(paths, std::move(scan));
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

bool MetaEditor::waitForScans(int ms)
{
    QElapsedTimer t;
    t.start();
    while (scanning_ > 0 && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return scanning_ == 0;
}

void MetaEditor::setIncludeSubfolders(bool on)
{
    if (on == includeSubfolders_) return;
    includeSubfolders_ = on;
    emit includeSubfoldersChanged();
}

void MetaEditor::addPaths(const QStringList& paths) { addScanned(paths, scanPaths(paths, includeSubfolders_)); }

void MetaEditor::addScanned(const QStringList& paths, Scan scan)
{
    const int skipped = scan.skipped;
    const bool deeper = scan.deeper;
    const bool foundAny = !scan.files.isEmpty();
    // Remembered for the next dialog: the folder added, or the files' folder.
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        if (fi.isDir()) setLastFolder(fi.absoluteFilePath());
        else if (fi.isFile()) setLastFolder(fi.absolutePath());
    }

    QList<MetaFile> fresh;
    QSet<QString> seen;
    for (MetaFile& f : scan.files) {
        const QString key = f.path.toLower();
        if (rowOf_.contains(key) || seen.contains(key)) continue;
        seen.insert(key);
        fresh.append(std::move(f));
    }
    const int added = int(fresh.size());
    if (added) {
        const int first = int(list_.size());
        files_.beginInsertRows({}, first, first + added - 1);
        list_.reserve(first + added);
        for (MetaFile& f : fresh) {
            rowOf_.insert(f.path.toLower(), int(list_.size()));
            list_.append(std::move(f));
        }
        files_.endInsertRows();
        loading_ += added;
        for (int i = first; i < list_.size(); ++i) startRead(list_[i].path);
        emit countChanged();
    }
    if (added && list_.size() == added) {   // the first files: show the first one
        list_[0].selected = true;
        anchor_ = current_ = 0;
        files_.rowChanged(0);
        selectionDidChange();
    }
    if (!added && !foundAny && deeper)
        say("Nothing to add in that folder itself, but its subfolders have files. Tick Include subfolders to add them.",
            "warning");
    else if (!added && !foundAny && (skipped || !paths.isEmpty()))
        say("Nothing to add: these aren't files it can edit (music, video, pictures, documents or FBX models).", "warning");
    else if (!added && foundAny)
        say("Those files are already in the list.");
    else if (skipped)
        say(QString("Added %1. Skipped %2 that aren't a supported type.").arg(added).arg(skipped));
    else if (added)
        say(added == 1 ? QString("Added 1 file.") : QString("Added %1 files.").arg(added));
}

void MetaEditor::setLastFolder(const QString& folder)
{
    const QString f = QDir::fromNativeSeparators(folder);
    if (f == lastFolder_) return;
    lastFolder_ = f;
    emit lastFolderChanged();
}

// Nothing (the dialog's own default) when the folder is gone.
QUrl MetaEditor::lastFolderUrl() const
{
    return !lastFolder_.isEmpty() && QDir(lastFolder_).exists() ? QUrl::fromLocalFile(lastFolder_) : QUrl();
}

QUrl MetaEditor::lastFolderParentUrl() const
{
    if (lastFolderUrl().isEmpty()) return {};
    QDir up(lastFolder_);
    return up.cdUp() ? QUrl::fromLocalFile(up.absolutePath()) : QUrl::fromLocalFile(lastFolder_);
}

void MetaEditor::startRead(const QString& path)
{
    // Workers can't outlive the editor (its destructor waits for them).
    pool_->start([this, path] {
        ReadResult r = meta::read(path);
        QMutexLocker lock(&readsMutex_);
        reads_.append({path, std::move(r)});
        if (!takeQueued_) {
            takeQueued_ = true;
            QMetaObject::invokeMethod(this, &MetaEditor::takeReads, Qt::QueuedConnection);
        }
    });
}

void MetaEditor::takeReads()
{
    QList<QPair<QString, ReadResult>> batch;
    {
        QMutexLocker lock(&readsMutex_);
        batch.swap(reads_);
        takeQueued_ = false;
    }
    int lo = INT_MAX, hi = -1;
    bool shown = false;
    for (auto& [path, r] : batch) {
        const int i = indexOf(path);
        if (i < 0) continue;   // removed meanwhile
        MetaFile& f = list_[i];
        if (f.state == MetaFile::Saving) continue;
        if (f.state == MetaFile::Loading) --loading_;
        f.state = r.ok ? MetaFile::Ready : MetaFile::Failed;
        f.error = r.ok ? QString() : r.error;
        f.tags = std::move(r.tags);
        f.info = std::move(r.info);
        f.credentials = std::move(r.credentials);
        lo = std::min(lo, i);
        hi = std::max(hi, i);
        shown = shown || f.selected;
    }
    if (hi < 0) return;
    emit files_.dataChanged(files_.index(lo), files_.index(hi));
    emit countChanged();   // loadingCount
    if (shown) selectionDidChange();
}

// The record's data hash means reading the whole file, so it is checked for
// the one file on show, in the background, ahead of any reads still queued.
void MetaEditor::checkShownCredentials()
{
    const QList<int> sel = selectedRows();
    if (sel.size() != 1) return;
    const MetaFile& f = list_[sel.first()];
    if (f.state != MetaFile::Ready || !f.hasCredentials() || !f.credentials.value("hashable").toBool()
        || !f.credentials.value("match").toString().isEmpty() || checking_.contains(f.path))
        return;
    checking_.insert(f.path);
    const QString path = f.path;
    pool_->start([this, path] {
        const QVariantMap checked = c2pa::read(path, true);
        QMetaObject::invokeMethod(this, [this, path, checked] {
            checking_.remove(path);
            const int i = indexOf(path);
            if (i < 0 || list_[i].state != MetaFile::Ready || !list_[i].hasCredentials()) return;
            list_[i].credentials = checked;
            if (list_[i].selected) emit currentChanged();
        }, Qt::QueuedConnection);
    }, 1);
}

bool MetaEditor::waitForLoads(int ms)
{
    QElapsedTimer t;
    t.start();
    while (loadingCount() > 0 && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return loadingCount() == 0;
}

// --- Selection -------------------------------------------------------------------

void MetaEditor::selectionDidChange()
{
    fields_.rebuild();
    emit selectionChanged();
    emit currentChanged();
    checkShownCredentials();
}

void MetaEditor::click(int row, int modifiers)
{
    if (row < 0 || row >= list_.size()) return;
    const bool ctrl = modifiers & Qt::ControlModifier, shift = modifiers & Qt::ShiftModifier;
    extraKeys_.clear();
    if (shift && anchor_ >= 0) {
        const int a = std::min(anchor_, row), b = std::max(anchor_, row);
        for (int i = 0; i < list_.size(); ++i) {
            const bool in = i >= a && i <= b;
            list_[i].selected = in || (ctrl && list_[i].selected);   // Ctrl+Shift adds the range
        }
    } else if (ctrl) {
        list_[row].selected = !list_[row].selected;
        anchor_ = row;
    } else {
        for (MetaFile& f : list_) f.selected = false;
        list_[row].selected = true;
        anchor_ = row;
    }
    current_ = row;
    files_.allChanged();
    selectionDidChange();
}

void MetaEditor::selectAll()
{
    extraKeys_.clear();
    for (MetaFile& f : list_) f.selected = true;
    files_.allChanged();
    selectionDidChange();
}

void MetaEditor::clearSelection()
{
    extraKeys_.clear();
    for (MetaFile& f : list_) f.selected = false;
    files_.allChanged();
    selectionDidChange();
}

void MetaEditor::step(int delta, bool extend)
{
    if (list_.isEmpty()) return;
    const int row = std::clamp(current_ < 0 ? 0 : current_ + delta, 0, int(list_.size()) - 1);
    click(row, extend ? Qt::ShiftModifier : 0);
}

bool MetaEditor::selectionHasChanges() const
{
    for (int i : selectedRows())
        if (list_[i].modified()) return true;
    return false;
}

void MetaEditor::removeSelected()
{
    if (busy_) return;
    const QList<int> sel = selectedRows();
    if (sel.isEmpty()) return;
    // Back to front, a run of neighbours at a time (Select all, Delete: one).
    for (int k = int(sel.size()) - 1; k >= 0;) {
        int start = k;
        while (start > 0 && sel[start - 1] == sel[start] - 1) --start;
        const int from = sel[start], to = sel[k];
        for (int i = from; i <= to; ++i)
            if (list_[i].state == MetaFile::Loading) --loading_;
        files_.beginRemoveRows({}, from, to);
        list_.remove(from, to - from + 1);
        files_.endRemoveRows();
        k = start - 1;
    }
    rebuildIndex();
    anchor_ = current_ = -1;
    if (!list_.isEmpty()) {   // select the file that took the first one's place
        const int next = std::min(sel.first(), int(list_.size()) - 1);
        list_[next].selected = true;
        anchor_ = current_ = next;
        files_.rowChanged(next);
    }
    extraKeys_.clear();
    emit countChanged();
    emit modifiedChanged();
    selectionDidChange();
    say(sel.size() == 1 ? QString("Removed 1 file from the list.") : QString("Removed %1 files from the list.").arg(sel.size()));
}

// --- Editing -------------------------------------------------------------------------

void MetaEditor::setField(const QString& key, const QString& value)
{
    if (busy_ || key.isEmpty()) return;
    for (int i : readySelection()) {
        MetaFile& f = list_[i];
        if (!holds(f, key)) continue;
        if (f.original(key).value_or(QString()) == value) f.pending.remove(key);
        else f.pending.insert(key, value);
        files_.rowChanged(i);
    }
    fields_.refresh(key);
    emit modifiedChanged();
}

void MetaEditor::revertField(const QString& key)
{
    if (busy_) return;
    for (int i : readySelection()) {
        list_[i].pending.remove(key);
        files_.rowChanged(i);
    }
    fields_.rebuild();
    emit modifiedChanged();
    emit selectionChanged();
}

void MetaEditor::removeField(const QString& key)
{
    if (busy_) return;
    for (int i : readySelection()) {
        MetaFile& f = list_[i];
        if (f.original(key)) f.pending.insert(key, std::nullopt);
        else f.pending.remove(key);
        files_.rowChanged(i);
    }
    extraKeys_.removeAll(key);
    fields_.rebuild();
    emit modifiedChanged();
    emit selectionChanged();
}

void MetaEditor::addField(const QString& rawKey)
{
    const QString key = rawKey.trimmed();
    const QList<int> sel = readySelection();
    if (busy_ || key.isEmpty() || sel.isEmpty()) return;
    // Only a field some selected file can hold; otherwise say so, add nothing.
    if (std::none_of(sel.begin(), sel.end(), [&](int i) { return holds(list_[i], key); })) {
        QStringList kinds;
        for (int i : sel)
            if (!kinds.contains(list_[i].badge)) kinds.append(list_[i].badge);
        say(QString("%1 can't hold \"%2\", so it wasn't added.")
                .arg(sel.size() == 1 ? "This " + kinds.first() + " file" : kinds.join(" and ") + " files", fieldLabel(key)),
            "warning");
        return;
    }
    if (!extraKeys_.contains(key)) extraKeys_.append(key);
    fields_.rebuild();
    emit selectionChanged();   // addableFields
}

void MetaEditor::removeCredentials()
{
    if (busy_) return;
    int n = 0;
    for (int i : readySelection()) {
        MetaFile& f = list_[i];
        if (!f.hasCredentials() || f.dropCredentials) continue;
        f.dropCredentials = true;
        files_.rowChanged(i);
        ++n;
    }
    if (!n) return;
    emit modifiedChanged();
    emit currentChanged();
}

void MetaEditor::keepCredentials()
{
    if (busy_) return;
    for (int i : readySelection()) {
        list_[i].dropCredentials = false;
        files_.rowChanged(i);
    }
    emit modifiedChanged();
    emit currentChanged();
}

void MetaEditor::revertAll()
{
    if (busy_) return;
    for (MetaFile& f : list_) {
        f.pending.clear();
        f.dropCredentials = false;
    }
    extraKeys_.clear();
    files_.allChanged();
    emit modifiedChanged();
    selectionDidChange();
    say("Changes discarded. The files were not touched.");
}

// --- Apply ---------------------------------------------------------------------------

void MetaEditor::apply()
{
    if (busy_) return;
    struct Job {
        QString path;
        QHash<QString, QString> changes;
        bool dropCredentials = false;
    };
    QList<Job> jobs;
    for (int i = 0; i < list_.size(); ++i) {
        MetaFile& f = list_[i];
        if (!f.modified() || f.state != MetaFile::Ready) continue;
        Job j{f.path, {}, f.dropCredentials};
        for (auto it = f.pending.begin(); it != f.pending.end(); ++it) j.changes.insert(it.key(), it.value().value_or(QString()));
        jobs.append(j);
        f.state = MetaFile::Saving;
        files_.rowChanged(i);
    }
    if (jobs.isEmpty()) {
        say("Nothing to apply.");
        return;
    }
    busy_ = true;
    applyTotal_ = int(jobs.size());
    applyDone_ = applyFailed_ = applyPartial_ = 0;
    progress_ = 0;
    emit busyChanged();
    emit progressChanged();
    say(applyTotal_ == 1 ? QString("Saving 1 file...") : QString("Saving %1 files...").arg(applyTotal_));

    // A few files at a time: most of a save is small reads and writes, which
    // an SSD does side by side. (Big videos are edited in place, see Mp4Meta.)
    QPointer<MetaEditor> self(this);
    for (const Job& j : jobs)
        applyPool_->start([self, j] {
            const WriteResult w = meta::write(j.path, j.changes, j.dropCredentials);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, path = j.path, w] {
                if (self) self->saved(path, w);
            }, Qt::QueuedConnection);
        });
}

void MetaEditor::saved(const QString& path, const WriteResult& w)
{
    ++applyDone_;
    progress_ = applyTotal_ ? double(applyDone_) / applyTotal_ : 1;
    emit progressChanged();
    const int i = indexOf(path);
    if (i >= 0) {
        MetaFile& f = list_[i];
        f.state = MetaFile::Ready;
        if (w.ok) {
            f.tags = w.after.tags;
            f.info = w.after.info;
            f.credentials = w.after.credentials;
            f.dropCredentials = false;
            f.pending.clear();
            f.error.clear();
            QStringList labels;
            for (const QString& k : w.notStored) labels.append(fieldLabel(k));
            f.warning = labels.isEmpty() ? QString()
                                         : QString("%1 can't hold %2, so %3 not saved.")
                                               .arg(f.badge, labels.join(", "), labels.size() == 1 ? "it was" : "they were");
            if (!labels.isEmpty()) ++applyPartial_;
        } else {
            f.error = w.error;
            ++applyFailed_;
        }
        files_.rowChanged(i);
    }
    if (applyDone_ >= applyTotal_) finishApply();
}

void MetaEditor::finishApply()
{
    busy_ = false;
    extraKeys_.clear();
    emit busyChanged();
    emit modifiedChanged();
    selectionDidChange();
    const int ok = applyTotal_ - applyFailed_;
    QString text = ok == 1 ? QString("Saved 1 file.") : QString("Saved %1 files.").arg(ok);
    QString kind = "success";
    if (applyPartial_) {
        text += QString(" %1 couldn't keep every field; select %2 to see which.")
                    .arg(applyPartial_)
                    .arg(applyPartial_ == 1 ? "it" : "them");
        kind = "warning";
    }
    if (applyFailed_) {
        text += QString(" %1 couldn't be saved; %2 marked in red and %3 changes are kept.")
                    .arg(applyFailed_)
                    .arg(applyFailed_ == 1 ? "it's" : "they're")
                    .arg(applyFailed_ == 1 ? "its" : "their");
        kind = "error";
    }
    say(text, kind);
}

bool MetaEditor::waitForApply(int ms)
{
    QElapsedTimer t;
    t.start();
    while (busy_ && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return !busy_;
}

void MetaEditor::showInFolder(const QString& path) const
{
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
}
