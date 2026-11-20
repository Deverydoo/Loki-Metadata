// PDF properties, through qpdf (Apache 2.0, linked statically).
//
// The document information dictionary (Title, Author, Subject, Keywords,
// Creator, and custom keys such as Copyright) and, if the PDF has one, its
// XMP packet, which Acrobat prefers when both are there. qpdf writes the PDF
// out again with every object kept as it was; content streams aren't decoded
// or recompressed. Signed PDFs are refused: rewriting would break the
// signature.

#include "MetaIO.h"
#include "Xmp.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

namespace meta {

namespace {

// Info keys for the named fields.
const QList<QPair<QString, std::string>>& infoKeys()
{
    static const QList<QPair<QString, std::string>> k{
        {"title", "/Title"},       {"artist", "/Author"},   {"subject", "/Subject"},
        {"keywords", "/Keywords"}, {"encoder", "/Creator"}, {"copyright", "/Copyright"},
    };
    return k;
}

std::string keyOf(const QString& field)
{
    for (const auto& [f, k] : infoKeys())
        if (f == field) return k;
    return "/" + field.toStdString();   // a custom field: its own key
}

bool isBookkeeping(const std::string& key)
{
    return key == "/Producer" || key == "/CreationDate" || key == "/ModDate" || key == "/Trapped";
}

QString fieldOf(const std::string& key)
{
    for (const auto& [f, k] : infoKeys())
        if (k == key) return f;
    return QString::fromStdString(key.substr(1));
}

QString text(QPDFObjectHandle o) { return o.isString() ? QString::fromStdString(o.getUTF8Value()).trimmed() : QString(); }

// "D:20261103070448-05'00'" as a readable date.
QString pdfDate(const QString& d)
{
    QString s = d.startsWith("D:") ? d.mid(2) : d;
    const QDateTime t = QDateTime::fromString(s.left(14), "yyyyMMddHHmmss");
    if (t.isValid()) return t.toString("yyyy-MM-dd HH:mm");
    const QDateTime day = QDateTime::fromString(s.left(8), "yyyyMMdd");
    return day.isValid() ? day.toString("yyyy-MM-dd") : d;
}

std::string native(const QString& path) { return QDir::toNativeSeparators(path).toUtf8().toStdString(); }

bool isSigned(QPDF& pdf)
{
    QPDFObjectHandle form = pdf.getRoot().getKey("/AcroForm");
    if (!form.isDictionary()) return false;
    QPDFObjectHandle flags = form.getKey("/SigFlags");
    return flags.isInteger() && (flags.getIntValue() & 1);
}

} // namespace

ReadResult readPdf(const QString& path)
{
    ReadResult r;
    try {
        QPDF pdf;
        pdf.processFile(native(path).c_str());
        QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
        r.info.append({QString("Format"), "PDF " + QString::fromStdString(pdf.getPDFVersion())});
        r.info.append({QString("Pages"), QString::number(pdf.getAllPages().size())});
        if (info.isDictionary()) {
            for (const std::string& key : info.getKeys()) {
                const QString v = text(info.getKey(key));
                if (v.isEmpty()) continue;
                if (key == "/Producer") r.info.append({QString("Made with"), v});
                else if (key == "/CreationDate") r.info.append({QString("Created"), pdfDate(v)});
                else if (key == "/ModDate") r.info.append({QString("Modified"), pdfDate(v)});
                else if (!isBookkeeping(key)) r.tags.append({fieldOf(key), v});
            }
        }
        QPDFObjectHandle meta = pdf.getRoot().getKey("/Metadata");
        if (meta.isStream()) {
            const auto data = meta.getStreamData(qpdf_dl_all);
            Pairs ignored;
            xmp::read(QByteArray(reinterpret_cast<const char*>(data->getBuffer()), qsizetype(data->getSize())), &r.tags, &ignored);
        }
        if (pdf.isEncrypted()) r.info.append({QString("Encrypted"), QString("Yes")});
        if (isSigned(pdf)) r.info.append({QString("Signed"), QString("Yes (can't be edited without breaking the signature)")});
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = "Can't read this PDF: " + QString::fromLocal8Bit(e.what());
    }
    return r;
}

QString writePdf(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    Q_UNUSED(notStored);   // the information dictionary takes any key
    const QString temp = tempPathFor(path);
    try {
        QPDF pdf;
        pdf.processFile(native(path).c_str());
        if (isSigned(pdf)) return "This PDF is digitally signed. Changing it would break the signature, so it was left as it is.";
        QPDFObjectHandle trailer = pdf.getTrailer();
        QPDFObjectHandle info = trailer.getKey("/Info");
        if (!info.isDictionary()) {
            info = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
            trailer.replaceKey("/Info", info);
        }
        for (auto it = changes.begin(); it != changes.end(); ++it) {
            const std::string key = keyOf(it.key());
            if (it.value().isEmpty()) info.removeKey(key);
            else info.replaceKey(key, QPDFObjectHandle::newUnicodeString(it.value().toStdString()));
        }
        // The XMP packet, if there is one, gets the same changes.
        QPDFObjectHandle meta = pdf.getRoot().getKey("/Metadata");
        if (meta.isStream()) {
            const auto data = meta.getStreamData(qpdf_dl_all);
            const QByteArray packet(reinterpret_cast<const char*>(data->getBuffer()), qsizetype(data->getSize()));
            const QByteArray updated = xmp::update(packet, changes);
            if (!updated.isEmpty() && updated != packet)
                meta.replaceStreamData(updated.toStdString(), QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
        }
        QPDFWriter w(pdf, native(temp).c_str());
        w.setObjectStreamMode(qpdf_o_preserve);
        w.setDecodeLevel(qpdf_dl_none);   // content left exactly as it was
        w.write();
    } catch (const std::exception& e) {
        QFile::remove(temp);
        return "Couldn't write this PDF: " + QString::fromLocal8Bit(e.what());
    }
    QString error;
    if (!replaceFile(path, temp, &error)) return error;
    return {};
}

} // namespace meta
