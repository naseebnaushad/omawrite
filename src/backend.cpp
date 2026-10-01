#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>
#include <utility>

#include "markdownhighlighter.h"

constexpr qreal typoraLineHeightPercent = 140;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("https://"));

    static const QRegularExpression schemeRe(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (!schemeRe.match(candidate).hasMatch())
        return {};

    const QUrl url(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

Backend::Backend(QObject *parent) : QObject(parent) {
    const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(stateDirectory);
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = 0; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < 100; ++slot) {
            const QString base = QDir(stateDirectory).filePath(
                QStringLiteral("recovery-%1").arg(slot));
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    for (int pn = 0; pn < 2; ++pn) {
        m_wordCountTimer[pn].setSingleShot(true);
        m_wordCountTimer[pn].setInterval(120);
        connect(&m_wordCountTimer[pn], &QTimer::timeout, this,
                [this, pn]() { refreshWordCount(pn); });
    }
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &Backend::writeRecovery);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                for (int pn = 0; pn < paneCount(); ++pn) {
                    PaneState &p = m_panes[pn];
                    if (path != p.fileUrl.toLocalFile())
                        continue;

                    const bool deleted = !QFileInfo::exists(path);
                    if (!deleted && p.hasKnownFileContents) {
                        QFile file(path);
                        if (file.open(QIODevice::ReadOnly)
                                && file.readAll() == p.lastKnownFileContents) {
                            // Atomic saves can replace the watched inode.
                            // Re-arm the watcher, but do not report our own
                            // save as an outside edit.
                            watchFiles();
                            return;
                        }
                    }

                    emit externalChangeDetected(pn, deleted, p.modified);
                    return;
                }
            });

    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
}

Backend::~Backend() = default;

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

PaneState &Backend::pane(int index) {
    return m_panes[qBound(0, index, 1)];
}

const PaneState &Backend::pane(int index) const {
    return m_panes[qBound(0, index, 1)];
}

int Backend::paneShowingTab(int tabIndex) const {
    for (int i = 0; i < 2; ++i) {
        if (m_panes[i].activeTab == tabIndex)
            return i;
    }
    return -1;
}

