// load_bench: how adding a big folder behaves. Not shipped.
//
//   load_bench <folder> [--ui] [--subfolders] [--apply]
//
// --apply then sets a copyright on every file and applies it: it WRITES TO
// THE FILES, so only point it at copies.
//
// Adds the folder to a MetaEditor (with the real page drawn offscreen when
// --ui), waits until every file is read, and reports how long that took, the
// longest the UI thread went without handling events, and peak memory.

#include "MetaEditor.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

Q_IMPORT_QML_PLUGIN(LokiMetadataPlugin)

static double mb(SIZE_T b) { return double(b) / (1024.0 * 1024.0); }

int main(int argc, char** argv)
{
    QStringList args;
    for (int i = 1; i < argc; ++i) args.append(QString::fromLocal8Bit(argv[i]));
    const bool ui = args.removeAll("--ui") > 0;
    const bool deep = args.removeAll("--subfolders") > 0;
    const bool apply = args.removeAll("--apply") > 0;
    if (args.isEmpty()) {
        std::printf("usage: load_bench <folder> [--ui] [--subfolders]\n");
        return 2;
    }
    if (ui) {
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
        if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR")) qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");

    MetaEditor editor;
    editor.setIncludeSubfolders(deep);
    QQmlApplicationEngine engine;
    if (ui) {
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.loadFromModule("LokiMetadata", "Main");
        if (engine.rootObjects().isEmpty()) return 1;
        if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) w->resize(1240, 780);
    }

    // The UI thread's longest silence: a 10 ms timer that notes late ticks.
    QElapsedTimer beat;
    qint64 worst = 0, ticks = 0, slow = 0;
    QTimer heart;
    heart.setInterval(10);
    QObject::connect(&heart, &QTimer::timeout, [&] {
        const qint64 gap = beat.restart();
        worst = std::max(worst, gap);
        ++ticks;
        if (gap > 100) ++slow;
    });

    QElapsedTimer t;
    t.start();
    beat.start();
    heart.start();
    editor.addPaths({args[0]});
    const qint64 added = t.elapsed();
    const int count = editor.count();
    int lastReport = -1;
    while (editor.loadingCount() > 0) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const int done = count - editor.loadingCount();
        if (done / 500 != lastReport) {
            lastReport = done / 500;
            PROCESS_MEMORY_COUNTERS pm{};
            GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof pm);
            std::printf("  %5d read  %6.1f s  %7.1f MB\n", done, t.elapsed() / 1000.0, mb(pm.WorkingSetSize));
            std::fflush(stdout);
        }
    }
    const qint64 loaded = t.elapsed();
    // Selecting everything: the field list for 4,000 files.
    QElapsedTimer s;
    s.start();
    editor.selectAll();
    QCoreApplication::processEvents();
    const qint64 selectAll = s.elapsed();
    qint64 applied = -1;
    if (apply) {
        editor.setField("copyright", "2026 Blood&Loot Entertainment " + QString::number(QDateTime::currentMSecsSinceEpoch()));
        QElapsedTimer a;
        a.start();
        editor.apply();
        while (editor.busy()) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        applied = a.elapsed();
        std::printf("apply: %s\n", qPrintable(editor.message()));
    }
    heart.stop();

    int failed = 0, withCr = 0;
    for (const MetaFile& f : editor.list()) {
        failed += f.state == MetaFile::Failed;
        withCr += f.hasCredentials();
    }
    PROCESS_MEMORY_COUNTERS pm{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof pm);
    std::printf("files %d (failed %d, with credentials %d)%s\n", count, failed, withCr, ui ? ", page drawn" : "");
    std::printf("addPaths %lld ms, all read %.1f s, select all %lld ms\n", added, loaded / 1000.0, selectAll);
    if (applied >= 0) std::printf("apply %.1f s\n", applied / 1000.0);
    std::printf("UI thread: longest stall %lld ms, %lld stalls over 100 ms\n", worst, slow);
    std::printf("memory: peak %.1f MB, now %.1f MB\n", mb(pm.PeakWorkingSetSize), mb(pm.WorkingSetSize));
    return 0;
}
