#include "MetaEditor.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QThread>
#include <QThreadPool>

#include <algorithm>

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

FieldListModel::Row FieldListModel::compute(const QString& key) const
{
    Row r{key, fieldLabel(key), {}, false, false, fieldMultiline(key)};
    bool first = true;
    const QList<int> sel = editor_->readySelection();
    r.total = int(sel.size());
    for (int i : sel) {
        const MetaFile& f = editor_->list_[i];
        if (!holds(f, key)) continue;   // not its kind of field: it doesn't count
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
        QStringList keys;
        for (int i : sel) {
            for (const QString& k : fieldsFor(editor_->list_[i].path))
                if (!keys.contains(k)) keys.append(k);
            for (const QString& k : editor_->list_[i].keys())
                if (!keys.contains(k)) keys.append(k);
        }
        for (const QString& k : editor_->extraKeys_)
            if (!keys.contains(k)) keys.append(k);
        sortKeys(keys);
        for (const QString& k : keys) rows_.append(compute(k));
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

MetaEditor::MetaEditor(QObject* parent) : QObject(parent), files_(this), fields_(this), pool_(new QThreadPool(this))
{
    pool_->setMaxThreadCount(std::max(2, QThread::idealThreadCount() / 2));
}

MetaEditor::~MetaEditor()
{
    pool_->clear();
    pool_->waitForDone();
}

int MetaEditor::indexOf(const QString& path) const
{
    for (int i = 0; i < list_.size(); ++i)
        if (list_[i].path.compare(path, Qt::CaseInsensitive) == 0) return i;
    return -1;
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

int MetaEditor::loadingCount() const
{
    return int(std::count_if(list_.begin(), list_.end(), [](const MetaFile& f) { return f.state == MetaFile::Loading; }));
}

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

void MetaEditor::addUrls(const QVariantList& urls)
{
    QStringList paths;
    for (const QVariant& u : urls) {
        const QUrl url = u.toUrl();
        paths.append(url.isLocalFile() ? url.toLocalFile() : u.toString());
    }
    addPaths(paths);
}

void MetaEditor::addPaths(const QStringList& paths)
{
    QStringList found;
    int skipped = 0;
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            QStringList filters;
            for (const QString& ext : supportedExtensions()) filters.append("*." + ext);
            QDirIterator it(fi.absoluteFilePath(), filters, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) found.append(it.next());
        } else if (isSupported(p)) {
            found.append(fi.absoluteFilePath());
        } else if (fi.exists()) {
            ++skipped;
        }
    }
    std::sort(found.begin(), found.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });

    int added = 0;
    for (const QString& p : found) {
        if (indexOf(p) >= 0 || p.contains(".runa-tmp.")) continue;
        const QFileInfo fi(p);
        MetaFile f;
        f.path = fi.absoluteFilePath();
        f.name = fi.fileName();
        f.folder = QDir::toNativeSeparators(fi.absolutePath());
        f.badge = formatBadge(p);
        f.kind = kindOf(p);
        const int row = int(list_.size());
        files_.beginInsertRows({}, row, row);
        list_.append(f);
        files_.endInsertRows();
        ++added;

        QPointer<MetaEditor> self(this);
        const QString path = f.path;
        pool_->start([self, path] {
            const ReadResult r = meta::read(path);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, path, r] {
                if (self) self->loaded(path, r);
            }, Qt::QueuedConnection);
        });
    }
    if (added) emit countChanged();
    if (added && list_.size() == added) {   // the first files: show the first one
        list_[0].selected = true;
        anchor_ = current_ = 0;
        files_.rowChanged(0);
        selectionDidChange();
    }
    if (!added && found.isEmpty() && (skipped || !paths.isEmpty()))
        say("Nothing to add: these aren't files it can edit (music, video, pictures, Office documents or PDFs).", "warning");
    else if (!added && !found.isEmpty())
        say("Those files are already in the list.");
    else if (skipped)
        say(QString("Added %1. Skipped %2 that aren't a supported type.").arg(added).arg(skipped));
    else if (added)
        say(added == 1 ? QString("Added 1 file.") : QString("Added %1 files.").arg(added));
}

void MetaEditor::loaded(const QString& path, const ReadResult& r)
{
    const int i = indexOf(path);
    if (i < 0) return;
    MetaFile& f = list_[i];
    f.state = r.ok ? MetaFile::Ready : MetaFile::Failed;
    f.error = r.ok ? QString() : r.error;
    f.tags = r.tags;
    f.info = r.info;
    f.credentials = r.credentials;
    files_.rowChanged(i);
    emit countChanged();   // loadingCount
    if (f.selected) selectionDidChange();
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
    for (int k = int(sel.size()) - 1; k >= 0; --k) {
        files_.beginRemoveRows({}, sel[k], sel[k]);
        list_.removeAt(sel[k]);
        files_.endRemoveRows();
    }
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

    // One file at a time: videos are big, and a disk does one write best.
    QPointer<MetaEditor> self(this);
    QThread* worker = QThread::create([self, jobs] {
        for (const Job& j : jobs) {
            const WriteResult w = meta::write(j.path, j.changes, j.dropCredentials);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, path = j.path, w] {
                if (self) self->saved(path, w);
            }, Qt::QueuedConnection);
        }
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
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