QString Backend::paneFileName(int paneIndex) const {
    const PaneState &p = pane(paneIndex);
    if (!p.fileUrl.isValid() || p.fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (p.fileUrl.isLocalFile()) {
        const QFileInfo info(p.fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = p.fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::setFocusedPane(int paneIndex) {
    paneIndex = qBound(0, paneIndex, 1);
    if (m_focusedPane == paneIndex)
        return;
    m_focusedPane = paneIndex;
    emit focusedPaneChanged();
}

void Backend::setSplitView(bool enabled) {
    if (m_splitView == enabled)
        return;

    if (!enabled) {
        // The second pane's QML TextEdit is about to be torn down: fold its
        // state back into the shared tab list before it goes away.
        syncPaneTabState(1);
        PaneState &p1 = m_panes[1];
        if (p1.highlighter)
            delete p1.highlighter.data();
        p1 = PaneState{};
        if (m_focusedPane == 1)
            setFocusedPane(0);
    }

    m_splitView = enabled;
    emit splitViewChanged();
    emit panesChanged();
}

void Backend::attachDocument(int paneIndex, QObject *textDocument) {
    paneIndex = qBound(0, paneIndex, 1);
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument || !quickDocument->textDocument()) {
        setPaneStatus(paneIndex, QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    PaneState &p = m_panes[paneIndex];
    if (p.highlighter)
        delete p.highlighter.data();

    p.document = quickDocument->textDocument();
    p.lastDocumentText = p.document->toPlainText();
    p.highlighter = new MarkdownHighlighter(p.document);
    p.highlighter->setDarkMode(m_darkMode);
    p.highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);

    connect(p.document, &QTextDocument::contentsChange, this,
            [this, paneIndex](int position, int, int charsAdded) {
                PaneState &pp = m_panes[paneIndex];
                if (pp.formattingTypography || pp.loading)
                    return;
                pp.lastChangePos = position;
                pp.lastChangeAdded = charsAdded;
            });

    applyDocumentTypography(paneIndex);

    const bool firstEverAttach = m_tabs.isEmpty();
    if (firstEverAttach) {
        m_tabs.append(TabData{});
        p.activeTab = 0;
    } else if (p.activeTab < 0) {
        // Pane just (re)created, e.g. split view was just turned on: give it
        // a tab that isn't already live in the other pane, or a fresh one.
        int candidate = -1;
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (paneShowingTab(i) < 0) {
                candidate = i;
                break;
            }
        }
        if (candidate < 0) {
            m_tabs.append(TabData{});
            candidate = m_tabs.size() - 1;
        }
        activateTabInPane(paneIndex, candidate);
    }

    if (firstEverAttach)
        restoreRecovery();

    emit tabsChanged();
    emit panesChanged();
}

void Backend::openDialog(int paneIndex) {
    emit openDialogRequested(qBound(0, paneIndex, 1));
}

void Backend::open(int paneIndex, const QUrl &url) {
    paneIndex = qBound(0, paneIndex, 1);
    if (!url.isLocalFile()) {
        setPaneStatus(paneIndex, QStringLiteral("Only local files can be opened."));
        return;
    }

    // Already open in some tab: just switch this pane to it instead of
    // opening a second copy.
    for (int i = 0; i < m_tabs.size(); ++i) {
        const int livePane = paneShowingTab(i);
        const QUrl tabUrl = livePane >= 0 ? m_panes[livePane].fileUrl : m_tabs.at(i).fileUrl;
        if (tabUrl == url) {
            switchTab(paneIndex, i);
            return;
        }
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setPaneStatus(paneIndex, QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    const QByteArray contents = file.readAll();
    PaneState &p = m_panes[paneIndex];

    // Reuse a single still-blank, never-touched tab (the common case: launch
    // the app, then immediately open a file) instead of leaving it behind as
    // a stray empty tab.
    const bool reuseActiveTab = m_tabs.size() == 1 && p.activeTab == 0
        && !p.modified && !p.fileUrl.isValid() && currentDocumentText(paneIndex).isEmpty();

    if (!reuseActiveTab) {
        syncPaneTabState(paneIndex);
        m_tabs.append(TabData{});
        activateTabInPane(paneIndex, m_tabs.size() - 1);
    }

    loadDocumentText(paneIndex, QString::fromUtf8(contents));
    p.lastKnownFileContents = contents;
    p.hasKnownFileContents = true;
    setPaneFileUrl(paneIndex, url);
    watchFiles();
    setPaneModified(paneIndex, false);
    setPaneStatus(paneIndex, QStringLiteral("Opened %1").arg(targetName));
    clearRecovery();
    emit tabsChanged();
    emit panesChanged();
}

void Backend::save(int paneIndex) {
    paneIndex = qBound(0, paneIndex, 1);
    const PaneState &p = m_panes[paneIndex];
    if (!p.fileUrl.isValid() || p.fileUrl.isEmpty()) {
        saveAsDialog(paneIndex);
        return;
    }

    saveTo(paneIndex, p.fileUrl);
}

void Backend::saveAsDialog(int paneIndex) {
    paneIndex = qBound(0, paneIndex, 1);
    emit saveDialogRequested(paneIndex, suggestedSaveUrl(paneIndex));
}

void Backend::saveAs(int paneIndex, const QUrl &url) {
    saveTo(qBound(0, paneIndex, 1), url);
}

void Backend::fileDialogCanceled(int paneIndex) {
    Q_UNUSED(paneIndex);
}

void Backend::discardRecovery() {
    clearRecovery();
}

void Backend::reloadFromDisk(int paneIndex) {
    paneIndex = qBound(0, paneIndex, 1);
    const QUrl url = m_panes[paneIndex].fileUrl;
    if (url.isLocalFile())
        open(paneIndex, url);
}

void Backend::keepExternalVersion(int paneIndex) {
    paneIndex = qBound(0, paneIndex, 1);
    PaneState &p = m_panes[paneIndex];
    QFile file(p.fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        p.lastKnownFileContents = file.readAll();
        p.hasKnownFileContents = true;
    } else {
        p.lastKnownFileContents.clear();
        p.hasKnownFileContents = false;
    }
    setPaneModified(paneIndex, true);
    scheduleRecovery();
    watchFiles();
    setPaneStatus(paneIndex, QStringLiteral("Kept your version"));
}

void Backend::printDocument(int paneIndex) {
    paneIndex = qBound(0, paneIndex, 1);
    PaneState &p = m_panes[paneIndex];
    if (!p.document) {
        setPaneStatus(paneIndex, QStringLiteral("There is no document to print."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(paneFileName(paneIndex)));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);

    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument rendered;
        rendered.setDefaultFont(p.document->defaultFont());
        rendered.setMarkdown(currentDocumentText(paneIndex));
        rendered.print(&printer);
    }
}

void Backend::newWindow() {
    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                                 QStringList());
    if (!started)
        setPaneStatus(m_focusedPane, QStringLiteral("Could not open a new window."));
}

void Backend::newTab(int paneIndex) {
    paneIndex = qBound(0, paneIndex, 1);
    syncPaneTabState(paneIndex);
    m_tabs.append(TabData{});
    activateTabInPane(paneIndex, m_tabs.size() - 1);
    emit tabsChanged();
    emit panesChanged();
}

void Backend::switchTab(int paneIndex, int index) {
    paneIndex = qBound(0, paneIndex, 1);
    if (index < 0 || index >= m_tabs.size())
        return;

    const PaneState &p = m_panes[paneIndex];
    if (index == p.activeTab)
        return;

    const int otherPane = 1 - paneIndex;
    if (paneCount() == 2 && m_panes[otherPane].activeTab == index) {
        // That tab is already live in the other pane: steal it, then give
        // the other pane a different tab (or a fresh blank one).
        syncPaneTabState(otherPane);
        syncPaneTabState(paneIndex);
        activateTabInPane(paneIndex, index);

        int candidate = -1;
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (paneShowingTab(i) < 0) {
                candidate = i;
                break;
            }
        }
        if (candidate < 0) {
            m_tabs.append(TabData{});
            candidate = m_tabs.size() - 1;
        }
        activateTabInPane(otherPane, candidate);
    } else {
        syncPaneTabState(paneIndex);
        activateTabInPane(paneIndex, index);
    }

    emit tabsChanged();
    emit panesChanged();
}

void Backend::closeTab(int index) {
    if (index < 0 || index >= m_tabs.size())
        return;

    const int livePane = paneShowingTab(index);
    if (livePane >= 0)
        syncPaneTabState(livePane);

    if (m_tabs.at(index).modified) {
        emit tabCloseNeedsConfirmation(livePane >= 0 ? livePane : m_focusedPane, index);
        return;
    }

    removeTab(index);
}

void Backend::forceCloseTab(int index) {
    removeTab(index);
}

bool Backend::anyModified() const {
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (tabDisplayModified(i))
            return true;
    }
    return false;
}

QString Backend::tabFileName(int index) const {
    if (index < 0 || index >= m_tabs.size())
        return QStringLiteral("Untitled.md");

    const QUrl &url = m_tabs.at(index).fileUrl;
    if (!url.isValid() || url.isEmpty())
        return QStringLiteral("Untitled.md");
    const QString name = QFileInfo(url.toLocalFile()).fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

QString Backend::tabDisplayFileName(int index) const {
    const int livePane = paneShowingTab(index);
    return livePane >= 0 ? paneFileName(livePane) : tabFileName(index);
}

bool Backend::tabDisplayModified(int index) const {
    const int livePane = paneShowingTab(index);
    if (livePane >= 0)
        return m_panes[livePane].modified;
    if (index < 0 || index >= m_tabs.size())
        return false;
    return m_tabs.at(index).modified;
}

QVariantList Backend::paneInfo() const {
    QVariantList result;
    for (int pn = 0; pn < paneCount(); ++pn) {
        const PaneState &p = m_panes[pn];

        QVariantList tabs;
        for (int i = 0; i < m_tabs.size(); ++i) {
            tabs.append(QVariantMap{
                {QStringLiteral("fileName"), tabDisplayFileName(i)},
                {QStringLiteral("modified"), tabDisplayModified(i)},
                {QStringLiteral("active"), i == p.activeTab},
            });
        }

        result.append(QVariantMap{
            {QStringLiteral("fileUrl"), p.fileUrl},
            {QStringLiteral("fileName"), paneFileName(pn)},
            {QStringLiteral("modified"), p.modified},
            {QStringLiteral("wordCount"), p.wordCount},
            {QStringLiteral("status"), p.status},
            {QStringLiteral("activeTabIndex"), p.activeTab},
            {QStringLiteral("tabs"), tabs},
        });
    }
    return result;
}

void Backend::syncPaneTabState(int paneIndex) {
    PaneState &p = pane(paneIndex);
    if (p.activeTab < 0 || p.activeTab >= m_tabs.size())
        return;

    TabData &tab = m_tabs[p.activeTab];
    tab.fileUrl = p.fileUrl;
    tab.text = currentDocumentText(paneIndex);
    tab.modified = p.modified;
    tab.lastKnownFileContents = p.lastKnownFileContents;
    tab.hasKnownFileContents = p.hasKnownFileContents;
}

void Backend::activateTabInPane(int paneIndex, int index) {
    if (index < 0 || index >= m_tabs.size())
        return;

    PaneState &p = pane(paneIndex);
    const TabData &tab = m_tabs.at(index);
    p.activeTab = index;

    setPaneFileUrl(paneIndex, tab.fileUrl);
    loadDocumentText(paneIndex, tab.text);
    p.lastKnownFileContents = tab.lastKnownFileContents;
    p.hasKnownFileContents = tab.hasKnownFileContents;
    watchFiles();
    setPaneModified(paneIndex, tab.modified);
    setPaneStatus(paneIndex, QString());
}

void Backend::removeTab(int index) {
    if (index < 0 || index >= m_tabs.size())
        return;

    m_tabs.remove(index);

    for (int pn = 0; pn < 2; ++pn) {
        PaneState &p = m_panes[pn];
        if (p.activeTab == index)
            p.activeTab = -1;
        else if (p.activeTab > index)
            --p.activeTab;
    }

    if (m_tabs.isEmpty())
        m_tabs.append(TabData{});

    for (int pn = 0; pn < paneCount(); ++pn) {
        if (m_panes[pn].activeTab >= 0)
            continue;
        int candidate = -1;
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (paneShowingTab(i) < 0) {
                candidate = i;
                break;
            }
        }
        if (candidate < 0)
            candidate = 0;
        activateTabInPane(pn, candidate);
    }

    writeRecovery();
    emit tabsChanged();
    emit panesChanged();
}

QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

bool Backend::editorTextChanged(int paneIndex) {
    paneIndex = qBound(0, paneIndex, 1);
    PaneState &p = m_panes[paneIndex];
    if (p.loading || p.formattingTypography)
        return false;

    const QString text = currentDocumentText(paneIndex);
    if (text == p.lastDocumentText)
        return false;
    p.lastDocumentText = text;

    if (p.document) {
        const int blockCount = p.document->blockCount();
        if (blockCount > p.formattedBlockCount)
            reapplyTypographyToChange(paneIndex);
        p.formattedBlockCount = blockCount;
    }

    scheduleWordCount(paneIndex);
    setPaneModified(paneIndex, true);
    setPaneStatus(paneIndex, QStringLiteral("Unsaved"));
    scheduleRecovery();
    return true;
}

QVariantList Backend::hiddenRangesAt(int paneIndex, int position) const {
    QVariantList ranges;
    const PaneState &p = pane(paneIndex);
    if (!p.document)
        return ranges;

    const QTextBlock block =
        p.document->findBlock(qBound(0, position, p.document->characterCount() - 1));
    if (!block.isValid())
        return ranges;

    const int lineStart = block.position();
    QList<QPair<int, int>> spans;
    const QList<MarkdownHighlighter::InlineMarkup> markup =
        MarkdownHighlighter::inlineMarkup(block.text());
    for (const MarkdownHighlighter::InlineMarkup &item : markup) {
        for (const MarkdownHighlighter::Span &marker : item.markers) {
            spans.append({lineStart + marker.start,
                          lineStart + marker.start + marker.length});
        }
    }
    std::sort(spans.begin(), spans.end());

    for (const auto &span : spans) {
        ranges.append(QVariantMap{{QStringLiteral("start"), span.first},
                                  {QStringLiteral("end"), span.second}});
    }
    return ranges;
}

void Backend::setSearchHighlight(int paneIndex, const QString &query, int currentMatchStart) {
    PaneState &p = pane(paneIndex);
    if (p.highlighter)
        p.highlighter->setSearch(query, currentMatchStart);
}

void Backend::openExternalUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("mailto"))
        QDesktopServices::openUrl(url);
}

