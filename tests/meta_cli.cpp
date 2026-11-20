// meta_cli: the editor's reading and writing from the command line, for
// checking files by hand (and against other programs). Not shipped.
//
//   meta_cli <file>                       print its fields and file info
//   meta_cli <file> key=value [key=...]   write fields (key= removes one)
//   meta_cli <file> --remove-credentials  delete its Content Credentials
//   meta_cli --samples <folder>           write the hand-built samples (docx, odt, webp, pdf)

#include "DocSamples.h"
#include "C2pa.h"
#include "MetaIO.h"

#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPdfWriter>

#include <cstdio>

static void print(const QString& s) { std::printf("%s\n", s.toUtf8().constData()); }

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        print("usage: meta_cli <file> [key=value ...] | --samples <folder>");
        return 2;
    }
    if (args[0] == "--samples" && args.size() > 1) {
        const QString dir = args[1] + "/";
        auto save = [&](const QString& name, const QByteArray& b) {
            QFile f(dir + name);
            return f.open(QIODevice::WriteOnly) && f.write(b) == b.size();
        };
        save("sample.docx", sampleDocx());
        save("sample.odt", sampleOdt());
        save("sample.webp", QByteArray::fromBase64("UklGRhoAAABXRUJQVlA4TA0AAAAvAAAAEAcQERGIiP4HAA=="));
        QPdfWriter pdf(dir + "sample.pdf");
        pdf.setTitle("Sample");
        QPainter p(&pdf);
        p.drawText(200, 200, "Hello");
        print("samples written");
        return 0;
    }
    const QString path = args[0];
    if (args.size() == 1) {
        const meta::ReadResult r = meta::read(path);
        if (!r.ok) {
            print("error: " + r.error);
            return 1;
        }
        for (const auto& [k, v] : r.tags) print(k + " = " + v);
        for (const auto& [k, v] : r.info) print("  (" + k + ": " + v + ")");
        const QVariantMap credentials = meta::c2pa::read(path);   // with the hash check
        for (auto it = credentials.begin(); it != credentials.end(); ++it) {
            const QVariant& v = it.value();
            print("  [credentials] " + it.key() + ": "
                  + (v.typeId() == QMetaType::QStringList ? v.toStringList().join(" | ") : v.toString()));
        }
        return 0;
    }
    QHash<QString, QString> changes;
    bool drop = false;
    for (const QString& a : args.mid(1)) {
        if (a == "--remove-credentials") drop = true;
        else changes.insert(a.section('=', 0, 0), a.section('=', 1));
    }
    const meta::WriteResult w = meta::write(path, changes, drop);
    if (!w.ok) {
        print("error: " + w.error);
        return 1;
    }
    print(w.notStored.isEmpty() ? QString("written") : "written; not kept: " + w.notStored.join(", "));
    return 0;
}
