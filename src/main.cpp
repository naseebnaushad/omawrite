#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QFileOpenEvent>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <QVector>
#include <QWindow>
#include <QFile>
#include <utility>

#include "backend.h"
#include "systemtheme.h"

// macOS delivers a double-clicked or "Open With"-launched document as a
// QFileOpenEvent (an Apple Event under the hood), not as a command-line
// argument. On a cold launch that event can arrive before the engine and
// Backend exist, so we buffer it and drain the buffer once they're ready.
class OmawriteApplication : public QApplication {
    Q_OBJECT

public:
    using QApplication::QApplication;

    QVector<QUrl> takePendingOpenFiles() {
        return std::exchange(m_pendingOpenFiles, {});
    }

signals:
    void fileOpenRequested(const QUrl &url);

protected:
    bool event(QEvent *e) override {
        if (e->type() == QEvent::FileOpen) {
            const QUrl url = QUrl::fromLocalFile(static_cast<QFileOpenEvent *>(e)->file());
            m_pendingOpenFiles.append(url);
            emit fileOpenRequested(url);
            return true;
        }
        return QApplication::event(e);
    }

private:
    QVector<QUrl> m_pendingOpenFiles;
};

int main(int argc, char *argv[]) {
    OmawriteApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omawrite"));
    app.setDesktopFileName(QStringLiteral("omawrite"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omawrite")));

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Italic.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-BoldItalic.ttf"));
    app.setOrganizationName(QStringLiteral("Omacom"));
    app.setOrganizationDomain(QStringLiteral("omacom.io"));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    Backend backend(&app);
    SystemTheme systemTheme(&app);
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend,
                     &Backend::setDarkMode);

    // Carry the desktop's text scale into the default font, so the chrome that
    // inherits it (dialog titles, buttons) grows along with the writing area.
    const QFont interfaceFont(QStringLiteral("iA Writer Mono S"));
    const qreal basePointSize = interfaceFont.pointSizeF() > 0
        ? interfaceFont.pointSizeF()
        : app.font().pointSizeF();
    const auto applyInterfaceFont = [&app, interfaceFont, basePointSize](qreal textScale) {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * textScale);
        app.setFont(scaled);
    };
    applyInterfaceFont(systemTheme.textScale());

    backend.setTextScale(systemTheme.textScale());
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &backend,
                     [&backend, applyInterfaceFont](qreal textScale) {
        applyInterfaceFont(textScale);
        backend.setTextScale(textScale);
    });

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the Omawrite interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }

    backend.setParentWindow(qobject_cast<QWindow *>(engine.rootObjects().constFirst()));

    const QStringList args = app.arguments();
    if (args.size() > 1)
        backend.open(0, QUrl::fromLocalFile(args.at(1)));

    // Drain any FileOpen events (e.g. Finder double-click) that arrived
    // before the window and Backend existed, then keep handling later ones
    // for as long as the app stays running. Opening a file always adds (or
    // switches to) a tab, so there's no need to gate this on whether the
    // current document has unsaved changes.
    for (const QUrl &url : app.takePendingOpenFiles())
        backend.open(0, url);

    QObject::connect(&app, &OmawriteApplication::fileOpenRequested, &backend,
                     [&backend](const QUrl &url) { backend.open(0, url); });

    return app.exec();
}

#include "main.moc"