QVariantMap Backend::windowGeometry() const {
    QSettings settings;
    return {{QStringLiteral("x"), settings.value(QStringLiteral("window/x"), -1)},
            {QStringLiteral("y"), settings.value(QStringLiteral("window/y"), -1)},
            {QStringLiteral("width"), settings.value(QStringLiteral("window/width"), 1280)},
            {QStringLiteral("height"), settings.value(QStringLiteral("window/height"), 820)},
            {QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized"), false)}};
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    if (!maximized) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void Backend::loadDocumentText(int paneIndex, const QString &text) {
    PaneState &p = pane(paneIndex);
    if (!p.document) {
        setPaneStatus(paneIndex, QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    p.loading = true;
    p.document->setPlainText(text);
    p.lastDocumentText = text;
    p.loading = false;

    applyDocumentTypography(paneIndex);
    m_wordCountTimer[qBound(0, paneIndex, 1)].stop();
    setPaneWordCount(paneIndex, countWords(text));
}

void Backend::setPaneFileUrl(int paneIndex, const QUrl &url) {
    PaneState &p = pane(paneIndex);
    if (p.fileUrl == url)
        return;

    p.fileUrl = url;
    watchFiles();
    emit panesChanged();
}

void Backend::setPaneModified(int paneIndex, bool modified) {
    PaneState &p = pane(paneIndex);
    if (p.modified == modified)
        return;

    p.modified = modified;
    emit tabsChanged();
    emit panesChanged();
}

void Backend::setPaneStatus(int paneIndex, const QString &status) {
    PaneState &p = pane(paneIndex);
    if (p.status == status)
        return;

    p.status = status;
    emit panesChanged();
}

void Backend::saveTo(int paneIndex, const QUrl &url) {
    paneIndex = qBound(0, paneIndex, 1);
    PaneState &p = m_panes[paneIndex];

    if (!url.isLocalFile()) {
        setPaneStatus(paneIndex, QStringLiteral("Only local files can be saved."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setPaneStatus(paneIndex, QStringLiteral("Could not save %1.").arg(targetName));
        return;
    }

    const QByteArray contents = currentDocumentText(paneIndex).toUtf8();
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching every path
    // before that replacement so our own write is not classified as
    // external (both panes may be watching different files).
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    // commit() flushes, fsyncs, and atomically renames the temp file into
    // place, returning false (and leaving the original untouched) on any
    // write error.
    if (!file.commit()) {
        watchFiles();
        setPaneStatus(paneIndex, QStringLiteral("Could not write %1.").arg(targetName));
        return;
    }

    p.lastKnownFileContents = contents;
    p.hasKnownFileContents = true;
    setPaneFileUrl(paneIndex, url);
    watchFiles();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(url.toLocalFile()).absolutePath());
    setPaneModified(paneIndex, false);
    setPaneStatus(paneIndex, QStringLiteral("Saved %1").arg(paneFileName(paneIndex)));
    clearRecovery();
    emit saveSucceeded(paneIndex);
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

QString Backend::recoveryPath() const {
    return m_recoveryPath;
}

void Backend::writeRecovery() {
    for (int pn = 0; pn < paneCount(); ++pn)
        syncPaneTabState(pn);

    const QString path = recoveryPath();
    if (path.isEmpty())
        return;

    QJsonArray tabsArray;
    for (const TabData &tab : std::as_const(m_tabs)) {
        if (!tab.modified)
            continue;
        tabsArray.append(QJsonObject{{QStringLiteral("fileUrl"), tab.fileUrl.toString()},
                                     {QStringLiteral("text"), tab.text}});
    }

    if (tabsArray.isEmpty()) {
        QFile::remove(path);
        return;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject recovery{{QStringLiteral("tabs"), tabsArray}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(recoveryPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject())
        return;

    QJsonArray tabsArray = json.object().value(QStringLiteral("tabs")).toArray();
    if (tabsArray.isEmpty()) {
        // Older, single-document recovery file from before tabs existed.
        if (!json.object().contains(QStringLiteral("text")))
            return;
        tabsArray.append(json.object());
    }

    QVector<TabData> recovered;
    for (const QJsonValue &value : std::as_const(tabsArray)) {
        const QJsonObject obj = value.toObject();
        TabData tab;
        tab.fileUrl = QUrl(obj.value(QStringLiteral("fileUrl")).toString());
        tab.text = obj.value(QStringLiteral("text")).toString();
        tab.modified = true;
        QFile diskFile(tab.fileUrl.toLocalFile());
        if (tab.fileUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly)) {
            tab.lastKnownFileContents = diskFile.readAll();
            tab.hasKnownFileContents = true;
        }
        recovered.append(tab);
    }

    if (recovered.isEmpty())
        return;

    m_tabs = recovered;
    for (int pn = 0; pn < 2; ++pn)
        m_panes[pn].activeTab = -1;
    activateTabInPane(0, 0);
    setPaneStatus(0, QStringLiteral("Recovered unsaved changes"));
}

void Backend::clearRecovery() {
    m_recoveryTimer.stop();
    // Rewrite (rather than unconditionally delete) the aggregate recovery
    // file, since other tabs may still have unsaved changes worth keeping.
    writeRecovery();
}

void Backend::watchFiles() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    for (int pn = 0; pn < paneCount(); ++pn) {
        const QUrl &url = m_panes[pn].fileUrl;
        if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile()))
            m_fileWatcher.addPath(url.toLocalFile());
    }
}

void Backend::loadOmarchyTheme() {
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                m_themeBackground = value;
            else if (key == QStringLiteral("foreground"))
                m_themeForeground = value;
            else if (key == QStringLiteral("accent"))
                m_themeAccent = value;
            else if (key == QStringLiteral("selection"))
                m_themeSelection = value;
        }
    }

    bool themeModeKnown = false;
    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
        themeModeKnown = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
        themeModeKnown = true;
    } else {
        const QColor background(m_themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            themeIsDark = luminance < 0.5;
            themeModeKnown = true;
        }
    }
    if (themeModeKnown && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    for (int pn = 0; pn < 2; ++pn) {
        if (m_panes[pn].highlighter) {
            m_panes[pn].highlighter->setDarkMode(m_darkMode);
            m_panes[pn].highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);
        }
    }

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}

QUrl Backend::suggestedSaveUrl(int paneIndex) const {
    const PaneState &p = pane(paneIndex);
    if (p.fileUrl.isLocalFile())
        return p.fileUrl;

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
    return QUrl::fromLocalFile(
        directory.filePath(suggestedFileName(currentDocumentText(paneIndex))));
}

QString Backend::currentDocumentText(int paneIndex) const {
    const PaneState &p = pane(paneIndex);
    return p.document ? p.document->toPlainText() : QString();
}

int Backend::countWords(const QString &text) {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));
    int count = 0;
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

QString Backend::suggestedFileName(const QString &text) {
    QString name = text.section(QLatin1Char('\n'), 0, 0).trimmed();
#ifdef Q_OS_WIN
    // Windows additionally rejects these in file names and strips trailing dots.
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    while (name.endsWith(QLatin1Char('.')))
        name.chop(1);
#else
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
#endif
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

void Backend::setPaneWordCount(int paneIndex, int words) {
    PaneState &p = pane(paneIndex);
    if (p.wordCount == words)
        return;

    p.wordCount = words;
    emit panesChanged();
}

void Backend::refreshWordCount(int paneIndex) {
    setPaneWordCount(paneIndex, countWords(currentDocumentText(paneIndex)));
}

void Backend::scheduleWordCount(int paneIndex) {
    m_wordCountTimer[qBound(0, paneIndex, 1)].start();
}

void Backend::applyDocumentTypography(int paneIndex) {
    PaneState &p = pane(paneIndex);
    if (!p.document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // A full pass is only used for freshly loaded/attached documents, so it is
    // safe to drop undo history here (re-enabling clears the stack anyway).
    const bool undoEnabled = p.document->isUndoRedoEnabled();
    p.document->setUndoRedoEnabled(false);

    p.formattingTypography = true;
    QTextCursor cursor(p.document);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(blockFormat);
    p.formattingTypography = false;

    p.document->setUndoRedoEnabled(undoEnabled);

    p.formattedBlockCount = p.document->blockCount();
}

void Backend::reapplyTypographyToChange(int paneIndex) {
    PaneState &p = pane(paneIndex);
    if (!p.document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // Format only the block(s) touched by the last edit instead of the whole
    // document, and fold the change into the preceding edit command so a single
    // undo reverts both the text and its formatting.
    const int maxPos = p.document->characterCount() - 1;
    const int start = qBound(0, p.lastChangePos, maxPos);
    const int end = qBound(start, p.lastChangePos + p.lastChangeAdded, maxPos);

    p.formattingTypography = true;
    QTextCursor cursor(p.document);
    cursor.joinPreviousEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.mergeBlockFormat(blockFormat);
    cursor.endEditBlock();
    p.formattingTypography = false;
}
