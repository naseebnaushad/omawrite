#pragma once

#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <memory>

class MarkdownHighlighter;
class QTextDocument;
class QWindow;
class QLockFile;

// One open document's state while it is not live in any pane. A live pane's
// authoritative state lives in that pane's PaneState; syncPaneTabState()
// copies it in here before the pane switches to a different tab, and
// activateTabInPane() copies it back out.
struct TabData {
    QUrl fileUrl;
    QString text;
    bool modified = false;
    QByteArray lastKnownFileContents;
    bool hasKnownFileContents = false;
};

// One editor pane's live state: the QTextDocument actually attached to a
// QML TextEdit, and everything derived from editing it. At most one pane
// has any given tab active at a time (activating a tab already open in the
// other pane steals it away from that pane instead of sharing the document).
struct PaneState {
    QPointer<QTextDocument> document;
    QPointer<MarkdownHighlighter> highlighter;
    QUrl fileUrl;
    bool modified = false;
    int activeTab = -1;
    int wordCount = 0;
    QString status;
    bool loading = false;
    bool formattingTypography = false;
    int formattedBlockCount = 0;
    int lastChangePos = 0;
    int lastChangeAdded = 0;
    QString lastDocumentText;
    QByteArray lastKnownFileContents;
    bool hasKnownFileContents = false;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool anyModified READ anyModified NOTIFY tabsChanged)
    Q_PROPERTY(bool splitView READ splitView NOTIFY splitViewChanged)
    Q_PROPERTY(int focusedPane READ focusedPane NOTIFY focusedPaneChanged)
    Q_PROPERTY(QVariantList paneInfo READ paneInfo NOTIFY panesChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    void setParentWindow(QWindow *window);

    bool anyModified() const;
    bool splitView() const { return m_splitView; }
    int focusedPane() const { return m_focusedPane; }
    QVariantList paneInfo() const;
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }
    static int countWords(const QString &text);
    static QString normalizedLinkUrl(const QString &clipboardText);
    static QString suggestedFileName(const QString &text);

    Q_INVOKABLE void attachDocument(int pane, QObject *textDocument);
    Q_INVOKABLE void setFocusedPane(int pane);
    Q_INVOKABLE void setSplitView(bool enabled);
    Q_INVOKABLE void openDialog(int pane);
    Q_INVOKABLE void open(int pane, const QUrl &url);
    Q_INVOKABLE void save(int pane);
    Q_INVOKABLE void saveAsDialog(int pane);
    Q_INVOKABLE void saveAs(int pane, const QUrl &url);
    Q_INVOKABLE void fileDialogCanceled(int pane);
    Q_INVOKABLE void discardRecovery();
    Q_INVOKABLE void reloadFromDisk(int pane);
    Q_INVOKABLE void keepExternalVersion(int pane);
    Q_INVOKABLE void printDocument(int pane);
    Q_INVOKABLE void newWindow();
    Q_INVOKABLE void newTab(int pane);
    Q_INVOKABLE void switchTab(int pane, int index);
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void forceCloseTab(int index);
    Q_INVOKABLE QString clipboardUrl() const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE bool editorTextChanged(int pane);
    Q_INVOKABLE QVariantList hiddenRangesAt(int pane, int position) const;
    Q_INVOKABLE void setSearchHighlight(int pane, const QString &query, int currentMatchStart);
    Q_INVOKABLE void openExternalUrl(const QUrl &url);
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);

signals:
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();
    void openDialogRequested(int pane);
    void saveDialogRequested(int pane, const QUrl &suggestedUrl);
    void saveSucceeded(int pane);
    void externalChangeDetected(int pane, bool deleted, bool locallyModified);
    void tabsChanged();
    void panesChanged();
    void splitViewChanged();
    void focusedPaneChanged();
    void tabCloseNeedsConfirmation(int pane, int index);

private:
    PaneState &pane(int index);
    const PaneState &pane(int index) const;
    int paneCount() const { return m_splitView ? 2 : 1; }
    int paneShowingTab(int tabIndex) const;

    void loadDocumentText(int pane, const QString &text);
    void setPaneFileUrl(int pane, const QUrl &url);
    void setPaneModified(int pane, bool modified);
    void setPaneStatus(int pane, const QString &status);
    void saveTo(int pane, const QUrl &url);
    QUrl suggestedSaveUrl(int pane) const;
    QString currentDocumentText(int pane) const;
    void setPaneWordCount(int pane, int words);
    void refreshWordCount(int pane);
    void scheduleWordCount(int pane);
    void applyDocumentTypography(int pane);
    void reapplyTypographyToChange(int pane);
    void scheduleRecovery();
    void writeRecovery();
    void restoreRecovery();
    void clearRecovery();
    QString recoveryPath() const;
    void watchFiles();
    void loadOmarchyTheme();
    void watchOmarchyTheme();
    void syncPaneTabState(int pane);
    void activateTabInPane(int pane, int index);
    void removeTab(int index);
    QString tabFileName(int index) const;
    QString tabDisplayFileName(int index) const;
    bool tabDisplayModified(int index) const;
    QString paneFileName(int pane) const;

    QVector<TabData> m_tabs;
    PaneState m_panes[2];
    bool m_splitView = false;
    int m_focusedPane = 0;

    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QTimer m_wordCountTimer[2];
    QTimer m_recoveryTimer;
    QFileSystemWatcher m_fileWatcher;
    QPointer<QWindow> m_parentWindow;
    QString m_recoveryPath;
    std::unique_ptr<QLockFile> m_recoveryLock;

    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QFileSystemWatcher m_themeWatcher;
};
