#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QMenu>
#include <QContextMenuEvent>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineContextMenuRequest>
#include <QWebEngineFullScreenRequest>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineHistory>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineDownloadRequest>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>
#include <QShortcut>
#include <QKeySequence>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialog>
#include <QTextBrowser>
#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QRegularExpression>
#include <QClipboard>
#include <QMouseEvent>
#include <QProcess>
#include <QFont>
#include <QScrollArea>
#include <QStyle>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QDateTime>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QGroupBox>
#include <QSlider>
#include <QApplication>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <functional>

#if defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#undef Bool
#undef Status
#undef None
#undef Cursor
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef FontChange
#endif

// --- Data Structures ---

struct BookmarkItem {
    QString title;
    QString url;
};

struct PasswordEntry {
    QString titleUrl;
    QString username;
    QString password;
};

struct StyleRule {
    QString pattern; // e.g. "gemini.google.com" or "*"
    QString cssCode;
};

struct UserAgentRule {
    QString pattern;  // e.g. "facebook.com", "gmx.net"
    QString mode;     // "Desktop", "Mobile", "Custom"
    QString customUA; // if mode == "Custom"
};

// --- Standard UA Strings ---

// Chrome-UA: Version 124 (wie v0.0.7, WAF-bewährt). NICHT an die Qt/Chromium-Version anpassen (Chrome/102) — die WAF wertet ein altes Chrome als Bot (Forum 406).
// Eine veraltete Chrome-Version lässt Seiten (z.B. GMX)
// Legacy-Code mit alter CSP servieren — dann laden Fonts/
// Icons nicht (GMX-Ordner-Icons = graue Platzhalter,
// weil s.uicdn.com nicht in font-src steht).
static QString muChromeUA() {
    return QStringLiteral(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
}

static QString muMobileUA() {
    return QStringLiteral(
        "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36");
}

static QString muFirefox131UA() {
    return QStringLiteral(
        "Mozilla/5.0 (X11; Linux x86_64; rv:131.0) "
        "Gecko/20100101 Firefox/131.0");
}

static bool isGoogleHost(const QString &host) {
    return host.contains(QStringLiteral("google.com"))
        || host.contains(QStringLiteral("google.de"))
        || host.contains(QStringLiteral("gstatic.com"))
        || host.contains(QStringLiteral("googleapis.com"))
        || host.contains(QStringLiteral("googleusercontent.com"));
}

// --- Draggable Widget (Window Dragging & IceWM Freeze Fix) ---
// Basis-Klasse für alle per Maus ziehbaren Flächen (Sidebar +
// URL-Bar-Spacer): Linksklick + Ziehen verschiebt das Fenster,
// Doppelklick wird geschluckt (IceWM friert sonst bei rahmenlosen
// Fenstern ein, wenn es den Doppelklick selbst verarbeiten will).
class DraggableWidget : public QWidget {
    Q_OBJECT
public:
    explicit DraggableWidget(QWidget *parent = nullptr)
        : QWidget(parent), m_dragging(false) {}

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragPos = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            window()->move(event->globalPosition().toPoint() - m_dragPos);
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        m_dragging = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override {
        // Catch double-clicks to prevent IceWM from freezing on frameless dragging!
        event->accept();
    }

private:
    bool m_dragging;
    QPoint m_dragPos;
};

// --- Sidebar Widget (Window Dragging & IceWM Freeze Fix) ---
class SideToolbarWidget : public DraggableWidget {
    Q_OBJECT
public:
    explicit SideToolbarWidget(QWidget *parent = nullptr)
        : DraggableWidget(parent) {
        // Entscheidend: QWidget-Subklassen malen ihren Stylesheet-Hintergrund
        // sonst nicht. toolbarContainer ist ein plain QWidget und malt ihn
        // deshalb, die Sidebar (Subklasse) nicht — die Sidebar-Transparenz
        // aus applyTransparency() (rgba-Stylesheet) wirkte optisch nie.
        // Mit WA_StyledBackground verhaelt sich die Sidebar wie ein plain
        // QWidget und das Stylesheet wird wirklich gemalt.
        setAttribute(Qt::WA_StyledBackground, true);
    }
};

// --- Symmetric Encryption Helper (SHA-256 Stream Cipher) ---

class SimpleCipher {
public:
    static QByteArray encrypt(const QByteArray &plaintext, const QString &masterPassword) {
        QByteArray salt(16, 0);
        for (int i = 0; i < 16; ++i) {
            salt[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
        }

        QByteArray keyHash = QCryptographicHash::hash(masterPassword.toUtf8() + salt, QCryptographicHash::Sha256);
        QByteArray checksum = QCryptographicHash::hash(plaintext + keyHash, QCryptographicHash::Sha256).left(16);

        QByteArray ciphertext;
        ciphertext.reserve(plaintext.size());

        quint32 blockCounter = 0;
        int ptLen = plaintext.size();
        int offset = 0;

        while (offset < ptLen) {
            QByteArray counterBytes;
            counterBytes.append(reinterpret_cast<const char*>(&blockCounter), sizeof(blockCounter));
            QByteArray blockKey = QCryptographicHash::hash(keyHash + counterBytes, QCryptographicHash::Sha256);
            int chunkSize = qMin(32, ptLen - offset);
            for (int i = 0; i < chunkSize; ++i) {
                ciphertext.append(plaintext[offset + i] ^ blockKey[i]);
            }
            offset += chunkSize;
            blockCounter++;
        }

        QByteArray result;
        result.append("MUPASS01"); // 8 bytes header magic
        result.append(salt);       // 16 bytes salt
        result.append(checksum);   // 16 bytes checksum
        result.append(ciphertext);
        return result.toBase64();
    }

    static bool decrypt(const QByteArray &base64Input, const QString &masterPassword, QByteArray &outPlaintext) {
        QByteArray data = QByteArray::fromBase64(base64Input);
        if (data.size() < 40) return false;
        if (!data.startsWith("MUPASS01")) return false;

        QByteArray salt = data.mid(8, 16);
        QByteArray expectedChecksum = data.mid(24, 16);
        QByteArray ciphertext = data.mid(40);

        QByteArray keyHash = QCryptographicHash::hash(masterPassword.toUtf8() + salt, QCryptographicHash::Sha256);

        QByteArray plaintext;
        plaintext.reserve(ciphertext.size());

        quint32 blockCounter = 0;
        int ctLen = ciphertext.size();
        int offset = 0;

        while (offset < ctLen) {
            QByteArray counterBytes;
            counterBytes.append(reinterpret_cast<const char*>(&blockCounter), sizeof(blockCounter));
            QByteArray blockKey = QCryptographicHash::hash(keyHash + counterBytes, QCryptographicHash::Sha256);
            int chunkSize = qMin(32, ctLen - offset);
            for (int i = 0; i < chunkSize; ++i) {
                plaintext.append(ciphertext[offset + i] ^ blockKey[i]);
            }
            offset += chunkSize;
            blockCounter++;
        }

        QByteArray actualChecksum = QCryptographicHash::hash(plaintext + keyHash, QCryptographicHash::Sha256).left(16);
        if (actualChecksum != expectedChecksum) {
            return false;
        }

        outPlaintext = plaintext;
        return true;
    }
};

// --- Custom Clickable Controls ---

class ClickableLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickableLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setCursor(Qt::PointingHandCursor);
    }
signals:
    void clicked();
protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
        }
        QLabel::mousePressEvent(event);
    }
};

class ClickableComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit ClickableComboBox(QWidget *parent = nullptr) : QComboBox(parent) {}
    std::function<bool()> onBeforePopup;

    void showPopup() override {
        if (onBeforePopup) {
            if (!onBeforePopup()) return;
        }
        QComboBox::showPopup();
    }
};

class MiddleClickLineEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit MiddleClickLineEdit(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::MiddleButton) {
            QClipboard *clipboard = QApplication::clipboard();
            QString text = clipboard->text(QClipboard::Selection);
            if (text.isEmpty()) {
                text = clipboard->text(QClipboard::Clipboard);
            }
            if (!text.isEmpty()) {
                insert(text);
                event->accept();
                return;
            }
        }
        QLineEdit::mousePressEvent(event);
    }
};

static void installSameHostTargetFixOnPage(QWebEnginePage *page) {
    if (!page) return;
    QObject::connect(page, &QWebEnginePage::loadFinished, page,
        [page](bool /*ok*/) {
            page->runJavaScript(QStringLiteral(R"JS(
(function(){
  if (window.__muSameHostFixInstalled) return;
  window.__muSameHostFixInstalled = true;
  document.addEventListener('click', function(e){
    var a = (e.target && e.target.closest) ? e.target.closest('a') : null;
    if (!a || !a.target) return;
    try {
      var linkUrl = new URL(a.href, location.href);
      if (linkUrl.hostname === location.hostname) {
        a.removeAttribute('target');
      }
    } catch (_) {}
  }, true);
})();
)JS"));
        });
}

// --- Request Interceptor with Dynamic Domain Rules ---

class MuRequestInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit MuRequestInterceptor(QObject *parent = nullptr)
        : QWebEngineUrlRequestInterceptor(parent) {}

    void setRules(const QList<UserAgentRule> &rules) {
        m_rules = rules;
    }

    // Generischer, einfacher Werbeblocker (nicht GMX-spezifisch):
    // Blockt Requests an bekannte Werbe-/Tracking-Domains.
    // Das ist die frühere „Werbung ausgeblendet"-Wirkung als
    // bewusstes Feature statt als Accept-Header-Nebeneffekt.
    void setAdBlockerEnabled(bool enabled) { m_adBlocker = enabled; }
    // Global mobile mode (user's Mobile/Desktop toggle, saved in config).
    // When on, the fallback (no UA rule matches) sends the Mobile UA.
    // UA rules still win (e.g. GMX stays Desktop).
    void setMobileMode(bool mobile) { m_mobileMode = mobile; }
    // Ad blocker exceptions: domains never blocked. List from
    // adblock_exceptions.json, editable in Privacy settings.
    void setAdBlockExceptions(const QStringList &exceptions) { m_adBlockExceptions = exceptions; }
    bool isAdBlockException(const QString &host) const {
        for (const QString &ex : m_adBlockExceptions) {
            if (!ex.isEmpty() && host.contains(ex, Qt::CaseInsensitive)) return true;
        }
        return false;
    }
    bool isAdBlockerEnabled() const { return m_adBlocker; }

    static bool isAdDomain(const QString &host) {
        // Bekannte Werbe-/Tracking-Domains (Subdomains eingeschlossen).
        // Klein, kuratiert — kein vollwertiger Filterlisten-Parser.
        static const char *domains[] = {
            "doubleclick.net", "googlesyndication.com", "googleadservices.com",
            "googletagmanager.com", "google-analytics.com", "googletagservices.com",
            "adservice.google.com", "pagead2.googlesyndication.com",
            "adnxs.com", "rubiconproject.com", "criteo.com", "criteo.net",
            "outbrain.com", "taboola.com", "amazon-adsystem.com",
            "adsystem.com", "moatads.com", "scorecardresearch.com",
            "quantserve.com", "adsrvr.org", "pubmatic.com", "smartadserver.com",
            "advertising.com", "ads.yahoo.com", "analytics.yahoo.com",
            "facebook.net", "connect.facebook.net",
            // NOTE: fbcdn.net is NOT blocked - it is Facebook's CORE CDN
            // (JS, CSS, images). Blocking it breaks Facebook completely
            // (only the logo loads, no feed). facebook.net / connect.facebook.net
            // are the trackers (Facebook Pixel) and stay blocked.
            "hotjar.com", "fullstory.com", "newrelic.com", "mixpanel.com",
            "segment.io", "amplitude.com", "branch.io", "appsflyer.com",
            "adjust.com", "taboola.com", "zemanta.com", "revcontent.com",
            "mgid.com", "content.ad", "adroll.com", "perfectaudience.com",
            "bluekai.com", "exelator.com", "mathtag.com", "openx.net",
            "yieldmo.com", "sharethrough.com", "triplelift.com",
            "sovrn.com", "lijit.com", "districtm.io", "sonobi.com",
            "contextweb.com", "pulsepoint.com", "appnexus.com",
        };
        for (const char *d : domains) {
            if (host == d || host.endsWith(QString(".") + d)) return true;
        }
        return false;
    }

    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const QString host = info.requestUrl().host();
        // Nur Dokument-Navigation (Haupt-/Unter-Frame) bekommt den
        // Dokument-Accept-Header. Bilder, Fonts, CSS, JS usw. behalten
        // den nativen Browser-Accept — sonst antwortet ein Bild-/Font-
        // Server mit Content-Negotiation mit HTML/406 statt Bild/Font
        // (z.B. GMX-Ordner-Icons = graue Platzhalter).
        const bool isDocument =
            info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame ||
            info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeSubFrame;
        // Prinzip (generisch, gilt für ALLE Dienste, nicht nur GMX):
        // Der Interceptor setzt nur IDENTITÄT (User-Agent, Sec-CH-UA-*)
        // und Accept (nur für Dokumente). Accept-Language und
        // Accept-Encoding werden bewusst NICHT gesetzt — der Browser
        // kennt Locale (de-DE) und Fähigkeiten (zstd) besser als ein
        // fest verdrahteter Wert. Feste Werte hier würden allen Seiten
        // falsche Sprache / schlechtere Kompression aufzwingen.
        
        if (host.endsWith("gstatic.com")) {
            return;
        }

        // Werbeblocker (generisch, alle Seiten): Werbe-/Tracking-Domain
        // -> Request blocken. Nur Sub-Ressourcen (Bilder, Skripte, XHR);
        // keine Dokument-Navigation blocken (sonst ginge z.B. ein Klick
        // auf eine Werbe-Domain ins Leere statt auf eine Seite).
        if (m_adBlocker && isAdDomain(host) && !isAdBlockException(host) &&
            info.resourceType() != QWebEngineUrlRequestInfo::ResourceTypeMainFrame &&
            info.resourceType() != QWebEngineUrlRequestInfo::ResourceTypeSubFrame) {
            info.block(true);
            return;
        }

        // Check if a per-domain user agent rule applies
        for (const auto &rule : m_rules) {
            if (!rule.pattern.isEmpty() && (host.contains(rule.pattern, Qt::CaseInsensitive) || rule.pattern == "*")) {
                if (rule.mode == "Mobile") {
                    info.setHttpHeader("User-Agent", muMobileUA().toUtf8());
                    info.setHttpHeader("Sec-CH-UA-Mobile", "?1");
                    info.setHttpHeader("Sec-CH-UA-Platform", "\"Android\"");
                    info.setHttpHeader("Sec-CH-UA", "\"Chromium\";v=\"124\", \"Google Chrome\";v=\"124\", \"Not-A.Brand\";v=\"99\"");
                    if (isDocument) info.setHttpHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8");
                    info.setHttpHeader("Accept-Language", "en-US,en;q=0.9,de-DE;q=0.8");
                    info.setHttpHeader("Accept-Encoding", "gzip, deflate, br");
                    return;
                } else if (rule.mode == "Custom" && !rule.customUA.isEmpty()) {
                    info.setHttpHeader("User-Agent", rule.customUA.toUtf8());
                    info.setHttpHeader("Sec-CH-UA-Mobile", "?0");
                    return;
                } else if (rule.mode == "Desktop") {
                    info.setHttpHeader("User-Agent", muChromeUA().toUtf8());
                    info.setHttpHeader("Sec-CH-UA-Mobile", "?0");
                    info.setHttpHeader("Sec-CH-UA-Platform", "\"Linux\"");
                    // KEIN Sec-CH-UA hier (wie 007). Manuell gesetzter
                    // Hint kann vom nativen abweichen -> Mod_Security 406.
                    return;
                }
            }
        }

        if (isGoogleHost(host)) {
            info.setHttpHeader("User-Agent", muFirefox131UA().toUtf8());
            info.setHttpHeader("Sec-CH-UA", "");
            info.setHttpHeader("Sec-CH-UA-Mobile", "");
            info.setHttpHeader("Sec-CH-UA-Platform", "");
            info.setHttpHeader("Sec-CH-UA-Full-Version", "");
            info.setHttpHeader("Sec-CH-UA-Arch", "");
            info.setHttpHeader("Sec-CH-UA-Bitness", "");
            info.setHttpHeader("Sec-CH-UA-Model", "");
            info.setHttpHeader("Sec-CH-UA-Platform-Version", "");
            if (isDocument) info.setHttpHeader("Accept",
                "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8");
            info.setHttpHeader("Accept-Language", "en-US,en;q=0.5");
            info.setHttpHeader("Accept-Encoding", "gzip, deflate, br");
        } else {
            // Fallback: no UA rule matches. Use the GLOBAL mobile mode
            // (user's Mobile/Desktop toggle, saved in config).
            if (m_mobileMode) {
                info.setHttpHeader("User-Agent", muMobileUA().toUtf8());
                info.setHttpHeader("Sec-CH-UA-Mobile", "?1");
                info.setHttpHeader("Sec-CH-UA-Platform", "\"Android\"");
                info.setHttpHeader("Sec-CH-UA", "\"Chromium\";v=\"124\", \"Google Chrome\";v=\"124\", \"Not-A.Brand\";v=\"99\"");
                if (isDocument) info.setHttpHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8");
                info.setHttpHeader("Accept-Language", "en-US,en;q=0.9,de-DE;q=0.8");
                info.setHttpHeader("Accept-Encoding", "gzip, deflate, br");
            } else {
                info.setHttpHeader("User-Agent", muChromeUA().toUtf8());
                if (isDocument) info.setHttpHeader("Accept",
                    "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8");
                info.setHttpHeader("Accept-Language", "en-US,en;q=0.9,de-DE;q=0.8");
                info.setHttpHeader("Accept-Encoding", "gzip, deflate, br");
            }
        }
    }

private:
    QList<UserAgentRule> m_rules;
    bool m_adBlocker = true;
    QStringList m_adBlockExceptions; // Ad blocker exceptions
    bool m_mobileMode = false; // Global Mobile/Desktop toggle // Default: an (früher war Werbung ausgeblendet).
};

// --- Custom Web Page ---

class CustomWebPage : public QWebEnginePage {
    Q_OBJECT
public:
    explicit CustomWebPage(QWebEngineProfile *profile, QWebEngineView *view) 
        : QWebEnginePage(profile, view), m_view(view) {
        
        settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
        settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
        settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
        settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
        settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        settings()->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
        settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
        settings()->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
        settings()->setAttribute(QWebEngineSettings::ErrorPageEnabled, true);

        connect(this, &QWebEnginePage::fullScreenRequested, this, [view](QWebEngineFullScreenRequest request) {
            if (request.toggleOn()) {
                view->showFullScreen();
            } else {
                view->showNormal();
            }
            request.accept();
        });
    }

    void setNewPageFactory(std::function<QWebEnginePage*()> factory) { m_newPageFactory = std::move(factory); }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override {
        Q_UNUSED(url); Q_UNUSED(type); Q_UNUSED(isMainFrame);
        return true;
    }

    QWebEnginePage *createWindow(WebWindowType type) override {
        Q_UNUSED(type);
        if (m_newPageFactory) return m_newPageFactory();
        return nullptr;
    }

private:
    QWebEngineView *m_view;
    std::function<QWebEnginePage*()> m_newPageFactory;
};

class DesktopBrowserWindow;

// --- Custom WebEngineView ---

class CustomWebEngineView : public QWebEngineView {
    Q_OBJECT
public:
    explicit CustomWebEngineView(DesktopBrowserWindow *browserWindow, QWidget *parent = nullptr) 
        : QWebEngineView(parent), m_browserWindow(browserWindow), m_useUnicodeIcons(true), m_isMobileMode(false) {}

    void setUseUnicodeIcons(bool enable) { m_useUnicodeIcons = enable; }
    bool isMobileMode() const { return m_isMobileMode; }

    // Sync the view's flag with the global mode (no toggle, no reload).
    void setMobileMode(bool mobile) { m_isMobileMode = mobile; }
    // Mobile/Desktop toggle: delegates to the BrowserWindow's GLOBAL
    // mobile mode. DECLARED here, DEFINED after BrowserWindow —
    // BrowserWindow is only forward-declared at this point, so calling
    // m_browserWindow->toggleMobileMode() here would be an
    // "invalid use of incomplete type" error.
    void toggleMobileMode();

signals:
    void toggleToolbarRequested();
    void toggleTabbarRequested();
    void toggleDarkModeRequested();
    void setAsHomepageRequested();
    void openSettingsRequested();
    void openUrlInNewTabRequested(const QString &url);
    void addCurrentBookmarkRequested();
    void mobileModeToggled(bool isMobile);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override {
        QMenu menu(this);
        auto lbl = [&](const char *icon, const char *text) { return m_useUnicodeIcons ? QString::fromUtf8(icon) + " " + QString::fromUtf8(text) : QString::fromUtf8(text); };

        
        QAction *backAction = menu.addAction(lbl("\u25C0", "Back"));
        backAction->setEnabled(history()->canGoBack());
        connect(backAction, &QAction::triggered, this, &QWebEngineView::back);

        QAction *forwardAction = menu.addAction(lbl("\u25B6", "Forward"));
        forwardAction->setEnabled(history()->canGoForward());
        connect(forwardAction, &QAction::triggered, this, &QWebEngineView::forward);

        QAction *reloadAction = menu.addAction(lbl("\u27F3", "Reload"));
        connect(reloadAction, &QAction::triggered, this, [this]() {
            page()->triggerAction(QWebEnginePage::Reload);
        });

        menu.addSeparator();

        if (hasSelection()) {
            QString selText = selectedText().trimmed();
            if (!selText.isEmpty()) {
                QAction *openSelAction = menu.addAction(lbl("\u2197", "Open selected text in new tab"));
                connect(openSelAction, &QAction::triggered, this, [this, selText]() {
                    QString targetUrl = selText;
                    if (!targetUrl.startsWith("http://") && !targetUrl.startsWith("https://") && !targetUrl.startsWith("file://")) {
                        if (targetUrl.contains(".") && !targetUrl.contains(" ")) {
                            targetUrl = "https://" + targetUrl;
                        } else {
                            targetUrl = "https://duckduckgo.com/?q=" + QUrl::toPercentEncoding(selText);
                        }
                    }
                    emit openUrlInNewTabRequested(targetUrl);
                });
                menu.addSeparator();
            }
        }

        QAction *copyAction = menu.addAction(lbl("\u29C9", "Copy"));
        copyAction->setEnabled(hasSelection());
        connect(copyAction, &QAction::triggered, this, [this]() {
            triggerPageAction(QWebEnginePage::Copy);
        });

        QAction *pasteAction = menu.addAction(lbl("\U0001F4CB", "Paste"));
        connect(pasteAction, &QAction::triggered, this, [this]() {
            triggerPageAction(QWebEnginePage::Paste);
        });

        auto contextData = lastContextMenuRequest();
        
        if (contextData && !contextData->linkUrl().isEmpty()) {
            QUrl linkUrl = contextData->linkUrl();
            if (linkUrl.isValid()) {
                menu.addSeparator();
                QAction *openLinkAction = menu.addAction(lbl("\u2197", "Open Link in New Tab"));
                connect(openLinkAction, &QAction::triggered, this, [this, linkUrl]() {
                    emit openUrlInNewTabRequested(linkUrl.toString());
                });
            }
        }

        if (contextData && contextData->mediaType() == QWebEngineContextMenuRequest::MediaTypeImage) {
            QUrl mediaUrl = contextData->mediaUrl();
            if (mediaUrl.isValid()) {
                menu.addSeparator();
                QAction *openImgAction = menu.addAction(lbl("\U0001F5BC", "Open Image in New Tab"));
                connect(openImgAction, &QAction::triggered, this, [this, mediaUrl]() {
                    emit openUrlInNewTabRequested(mediaUrl.toString());
                });
                QAction *copyImgAction = menu.addAction(lbl("\u29C9", "Copy Image"), this, [this]() {
                    triggerPageAction(QWebEnginePage::CopyImageToClipboard);
                });
                QAction *saveImgAction = menu.addAction(lbl("\U0001F4BE", "Save Image"), this, [this]() {
                    triggerPageAction(QWebEnginePage::DownloadImageToDisk);
                });

                auto openInExternalEditor = [mediaUrl](const QString &program) {
                    QString targetPath;
                    if (mediaUrl.isLocalFile()) {
                        targetPath = mediaUrl.toLocalFile();
                    } else if (mediaUrl.scheme() == "http" || mediaUrl.scheme() == "https") {
                        QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + QFileInfo(mediaUrl.path()).fileName();
                        if (tempPath.endsWith("/")) tempPath += "downloaded_image.png";
                        
                        QNetworkAccessManager manager;
                        QNetworkRequest request(mediaUrl);
                        QNetworkReply *reply = manager.get(request);
                        QEventLoop loop;
                        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                        loop.exec();
                        
                        if (reply->error() == QNetworkReply::NoError) {
                            QFile file(tempPath);
                            if (file.open(QIODevice::WriteOnly)) {
                                file.write(reply->readAll());
                                file.close();
                                targetPath = tempPath;
                            }
                        }
                        reply->deleteLater();
                    }

                    if (!targetPath.isEmpty() && QFile::exists(targetPath)) {
                        QProcess::startDetached(program, QStringList() << targetPath);
                    } else {
                        QMessageBox::warning(nullptr, "Error", QString("Could not open image in %1.").arg(program));
                    }
                };

                QAction *gimpAction = menu.addAction(lbl("\U0001F3A8", "Open in GIMP"));
                connect(gimpAction, &QAction::triggered, this, [openInExternalEditor]() {
                    openInExternalEditor("gimp");
                });

                QAction *mtpaintAction = menu.addAction(lbl("\U0001F58C", "Open in mtPaint"));
                connect(mtpaintAction, &QAction::triggered, this, [openInExternalEditor]() {
                    openInExternalEditor("mtpaint");
                });
            }
        }

        if (contextData && contextData->mediaType() == QWebEngineContextMenuRequest::MediaTypeVideo) {
            QUrl mediaUrl = contextData->mediaUrl();
            if (mediaUrl.isValid()) {
                menu.addSeparator();
                QAction *openVideoAction = menu.addAction(lbl("\U0001F3A5", "Open Video in New Tab"));
                connect(openVideoAction, &QAction::triggered, this, [this, mediaUrl]() {
                    emit openUrlInNewTabRequested(mediaUrl.toString());
                });
                QAction *saveVideoAction = menu.addAction(lbl("\U0001F4BE", "Save Video"), this, [this, mediaUrl]() {
                    QString defaultName = QFileInfo(mediaUrl.path()).fileName();
                    if (defaultName.isEmpty()) defaultName = "video.mp4";
                    QString savePath = QFileDialog::getSaveFileName(this, "Save Video As", QDir::homePath() + "/" + defaultName, "Video Files (*.mp4 *.webm *.ogg *.mkv *.avi);;All Files (*.*)");
                    if (!savePath.isEmpty()) {
                        QNetworkAccessManager *manager = new QNetworkAccessManager(this);
                        QNetworkRequest request(mediaUrl);
                        QNetworkReply *reply = manager->get(request);
                        QMessageBox *progressBox = new QMessageBox(QMessageBox::Information, "Downloading Video", "Downloading video from:\n" + mediaUrl.toString() + "\nPlease wait...", QMessageBox::NoButton, this);
                        progressBox->setStandardButtons(QMessageBox::Cancel);
                        connect(progressBox, &QMessageBox::buttonClicked, reply, &QNetworkReply::abort);
                        progressBox->show();

                        connect(reply, &QNetworkReply::finished, this, [reply, savePath, progressBox]() {
                            progressBox->deleteLater();
                            if (reply->error() == QNetworkReply::NoError) {
                                QFile file(savePath);
                                if (file.open(QIODevice::WriteOnly)) {
                                    file.write(reply->readAll());
                                    file.close();
                                    QMessageBox::information(nullptr, "Success", "Video saved successfully to:\n" + savePath);
                                } else {
                                    QMessageBox::warning(nullptr, "Error", "Could not write file to:\n" + savePath);
                                }
                            } else if (reply->error() != QNetworkReply::OperationCanceledError) {
                                QMessageBox::warning(nullptr, "Error", "Failed to download video: " + reply->errorString());
                            }
                            reply->deleteLater();
                        });
                    }
                });

                QAction *vlcAction = menu.addAction(lbl("\U0001F3AC", "Open in VLC"));
                connect(vlcAction, &QAction::triggered, this, [mediaUrl]() {
                    if (!QProcess::startDetached("vlc", QStringList() << mediaUrl.toString())) {
                        QMessageBox::warning(nullptr, "Error", "Could not open in VLC. Make sure VLC is installed.");
                    }
                });

                QAction *mpvAction = menu.addAction(lbl("\U0001F3A5", "Open in MPV"));
                connect(mpvAction, &QAction::triggered, this, [mediaUrl]() {
                    if (!QProcess::startDetached("mpv", QStringList() << mediaUrl.toString())) {
                        QMessageBox::warning(nullptr, "Error", "Could not open in MPV. Make sure MPV is installed.");
                    }
                });
            }
        }

        menu.addSeparator();

        QAction *findVideoAction = menu.addAction(lbl("\U0001F50D", "Find & Save Video..."));
        connect(findVideoAction, &QAction::triggered, this, [this]() {
            page()->runJavaScript(R"(
                (function() {
                    let urls = [];
                    document.querySelectorAll('video').forEach(v => {
                        if (v.src) urls.push(v.src);
                        v.querySelectorAll('source').forEach(s => { if (s.src) urls.push(s.src); });
                    });
                    document.querySelectorAll('source, embed, object').forEach(el => {
                        if (el.src) urls.push(el.src);
                    });
                    return Array.from(new Set(urls));
                })();
            )", [this](const QVariant &result) {
                QVariantList list = result.toList();
                if (list.isEmpty()) {
                    QMessageBox::information(this, "Find & Save Video", "No video sources found on this page.");
                    return;
                }
                QString chosen;
                if (list.size() == 1) {
                    chosen = list.first().toString();
                } else {
                    QStringList items;
                    for (const auto &v : list) items.append(v.toString());
                    bool ok = false;
                    chosen = QInputDialog::getItem(this, "Find & Save Video", "Select video to download:", items, 0, false, &ok);
                    if (!ok || chosen.isEmpty()) return;
                }

                if (!chosen.isEmpty()) {
                    QUrl targetUrl(chosen);
                    QString defaultName = QFileInfo(targetUrl.path()).fileName();
                    if (defaultName.isEmpty()) defaultName = "video.mp4";
                    QString savePath = QFileDialog::getSaveFileName(this, "Save Video As", QDir::homePath() + "/" + defaultName, "Video Files (*.mp4 *.webm *.ogg *.mkv *.avi);;All Files (*.*)");
                    if (!savePath.isEmpty()) {
                        QNetworkAccessManager *manager = new QNetworkAccessManager(this);
                        QNetworkRequest request(targetUrl);
                        QNetworkReply *reply = manager->get(request);
                        QMessageBox *progressBox = new QMessageBox(QMessageBox::Information, "Downloading Video", "Downloading video from:\n" + chosen + "\nPlease wait...", QMessageBox::NoButton, this);
                        progressBox->setStandardButtons(QMessageBox::Cancel);
                        connect(progressBox, &QMessageBox::buttonClicked, reply, &QNetworkReply::abort);
                        progressBox->show();

                        connect(reply, &QNetworkReply::finished, this, [reply, savePath, progressBox]() {
                            progressBox->deleteLater();
                            if (reply->error() == QNetworkReply::NoError) {
                                QFile file(savePath);
                                if (file.open(QIODevice::WriteOnly)) {
                                    file.write(reply->readAll());
                                    file.close();
                                    QMessageBox::information(nullptr, "Success", "Video saved successfully to:\n" + savePath);
                                } else {
                                    QMessageBox::warning(nullptr, "Error", "Could not write file to:\n" + savePath);
                                }
                            } else if (reply->error() != QNetworkReply::OperationCanceledError) {
                                QMessageBox::warning(nullptr, "Error", "Failed to download video: " + reply->errorString());
                            }
                            reply->deleteLater();
                        });
                    }
                }
            });
        });

        QAction *ytdlpAction = menu.addAction(lbl("\U0001F4E5", "Download with yt-dlp..."));
        connect(ytdlpAction, &QAction::triggered, this, [this]() {
            QString pageUrl = url().toString();
            QString defaultName = "video.mp4";
            QString savePath = QFileDialog::getSaveFileName(this, "Download Video with yt-dlp", QDir::homePath() + "/" + defaultName, "Video Files (*.mp4 *.webm *.mkv);;All Files (*.*)");
            if (!savePath.isEmpty()) {
                QString termProg = "xterm";
                if (!QStandardPaths::findExecutable("xterm").isEmpty()) {
                    termProg = "xterm";
                } else if (!QStandardPaths::findExecutable("urxvt").isEmpty()) {
                    termProg = "urxvt";
                } else if (!QStandardPaths::findExecutable("rxvt").isEmpty()) {
                    termProg = "rxvt";
                } else if (!QStandardPaths::findExecutable("lxterminal").isEmpty()) {
                    termProg = "lxterminal";
                } else if (!QStandardPaths::findExecutable("x-terminal-emulator").isEmpty()) {
                    termProg = "x-terminal-emulator";
                }
                QStringList args;
                args << "-e" << QString("yt-dlp --no-playlist -o \"%1\" \"%2\"; echo; echo 'Press Enter to close terminal...'; read").arg(savePath).arg(pageUrl);
                QProcess::startDetached(termProg, args);
            }
        });

        if (url().toString().contains("youtube.com") || url().toString().contains("youtu.be")) {
            menu.addSeparator();
            QAction *vlcPageAction = menu.addAction(lbl("\U0001F3AC", "Play YouTube in VLC"));
            connect(vlcPageAction, &QAction::triggered, this, [this]() {
                if (!QProcess::startDetached("vlc", QStringList() << url().toString())) {
                    QMessageBox::warning(nullptr, "Error", "Could not open in VLC. Make sure VLC is installed.");
                }
            });
            QAction *mpvPageAction = menu.addAction(lbl("\U0001F3A5", "Play YouTube in MPV"));
            connect(mpvPageAction, &QAction::triggered, this, [this]() {
                if (!QProcess::startDetached("mpv", QStringList() << url().toString())) {
                    QMessageBox::warning(nullptr, "Error", "Could not open in MPV. Make sure MPV is installed.");
                }
            });
        }

        menu.addSeparator();

        QString mobStr = lbl("\U0001F4F1", m_isMobileMode ? "Switch to Desktop Mode" : "Switch to Mobile Mode");
        QAction *mobAction = menu.addAction(mobStr);
        connect(mobAction, &QAction::triggered, this, [this]() {
            toggleMobileMode();
            emit mobileModeToggled(m_isMobileMode);
        });

        QAction *addBmAction = menu.addAction(lbl("\u2605", "Add to Bookmarks"));
        connect(addBmAction, &QAction::triggered, this, &CustomWebEngineView::addCurrentBookmarkRequested);

        QAction *homeAction = menu.addAction(lbl("\u2302", "Set as Homepage"));
        connect(homeAction, &QAction::triggered, this, &CustomWebEngineView::setAsHomepageRequested);

        QAction *settingsAction = menu.addAction(lbl("\U0001F6E0", "Browser Settings..."));
        connect(settingsAction, &QAction::triggered, this, &CustomWebEngineView::openSettingsRequested);

        menu.addSeparator();

        QAction *fullscreenAction = menu.addAction(lbl("\u26F6", "Toggle Fullscreen"));
        connect(fullscreenAction, &QAction::triggered, this, [this]() {
            if (auto *win = this->window()) {
                if (win->isFullScreen()) win->showNormal();
                else win->showFullScreen();
            }
        });

        QAction *toggleAction = menu.addAction(lbl("\u25AD", "Toggle Toolbar"));
        connect(toggleAction, &QAction::triggered, this, &CustomWebEngineView::toggleToolbarRequested);

        QAction *toggleTabsAction = menu.addAction(lbl("\u21E5", "Toggle Tabbar"));
        connect(toggleTabsAction, &QAction::triggered, this, &CustomWebEngineView::toggleTabbarRequested);

        QAction *darkModeAction = menu.addAction(lbl("\U0001F319", "Toggle Dark Mode"));
        connect(darkModeAction, &QAction::triggered, this, &CustomWebEngineView::toggleDarkModeRequested);

        menu.exec(event->globalPos());
    }

private:
    DesktopBrowserWindow *m_browserWindow;
    bool m_useUnicodeIcons;
    bool m_isMobileMode;
};

// --- Editor Dialogs ---

class BookmarkEditorDialog : public QDialog {
    Q_OBJECT
public:
    BookmarkEditorDialog(const QList<BookmarkItem> &bookmarks, QWidget *parent = nullptr)
        : QDialog(parent), m_bookmarks(bookmarks)
    {
        setWindowTitle("Edit Bookmarks");
        resize(500, 350);

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("Format per line: Title | URL", this));

        textEdit = new QTextEdit(this);
        QString content;
        for (const auto &b : m_bookmarks) {
            content += b.title + " | " + b.url + "\n";
        }
        textEdit->setText(content);
        layout->addWidget(textEdit);

        auto *btnLayout = new QHBoxLayout();
        auto *btnSave = new QPushButton("Save", this);
        auto *btnCancel = new QPushButton("Cancel", this);
        btnLayout->addStretch();
        btnLayout->addWidget(btnSave);
        btnLayout->addWidget(btnCancel);
        layout->addLayout(btnLayout);

        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QList<BookmarkItem> getBookmarks() const {
        QList<BookmarkItem> list;
        QStringList lines = textEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            int idx = line.indexOf(" | ");
            if (idx != -1) {
                QString title = line.left(idx).trimmed();
                QString url = line.mid(idx + 3).trimmed();
                if (!title.isEmpty() && !url.isEmpty()) {
                    list.append(BookmarkItem{title, url});
                }
            }
        }
        return list;
    }

private:
    QTextEdit *textEdit;
    QList<BookmarkItem> m_bookmarks;
};

class PasswordEditorDialog : public QDialog {
    Q_OBJECT
public:
    PasswordEditorDialog(const QList<PasswordEntry> &entries, std::function<void(const QString&)> onChangeMasterPass, QWidget *parent = nullptr)
        : QDialog(parent), m_entries(entries), m_onChangeMasterPass(onChangeMasterPass)
    {
        setWindowTitle("Encrypted Password Manager Editor");
        resize(550, 420);

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("<b>Format per line:</b> Title/Domain | Username | Password", this));

        textEdit = new QTextEdit(this);
        QString content;
        for (const auto &e : m_entries) {
            content += e.titleUrl + " | " + e.username + " | " + e.password + "\n";
        }
        textEdit->setText(content);
        layout->addWidget(textEdit);

        auto *btnLayout = new QHBoxLayout();
        auto *btnChangeMaster = new QPushButton("Change Master Password...", this);
        auto *btnSave = new QPushButton("Save Encrypted", this);
        auto *btnCancel = new QPushButton("Cancel", this);

        btnLayout->addWidget(btnChangeMaster);
        btnLayout->addStretch();
        btnLayout->addWidget(btnSave);
        btnLayout->addWidget(btnCancel);
        layout->addLayout(btnLayout);

        connect(btnChangeMaster, &QPushButton::clicked, this, [this]() {
            bool ok1 = false;
            QString newPass = QInputDialog::getText(this, "Change Master Password", "Enter NEW Master Password:", QLineEdit::Password, "", &ok1);
            if (!ok1 || newPass.isEmpty()) return;

            bool ok2 = false;
            QString confirmPass = QInputDialog::getText(this, "Confirm Master Password", "Confirm NEW Master Password:", QLineEdit::Password, "", &ok2);
            if (!ok2 || confirmPass != newPass) {
                QMessageBox::warning(this, "Error", "Passwords do not match!");
                return;
            }

            if (m_onChangeMasterPass) {
                m_onChangeMasterPass(newPass);
                QMessageBox::information(this, "Success", "Master Password changed successfully!");
            }
        });

        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QList<PasswordEntry> getEntries() const {
        QList<PasswordEntry> list;
        QStringList lines = textEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            QStringList parts = line.split(" | ");
            if (parts.size() >= 3) {
                QString title = parts[0].trimmed();
                QString user = parts[1].trimmed();
                QString pass = parts.mid(2).join(" | ").trimmed();
                if (!title.isEmpty()) {
                    list.append(PasswordEntry{title, user, pass});
                }
            }
        }
        return list;
    }

private:
    QTextEdit *textEdit;
    QList<PasswordEntry> m_entries;
    std::function<void(const QString&)> m_onChangeMasterPass;
};

class CssEditorDialog : public QDialog {
    Q_OBJECT
public:
    CssEditorDialog(const QList<StyleRule> &rules, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Domain-Specific User CSS Styles Editor");
        resize(600, 450);

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("<b>Format per rule:</b> Domain/URL Pattern | CSS Code\n<i>Example: google.com | main, div[role=\"main\"] { margin-left: 80px !important; }</i>", this));

        textEdit = new QTextEdit(this);
        QString content;
        for (const auto &r : rules) {
            content += r.pattern + " | " + r.cssCode + "\n";
        }
        textEdit->setText(content);
        layout->addWidget(textEdit);

        auto *btnLayout = new QHBoxLayout();
        auto *btnSave = new QPushButton("Save Styles", this);
        auto *btnCancel = new QPushButton("Cancel", this);
        btnLayout->addStretch();
        btnLayout->addWidget(btnSave);
        btnLayout->addWidget(btnCancel);
        layout->addLayout(btnLayout);

        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QList<StyleRule> getRules() const {
        QList<StyleRule> list;
        QStringList lines = textEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            int idx = line.indexOf(" | ");
            if (idx != -1) {
                QString pattern = line.left(idx).trimmed();
                QString css = line.mid(idx + 3).trimmed();
                if (!pattern.isEmpty() && !css.isEmpty()) {
                    list.append(StyleRule{pattern, css});
                }
            }
        }
        return list;
    }

private:
    QTextEdit *textEdit;
};

class UserAgentEditorDialog : public QDialog {
    Q_OBJECT
public:
    UserAgentEditorDialog(const QList<UserAgentRule> &rules, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Per-Domain User-Agent Rules");
        resize(650, 450);

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("<b>Format per line:</b> Domain/Pattern | Mode (Desktop/Mobile/Custom) | [Optional Custom UA]\n<i>Example: google.com | Custom | Mozilla/5.0 (X11; Linux x86_64; rv:131.0) Gecko/20100101 Firefox/131.0</i>\n<i>Example: facebook.com | Mobile | </i>", this));

        textEdit = new QTextEdit(this);
        QString content;
        for (const auto &r : rules) {
            content += r.pattern + " | " + r.mode + (r.customUA.isEmpty() ? "" : (" | " + r.customUA)) + "\n";
        }
        textEdit->setText(content);
        layout->addWidget(textEdit);

        auto *btnLayout = new QHBoxLayout();
        auto *btnSave = new QPushButton("Save UA Rules", this);
        auto *btnCancel = new QPushButton("Cancel", this);
        btnLayout->addStretch();
        btnLayout->addWidget(btnSave);
        btnLayout->addWidget(btnCancel);
        layout->addLayout(btnLayout);

        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QList<UserAgentRule> getRules() const {
        QList<UserAgentRule> list;
        QStringList lines = textEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            QStringList parts = line.split(" | ");
            if (parts.size() >= 2) {
                QString pattern = parts[0].trimmed();
                QString mode = parts[1].trimmed();
                QString customUA = parts.size() >= 3 ? parts.mid(2).join(" | ").trimmed() : "";
                if (!pattern.isEmpty() && !mode.isEmpty()) {
                    list.append(UserAgentRule{pattern, mode, customUA});
                }
            }
        }
        return list;
    }

private:
    QTextEdit *textEdit;
};

class NotesDialog : public QDialog {
    Q_OBJECT
public:
    explicit NotesDialog(const QString &filePath, QWidget *parent = nullptr)
        : QDialog(parent), m_filePath(filePath)
    {
        setWindowTitle("Mini Notes Editor (" + QFileInfo(filePath).fileName() + ")");
        resize(500, 400);

        auto *layout = new QVBoxLayout(this);
        textEdit = new QTextEdit(this);

        QFile file(m_filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            textEdit->setText(QString::fromUtf8(file.readAll()));
            file.close();
        }
        layout->addWidget(textEdit);

        auto *btnLayout = new QHBoxLayout();
        auto *btnSave = new QPushButton("Save Notes", this);
        auto *btnClose = new QPushButton("Close", this);
        btnLayout->addStretch();
        btnLayout->addWidget(btnSave);
        btnLayout->addWidget(btnClose);
        layout->addLayout(btnLayout);

        connect(btnSave, &QPushButton::clicked, this, &NotesDialog::save);
        connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    }

private:
    void save() {
        QFileInfo fi(m_filePath);
        QDir().mkpath(fi.absolutePath());
        QFile file(m_filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(textEdit->toPlainText().toUtf8());
            file.close();
            QMessageBox::information(this, "Saved", "Notes saved successfully to:\n" + m_filePath);
        } else {
            QMessageBox::warning(this, "Error", "Could not save notes file:\n" + m_filePath);
        }
    }

    QString m_filePath;
    QTextEdit *textEdit;
};

// --- Categorized Notebook Settings Dialog ---

// rgba() mit Integer-Alpha (0-255). Qt-Stylesheets parsen Float-Alpha
// (0.0-1.0) als ~0, was alles fast unsichtbar machte (Geminis Fix).
static QString muRgba(const QColor &c, int pct) {
    int a = qBound(0, (qBound(10, pct, 100) * 255) / 100, 255);
    return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
}

// Ist das System-Qt-Theme dunkel? (Luminanz-Check, Geminis Technik aus
// der Test-App). Wichtig: m_darkMode ist nur die App-Einstellung. Wenn
// die aus ist, muss trotzdem das echte System-Theme gelten — sonst
// springt die App bei dunklem System-Theme auf Weiss.
static bool paletteIsDark() {
    QColor w = QApplication::palette().window().color();
    return (0.299 * w.red() + 0.587 * w.green() + 0.114 * w.blue()) < 128;
}

// Gezeichnete Button-Icons (QPainter). Farbe folgt der UI-Helligkeit.
static QIcon drawBtnIcon(const QString &kind, bool darkUi) {
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor fg = darkUi ? QColor(0xe0, 0xe0, 0xe0) : QColor(0x20, 0x20, 0x20);
    QPen pen(fg, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    if (kind == "back") { QPolygonF t; t << QPointF(15, 4) << QPointF(5, 10) << QPointF(15, 16); p.drawPolygon(t); }
    else if (kind == "forward") { QPolygonF t; t << QPointF(5, 4) << QPointF(15, 10) << QPointF(5, 16); p.drawPolygon(t); }
    else if (kind == "reload") { p.drawArc(3, 3, 14, 14, 45 * 16, 270 * 16); QPolygonF t; t << QPointF(16, 2) << QPointF(18, 6) << QPointF(13, 5); p.drawPolygon(t); }
    else if (kind == "home") { QPolygonF t; t << QPointF(10, 3) << QPointF(3, 10) << QPointF(17, 10); p.drawPolygon(t); p.drawRect(6, 10, 8, 7); }
    else if (kind == "settings") { // Proper gear: body + 8 teeth + center hole.
        // Distinct from "winmenu" (window frame). Fixed coords, no QtMath.
        p.drawEllipse(6, 6, 8, 8); p.drawEllipse(8, 8, 4, 4);
        p.drawLine(10, 3, 10, 5); p.drawLine(15, 5, 13, 7); p.drawLine(17, 10, 15, 10);
        p.drawLine(15, 15, 13, 13); p.drawLine(10, 17, 10, 15); p.drawLine(5, 15, 7, 13);
        p.drawLine(3, 10, 5, 10); p.drawLine(5, 5, 7, 7); }
    else if (kind == "winmenu") { // Window Menu: window frame + title bar + 3 menu lines.
        // Distinct from "settings" (gear). Top sidebar button.
        p.drawRect(3, 3, 14, 14); p.drawLine(3, 7, 17, 7); p.drawEllipse(14, 4, 2, 2);
        p.drawLine(6, 10, 14, 10); p.drawLine(6, 12, 14, 12); p.drawLine(6, 14, 14, 14); }
    else if (kind == "user") { p.drawEllipse(7, 3, 6, 6); p.drawArc(3, 10, 14, 10, 0, 180 * 16); }
    else if (kind == "pass") { p.drawEllipse(4, 4, 8, 8); p.drawLine(10, 12, 10, 17); p.drawLine(10, 17, 14, 17); }
    else if (kind == "edit") { p.drawLine(5, 15, 13, 7); p.drawLine(13, 7, 16, 4); p.drawLine(5, 15, 8, 16); }
    else if (kind == "mobile") { p.drawRect(6, 2, 8, 16); p.drawLine(8, 15, 12, 15); }
    else if (kind == "notes") { p.drawRect(4, 3, 12, 14); p.drawLine(7, 8, 13, 8); p.drawLine(7, 11, 13, 11); }
    else if (kind == "newtab") { p.drawLine(10, 4, 10, 16); p.drawLine(4, 10, 16, 10); }
    // Domainliste (Passwort-Popup): drei Listen-Zeilen mit Punkten.
    else if (kind == "passlist") { p.drawEllipse(4, 4, 3, 3); p.drawLine(9, 5, 16, 5); p.drawEllipse(4, 9, 3, 3); p.drawLine(9, 10, 16, 10); p.drawEllipse(4, 14, 3, 3); p.drawLine(9, 15, 16, 15); }
    // Bookmarks (Popup): Lesezeichen-Band.
    else if (kind == "bookmarks") { p.drawRect(6, 3, 8, 14); p.drawLine(6, 17, 10, 13); p.drawLine(10, 13, 14, 17); }
    else { p.drawEllipse(5, 5, 10, 10); }
    return QIcon(pm);
}

class BrowserSettingsDialog : public QDialog {
    Q_OBJECT
public:
    BrowserSettingsDialog(const QString &homepage, bool strictPrivacy, bool adBlocker, bool darkMode, bool unicodeIcons, bool frameless,
                          int urlMinWidth, int reloadMinWidth, int urlBarSpacerMinWidth, int defaultZoom, int uiZoom, int tabZoom,
                          bool showNav, bool showReload, bool showHome, bool showUrl, 
                          bool showGo, bool showBookmarks, bool showAddBookmark, 
                          bool showTabJump, bool showNewTab, bool showZoom,
                          bool showPassList, bool showPassButtons, bool showMobileToggle, bool showClock, bool showNotes,
                          int toolbarPosition, bool showSideToolbar,
                          int tabBarPosition, int sideBarPosition, bool showTabNewTab, bool clock24h,
                          bool showSideWinMenu, bool showSideSettings, bool showSideReload, bool showSideNav, bool showSideHome,
                          bool showSidePassManager, bool showSideMobile, bool showSideNotes, bool showSideNewTab,
                          const QString &clockCmd, const QString &notesCmd,
                          bool useButtonIcons,
                          int transTabBarBg, int transTabBarControls, int transToolbarBg, int transToolbarControls,
                          int transSideBg, int transSideControls, int transWebpage,
                          const QStringList &adBlockExceptions,
                          QWidget *parent = nullptr)
        : QDialog(parent) 
    {
        setWindowTitle("mu-browser-desktop Setup & Settings");
        resize(560, 620);

        auto *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(8, 8, 8, 8);

        tabWidget = new QTabWidget(this);

        // --- TAB 1: General & Commands ---
        auto *tabGeneral = new QWidget(this);
        auto *layGeneral = new QVBoxLayout(tabGeneral);

        layGeneral->addWidget(new QLabel("<b>Homepage URL:</b>", this));
        homepageEdit = new QLineEdit(homepage, this);
        layGeneral->addWidget(homepageEdit);

        layGeneral->addWidget(new QLabel("<b>Clock Widget Command on Click:</b>", this));
        clockCmdEdit = new QLineEdit(clockCmd, this);
        layGeneral->addWidget(clockCmdEdit);

        chkClock24h = new QCheckBox("Use 24-Hour Clock Format", this);
        chkClock24h->setChecked(clock24h);
        layGeneral->addWidget(chkClock24h);

        layGeneral->addWidget(new QLabel("<b>Notes Button ('n') Command:</b>", this));
        notesCmdEdit = new QLineEdit(notesCmd, this);
        layGeneral->addWidget(notesCmdEdit);

        layGeneral->addWidget(new QLabel("<b>Default Web Page Zoom (%):</b>", this));
        zoomSpin = new QSpinBox(this);
        zoomSpin->setRange(40, 250);
        zoomSpin->setSingleStep(10);
        zoomSpin->setValue(defaultZoom);
        layGeneral->addWidget(zoomSpin);

        layGeneral->addWidget(new QLabel("<b>Minimum URL Bar Width (px):</b>", this));
        urlWidthSpin = new QSpinBox(this);
        urlWidthSpin->setRange(50, 2000);
        urlWidthSpin->setValue(urlMinWidth);
        layGeneral->addWidget(urlWidthSpin);
        // Spacer hinter der URL-Bar: Mindestbreite (transparentes Loch).
        layGeneral->addWidget(new QLabel("URL Bar Spacer Minimum Width (transparent gap)", this));
        urlBarSpacerMinWidthSpin = new QSpinBox(this);
        urlBarSpacerMinWidthSpin->setRange(0, 800);
        urlBarSpacerMinWidthSpin->setValue(urlBarSpacerMinWidth);
        layGeneral->addWidget(urlBarSpacerMinWidthSpin);

        layGeneral->addWidget(new QLabel("<b>Minimum Reload Button Width (px):</b>", this));
        reloadWidthSpin = new QSpinBox(this);
        reloadWidthSpin->setRange(20, 500);
        reloadWidthSpin->setValue(reloadMinWidth);
        layGeneral->addWidget(reloadWidthSpin);

        // Darstellung: gehoert nicht in Privacy (war dort vorher).
        darkModeCheck = new QCheckBox("Enable Dark Mode (True Black & Scrollbars)", this);
        darkModeCheck->setChecked(darkMode);
        layGeneral->addWidget(darkModeCheck);
        unicodeIconsCheck = new QCheckBox("Use Unicode Symbols in Context Menu", this);
        unicodeIconsCheck->setChecked(unicodeIcons);
        layGeneral->addWidget(unicodeIconsCheck);
        chkUseButtonIcons = new QCheckBox("Drawn Icons Instead of Letters for Buttons (u/p/e/m/n ...)", this);
        chkUseButtonIcons->setChecked(useButtonIcons);
        layGeneral->addWidget(chkUseButtonIcons);

        layGeneral->addStretch(1);

        tabWidget->addTab(tabGeneral, "🌐 General");

        // --- TAB 2: Layout & Toolbars ---
        auto *tabLayout = new QWidget(this);
        auto *layLayout = new QVBoxLayout(tabLayout);

        layLayout->addWidget(new QLabel("<b>Main Toolbar Position:</b>", this));
        comboToolbarPos = new QComboBox(this);
        comboToolbarPos->addItem("Top (Above Tabbar)", 0);
        comboToolbarPos->addItem("Middle (Below Tabbar, Above Page)", 1);
        comboToolbarPos->addItem("Bottom (Below Web Page)", 2);
        comboToolbarPos->setCurrentIndex(qBound(0, toolbarPosition, 2));
        layLayout->addWidget(comboToolbarPos);

        layLayout->addWidget(new QLabel("<b>Tab Bar Position:</b>", this));
        comboTabBarPos = new QComboBox(this);
        comboTabBarPos->addItem("Top (Above Web Content)", 0);
        comboTabBarPos->addItem("Bottom (Below Web Content)", 1);
        comboTabBarPos->setCurrentIndex(qBound(0, tabBarPosition, 1));
        layLayout->addWidget(comboTabBarPos);
        // Sidebar-Position (links/rechts).
        layLayout->addWidget(new QLabel("Sidebar Position", this));
        comboSideBarPos = new QComboBox(this);
        comboSideBarPos->addItem("Left", 0);
        comboSideBarPos->addItem("Right", 1);
        comboSideBarPos->setCurrentIndex(qBound(0, sideBarPosition, 1));
        layLayout->addWidget(comboSideBarPos);

        chkTabNewTab = new QCheckBox("Show New Tab Button Beside Tab Bar (+)", this);
        chkTabNewTab->setChecked(showTabNewTab);
        layLayout->addWidget(chkTabNewTab);

        chkShowSideToolbar = new QCheckBox("Add Vertical Side Toolbar (Pseudo-Titlebar)", this);
        chkShowSideToolbar->setChecked(showSideToolbar);
        layLayout->addWidget(chkShowSideToolbar);

        chkFrameless = new QCheckBox("Remove Window Titlebar (Frameless)", this);
        chkFrameless->setChecked(frameless);
        layLayout->addWidget(chkFrameless);

        layLayout->addWidget(new QLabel("<b>UI Scaling / Toolbar Size (%):</b> [50 - 250]", this));
        uiZoomSpin = new QSpinBox(this);
        uiZoomSpin->setRange(50, 250);
        uiZoomSpin->setSingleStep(10);
        uiZoomSpin->setValue(uiZoom);
        layLayout->addWidget(uiZoomSpin);

        layLayout->addWidget(new QLabel("<b>Tab Bar Font Scaling (%):</b> [50 - 250]", this));
        tabZoomSpin = new QSpinBox(this);
        tabZoomSpin->setRange(50, 250);
        tabZoomSpin->setSingleStep(10);
        tabZoomSpin->setValue(tabZoom);
        layLayout->addWidget(tabZoomSpin);
        layLayout->addStretch(1);

        tabWidget->addTab(tabLayout, "📐 Layout & Positioning");

        // --- TAB 3: Main Toolbar Elements ---
        auto *tabToolbarBtns = new QWidget(this);
        auto *scrollToolbar = new QScrollArea(tabToolbarBtns);
        scrollToolbar->setWidgetResizable(true);
        auto *contentToolbar = new QWidget(scrollToolbar);
        auto *layToolbarBtns = new QVBoxLayout(contentToolbar);

        chkNav = new QCheckBox("Show Back/Forward Buttons", this); chkNav->setChecked(showNav);
        chkReload = new QCheckBox("Show Reload Button", this); chkReload->setChecked(showReload);
        chkHome = new QCheckBox("Show Home Button", this); chkHome->setChecked(showHome);
        chkUrl = new QCheckBox("Show URL Bar", this); chkUrl->setChecked(showUrl);
        chkGo = new QCheckBox("Show Go Button", this); chkGo->setChecked(showGo);
        chkBookmarks = new QCheckBox("Show Bookmarks Dropdown", this); chkBookmarks->setChecked(showBookmarks);
        chkAddBookmark = new QCheckBox("Show Add Bookmark (+) Button", this); chkAddBookmark->setChecked(showAddBookmark);
        chkTabJump = new QCheckBox("Show Tab Navigator Dropdown", this); chkTabJump->setChecked(showTabJump);
        chkNewTab = new QCheckBox("Show New Tab Button", this); chkNewTab->setChecked(showNewTab);
        chkZoom = new QCheckBox("Show Zoom Dropdown", this); chkZoom->setChecked(showZoom);
        chkPassList = new QCheckBox("Show Saved Passwords Dropdown List", this); chkPassList->setChecked(showPassList);
        chkPassButtons = new QCheckBox("Show Password Action Buttons (u, p, e)", this); chkPassButtons->setChecked(showPassButtons);
        chkMobileToggle = new QCheckBox("Show Mobile Quick Switcher (m)", this); chkMobileToggle->setChecked(showMobileToggle);
        chkClock = new QCheckBox("Show Clock Widget", this); chkClock->setChecked(showClock);
        chkNotes = new QCheckBox("Show Notes Button (n)", this); chkNotes->setChecked(showNotes);

        layToolbarBtns->addWidget(chkNav);
        layToolbarBtns->addWidget(chkReload);
        layToolbarBtns->addWidget(chkHome);
        layToolbarBtns->addWidget(chkUrl);
        layToolbarBtns->addWidget(chkGo);
        layToolbarBtns->addWidget(chkBookmarks);
        layToolbarBtns->addWidget(chkAddBookmark);
        layToolbarBtns->addWidget(chkTabJump);
        layToolbarBtns->addWidget(chkNewTab);
        layToolbarBtns->addWidget(chkZoom);
        layToolbarBtns->addWidget(chkPassList);
        layToolbarBtns->addWidget(chkPassButtons);
        layToolbarBtns->addWidget(chkMobileToggle);
        layToolbarBtns->addWidget(chkClock);
        layToolbarBtns->addWidget(chkNotes);
        layToolbarBtns->addStretch(1);

        scrollToolbar->setWidget(contentToolbar);
        auto *layScrollWrap1 = new QVBoxLayout(tabToolbarBtns);
        layScrollWrap1->setContentsMargins(0, 0, 0, 0);
        layScrollWrap1->addWidget(scrollToolbar);

        tabWidget->addTab(tabToolbarBtns, "👁️ Main Toolbar Elements");

        // --- TAB 4: Sidebar Buttons ---
        auto *tabSideBtns = new QWidget(this);
        auto *laySideBtns = new QVBoxLayout(tabSideBtns);

        laySideBtns->addWidget(new QLabel("<b>Select visible buttons on Sidebar:</b>", this));

        chkSideWinMenu = new QCheckBox("Show Window Control Menu Button (⚙)", this); chkSideWinMenu->setChecked(showSideWinMenu);
        chkSideSettings = new QCheckBox("Show Settings Button (🛠)", this); chkSideSettings->setChecked(showSideSettings);
        chkSideReload = new QCheckBox("Show Reload Button (⟳)", this); chkSideReload->setChecked(showSideReload);
        chkSideNav = new QCheckBox("Show Back / Forward Buttons (◀ ▶)", this); chkSideNav->setChecked(showSideNav);
        chkSideHome = new QCheckBox("Show Homepage Button (⌂)", this); chkSideHome->setChecked(showSideHome);
        chkSidePassManager = new QCheckBox("Show Password Manager Buttons (u, p, e)", this); chkSidePassManager->setChecked(showSidePassManager);
        chkSideMobile = new QCheckBox("Show Mobile Switcher Button (m)", this); chkSideMobile->setChecked(showSideMobile);
        chkSideNotes = new QCheckBox("Show Notes Button (n)", this); chkSideNotes->setChecked(showSideNotes);
        chkSideNewTab = new QCheckBox("Show New Tab Button (t)", this); chkSideNewTab->setChecked(showSideNewTab);

        laySideBtns->addWidget(chkSideWinMenu);
        laySideBtns->addWidget(chkSideSettings);
        laySideBtns->addWidget(chkSideReload);
        laySideBtns->addWidget(chkSideNav);
        laySideBtns->addWidget(chkSideHome);
        laySideBtns->addWidget(chkSidePassManager);
        laySideBtns->addWidget(chkSideMobile);
        laySideBtns->addWidget(chkSideNotes);
        laySideBtns->addWidget(chkSideNewTab);
        laySideBtns->addStretch(1);

        tabWidget->addTab(tabSideBtns, "🛡️ Sidebar Buttons");

        // --- TAB 5: Privacy & Storage ---
        auto *tabPrivacy = new QWidget(this);
        auto *layPrivacy = new QVBoxLayout(tabPrivacy);

        strictPrivacyCheck = new QCheckBox("Enable Strict Privacy Flags (Requires restart)", this);
        strictPrivacyCheck->setChecked(strictPrivacy);
        layPrivacy->addWidget(strictPrivacyCheck);
        adBlockerCheck = new QCheckBox("Ad Blocker: Block ads & trackers (generic, all pages)", this);
        adBlockerCheck->setChecked(adBlocker);
        layPrivacy->addWidget(adBlockerCheck);

        layPrivacy->addSpacing(10);
        auto *lblAdBlockEx = new QLabel("Ad Blocker Exceptions (one domain per line):\n"
                                       "Domains here are never blocked, even if in the ad list.\n"
                                       "Use when a site breaks or blocks you (406/403) because a request is missing.", this);
        lblAdBlockEx->setWordWrap(true);
        layPrivacy->addWidget(lblAdBlockEx);
        adBlockExceptionsEdit = new QPlainTextEdit(this);
        adBlockExceptionsEdit->setPlainText(adBlockExceptions.join("\n"));
        adBlockExceptionsEdit->setMinimumHeight(80);
        adBlockExceptionsEdit->setPlaceholderText("example.com\ncdn.example.net");
        layPrivacy->addWidget(adBlockExceptionsEdit);

        layPrivacy->addSpacing(15);
        auto *btnClearGmxStorage = new QPushButton("🧹 Clear WebStorage & Cookies (GMX Technical Error Fix)", this);
        layPrivacy->addWidget(btnClearGmxStorage);

        connect(btnClearGmxStorage, &QPushButton::clicked, this, []() {
            QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            QDir(dataPath + "/storage").removeRecursively();
            QDir(dataPath + "/cache").removeRecursively();
            QDir().mkpath(dataPath + "/storage");
            QDir().mkpath(dataPath + "/cache");
            QMessageBox::information(nullptr, "Storage & Cache Cleared",
                "GMX session storage and browser cookies/cache have been reset.\nPlease restart the browser and log in to GMX.");
        });

        layPrivacy->addStretch(1);
        tabWidget->addTab(tabPrivacy, "🔒 Privacy & Storage");

        // --- TAB: Transparency (Tech-HUD Style, 7 Regler) ---
        // Like GTK/TechHUD: one opacity per area. The top-level window gets
        // WA_TranslucentBackground (the Qt equivalent of GTK's RGBA visual +
        // app_paintable) and each panel is painted with its own rgba().
        // All values are 10-100 % (100 = opaque, 10 = most transparent).
        auto *tabTrans = new QWidget(this);
        auto *layTrans = new QVBoxLayout(tabTrans);
        layTrans->addWidget(new QLabel("<b>Opacity in % (100 = opaque, 10 = most transparent).</b>", this));
        layTrans->addWidget(new QLabel("Lower values let the desktop show through. Works best with a frameless window.", this));
        layTrans->addWidget(new QLabel("The web page is faded on every page load.", this));

        // Slider + SpinBox per row, synced both ways.
        auto addTransRow = [&](QVBoxLayout *lay, const QString &label, int value, QSlider *&slider, QSpinBox *&spin) {
            lay->addWidget(new QLabel(label, this));
            auto *row = new QHBoxLayout();
            slider = new QSlider(Qt::Horizontal, this);
            slider->setRange(10, 100);
            slider->setValue(value);
            spin = new QSpinBox(this);
            spin->setRange(10, 100);
            spin->setSuffix(" %");
            spin->setValue(value);
            row->addWidget(slider, 1);
            row->addWidget(spin);
            lay->addLayout(row);
            connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
        };

        auto *grpTabBar = new QGroupBox("Tab Bar", this);
        auto *layTransTabBar = new QVBoxLayout(grpTabBar);
        addTransRow(layTransTabBar, "Background:", transTabBarBg, transTabBarBgSlider, transTabBarBgSpin);
        addTransRow(layTransTabBar, "Controls (tabs, + button):", transTabBarControls, transTabBarControlsSlider, transTabBarControlsSpin);
        layTrans->addWidget(grpTabBar);

        auto *grpToolbar = new QGroupBox("Toolbar", this);
        auto *layTransToolbar = new QVBoxLayout(grpToolbar);
        addTransRow(layTransToolbar, "Background:", transToolbarBg, transToolbarBgSlider, transToolbarBgSpin);
        addTransRow(layTransToolbar, "Controls (buttons, URL bar):", transToolbarControls, transToolbarControlsSlider, transToolbarControlsSpin);
        layTrans->addWidget(grpToolbar);

        auto *grpSide = new QGroupBox("Side Bar", this);
        auto *layTransSide = new QVBoxLayout(grpSide);
        addTransRow(layTransSide, "Background:", transSideBg, transSideBgSlider, transSideBgSpin);
        addTransRow(layTransSide, "Controls (buttons):", transSideControls, transSideControlsSlider, transSideControlsSpin);
        layTrans->addWidget(grpSide);

        auto *grpWeb = new QGroupBox("Web Page", this);
        auto *layTransWeb = new QVBoxLayout(grpWeb);
        addTransRow(layTransWeb, "Entire page (including content):", transWebpage, transWebpageSlider, transWebpageSpin);
        layTrans->addWidget(grpWeb);

        layTrans->addStretch(1);
        tabWidget->addTab(tabTrans, "👻 Transparency");

        mainLayout->addWidget(tabWidget);

        auto *btnLayout = new QHBoxLayout();
        btnLayout->setContentsMargins(0, 4, 0, 0);
        auto *btnClearCache = new QPushButton("Clear Cache", this);
        auto *btnSave = new QPushButton("Save Settings", this);
        auto *btnCancel = new QPushButton("Cancel", this);
        
        btnLayout->addWidget(btnClearCache);
        btnLayout->addStretch();
        btnLayout->addWidget(btnSave);
        btnLayout->addWidget(btnCancel);
        mainLayout->addLayout(btnLayout);

        connect(btnClearCache, &QPushButton::clicked, this, []() {
            QString cachePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/cache";
            QDir(cachePath).removeRecursively();
            QMessageBox::information(nullptr, "Cache Cleared", "Browser cache has been cleared successfully.");
        });

        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QString getHomepage() const { return homepageEdit->text().trimmed(); }
    QString getClockCommand() const { return clockCmdEdit->text().trimmed(); }
    QString getNotesCommand() const { return notesCmdEdit->text().trimmed(); }
    int getDefaultZoom() const { return zoomSpin->value(); }
    int getUiZoom() const { return uiZoomSpin->value(); }
    int getTabZoom() const { return tabZoomSpin->value(); }
    int getUrlMinWidth() const { return urlWidthSpin->value(); }
    int getUrlBarSpacerMinWidth() const { return urlBarSpacerMinWidthSpin->value(); }
    int getReloadMinWidth() const { return reloadWidthSpin->value(); }
    bool isStrictPrivacy() const { return strictPrivacyCheck->isChecked(); }
    bool isAdBlocker() const { return adBlockerCheck->isChecked(); }
    QStringList getAdBlockExceptions() const {
        QStringList out;
        for (const QString &line : adBlockExceptionsEdit->toPlainText().split('\n')) {
            QString s = line.trimmed();
            if (!s.isEmpty()) out.append(s);
        }
        return out;
    }
    bool isDarkMode() const { return darkModeCheck->isChecked(); }
    bool isUnicodeIcons() const { return unicodeIconsCheck->isChecked(); }
    bool useButtonIcons() const { return chkUseButtonIcons->isChecked(); }
    int getTransTabBarBg() const { return transTabBarBgSpin->value(); }
    int getTransTabBarControls() const { return transTabBarControlsSpin->value(); }
    int getTransToolbarBg() const { return transToolbarBgSpin->value(); }
    int getTransToolbarControls() const { return transToolbarControlsSpin->value(); }
    int getTransSideBg() const { return transSideBgSpin->value(); }
    int getTransSideControls() const { return transSideControlsSpin->value(); }
    int getTransWebpage() const { return transWebpageSpin->value(); }
    bool isFrameless() const { return chkFrameless->isChecked(); }
    int getToolbarPosition() const { return comboToolbarPos->currentIndex(); }
    int getTabBarPosition() const { return comboTabBarPos->currentIndex(); }
    int getSideBarPosition() const { return comboSideBarPos->currentIndex(); }
    bool showTabNewTab() const { return chkTabNewTab->isChecked(); }
    bool showSideToolbar() const { return chkShowSideToolbar->isChecked(); }
    bool isClock24h() const { return chkClock24h->isChecked(); }

    bool showNav() const { return chkNav->isChecked(); }
    bool showReload() const { return chkReload->isChecked(); }
    bool showHome() const { return chkHome->isChecked(); }
    bool showUrl() const { return chkUrl->isChecked(); }
    bool showGo() const { return chkGo->isChecked(); }
    bool showBookmarks() const { return chkBookmarks->isChecked(); }
    bool showAddBookmark() const { return chkAddBookmark->isChecked(); }
    bool showTabJump() const { return chkTabJump->isChecked(); }
    bool showNewTab() const { return chkNewTab->isChecked(); }
    bool showZoom() const { return chkZoom->isChecked(); }
    bool showPassList() const { return chkPassList->isChecked(); }
    bool showPassButtons() const { return chkPassButtons->isChecked(); }
    bool showMobileToggle() const { return chkMobileToggle->isChecked(); }
    bool showClock() const { return chkClock->isChecked(); }
    bool showNotes() const { return chkNotes->isChecked(); }

    bool showSideWinMenu() const { return chkSideWinMenu->isChecked(); }
    bool showSideSettings() const { return chkSideSettings->isChecked(); }
    bool showSideReload() const { return chkSideReload->isChecked(); }
    bool showSideNav() const { return chkSideNav->isChecked(); }
    bool showSideHome() const { return chkSideHome->isChecked(); }
    bool showSidePassManager() const { return chkSidePassManager->isChecked(); }
    bool showSideMobile() const { return chkSideMobile->isChecked(); }
    bool showSideNotes() const { return chkSideNotes->isChecked(); }
    bool showSideNewTab() const { return chkSideNewTab->isChecked(); }

private:
    QTabWidget *tabWidget;
    QLineEdit *homepageEdit;
    QLineEdit *clockCmdEdit;
    QLineEdit *notesCmdEdit;
    QSpinBox *zoomSpin;
    QSpinBox *uiZoomSpin;
    QSpinBox *tabZoomSpin;
    QSpinBox *urlWidthSpin;
    QSpinBox *urlBarSpacerMinWidthSpin;
    QSpinBox *reloadWidthSpin;
    QCheckBox *strictPrivacyCheck;
    QCheckBox *adBlockerCheck;
    QPlainTextEdit *adBlockExceptionsEdit = nullptr;
    QCheckBox *darkModeCheck;
    QCheckBox *unicodeIconsCheck;
    QCheckBox *chkUseButtonIcons;
    QSlider *transTabBarBgSlider = nullptr, *transTabBarControlsSlider = nullptr;
    QSlider *transToolbarBgSlider = nullptr, *transToolbarControlsSlider = nullptr;
    QSlider *transSideBgSlider = nullptr, *transSideControlsSlider = nullptr;
    QSlider *transWebpageSlider = nullptr;
    QSpinBox *transTabBarBgSpin = nullptr, *transTabBarControlsSpin = nullptr;
    QSpinBox *transToolbarBgSpin = nullptr, *transToolbarControlsSpin = nullptr;
    QSpinBox *transSideBgSpin = nullptr, *transSideControlsSpin = nullptr;
    QSpinBox *transWebpageSpin = nullptr;
    QCheckBox *chkFrameless;
    QCheckBox *chkClock24h;
    QComboBox *comboToolbarPos;
    QComboBox *comboTabBarPos;
    QComboBox *comboSideBarPos;
    QCheckBox *chkTabNewTab;
    QCheckBox *chkShowSideToolbar;

    QCheckBox *chkNav, *chkReload, *chkHome, *chkUrl, *chkGo, *chkBookmarks, *chkAddBookmark, *chkTabJump, *chkNewTab, *chkZoom;
    QCheckBox *chkPassList, *chkPassButtons, *chkMobileToggle, *chkClock, *chkNotes;
    QCheckBox *chkSideWinMenu, *chkSideSettings, *chkSideReload, *chkSideNav, *chkSideHome, *chkSidePassManager, *chkSideMobile, *chkSideNotes, *chkSideNewTab;
};

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    AboutDialog(const QString &appName, const QString &version, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("About " + appName);
        resize(450, 300);
        auto *layout = new QVBoxLayout(this);
        auto *textBrowser = new QTextBrowser(this);
        textBrowser->setOpenExternalLinks(false);
        textBrowser->setHtml(QString(
            "<b>%1</b><br>"
            "Version %2<br><br>"
            "A fast, lightweight Qt WebEngine browser.<br><br>"
            "License: <b>GNU General Public License v2 or later (GPL v2+)</b><br>"
            "This program is free software; you can redistribute it and/or modify "
            "it under the terms of the GNU General Public License as published by "
            "the Free Software Foundation.<br><br>"
            "Contact: <b>Mark Ulrich</b><br>"
            "Web: <a href=\"https://www.schinkenpizza.de\">www.schinkenpizza.de</a><br>"
            "Mail: <a href=\"mailto:markulrich@gmx.de\">markulrich@gmx.de</a>"
        ).arg(appName, version));
        connect(textBrowser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
            emit openUrlRequested(url.toString());
            accept();
        });
        layout->addWidget(textBrowser);
        auto *btnOk = new QPushButton("OK", this);
        connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
        layout->addWidget(btnOk, 0, Qt::AlignCenter);
    }
signals:
    void openUrlRequested(const QString &url);
};

// --- Desktop Browser Main Window ---

class DesktopBrowserWindow : public QWidget {
    Q_OBJECT
public:
    bool hasSavedGeometry() const { return !m_windowGeometry.isEmpty(); }
    DesktopBrowserWindow(const QString &initialUrl, QWebEngineProfile *profile, MuRequestInterceptor *interceptor,
                         bool frameless, bool hideToolbar, const QString &layer, QWidget *parent = nullptr)
        : QWidget(parent), m_profile(profile), m_interceptor(interceptor), m_isFrameless(frameless), m_hideToolbar(hideToolbar), m_layer(layer),
          m_darkMode(false), m_strictPrivacy(false), m_unicodeIcons(true), m_urlBarMinWidth(300), m_reloadButtonMinWidth(30),
          m_defaultZoom(100), m_uiZoom(100), m_tabZoom(100),
          m_showNav(true), m_showReload(true), m_showHome(true), m_showUrl(true), m_showGo(true),
          m_showBookmarks(true), m_showAddBookmark(true), m_showTabJump(true), m_showNewTab(true), m_showZoom(true),
          m_showPassList(true), m_showPassButtons(true), m_showMobileToggle(true), m_showClock(true), m_showNotes(true),
          m_toolbarPosition(0), m_tabBarPosition(0), m_showSideToolbar(false),
          m_showTabNewTab(true), m_showSideNewTab(true),
          m_showSideWinMenu(true), m_showSideReload(true), m_showSideNav(true), m_showSideHome(true),
          m_showSidePassManager(true), m_showSideMobile(true), m_showSideNotes(true),
          m_clockCommand("mu-calendar"), m_notesCommand("mu-notepad ~/notes.txt"),
          m_updatingTabsCombo(false), m_updatingZoomCombo(false), m_passUnlocked(false)
    {
        loadConfig();
        // Werbeblocker-Einstellung an den Interceptor weitergeben.
        if (m_interceptor) m_interceptor->setAdBlockerEnabled(m_adBlocker);
        // Objektnamen: #mainWindow-Regel in applyTransparency greift nur,
        // wenn das Fenster wirklich so heisst (Geminis Technik).
        setObjectName("mainWindow");
        // Fenster-Geometrie wiederherstellen (Position + Groesse + maximiert).
        if (!m_windowGeometry.isEmpty()) restoreGeometry(m_windowGeometry);
        // WA_TranslucentBackground muss VOR show() gesetzt werden.
        setAttribute(Qt::WA_TranslucentBackground, anyTransparency());

        if (m_isFrameless) {
            setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        }

        loadBookmarks();
        loadStyleRules();
        loadUserAgentRules();
        loadAdBlockExceptions();
        if (m_interceptor) m_interceptor->setMobileMode(m_mobileMode); // Global Mobile/Desktop toggle

        outerLayout = new QHBoxLayout(this);
        outerLayout->setContentsMargins(0, 0, 0, 0);
        outerLayout->setSpacing(0);

        // Left Vertical Side Toolbar (Window Dragging & Scaling Spacers)
        sideToolbarWidget = new SideToolbarWidget(this);
        sideToolbarWidget->setObjectName("sideToolbarWidget");
        sideLayout = new QVBoxLayout(sideToolbarWidget);
        sideLayout->setContentsMargins(3, 3, 3, 3);
        sideLayout->setSpacing(0);

        btnWinMenu = new QPushButton("⚙", this);
        btnWinMenu->setToolTip("Window Menu (Minimize / Maximize / Close)");
        connect(btnWinMenu, &QPushButton::clicked, this, &DesktopBrowserWindow::showWindowMenu);

        btnSideSettings = new QPushButton("🛠", this);
        btnSideSettings->setToolTip("Browser Settings");
        connect(btnSideSettings, &QPushButton::clicked, this, &DesktopBrowserWindow::openSettingsDialog);

        btnSideReload = new QPushButton("⟳", this);
        btnSideReload->setToolTip("Reload Page");
        connect(btnSideReload, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) view->reload();
        });

        btnSideBack = new QPushButton("◀", this);
        btnSideBack->setToolTip("Go Back");
        btnSideForward = new QPushButton("▶", this);
        btnSideForward->setToolTip("Go Forward");
        connect(btnSideBack, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) view->back();
        });
        connect(btnSideForward, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) view->forward();
        });

        btnSideHome = new QPushButton("⌂", this);
        btnSideHome->setToolTip("Go to Homepage");
        connect(btnSideHome, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) view->load(QUrl(m_homepageUrl));
        });

        btnSideUser = new QPushButton("u", this);
        btnSideUser->setToolTip("Copy Username of selected login");
        btnSidePass = new QPushButton("p", this);
        btnSidePass->setToolTip("Copy Password of selected login");
        btnSideEditPass = new QPushButton("e", this);
        // Erster Passwort-Button: Domainliste als Popup.
        btnSidePassList = new QPushButton("D", this);
        connect(btnSidePassList, &QPushButton::clicked, this, &DesktopBrowserWindow::showPasswordDomainPopup);
        // Bookmark-Button: Bookmarkliste als Popup (direkt unter Home).
        btnSideBookmarks = new QPushButton("★", this);
        btnSideBookmarks->setToolTip("Bookmarks Menu");
        connect(btnSideBookmarks, &QPushButton::clicked, this, &DesktopBrowserWindow::showBookmarksPopup);
        btnSideEditPass->setToolTip("Edit Password Database");

        connect(btnSideUser, &QPushButton::clicked, this, &DesktopBrowserWindow::copyPasswordUsername);
        connect(btnSidePass, &QPushButton::clicked, this, &DesktopBrowserWindow::copyPasswordValue);
        connect(btnSideEditPass, &QPushButton::clicked, this, &DesktopBrowserWindow::editPasswordsDialog);

        btnSideMobile = new QPushButton("m", this);
        btnSideMobile->setToolTip("Toggle Mobile / Desktop Mode");
        connect(btnSideMobile, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) {
                view->toggleMobileMode();
                updateMobileToggleButton(view->isMobileMode());
            }
        });

        btnSideNotes = new QPushButton("n", this);
        btnSideNotes->setToolTip("Open Quick Notes");
        connect(btnSideNotes, &QPushButton::clicked, this, &DesktopBrowserWindow::onNotesClicked);

        btnSideNewTab = new QPushButton("t", this);
        btnSideNewTab->setToolTip("Open New Tab");
        connect(btnSideNewTab, &QPushButton::clicked, this, [this]() {
            addNewTab(m_homepageUrl);
        });

        rebuildSideToolbarLayout();

        outerLayout->addWidget(sideToolbarWidget, 0);
        sideToolbarWidget->setVisible(m_showSideToolbar);

        // Center Content Container
        centerContentWidget = new QWidget(this);
        mainLayout = new QVBoxLayout(centerContentWidget);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);
        outerLayout->addWidget(centerContentWidget, 1);
        applySideBarPosition(); // Sidebar links/rechts aus Config.

        toolbarContainer = new QWidget(this);
        toolbarContainer->setObjectName("toolbarContainer");
        toolsLayout = new QHBoxLayout(toolbarContainer);
        toolsLayout->setContentsMargins(2, 2, 2, 2);
        toolsLayout->setSpacing(3);

        btnBack = new QPushButton("◀", this);
        btnBack->setToolTip("Go Back");
        btnForward = new QPushButton("▶", this);
        btnForward->setToolTip("Go Forward");
        btnReload = new QPushButton("⟳", this);
        btnReload->setToolTip("Reload Page");
        btnReload->setMinimumWidth(m_reloadButtonMinWidth);

        btnHome = new QPushButton("⌂", this);
        btnHome->setToolTip("Go to Homepage");

        urlEdit = new MiddleClickLineEdit(this);
        // Spacer hinter der URL-Bar: transparentes Loch in der Toolbar,
        // durch das der (transparente) Hintergrund sichtbar wird.
        // Mindestbreite ist in den Einstellungen setzbar.
        urlBarSpacer = new DraggableWidget(this); // Ziehbar wie die Sidebar (inkl. IceWM-Doppelklick-Fix).
        urlBarSpacer->setObjectName("urlBarSpacer");
        urlBarSpacer->setStyleSheet("#urlBarSpacer { background-color: transparent; border: 0px; }");
        urlBarSpacer->setMinimumWidth(m_urlBarSpacerMinWidth);
        urlEdit->setMinimumWidth(100);
        urlEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        btnGo = new QPushButton("Go", this);
        btnGo->setToolTip("Go to URL");

        bookmarkCombo = new QComboBox(this);
        bookmarkCombo->setToolTip("Bookmarks Menu");
        bookmarkCombo->setMaximumWidth(140);
        btnAddBookmark = new QPushButton("+", this);
        btnAddBookmark->setToolTip("Add current page as bookmark");

        tabJumpCombo = new QComboBox(this);
        tabJumpCombo->setToolTip("Jump to tab (Tab Navigator)");
        tabJumpCombo->setMaximumWidth(120);

        zoomCombo = new QComboBox(this);
        zoomCombo->setToolTip("Zoom Level");
        zoomCombo->setMaximumWidth(75);
        for (int z = 40; z <= 250; z += 10) {
            zoomCombo->addItem(QString("%1%").arg(z), z);
        }
        updateZoomComboSelection(m_defaultZoom);

        btnNewTab = new QPushButton("+ Tab", this);
        btnNewTab->setToolTip("Open new tab");

        // Password Manager Elements (lowercase without brackets)
        passCombo = new ClickableComboBox(this);
        passCombo->setToolTip("Encrypted Saved Logins");
        passCombo->setMaximumWidth(130);
        passCombo->onBeforePopup = [this]() -> bool {
            return unlockPasswords();
        };

        btnCopyUser = new QPushButton("u", this);
        btnCopyUser->setToolTip("Copy Username of selected login to clipboard");

        btnCopyPass = new QPushButton("p", this);
        btnCopyPass->setToolTip("Copy Password of selected login to clipboard (Auto-clears in 15s)");

        btnEditPass = new QPushButton("e", this);
        btnEditPass->setToolTip("Edit Encrypted Password Database");

        // Quick Mobile Toggle Button (lowercase m)
        btnMobileToggle = new QPushButton("m", this);
        btnMobileToggle->setToolTip("Toggle Mobile / Desktop mode for active tab");

        // Clock Widget
        clockLabel = new ClickableLabel(this);
        clockLabel->setToolTip("Current Time (Click to launch Calendar / Program)");
        clockTimer = new QTimer(this);
        connect(clockTimer, &QTimer::timeout, this, &DesktopBrowserWindow::updateClock);
        clockTimer->start(1000);
        updateClock();

        connect(clockLabel, &ClickableLabel::clicked, this, &DesktopBrowserWindow::onClockClicked);

        // Notes Button (lowercase n)
        btnNotes = new QPushButton("n", this);
        btnNotes->setToolTip("Open Quick Notes Editor");
        connect(btnNotes, &QPushButton::clicked, this, &DesktopBrowserWindow::onNotesClicked);

        // Tab Widgets & Beside Tab Container
        tabBarContainerWidget = new QWidget(this);
        tabBarContainerWidget->setObjectName("tabBarContainerWidget");
        tabBarLayout = new QHBoxLayout(tabBarContainerWidget);
        tabBarLayout->setContentsMargins(0, 0, 0, 0);
        tabBarLayout->setSpacing(2);

        tabBarWidget = new QTabBar(this);
        tabBarWidget->setTabsClosable(true);
        tabBarWidget->setMovable(true);

        btnTabNewTab = new QPushButton("+", this);
        btnTabNewTab->setToolTip("Open New Tab");
        connect(btnTabNewTab, &QPushButton::clicked, this, [this]() { addNewTab(m_homepageUrl); });

        tabBarLayout->addWidget(tabBarWidget, 1);
        tabBarLayout->addWidget(btnTabNewTab, 0);

        pageStackedWidget = new QStackedWidget(this);

        updatePasswordCombo();
        rebuildToolbarLayout();

        connect(tabBarWidget, &QTabBar::currentChanged, this, &DesktopBrowserWindow::onTabChanged);
        connect(tabBarWidget, &QTabBar::tabCloseRequested, this, &DesktopBrowserWindow::closeTab);
        connect(tabBarWidget, &QTabBar::tabMoved, this, [this](int from, int to) {
            QWidget *w = pageStackedWidget->widget(from);
            pageStackedWidget->removeWidget(w);
            pageStackedWidget->insertWidget(to, w);
            updateTabsCombo();
        });

        updateBookmarkCombo();
        applyUiScaling(m_uiZoom);
        applyTabScaling(m_tabZoom);
        applyTheme(m_darkMode);

        connect(btnBack, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) view->back();
        });
        connect(btnForward, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) view->forward();
        });
        connect(btnReload, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) view->page()->triggerAction(QWebEnginePage::Reload);
        });
        connect(btnHome, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) {
                view->load(QUrl(m_homepageUrl));
            } else {
                addNewTab(m_homepageUrl);
            }
        });

        auto loadUrlFromInput = [this]() {
            QString input = urlEdit->text().trimmed();
            if (input.isEmpty()) return;
            if (!input.startsWith("http://") && !input.startsWith("https://") && !input.startsWith("file://")) {
                input = "https://" + input;
            }
            if (auto *view = currentWebView()) view->load(QUrl(input));
        };

        connect(btnGo, &QPushButton::clicked, loadUrlFromInput);
        connect(urlEdit, &QLineEdit::returnPressed, loadUrlFromInput);

        // Bookmark Combobox Handling
        connect(bookmarkCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (index == 0) {
                // Header "Bookmarks" (non-functional)
                bookmarkCombo->setCurrentIndex(0);
                return;
            }
            if (index == 1) {
                addCurrentBookmark();
                bookmarkCombo->setCurrentIndex(0);
                return;
            }
            if (index == 2) {
                editBookmarksDialog();
                bookmarkCombo->setCurrentIndex(0);
                return;
            }
            QString url = bookmarkCombo->itemData(index).toString();
            if (!url.isEmpty() && currentWebView()) {
                currentWebView()->load(QUrl(url));
            }
            bookmarkCombo->setCurrentIndex(0);
        });

        connect(tabJumpCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (m_updatingTabsCombo || index < 0) return;
            if (index < tabBarWidget->count()) {
                tabBarWidget->setCurrentIndex(index);
            }
        });

        connect(zoomCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (m_updatingZoomCombo || index < 0) return;
            int zoomVal = zoomCombo->itemData(index).toInt();
            if (zoomVal > 0) {
                qreal factor = static_cast<qreal>(zoomVal) / 100.0;
                if (auto *view = currentWebView()) {
                    view->setZoomFactor(factor);
                }
            }
        });

        connect(btnAddBookmark, &QPushButton::clicked, this, &DesktopBrowserWindow::addCurrentBookmark);
        connect(btnNewTab, &QPushButton::clicked, this, [this]() { addNewTab(m_homepageUrl); });

        // Password Manager Actions
        connect(passCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (index > 0 && !m_passUnlocked) {
                if (!unlockPasswords()) {
                    passCombo->setCurrentIndex(0);
                }
            }
        });

        connect(btnCopyUser, &QPushButton::clicked, this, &DesktopBrowserWindow::copyPasswordUsername);
        connect(btnCopyPass, &QPushButton::clicked, this, &DesktopBrowserWindow::copyPasswordValue);
        connect(btnEditPass, &QPushButton::clicked, this, &DesktopBrowserWindow::editPasswordsDialog);

        // Mobile Toggle Action
        connect(btnMobileToggle, &QPushButton::clicked, this, [this]() {
            if (auto *view = currentWebView()) {
                view->toggleMobileMode();
                updateMobileToggleButton(view->isMobileMode());
            }
        });

        // Keyboard Shortcuts
        setupKeyboardShortcuts();

        if (!restoreSession()) {
            QString startUrl = initialUrl;
            if (startUrl == "https://schinkenpizza.de" && !m_homepageUrl.isEmpty()) {
                startUrl = m_homepageUrl;
            }
            addNewTab(startUrl);
        }
    }

    // About-Dialog: Lizenz, Version, Kontakt.
    // About-Dialog: Lizenz, Version, Kontakt.
    // Version kommt aus dem Build: MU_VERSION (CMake) oder
    // VERSION (Makefile: VERSION ?= 0.0.9, übergeben als
    // -DVERSION="..."). Ohne beides: Fallback 0.0.9.
    void showAboutDialog() {
#if defined(MU_VERSION)
        const QString version = QString::fromUtf8(MU_VERSION);
#elif defined(VERSION)
        const QString version = QString::fromUtf8(VERSION);
#else
        const QString version = QStringLiteral("0.1.2");
#endif
        AboutDialog dlg("mu-browser-desktop", version, this);
        connect(&dlg, &AboutDialog::openUrlRequested, this, [this](const QString &url) {
            addNewTab(url);
        });
        dlg.exec();
    }

    void showWindowMenu() {
        QMenu menu(this);
        menu.addAction("🗕 Minimize", this, &QWidget::showMinimized);
        menu.addAction(isMaximized() ? "🗗 Restore" : "🗖 Maximize", this, [this]() {
            if (isMaximized()) showNormal();
            else showMaximized();
        });
        menu.addSeparator();
        menu.addAction("ℹ About...", this, &DesktopBrowserWindow::showAboutDialog);
        menu.addSeparator();
        menu.addAction("🗙 Close", this, &QWidget::close);
        menu.exec(btnWinMenu->mapToGlobal(QPoint(0, btnWinMenu->height())));
    }

    void setupKeyboardShortcuts() {
        auto *scNewTab = new QShortcut(QKeySequence("Ctrl+T"), this);
        connect(scNewTab, &QShortcut::activated, this, [this]() { addNewTab(m_homepageUrl); });

        auto *scCloseTab = new QShortcut(QKeySequence("Ctrl+W"), this);
        connect(scCloseTab, &QShortcut::activated, this, [this]() {
            int idx = tabBarWidget->currentIndex();
            if (idx >= 0) closeTab(idx);
        });

        auto *scReload1 = new QShortcut(QKeySequence("Ctrl+R"), this);
        connect(scReload1, &QShortcut::activated, this, [this]() {
            if (auto *view = currentWebView()) view->reload();
        });

        auto *scReload2 = new QShortcut(QKeySequence(Qt::Key_F5), this);
        connect(scReload2, &QShortcut::activated, this, [this]() {
            if (auto *view = currentWebView()) view->reload();
        });

        auto *scFocusUrl1 = new QShortcut(QKeySequence("Ctrl+L"), this);
        connect(scFocusUrl1, &QShortcut::activated, this, [this]() {
            urlEdit->setFocus();
            urlEdit->selectAll();
        });

        auto *scFocusUrl2 = new QShortcut(QKeySequence("Alt+D"), this);
        connect(scFocusUrl2, &QShortcut::activated, this, [this]() {
            urlEdit->setFocus();
            urlEdit->selectAll();
        });

        auto *scAddBookmark = new QShortcut(QKeySequence("Ctrl+D"), this);
        connect(scAddBookmark, &QShortcut::activated, this, &DesktopBrowserWindow::addCurrentBookmark);

        auto *scHome = new QShortcut(QKeySequence("Ctrl+H"), this);
        connect(scHome, &QShortcut::activated, this, [this]() {
            if (auto *view = currentWebView()) view->load(QUrl(m_homepageUrl));
        });

        auto *scZoomIn1 = new QShortcut(QKeySequence("Ctrl++"), this);
        connect(scZoomIn1, &QShortcut::activated, this, [this]() { changeZoomDelta(10); });

        auto *scZoomIn2 = new QShortcut(QKeySequence("Ctrl+="), this);
        connect(scZoomIn2, &QShortcut::activated, this, [this]() { changeZoomDelta(10); });

        auto *scZoomOut = new QShortcut(QKeySequence("Ctrl+-"), this);
        connect(scZoomOut, &QShortcut::activated, this, [this]() { changeZoomDelta(-10); });

        auto *scZoomReset = new QShortcut(QKeySequence("Ctrl+0"), this);
        connect(scZoomReset, &QShortcut::activated, this, [this]() {
            if (auto *view = currentWebView()) view->setZoomFactor(1.0);
            updateZoomComboSelection(100);
        });

        auto *scFullscreen = new QShortcut(QKeySequence(Qt::Key_F11), this);
        connect(scFullscreen, &QShortcut::activated, this, [this]() {
            if (isFullScreen()) showNormal();
            else showFullScreen();
        });

        auto *scToggleToolbar = new QShortcut(QKeySequence("Ctrl+Shift+T"), this);
        connect(scToggleToolbar, &QShortcut::activated, this, [this]() {
            toolbarContainer->setVisible(!toolbarContainer->isVisible());
        });
    }

    void changeZoomDelta(int delta) {
        if (auto *view = currentWebView()) {
            int currentPct = qRound(view->zoomFactor() * 100.0);
            int newPct = qBound(40, currentPct + delta, 250);
            view->setZoomFactor(static_cast<qreal>(newPct) / 100.0);
            updateZoomComboSelection(newPct);
        }
    }

    void updateClock() {
        QString fmt = m_clock24h ? "HH:mm" : "h:mm";
        clockLabel->setText(QDateTime::currentDateTime().toString(fmt));
    }

    void onClockClicked() {
        if (!m_clockCommand.isEmpty()) {
            QProcess::startDetached(m_clockCommand, QStringList());
        }
    }

    void onNotesClicked() {
        if (!m_notesCommand.isEmpty()) {
            QString cmd = m_notesCommand;
            QString homeDir = QDir::homePath();
            cmd.replace("~", homeDir);

            QStringList args = QProcess::splitCommand(cmd);
            if (!args.isEmpty()) {
                QString prog = args.takeFirst();
                if (QProcess::startDetached(prog, args)) {
                    return;
                }
            }
        }
        // Fallback: Open built-in mini notes dialog
        QString defaultNotesPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/notes.txt";
        NotesDialog dlg(defaultNotesPath, this);
        dlg.exec();
    }

    void toggleDarkMode() {
        m_darkMode = !m_darkMode;
        applyTheme(m_darkMode);
        saveConfig();
        for (int i = 0; i < pageStackedWidget->count(); ++i) {
            if (auto *view = qobject_cast<CustomWebEngineView*>(pageStackedWidget->widget(i))) {
                if (m_darkMode) injectDarkModeToPage(view->page());
            }
        }
    }
    // Global Mobile/Desktop toggle. Flips the global mode, updates the
    // interceptor (fallback sends Mobile UA), saves to config, syncs
    // all views, reloads the current view, updates the button.
    void toggleMobileMode() {
        m_mobileMode = !m_mobileMode;
        if (m_interceptor) m_interceptor->setMobileMode(m_mobileMode);
        saveConfig();
        for (int i = 0; i < pageStackedWidget->count(); ++i) {
            if (auto *view = qobject_cast<CustomWebEngineView*>(pageStackedWidget->widget(i))) {
                view->setMobileMode(m_mobileMode);
            }
        }
        if (auto *view = currentWebView()) view->reload();
        updateMobileToggleButton(m_mobileMode);
    }
    bool isMobileMode() const { return m_mobileMode; }

    void setHomepage(const QString &url) {
        m_homepageUrl = url;
        saveConfig();
        QMessageBox::information(this, "Homepage", "New homepage saved:\n" + url);
    }

    void editBookmarksDialog() {
        BookmarkEditorDialog dlg(m_bookmarks, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_bookmarks = dlg.getBookmarks();
            saveBookmarks();
            updateBookmarkCombo();
        }
    }

    bool unlockPasswords() {
        if (m_passUnlocked) return true;

        bool ok = false;
        QString pass = QInputDialog::getText(this, "Encrypted Password Manager",
                                             "Enter Master Password to unlock credentials:",
                                             QLineEdit::Password, "", &ok);
        if (!ok || pass.isEmpty()) return false;

        QFile file(getPasswordsFilePath());
        if (!file.exists()) {
            m_masterPassword = pass;
            m_passUnlocked = true;
            m_passwords.clear();
            savePasswords();
            updatePasswordCombo();
            return true;
        }

        if (file.open(QIODevice::ReadOnly)) {
            QByteArray encryptedData = file.readAll();
            file.close();
            QByteArray plainJson;
            if (SimpleCipher::decrypt(encryptedData, pass, plainJson)) {
                m_masterPassword = pass;
                m_passUnlocked = true;
                QJsonDocument doc = QJsonDocument::fromJson(plainJson);
                m_passwords.clear();
                if (doc.isArray()) {
                    for (const auto &val : doc.array()) {
                        QJsonObject obj = val.toObject();
                        m_passwords.append(PasswordEntry{
                            obj["title"].toString(),
                            obj["username"].toString(),
                            obj["password"].toString()
                        });
                    }
                }
                updatePasswordCombo();
                if (currentWebView()) {
                    autoMatchPasswordDomain(currentWebView()->url());
                }
                return true;
            }
        }

        QMessageBox::warning(this, "Access Denied", "Incorrect Master Password!");
        return false;
    }

    void editPasswordsDialog() {
        if (!unlockPasswords()) return;

        PasswordEditorDialog dlg(m_passwords, [this](const QString &newPass) {
            m_masterPassword = newPass;
            savePasswords();
        }, this);

        if (dlg.exec() == QDialog::Accepted) {
            m_passwords = dlg.getEntries();
            savePasswords();
            updatePasswordCombo();
            if (currentWebView()) {
                autoMatchPasswordDomain(currentWebView()->url());
            }
        }
    }

    void copyPasswordUsername() {
        // Master-Passwort-Dialog auch ohne gewählte Domain zeigen:
        // Sonst passiert beim Klick auf u/p gar nichts, wenn noch
        // kein Master-Passwort eingegeben wurde.
        if (!unlockPasswords()) return;
        int idx = passCombo->currentIndex();
        if (idx <= 0) return;

        if (idx - 1 < m_passwords.size()) {
            QApplication::clipboard()->setText(m_passwords[idx - 1].username);
            QMessageBox::information(this, "Copied", "Username copied to clipboard!");
        }
    }

    void copyPasswordValue() {
        // Master-Passwort-Dialog auch ohne gewählte Domain zeigen:
        // Sonst passiert beim Klick auf u/p gar nichts, wenn noch
        // kein Master-Passwort eingegeben wurde.
        if (!unlockPasswords()) return;
        int idx = passCombo->currentIndex();
        if (idx <= 0) return;

        if (idx - 1 < m_passwords.size()) {
            QString passVal = m_passwords[idx - 1].password;
            QApplication::clipboard()->setText(passVal);

            // Security: Auto-clear clipboard after 15 seconds if it still contains the password
            QTimer::singleShot(15000, [passVal]() {
                if (QApplication::clipboard()->text() == passVal) {
                    QApplication::clipboard()->clear();
                }
            });

            QMessageBox::information(this, "Copied", "Password copied to clipboard!\n(Will be automatically cleared from clipboard in 15 seconds)");
        }
    }

    // Domainliste (Passwoerter) als Popup am ersten Passwort-Button.
    void showPasswordDomainPopup() {
        // Master-Passwort-Dialog zuerst: Ohne Entsperren gibt es
        // keine Domains zu sehen (wie passCombo->onBeforePopup).
        if (!unlockPasswords()) return;
        QMenu menu(this);
        if (m_passwords.isEmpty()) {
            QAction *act = menu.addAction("(No passwords stored)");
            act->setEnabled(false);
        } else {
            for (int i = 0; i < m_passwords.size(); ++i) {
                const auto &pw = m_passwords[i];
                QAction *act = menu.addAction(pw.titleUrl);
                connect(act, &QAction::triggered, this, [this, i]() {
                    passCombo->setCurrentIndex(i + 1);
                    if (!m_passUnlocked) unlockPasswords();
                });
            }
        }
        menu.exec(btnSidePassList->mapToGlobal(QPoint(btnSidePassList->width(), 0)));
    }

    // Bookmarkliste als Popup am Bookmark-Button (unter Home).
    void showBookmarksPopup() {
        QMenu menu(this);
        QAction *addAct = menu.addAction("★ Add Current Page");
        connect(addAct, &QAction::triggered, this, [this]() { addCurrentBookmark(); });
        QAction *editAct = menu.addAction("✏ Edit Bookmarks...");
        connect(editAct, &QAction::triggered, this, [this]() { editBookmarksDialog(); });
        menu.addSeparator();
        if (m_bookmarks.isEmpty()) {
            QAction *act = menu.addAction("(No bookmarks)");
            act->setEnabled(false);
        } else {
            for (const auto &b : m_bookmarks) {
                QAction *act = menu.addAction(b.title);
                QString url = b.url;
                connect(act, &QAction::triggered, this, [this, url]() {
                    if (currentWebView()) currentWebView()->load(QUrl(url));
                });
            }
        }
        menu.exec(btnSideBookmarks->mapToGlobal(QPoint(btnSideBookmarks->width(), 0)));
    }

    void autoMatchPasswordDomain(const QUrl &url) {
        if (!url.isValid() || m_passwords.isEmpty()) return;
        QString host = url.host().toLower();
        if (host.isEmpty()) return;

        for (int i = 0; i < m_passwords.size(); ++i) {
            QString target = m_passwords[i].titleUrl.toLower();
            if (target.contains(host) || host.contains(target)) {
                passCombo->setCurrentIndex(i + 1);
                return;
            }
        }
        passCombo->setCurrentIndex(0);
    }

    void editCssStylesDialog() {
        CssEditorDialog dlg(m_styleRules, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_styleRules = dlg.getRules();
            saveStyleRules();
        }
    }

    void editUserAgentRulesDialog() {
        UserAgentEditorDialog dlg(m_uaRules, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_uaRules = dlg.getRules();
            saveUserAgentRules();
            if (m_interceptor) {
                m_interceptor->setRules(m_uaRules);
            }
        }
    }

    void openSettingsDialog() {
        BrowserSettingsDialog dlg(m_homepageUrl, m_strictPrivacy, m_adBlocker, m_darkMode, m_unicodeIcons, m_isFrameless,
                                  m_urlBarMinWidth, m_reloadButtonMinWidth, m_urlBarSpacerMinWidth, m_defaultZoom, m_uiZoom, m_tabZoom,
                                  m_showNav, m_showReload, m_showHome, m_showUrl, 
                                  m_showGo, m_showBookmarks, m_showAddBookmark, 
                                  m_showTabJump, m_showNewTab, m_showZoom,
                                  m_showPassList, m_showPassButtons, m_showMobileToggle, m_showClock, m_showNotes,
                                  m_toolbarPosition, m_showSideToolbar,
                                  m_tabBarPosition, m_sideBarPosition, m_showTabNewTab, m_clock24h,
                                  m_showSideWinMenu, m_showSideSettings, m_showSideReload, m_showSideNav, m_showSideHome,
                                  m_showSidePassManager, m_showSideMobile, m_showSideNotes, m_showSideNewTab,
                                  m_clockCommand, m_notesCommand,
                                  m_useButtonIcons,
                                  m_transTabBarBg, m_transTabBarControls, m_transToolbarBg, m_transToolbarControls,
                                  m_transSideBg, m_transSideControls, m_transWebpage, m_adBlockExceptions, this);

        // Add buttons in settings for CSS and UA rule editors
        auto *extraBtnLayout = new QHBoxLayout();
        auto *btnCssEditor = new QPushButton("Domain User CSS Styles...", &dlg);
        auto *btnUaEditor = new QPushButton("Per-Domain User-Agents...", &dlg);
        extraBtnLayout->addWidget(btnCssEditor);
        extraBtnLayout->addWidget(btnUaEditor);

        if (auto *layout = qobject_cast<QVBoxLayout*>(dlg.layout())) {
            layout->insertLayout(layout->count() - 1, extraBtnLayout);
        }

        connect(btnCssEditor, &QPushButton::clicked, this, &DesktopBrowserWindow::editCssStylesDialog);
        connect(btnUaEditor, &QPushButton::clicked, this, &DesktopBrowserWindow::editUserAgentRulesDialog);

        if (dlg.exec() == QDialog::Accepted) {
            bool oldPrivacy = m_strictPrivacy;
            bool oldAnyTrans = anyTransparency();
            int oldToolbarPos = m_toolbarPosition;
            int oldTabBarPos = m_tabBarPosition;
            bool oldShowTabNewTab = m_showTabNewTab;
            bool oldFrameless = m_isFrameless;

            m_homepageUrl = dlg.getHomepage();
            m_clockCommand = dlg.getClockCommand();
            m_notesCommand = dlg.getNotesCommand();
            m_strictPrivacy = dlg.isStrictPrivacy();
            m_adBlocker = dlg.isAdBlocker();
            if (m_interceptor) m_interceptor->setAdBlockerEnabled(m_adBlocker);
            m_adBlockExceptions = dlg.getAdBlockExceptions();
            saveAdBlockExceptions();
            if (m_interceptor) m_interceptor->setAdBlockExceptions(m_adBlockExceptions);
            m_darkMode = dlg.isDarkMode();
            m_unicodeIcons = dlg.isUnicodeIcons();
            m_useButtonIcons = dlg.useButtonIcons();
            m_transTabBarBg = dlg.getTransTabBarBg();
            m_transTabBarControls = dlg.getTransTabBarControls();
            m_transToolbarBg = dlg.getTransToolbarBg();
            m_transToolbarControls = dlg.getTransToolbarControls();
            m_transSideBg = dlg.getTransSideBg();
            m_transSideControls = dlg.getTransSideControls();
            m_transWebpage = dlg.getTransWebpage();
            m_isFrameless = dlg.isFrameless();
            m_urlBarMinWidth = dlg.getUrlMinWidth();
            m_urlBarSpacerMinWidth = dlg.getUrlBarSpacerMinWidth();
            m_reloadButtonMinWidth = dlg.getReloadMinWidth();
            m_defaultZoom = dlg.getDefaultZoom();
            int newUiZoom = dlg.getUiZoom();
            int newTabZoom = dlg.getTabZoom();
            m_toolbarPosition = dlg.getToolbarPosition();
            m_tabBarPosition = dlg.getTabBarPosition();
            m_sideBarPosition = dlg.getSideBarPosition();
            applySideBarPosition();
            m_showTabNewTab = dlg.showTabNewTab();
            m_showSideToolbar = dlg.showSideToolbar();
            m_clock24h = dlg.isClock24h();

            m_showNav = dlg.showNav();
            m_showReload = dlg.showReload();
            m_showHome = dlg.showHome();
            m_showUrl = dlg.showUrl();
            m_showGo = dlg.showGo();
            m_showBookmarks = dlg.showBookmarks();
            m_showAddBookmark = dlg.showAddBookmark();
            m_showTabJump = dlg.showTabJump();
            m_showNewTab = dlg.showNewTab();
            m_showZoom = dlg.showZoom();
            m_showPassList = dlg.showPassList();
            m_showPassButtons = dlg.showPassButtons();
            m_showMobileToggle = dlg.showMobileToggle();
            m_showClock = dlg.showClock();
            m_showNotes = dlg.showNotes();

            m_showSideWinMenu = dlg.showSideWinMenu();
            m_showSideSettings = dlg.showSideSettings();
            m_showSideReload = dlg.showSideReload();
            m_showSideNav = dlg.showSideNav();
            m_showSideHome = dlg.showSideHome();
            m_showSidePassManager = dlg.showSidePassManager();
            m_showSideMobile = dlg.showSideMobile();
            m_showSideNotes = dlg.showSideNotes();
            m_showSideNewTab = dlg.showSideNewTab();

            btnReload->setMinimumWidth(m_reloadButtonMinWidth);
            updateZoomComboSelection(m_defaultZoom);

            sideToolbarWidget->setVisible(m_showSideToolbar);
            rebuildSideToolbarLayout();

            if (oldFrameless != m_isFrameless) {
                setWindowFlag(Qt::FramelessWindowHint, m_isFrameless);
                show();
            }

            if (oldToolbarPos != m_toolbarPosition || oldTabBarPos != m_tabBarPosition || oldShowTabNewTab != m_showTabNewTab) {
                rebuildMainLayout();
            }

            rebuildToolbarLayout();

            if (m_uiZoom != newUiZoom) {
                m_uiZoom = newUiZoom;
                applyUiScaling(m_uiZoom);
            }

            if (m_tabZoom != newTabZoom) {
                m_tabZoom = newTabZoom;
                applyTabScaling(m_tabZoom);
            }

            qreal factor = static_cast<qreal>(m_defaultZoom) / 100.0;
            for (int i = 0; i < pageStackedWidget->count(); ++i) {
                if (auto *view = qobject_cast<CustomWebEngineView*>(pageStackedWidget->widget(i))) {
                    view->setZoomFactor(factor);
                    view->setUseUnicodeIcons(m_unicodeIcons);
                }
            }

            applyTheme(m_darkMode);
            saveConfig();

            // WA_TranslucentBackground braucht ein neues natives Fenster.
            if (oldAnyTrans != anyTransparency()) {
                hide();
                setAttribute(Qt::WA_TranslucentBackground, anyTransparency());
                show();
                applyTheme(m_darkMode);
            }

            if (oldPrivacy != m_strictPrivacy) {
                QMessageBox::information(this, "Settings Saved", 
                    "Privacy settings updated.\nPlease restart the browser for the Chromium flags to take full effect.");
            }
        }
    }

    void injectDarkModeToPage(QWebEnginePage *page) {
        if (!page) return;
        page->runJavaScript(R"JS(
            (function() {
                let doc = document.documentElement;
                if (!doc) return;
                doc.style.setProperty('color-scheme', 'dark', 'important');
                
                let existing = document.getElementById('mu-dark-scrollbar-style');
                if (!existing) {
                    let style = document.createElement('style');
                    style.id = 'mu-dark-scrollbar-style';
                    style.innerHTML = `
                        html, body, * { color-scheme: dark !important; }
                        ::-webkit-scrollbar { width: 12px !important; height: 12px !important; }
                        ::-webkit-scrollbar-track { background: #121212 !important; }
                        ::-webkit-scrollbar-thumb { background: #555555 !important; border-radius: 4px !important; }
                        ::-webkit-scrollbar-thumb:hover { background: #777777 !important; }
                    `;
                    (document.head || doc).appendChild(style);
                }
            })();
        )JS");
    }

    void injectUserCssToPage(QWebEnginePage *page) {
        if (!page) return;
        QString url = page->url().toString();
        for (const auto &rule : m_styleRules) {
            if (!rule.pattern.isEmpty() && (url.contains(rule.pattern, Qt::CaseInsensitive) || rule.pattern == "*")) {
                QString cssEscaped = rule.cssCode;
                cssEscaped.replace("\\", "\\\\").replace("'", "\\'").replace("\n", " ");
                QString js = QString(R"JS(
                    (function() {
                        let style = document.createElement('style');
                        style.type = 'text/css';
                        style.appendChild(document.createTextNode('%1'));
                        (document.head || document.documentElement).appendChild(style);
                    })();
                )JS").arg(cssEscaped);
                page->runJavaScript(js);
            }
        }
    }

    void updateMobileToggleButton(bool isMobile) {
        if (isMobile) {
            // Im Icon-Modus bleibt das Icon; nur die Markierung aendert sich.
            if (!m_useButtonIcons) {
                btnMobileToggle->setText("m (on)");
                btnSideMobile->setText("m (on)");
            }
            btnMobileToggle->setStyleSheet("background-color: #005588; color: #ffffff; font-weight: bold;");
            btnSideMobile->setStyleSheet("background-color: #005588; color: #ffffff; font-weight: bold;");
        } else {
            if (!m_useButtonIcons) {
                btnMobileToggle->setText("m");
                btnSideMobile->setText("m");
            }
            btnMobileToggle->setStyleSheet("");
            btnSideMobile->setStyleSheet("");
        }
    }

    void addNewTab(const QString &urlStr) {
        auto *webView = new CustomWebEngineView(this, this);
        webView->setUseUnicodeIcons(m_unicodeIcons);
        webView->setMobileMode(m_mobileMode); // New tab inherits global Mobile/Desktop mode
        auto *page = new CustomWebPage(m_profile, webView);
        
        webView->setPage(page);
        webView->setZoomFactor(static_cast<qreal>(m_defaultZoom) / 100.0);

        page->setNewPageFactory([this]() -> QWebEnginePage* { return createPopupTabPage(); });
        installSameHostTargetFixOnPage(page);

        QObject::connect(page, &QWebEnginePage::loadFinished, page, [this, page](bool ok) {
            if (ok) {
                if (m_darkMode) injectDarkModeToPage(page);
                injectUserCssToPage(page);
                // Seiten-Transparenz bei jedem Load (Seite ist sonst opaque).
                page->setBackgroundColor(m_transWebpage < 100 ? Qt::transparent : Qt::white);
                injectPageTransparencyToPage(page);
            }
        });

        connect(webView, &QWebEngineView::titleChanged, this, [this, webView](const QString &title) {
            int idx = pageStackedWidget->indexOf(webView);
            if (idx != -1) {
                QString tabTitle = title.trimmed();
                if (tabTitle.length() > 25) tabTitle = tabTitle.left(22) + "...";
                tabBarWidget->setTabText(idx, tabTitle.isEmpty() ? "New Tab" : tabTitle);
                updateTabsCombo();
            }
        });

        // Favicon ins Tab (Opera-Stil): iconChanged feuert, sobald das
        // Favicon der Seite geladen ist.
        connect(webView, &QWebEngineView::iconChanged, this, [this, webView](const QIcon &icon) {
            int idx = pageStackedWidget->indexOf(webView);
            if (idx != -1 && !icon.isNull()) {
                tabBarWidget->setTabIcon(idx, icon);
            }
        });

        connect(webView, &QWebEngineView::urlChanged, this, [this, webView](const QUrl &url) {
            if (webView == currentWebView()) {
                urlEdit->setText(url.toString());
                autoMatchPasswordDomain(url);
            }
        });

        connect(webView, &CustomWebEngineView::toggleToolbarRequested, this, [this]() {
            toolbarContainer->setVisible(!toolbarContainer->isVisible());
        });

        connect(webView, &CustomWebEngineView::toggleTabbarRequested, this, [this]() {
            tabBarWidget->setVisible(!tabBarWidget->isVisible());
        });

        connect(webView, &CustomWebEngineView::toggleDarkModeRequested, this, [this]() {
            toggleDarkMode();
        });

        connect(webView, &CustomWebEngineView::setAsHomepageRequested, this, [this, webView]() {
            setHomepage(webView->url().toString());
        });

        connect(webView, &CustomWebEngineView::openSettingsRequested, this, [this]() {
            openSettingsDialog();
        });

        connect(webView, &CustomWebEngineView::openUrlInNewTabRequested, this, [this](const QString &url) {
            addNewTab(url);
        });

        connect(webView, &CustomWebEngineView::addCurrentBookmarkRequested, this, [this]() {
            addCurrentBookmark();
        });

        connect(webView, &CustomWebEngineView::mobileModeToggled, this, [this, webView](bool isMobile) {
            if (webView == currentWebView()) {
                updateMobileToggleButton(isMobile);
            }
        });

        int idx = tabBarWidget->addTab("Loading...");
        pageStackedWidget->addWidget(webView);
        tabBarWidget->setCurrentIndex(idx);
        pageStackedWidget->setCurrentIndex(idx);
        updateTabsCombo();
        webView->load(QUrl(urlStr));
    }

protected:
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        applyDesktopLayer();
    }

    void closeEvent(QCloseEvent *event) override {
        saveSession();
        saveConfig(); // Speichert auch die aktuelle Fenster-Geometrie.
        QWidget::closeEvent(event);
    }

private:
    void applyUiScaling(int percent) {
        QFont defaultFont = QApplication::font();
        int basePt = defaultFont.pointSize();
        if (basePt <= 0) basePt = 10;
        
        int scaledPt = qMax(4, qRound(basePt * (static_cast<qreal>(percent) / 100.0)));
        defaultFont.setPointSizeF(scaledPt);
        
        int scaledHeight = qMax(20, qRound(28 * (static_cast<qreal>(percent) / 100.0)));

        if (urlEdit) { urlEdit->setFont(defaultFont); urlEdit->setMaximumHeight(scaledHeight + 6); }
        if (btnBack) { btnBack->setFont(defaultFont); btnBack->setMaximumHeight(scaledHeight); btnBack->setMinimumWidth(scaledHeight); }
        if (btnForward) { btnForward->setFont(defaultFont); btnForward->setMaximumHeight(scaledHeight); btnForward->setMinimumWidth(scaledHeight); }
        if (btnReload) { btnReload->setFont(defaultFont); btnReload->setMaximumHeight(scaledHeight); }
        if (btnHome) { btnHome->setFont(defaultFont); btnHome->setMaximumHeight(scaledHeight); }
        if (btnGo) { btnGo->setFont(defaultFont); btnGo->setMaximumHeight(scaledHeight); }
        if (bookmarkCombo) { bookmarkCombo->setFont(defaultFont); bookmarkCombo->setMaximumHeight(scaledHeight + 6); }
        if (tabJumpCombo) { tabJumpCombo->setFont(defaultFont); tabJumpCombo->setMaximumHeight(scaledHeight + 6); }
        if (zoomCombo) { zoomCombo->setFont(defaultFont); zoomCombo->setMaximumHeight(scaledHeight + 6); }
        if (btnNewTab) { btnNewTab->setFont(defaultFont); btnNewTab->setMaximumHeight(scaledHeight); }
        if (btnTabNewTab) { btnTabNewTab->setFont(defaultFont); btnTabNewTab->setMaximumHeight(scaledHeight); }
        if (btnSideNewTab) { btnSideNewTab->setFont(defaultFont); }
        if (btnAddBookmark) { btnAddBookmark->setFont(defaultFont); btnAddBookmark->setMaximumHeight(scaledHeight); btnAddBookmark->setMinimumWidth(scaledHeight); }
        if (passCombo) { passCombo->setFont(defaultFont); passCombo->setMaximumHeight(scaledHeight + 6); }
        if (btnCopyUser) { btnCopyUser->setFont(defaultFont); btnCopyUser->setMaximumHeight(scaledHeight); }
        if (btnCopyPass) { btnCopyPass->setFont(defaultFont); btnCopyPass->setMaximumHeight(scaledHeight); }
        if (btnEditPass) { btnEditPass->setFont(defaultFont); btnEditPass->setMaximumHeight(scaledHeight); }
        if (btnMobileToggle) { btnMobileToggle->setFont(defaultFont); btnMobileToggle->setMaximumHeight(scaledHeight); }
        if (clockLabel) { clockLabel->setFont(defaultFont); clockLabel->setMaximumHeight(scaledHeight); }
        if (btnNotes) { btnNotes->setFont(defaultFont); btnNotes->setMaximumHeight(scaledHeight); }
    }

    void applyTabScaling(int percent) {
        QFont tabFont = QApplication::font();
        int basePt = tabFont.pointSize();
        if (basePt <= 0) basePt = 10;

        int scaledPt = qMax(4, qRound(basePt * (static_cast<qreal>(percent) / 100.0)));
        tabFont.setPointSizeF(scaledPt);

        if (tabBarWidget) {
            tabBarWidget->setFont(tabFont);
        }
        if (btnTabNewTab) {
            btnTabNewTab->setFont(tabFont);
        }
    }

    void rebuildMainLayout() {
        mainLayout->removeWidget(toolbarContainer);
        mainLayout->removeWidget(tabBarContainerWidget);
        mainLayout->removeWidget(pageStackedWidget);

        tabBarWidget->setShape(m_tabBarPosition == 1 ? QTabBar::RoundedSouth : QTabBar::RoundedNorth);
        btnTabNewTab->setVisible(m_showTabNewTab);

        if (m_tabBarPosition == 0) { // Top Tab Bar (Above Web Content)
            if (m_toolbarPosition == 0) { // Top (Above Tabbar)
                mainLayout->addWidget(toolbarContainer);
                mainLayout->addWidget(tabBarContainerWidget);
                mainLayout->addWidget(pageStackedWidget, 1);
            } else if (m_toolbarPosition == 1) { // Middle (Below Tabbar, Above Page)
                mainLayout->addWidget(tabBarContainerWidget);
                mainLayout->addWidget(toolbarContainer);
                mainLayout->addWidget(pageStackedWidget, 1);
            } else { // Bottom (Below Web Page)
                mainLayout->addWidget(tabBarContainerWidget);
                mainLayout->addWidget(pageStackedWidget, 1);
                mainLayout->addWidget(toolbarContainer);
            }
        } else { // Bottom Tab Bar (Below Web Content)
            if (m_toolbarPosition == 0) { // Top Toolbar
                mainLayout->addWidget(toolbarContainer);
                mainLayout->addWidget(pageStackedWidget, 1);
                mainLayout->addWidget(tabBarContainerWidget);
            } else if (m_toolbarPosition == 1) { // Middle (Below Page, Above Tabbar)
                mainLayout->addWidget(pageStackedWidget, 1);
                mainLayout->addWidget(toolbarContainer);
                mainLayout->addWidget(tabBarContainerWidget);
            } else { // Bottom (Below Tabbar)
                mainLayout->addWidget(pageStackedWidget, 1);
                mainLayout->addWidget(tabBarContainerWidget);
                mainLayout->addWidget(toolbarContainer);
            }
        }
    }

    void rebuildSideToolbarLayout() {
        QLayoutItem *item;
        while ((item = sideLayout->takeAt(0)) != nullptr) {
            delete item;
        }

        if (m_showSideWinMenu) sideLayout->addWidget(btnWinMenu);
        btnWinMenu->setVisible(m_showSideWinMenu);

        if (m_showSideSettings) sideLayout->addWidget(btnSideSettings);
        btnSideSettings->setVisible(m_showSideSettings);

        sideLayout->addStretch(1);

        if (m_showSideReload) sideLayout->addWidget(btnSideReload);
        btnSideReload->setVisible(m_showSideReload);

        sideLayout->addStretch(1);

        if (m_showSideNav) {
            sideLayout->addWidget(btnSideBack);
            sideLayout->addWidget(btnSideForward);
        }
        btnSideBack->setVisible(m_showSideNav);
        btnSideForward->setVisible(m_showSideNav);

        sideLayout->addStretch(1);

        if (m_showSideHome) sideLayout->addWidget(btnSideHome);
        btnSideHome->setVisible(m_showSideHome);

        // Bookmark-Button direkt unter Home, ohne extra Space
        // (kein Stretch dazwischen, Spacing ist 0).
        sideLayout->addWidget(btnSideBookmarks);

        sideLayout->addStretch(1);

        if (m_showSidePassManager) {
            // Erster Passwort-Button: Domainliste als Popup.
            sideLayout->addWidget(btnSidePassList);
            sideLayout->addWidget(btnSideUser);
            sideLayout->addWidget(btnSidePass);
            sideLayout->addWidget(btnSideEditPass);
        }
        btnSideUser->setVisible(m_showSidePassManager);
        btnSidePass->setVisible(m_showSidePassManager);
        btnSideEditPass->setVisible(m_showSidePassManager);

        sideLayout->addStretch(1);

        if (m_showSideMobile) sideLayout->addWidget(btnSideMobile);
        btnSideMobile->setVisible(m_showSideMobile);

        if (m_showSideNotes) sideLayout->addWidget(btnSideNotes);
        btnSideNotes->setVisible(m_showSideNotes);

        sideLayout->addStretch(1);

        if (m_showSideNewTab) sideLayout->addWidget(btnSideNewTab);
        btnSideNewTab->setVisible(m_showSideNewTab);

        sideLayout->addStretch(2);
    }

    // Sidebar links oder rechts: outerLayout neu anordnen.
    void applySideBarPosition() {
        outerLayout->removeWidget(sideToolbarWidget);
        outerLayout->removeWidget(centerContentWidget);
        if (m_sideBarPosition == 0) {
            // Links (Standard).
            outerLayout->addWidget(sideToolbarWidget, 0);
            outerLayout->addWidget(centerContentWidget, 1);
        } else {
            // Rechts.
            outerLayout->addWidget(centerContentWidget, 1);
            outerLayout->addWidget(sideToolbarWidget, 0);
        }
    }

    void rebuildToolbarLayout() {
        rebuildMainLayout();

        QLayoutItem *item;
        while ((item = toolsLayout->takeAt(0)) != nullptr) {
            delete item;
        }

        if (m_showNav) {
            toolsLayout->addWidget(btnBack);
            toolsLayout->addWidget(btnForward);
        }
        if (m_showReload) {
            toolsLayout->addWidget(btnReload);
        }
        if (m_showHome) {
            toolsLayout->addWidget(btnHome);
        }
        if (m_showUrl) {
            toolsLayout->addWidget(urlEdit, 1);
            // Spacer hinter der URL-Bar (transparent, Mindestbreite einstellbar).
            urlBarSpacer->setMinimumWidth(m_urlBarSpacerMinWidth);
            toolsLayout->addWidget(urlBarSpacer);
        }
        if (m_showGo) {
            toolsLayout->addWidget(btnGo);
        }
        if (m_showZoom) {
            toolsLayout->addWidget(zoomCombo);
        }
        if (m_showBookmarks) {
            toolsLayout->addWidget(bookmarkCombo);
        }
        if (m_showAddBookmark) {
            toolsLayout->addWidget(btnAddBookmark);
        }
        if (m_showPassList) {
            toolsLayout->addWidget(passCombo);
        }
        if (m_showPassButtons) {
            toolsLayout->addWidget(btnCopyUser);
            toolsLayout->addWidget(btnCopyPass);
            toolsLayout->addWidget(btnEditPass);
        }
        if (m_showMobileToggle) {
            toolsLayout->addWidget(btnMobileToggle);
        }
        if (m_showNotes) {
            toolsLayout->addWidget(btnNotes);
        }

        // Tab Combo + New Tab button placed just left of the clock!
        if (m_showTabJump) {
            toolsLayout->addWidget(tabJumpCombo);
        }
        if (m_showNewTab) {
            toolsLayout->addWidget(btnNewTab);
        }

        if (m_showClock) {
            toolsLayout->addWidget(clockLabel);
        }

        btnBack->setVisible(m_showNav);
        btnForward->setVisible(m_showNav);
        btnReload->setVisible(m_showReload);
        btnHome->setVisible(m_showHome);
        urlEdit->setVisible(m_showUrl);
        btnGo->setVisible(m_showGo);
        zoomCombo->setVisible(m_showZoom);
        bookmarkCombo->setVisible(m_showBookmarks);
        btnAddBookmark->setVisible(m_showAddBookmark);
        tabJumpCombo->setVisible(m_showTabJump);
        btnNewTab->setVisible(m_showNewTab);

        passCombo->setVisible(m_showPassList);
        btnCopyUser->setVisible(m_showPassButtons);
        btnCopyPass->setVisible(m_showPassButtons);
        btnEditPass->setVisible(m_showPassButtons);

        btnMobileToggle->setVisible(m_showMobileToggle);
        btnNotes->setVisible(m_showNotes);
        clockLabel->setVisible(m_showClock);

        toolbarContainer->setVisible(!m_hideToolbar);
    }

    CustomWebEngineView* currentWebView() const {
        return qobject_cast<CustomWebEngineView*>(pageStackedWidget->currentWidget());
    }

    void applyTheme(bool dark) {
        // Geminis Technik: Bei Transparenz KEIN opakes App-Stylesheet.
        // Opake Hintergruende wuerden die Transparenz zudecken (der Desktop
        // kaeme nie durch). Die Hintergruende kommen dann nur aus
        // applyTransparency (rgba pro Bereich). Textfarben, Rahmen,
        // Padding und Scrollbars bleiben.
        if (anyTransparency()) {
            if (dark) {
                setStyleSheet(
                    "QWidget { color: #e0e0e0; }"
                    "#toolbarContainer, #sideToolbarWidget, #tabBarContainerWidget { border: 0px; }"
                    "QLineEdit, QComboBox, QPushButton, QDialog, QTextEdit, QScrollArea { color: #e0e0e0; border: 1px solid #444444; padding: 3px 6px; border-radius: 3px; }"
                    "QTabBar::tab { color: #b0b0b0; padding: 5px 10px; margin: 2px 3px; border: 1px solid #444444; border-radius: 4px; }"
                    "QTabBar::tab:selected { color: #ffffff; }"
                    "QScrollBar:vertical { width: 12px; margin: 0px; border-radius: 4px; }"
                    "QScrollBar::handle:vertical { min-height: 25px; border-radius: 4px; }"
                    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
                    "QScrollBar:horizontal { height: 12px; margin: 0px; border-radius: 4px; }"
                    "QScrollBar::handle:horizontal { min-width: 25px; border-radius: 4px; }"
                    "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
                );
            } else {
                setStyleSheet(
                    "#toolbarContainer, #sideToolbarWidget, #tabBarContainerWidget { border: 0px; }"
                    "QLineEdit, QComboBox, QPushButton { padding: 3px 6px; }"
                );
            }
        } else if (dark) {
            setStyleSheet(
                "QWidget { background-color: #121212; color: #e0e0e0; }"
                "#toolbarContainer, #sideToolbarWidget, #tabBarContainerWidget { background-color: #1a1a1a; border: 0px; }"
                "QLineEdit, QComboBox, QPushButton, QDialog, QTextEdit, QScrollArea { background-color: #1a1a1a; color: #e0e0e0; border: 1px solid #444444; padding: 3px 6px; border-radius: 3px; }"
                "QPushButton:hover { background-color: #333333; }"
                "QTabBar::tab { background: #1a1a1a; color: #b0b0b0; padding: 5px 10px; margin: 2px 3px; border: 1px solid #444444; border-radius: 4px; }"
                "QTabBar::tab:selected { background: #333333; color: #ffffff; }"
                "QScrollBar:vertical { background: #1a1a1a; width: 12px; margin: 0px; border-radius: 4px; }"
                "QScrollBar::handle:vertical { background: #555555; min-height: 25px; border-radius: 4px; }"
                "QScrollBar::handle:vertical:hover { background: #777777; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
                "QScrollBar:horizontal { background: #1a1a1a; height: 12px; margin: 0px; border-radius: 4px; }"
                "QScrollBar::handle:horizontal { background: #555555; min-width: 25px; border-radius: 4px; }"
                "QScrollBar::handle:horizontal:hover { background: #777777; }"
                "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
            );
        } else {
            setStyleSheet(
                "#toolbarContainer, #sideToolbarWidget, #tabBarContainerWidget { border: 0px; }"
                "QLineEdit, QComboBox, QPushButton { padding: 3px 6px; }"
            );
        }
        // Transparenz + gezeichnete Icons gehoeren zum Theme.
        applyTransparency();
        applyButtonIcons();
    }

    bool anyTransparency() const {
        return m_transTabBarBg < 100 || m_transTabBarControls < 100
            || m_transToolbarBg < 100 || m_transToolbarControls < 100
            || m_transSideBg < 100 || m_transSideControls < 100
            || m_transWebpage < 100;
    }

    void applyTransparency() {
        // Translucency only on the top-level window. Set in the ctor (before
        // show()); in the dialog, hide/show recreates the native window
        // when the state changes (see openSettingsDialog).
        setAttribute(Qt::WA_TranslucentBackground, anyTransparency());
        if (anyTransparency()) {
            // ID selector only: matches the main window itself, never a child.
            setStyleSheet(styleSheet() + "#mainWindow { background-color: transparent; }");
        }

        // Farben aus der System-Palette (Geminis Technik): Bei App-Dark-Mode
        // aus wird das echte Qt-Theme des Users genommen — dunkles
        // System-Theme bleibt dunkel, statt auf Weiss zu springen.
        bool darkUi = m_darkMode || paletteIsDark();
        QColor bg = m_darkMode ? QColor(0x1a, 0x1a, 0x1a) : QApplication::palette().window().color();
        QColor ctrl = m_darkMode ? QColor(0x1a, 0x1a, 0x1a) : QApplication::palette().button().color();
        QColor ctrlHover = m_darkMode ? QColor(0x33, 0x33, 0x33) : ctrl.lighter(120);

        if (!anyTransparency()) {
            toolbarContainer->setStyleSheet("");
            sideToolbarWidget->setStyleSheet("");
            tabBarContainerWidget->setStyleSheet("");
            centerContentWidget->setStyleSheet("");
            pageStackedWidget->setStyleSheet("");
        } else {
            toolbarContainer->setStyleSheet(
                QString("#toolbarContainer { background-color: %1; }"
                        "#toolbarContainer QPushButton, #toolbarContainer QLineEdit, #toolbarContainer QComboBox, #toolbarContainer QLabel { background-color: %2; }"
                        "#toolbarContainer QPushButton:hover { background-color: %3; }")
                    .arg(muRgba(bg, m_transToolbarBg)).arg(muRgba(ctrl, m_transToolbarControls)).arg(muRgba(ctrlHover, m_transToolbarControls)));
            sideToolbarWidget->setStyleSheet(
                QString("#sideToolbarWidget { background-color: %1; }"
                        "#sideToolbarWidget QPushButton { background-color: %2; }"
                        "#sideToolbarWidget QPushButton:hover { background-color: %3; }")
                    .arg(muRgba(bg, m_transSideBg)).arg(muRgba(ctrl, m_transSideControls)).arg(muRgba(ctrlHover, m_transSideControls)));
            // Tab text colors explicit (sonst Ghosting / veraltete Farbe).
            QString tabText = darkUi ? "#e0e0e0" : "#202020";
            QString tabTextSel = darkUi ? "#ffffff" : "#000000";
            tabBarContainerWidget->setStyleSheet(
                QString("#tabBarContainerWidget { background-color: %1; }"
                        "#tabBarContainerWidget QTabBar { background-color: transparent; }"
                        "#tabBarContainerWidget QTabBar::tab { background: %2; color: %4; padding: 5px 10px; margin: 2px 3px; border-radius: 4px; }"
                        "#tabBarContainerWidget QTabBar::tab:selected { background: %3; color: %5; }"
                        "#tabBarContainerWidget QPushButton { background-color: %2; }"
                        "#tabBarContainerWidget QPushButton:hover { background-color: %3; }")
                    .arg(muRgba(bg, m_transTabBarBg)).arg(muRgba(ctrl, m_transTabBarControls)).arg(muRgba(ctrlHover, m_transTabBarControls))
                    .arg(tabText).arg(tabTextSel));
            // ID selectors only: transparent containers, so the page area
            // shows the desktop. A bare rule here would also match every
            // button inside (the bug in the first version).
            centerContentWidget->setStyleSheet("#centerContentWidget { background-color: transparent; }");
            pageStackedWidget->setStyleSheet("#pageStackedWidget { background-color: transparent; }");
        }

        // Erzwungenes, SOFORTIGES Neuzeichnen (Ghosting auf transluzentem Fenster).
        // update() plant nur ein Neuzeichnen ein — auf einem transluzenten
        // Fenster bleibt der alte Inhalt sonst im Backing-Store sichtbar
        // (verwaschene Textreste, z.B. in der Seitenleiste). repaint() ist
        // synchron und loescht den Backing-Store wirklich.
        repaint();
        if (toolbarContainer) toolbarContainer->repaint();
        if (sideToolbarWidget) sideToolbarWidget->repaint();
        if (tabBarContainerWidget) tabBarContainerWidget->repaint();
        if (centerContentWidget) centerContentWidget->repaint();
        if (pageStackedWidget) pageStackedWidget->repaint();

        // Web page: background + opacity on every open page.
        for (int i = 0; i < pageStackedWidget->count(); ++i) {
            if (auto *view = qobject_cast<CustomWebEngineView*>(pageStackedWidget->widget(i))) {
                if (auto *p = view->page()) {
                    p->setBackgroundColor(m_transWebpage < 100 ? Qt::transparent : Qt::white);
                    injectPageTransparencyToPage(p);
                }
            }
        }
    }

    void injectPageTransparencyToPage(QWebEnginePage *page) {
        if (!page) return;
        if (m_transWebpage >= 100) {
            // Opaque: Seite zurueck auf Normalzustand.
            page->runJavaScript(R"JS(
                (function() {
                    let doc = document.documentElement;
                    if (!doc) return;
                    doc.style.removeProperty('opacity');
                })();
            )JS");
            return;
        }
        double op = static_cast<double>(qBound(10, m_transWebpage, 100)) / 100.0;
        QString js = QString(R"JS(
            (function() {
                let doc = document.documentElement;
                if (!doc) return;
                doc.style.setProperty('opacity', '%1', 'important');
            })();
        )JS").arg(op, 0, 'f', 2);
        page->runJavaScript(js);
    }

    void applyButtonIcons() {
        // Gezeichnete Icons oder Buchstaben/Symbole, je nach Einstellung.
        auto setBtn = [&](QPushButton *b, const QString &kind, const QString &letter) {
            if (!b) return;
            if (m_useButtonIcons) {
                b->setIcon(drawBtnIcon(kind, m_darkMode || paletteIsDark()));
                b->setIconSize(QSize(18, 18));
                b->setText("");
            } else {
                b->setIcon(QIcon());
                b->setText(letter);
            }
        };
        setBtn(btnWinMenu, "winmenu", "⚙"); // Window Menu: own icon (window), NOT settings gear
        setBtn(btnSideSettings, "settings", "🛠"); // Settings: gear (distinct from winmenu)
        setBtn(btnSideReload, "reload", "⟳");
        setBtn(btnSideBack, "back", "◀");
        setBtn(btnSideForward, "forward", "▶");
        setBtn(btnSideHome, "home", "⌂");
        setBtn(btnSideBookmarks, "bookmarks", "★");
        setBtn(btnSidePassList, "passlist", "D");
        setBtn(btnSideUser, "user", "u");
        setBtn(btnSidePass, "pass", "p");
        setBtn(btnSideEditPass, "edit", "e");
        setBtn(btnSideMobile, "mobile", "m");
        setBtn(btnSideNotes, "notes", "n");
        setBtn(btnSideNewTab, "newtab", "t");
        setBtn(btnBack, "back", "◀");
        setBtn(btnForward, "forward", "▶");
        setBtn(btnReload, "reload", "⟳");
        setBtn(btnHome, "home", "⌂");
        setBtn(btnCopyUser, "user", "u");
        setBtn(btnCopyPass, "pass", "p");
        setBtn(btnEditPass, "edit", "e");
        setBtn(btnMobileToggle, "mobile", "m");
        setBtn(btnNotes, "notes", "n");
        setBtn(btnTabNewTab, "newtab", "+");
        // Nach Icon-Wechsel sofort neuzeichnen (sonst Geist-Text).
        update();
    }

    QString getConfigFilePath() {
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataPath);
        return dataPath + "/desktop_config.json";
    }

    void loadConfig() {
        m_darkMode = false;
        m_unicodeIcons = true;
        m_isFrameless = false;
        m_homepageUrl = "https://duckduckgo.com";
        m_strictPrivacy = false;

        m_adBlocker = true; // Default: an (früher war Werbung ausgeblendet).
        m_urlBarMinWidth = 300;
        m_reloadButtonMinWidth = 30;
        m_defaultZoom = 100;
        m_uiZoom = 100;
        m_tabZoom = 100;

        m_useButtonIcons = false;
        m_transTabBarBg = 100; m_transTabBarControls = 100;
        m_transToolbarBg = 100; m_transToolbarControls = 100;
        m_transSideBg = 100; m_transSideControls = 100;
        m_transWebpage = 100;

        m_showNav = true;
        m_showReload = true;
        m_showHome = true;
        m_showUrl = true;
        m_showGo = true;
        m_showBookmarks = true;
        m_showAddBookmark = true;
        m_showTabJump = true;
        m_showNewTab = true;
        m_showZoom = true;

        m_showPassList = true;
        m_showPassButtons = true;
        m_showMobileToggle = true;
        m_showClock = true;
        m_showNotes = true;
        m_toolbarPosition = 0;
        m_tabBarPosition = 0;
        m_showSideToolbar = false;
        m_showTabNewTab = true;
        m_showSideNewTab = true;
        m_showSideSettings = true;
        m_clock24h = true;
        m_showSideWinMenu = true;
        m_showSideReload = true;
        m_showSideNav = true;
        m_showSideHome = true;
        m_showSidePassManager = true;
        m_showSideMobile = true;
        m_showSideNotes = true;

        m_clockCommand = "mu-calendar";
        m_notesCommand = "mu-notepad ~/notes.txt";

        QFile file(getConfigFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                m_darkMode = obj["darkMode"].toBool(false);
                m_unicodeIcons = obj["unicodeIcons"].toBool(true);
                m_isFrameless = obj["frameless"].toBool(false);
                m_strictPrivacy = obj["strictPrivacy"].toBool(false);

                if (obj.contains("adBlocker")) m_adBlocker = obj["adBlocker"].toBool(true);
                if (obj.contains("mobileMode")) m_mobileMode = obj["mobileMode"].toBool(false); // Global Mobile/Desktop toggle
                m_useButtonIcons = obj["useButtonIcons"].toBool(false);
                if (obj.contains("transTabBarBg")) m_transTabBarBg = obj["transTabBarBg"].toInt(100);
                if (obj.contains("transTabBarControls")) m_transTabBarControls = obj["transTabBarControls"].toInt(100);
                if (obj.contains("transToolbarBg")) m_transToolbarBg = obj["transToolbarBg"].toInt(100);
                if (obj.contains("transToolbarControls")) m_transToolbarControls = obj["transToolbarControls"].toInt(100);
                if (obj.contains("transSideBg")) m_transSideBg = obj["transSideBg"].toInt(100);
                if (obj.contains("transSideControls")) m_transSideControls = obj["transSideControls"].toInt(100);
                if (obj.contains("transWebpage")) m_transWebpage = obj["transWebpage"].toInt(100);
                if (obj.contains("urlBarSpacerMinWidth")) m_urlBarSpacerMinWidth = obj["urlBarSpacerMinWidth"].toInt(0);
                if (obj.contains("sideBarPosition")) m_sideBarPosition = obj["sideBarPosition"].toInt(0);
                if (obj.contains("windowGeometry")) m_windowGeometry = QByteArray::fromBase64(obj["windowGeometry"].toString().toLatin1());
                // Clamp: ungueltige Werte abfangen, nie crashen lassen.
                m_transTabBarBg = qBound(10, m_transTabBarBg, 100);
                m_transTabBarControls = qBound(10, m_transTabBarControls, 100);
                m_transToolbarBg = qBound(10, m_transToolbarBg, 100);
                m_transToolbarControls = qBound(10, m_transToolbarControls, 100);
                m_transSideBg = qBound(10, m_transSideBg, 100);
                m_transSideControls = qBound(10, m_transSideControls, 100);
                m_transWebpage = qBound(10, m_transWebpage, 100);
                m_urlBarMinWidth = obj["urlBarMinWidth"].toInt(300);
                m_reloadButtonMinWidth = obj["reloadButtonMinWidth"].toInt(30);
                m_defaultZoom = obj["defaultZoom"].toInt(100);
                m_uiZoom = obj["uiZoom"].toInt(100);
                m_tabZoom = obj["tabZoom"].toInt(100);

                if (obj.contains("homepage")) m_homepageUrl = obj["homepage"].toString();
                if (obj.contains("clockCommand")) m_clockCommand = obj["clockCommand"].toString();
                if (obj.contains("notesCommand")) m_notesCommand = obj["notesCommand"].toString();
                if (obj.contains("toolbarPosition")) m_toolbarPosition = obj["toolbarPosition"].toInt(0);
                else if (obj.contains("toolbarBelowTabBar") && obj["toolbarBelowTabBar"].toBool(false)) m_toolbarPosition = 1;

                if (obj.contains("tabBarPosition")) m_tabBarPosition = obj["tabBarPosition"].toInt(0);
                if (obj.contains("showTabNewTab")) m_showTabNewTab = obj["showTabNewTab"].toBool(true);
                if (obj.contains("showSideNewTab")) m_showSideNewTab = obj["showSideNewTab"].toBool(true);
                if (obj.contains("showSideSettings")) m_showSideSettings = obj["showSideSettings"].toBool(true);
                if (obj.contains("clock24h")) m_clock24h = obj["clock24h"].toBool(true);
                if (obj.contains("showSideToolbar")) m_showSideToolbar = obj["showSideToolbar"].toBool(false);
                if (obj.contains("showSideWinMenu")) m_showSideWinMenu = obj["showSideWinMenu"].toBool(true);
                if (obj.contains("showSideReload")) m_showSideReload = obj["showSideReload"].toBool(true);
                if (obj.contains("showSideNav")) m_showSideNav = obj["showSideNav"].toBool(true);
                if (obj.contains("showSideHome")) m_showSideHome = obj["showSideHome"].toBool(true);
                if (obj.contains("showSidePassManager")) m_showSidePassManager = obj["showSidePassManager"].toBool(true);
                if (obj.contains("showSideMobile")) m_showSideMobile = obj["showSideMobile"].toBool(true);
                if (obj.contains("showSideNotes")) m_showSideNotes = obj["showSideNotes"].toBool(true);

                if (obj.contains("showNav")) m_showNav = obj["showNav"].toBool(true);
                if (obj.contains("showReload")) m_showReload = obj["showReload"].toBool(true);
                if (obj.contains("showHome")) m_showHome = obj["showHome"].toBool(true);
                if (obj.contains("showUrl")) m_showUrl = obj["showUrl"].toBool(true);
                if (obj.contains("showGo")) m_showGo = obj["showGo"].toBool(true);
                if (obj.contains("showBookmarks")) m_showBookmarks = obj["showBookmarks"].toBool(true);
                if (obj.contains("showAddBookmark")) m_showAddBookmark = obj["showAddBookmark"].toBool(true);
                if (obj.contains("showTabJump")) m_showTabJump = obj["showTabJump"].toBool(true);
                if (obj.contains("showNewTab")) m_showNewTab = obj["showNewTab"].toBool(true);
                if (obj.contains("showZoom")) m_showZoom = obj["showZoom"].toBool(true);

                if (obj.contains("showPassList")) m_showPassList = obj["showPassList"].toBool(true);
                if (obj.contains("showPassButtons")) m_showPassButtons = obj["showPassButtons"].toBool(true);
                else if (obj.contains("showPassManager")) m_showPassList = m_showPassButtons = obj["showPassManager"].toBool(true);

                if (obj.contains("showMobileToggle")) m_showMobileToggle = obj["showMobileToggle"].toBool(true);
                if (obj.contains("showClock")) m_showClock = obj["showClock"].toBool(true);
                if (obj.contains("showNotes")) m_showNotes = obj["showNotes"].toBool(true);
            }
            file.close();
        }
    }

    void saveConfig() {
        QJsonObject obj;
        obj["darkMode"] = m_darkMode;
        obj["unicodeIcons"] = m_unicodeIcons;
        obj["frameless"] = m_isFrameless;
        obj["strictPrivacy"] = m_strictPrivacy;

        obj["adBlocker"] = m_adBlocker;
        obj["mobileMode"] = m_mobileMode; // Global Mobile/Desktop toggle
        obj["useButtonIcons"] = m_useButtonIcons;
        obj["transTabBarBg"] = m_transTabBarBg;
        obj["transTabBarControls"] = m_transTabBarControls;
        obj["transToolbarBg"] = m_transToolbarBg;
        obj["transToolbarControls"] = m_transToolbarControls;
        obj["transSideBg"] = m_transSideBg;
        obj["transSideControls"] = m_transSideControls;
        obj["transWebpage"] = m_transWebpage;
        obj["urlBarSpacerMinWidth"] = m_urlBarSpacerMinWidth;
        obj["sideBarPosition"] = m_sideBarPosition;
        // Fenster-Geometrie (Position + Groesse + maximiert). Nur wenn nicht
        // minimiert — sonst wuerde ein minimiert geschlossenes Fenster
        // minimiert wieder aufgehen.
        if (!isMinimized()) obj["windowGeometry"] = QString::fromLatin1(saveGeometry().toBase64());
        obj["homepage"] = m_homepageUrl;
        obj["clockCommand"] = m_clockCommand;
        obj["notesCommand"] = m_notesCommand;
        obj["toolbarPosition"] = m_toolbarPosition;
        obj["tabBarPosition"] = m_tabBarPosition;
        obj["showSideToolbar"] = m_showSideToolbar;
        obj["showTabNewTab"] = m_showTabNewTab;
        obj["showSideNewTab"] = m_showSideNewTab;
        obj["showSideSettings"] = m_showSideSettings;
        obj["clock24h"] = m_clock24h;
        obj["showSideWinMenu"] = m_showSideWinMenu;
        obj["showSideReload"] = m_showSideReload;
        obj["showSideNav"] = m_showSideNav;
        obj["showSideHome"] = m_showSideHome;
        obj["showSidePassManager"] = m_showSidePassManager;
        obj["showSideMobile"] = m_showSideMobile;
        obj["showSideNotes"] = m_showSideNotes;

        obj["urlBarMinWidth"] = m_urlBarMinWidth;
        obj["reloadButtonMinWidth"] = m_reloadButtonMinWidth;
        obj["defaultZoom"] = m_defaultZoom;
        obj["uiZoom"] = m_uiZoom;
        obj["tabZoom"] = m_tabZoom;

        obj["showNav"] = m_showNav;
        obj["showReload"] = m_showReload;
        obj["showHome"] = m_showHome;
        obj["showUrl"] = m_showUrl;
        obj["showGo"] = m_showGo;
        obj["showBookmarks"] = m_showBookmarks;
        obj["showAddBookmark"] = m_showAddBookmark;
        obj["showTabJump"] = m_showTabJump;
        obj["showNewTab"] = m_showNewTab;
        obj["showZoom"] = m_showZoom;
        obj["showPassList"] = m_showPassList;
        obj["showPassButtons"] = m_showPassButtons;
        obj["showMobileToggle"] = m_showMobileToggle;
        obj["showClock"] = m_showClock;
        obj["showNotes"] = m_showNotes;

        QFile file(getConfigFilePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(obj).toJson());
            file.close();
        }
    }

    void updateTabsCombo() {
        m_updatingTabsCombo = true;
        tabJumpCombo->clear();
        for (int i = 0; i < tabBarWidget->count(); ++i) {
            QString tabText = tabBarWidget->tabText(i);
            tabJumpCombo->addItem(QString("%1| %2").arg(i + 1).arg(tabText));
        }
        tabJumpCombo->setCurrentIndex(tabBarWidget->currentIndex());
        m_updatingTabsCombo = false;
    }

    void updateZoomComboSelection(int zoomVal) {
        m_updatingZoomCombo = true;
        for (int i = 0; i < zoomCombo->count(); ++i) {
            if (zoomCombo->itemData(i).toInt() == zoomVal) {
                zoomCombo->setCurrentIndex(i);
                break;
            }
        }
        m_updatingZoomCombo = false;
    }

    QWebEnginePage *createPopupTabPage() {
        auto *webView = new CustomWebEngineView(this, this);
        webView->setUseUnicodeIcons(m_unicodeIcons);
        auto *page = new CustomWebPage(m_profile, webView);
        
        webView->setPage(page);
        webView->setZoomFactor(static_cast<qreal>(m_defaultZoom) / 100.0);

        page->setNewPageFactory([this]() -> QWebEnginePage* { return createPopupTabPage(); });
        installSameHostTargetFixOnPage(page);

        QObject::connect(page, &QWebEnginePage::loadFinished, page, [this, page](bool ok) {
            if (ok) {
                if (m_darkMode) injectDarkModeToPage(page);
                injectUserCssToPage(page);
                // Seiten-Transparenz bei jedem Load (Seite ist sonst opaque).
                page->setBackgroundColor(m_transWebpage < 100 ? Qt::transparent : Qt::white);
                injectPageTransparencyToPage(page);
            }
        });

        connect(webView, &QWebEngineView::titleChanged, this, [this, webView](const QString &title) {
            int idx = pageStackedWidget->indexOf(webView);
            if (idx != -1) { 
                QString t = title.trimmed(); 
                if (t.length() > 25) t = t.left(22) + "..."; 
                tabBarWidget->setTabText(idx, t.isEmpty() ? "New Tab" : t);
                updateTabsCombo();
            }
        });

        // Favicon ins Tab (Opera-Stil): iconChanged feuert, sobald das
        // Favicon der Seite geladen ist.
        connect(webView, &QWebEngineView::iconChanged, this, [this, webView](const QIcon &icon) {
            int idx = pageStackedWidget->indexOf(webView);
            if (idx != -1 && !icon.isNull()) {
                tabBarWidget->setTabIcon(idx, icon);
            }
        });

        connect(webView, &QWebEngineView::urlChanged, this, [this, webView](const QUrl &url) {
            if (webView == currentWebView()) {
                urlEdit->setText(url.toString());
                autoMatchPasswordDomain(url);
            }
        });

        connect(webView, &CustomWebEngineView::toggleToolbarRequested, this, [this]() {
            toolbarContainer->setVisible(!toolbarContainer->isVisible());
        });
        connect(webView, &CustomWebEngineView::toggleTabbarRequested, this, [this]() {
            tabBarWidget->setVisible(!tabBarWidget->isVisible());
        });
        connect(webView, &CustomWebEngineView::toggleDarkModeRequested, this, [this]() {
            toggleDarkMode();
        });
        connect(webView, &CustomWebEngineView::setAsHomepageRequested, this, [this, webView]() {
            setHomepage(webView->url().toString());
        });
        connect(webView, &CustomWebEngineView::openSettingsRequested, this, [this]() {
            openSettingsDialog();
        });
        connect(webView, &CustomWebEngineView::openUrlInNewTabRequested, this, [this](const QString &url) {
            addNewTab(url);
        });
        connect(webView, &CustomWebEngineView::addCurrentBookmarkRequested, this, [this]() {
            addCurrentBookmark();
        });
        connect(webView, &CustomWebEngineView::mobileModeToggled, this, [this, webView](bool isMobile) {
            if (webView == currentWebView()) updateMobileToggleButton(isMobile);
        });

        connect(page, &QWebEnginePage::windowCloseRequested, this, [this, webView]() {
            int idx = pageStackedWidget->indexOf(webView);
            if (idx >= 0) closeTab(idx);
        });

        int idx = tabBarWidget->addTab("Loading...");
        pageStackedWidget->addWidget(webView);
        tabBarWidget->setCurrentIndex(idx);
        pageStackedWidget->setCurrentIndex(idx);
        updateTabsCombo();
        return page;
    }

    void closeTab(int index) {
        if (tabBarWidget->count() <= 1) {
            if (auto *view = currentWebView()) {
                view->load(QUrl(m_homepageUrl));
                tabBarWidget->setTabText(0, "DuckDuckGo");
            }
            return;
        }
        tabBarWidget->removeTab(index);
        auto *w = pageStackedWidget->widget(index);
        pageStackedWidget->removeWidget(w);
        delete w;
        updateTabsCombo();
    }

    void onTabChanged(int index) {
        if (index >= 0 && index < pageStackedWidget->count()) {
            pageStackedWidget->setCurrentIndex(index);
        }
        if (auto *view = currentWebView()) {
            urlEdit->setText(view->url().toString());
            qreal currentFactor = view->zoomFactor();
            int zoomPct = qRound(currentFactor * 100.0);
            updateZoomComboSelection(zoomPct);
            updateMobileToggleButton(view->isMobileMode());
            autoMatchPasswordDomain(view->url());
        }
        if (!m_updatingTabsCombo && index >= 0 && index < tabJumpCombo->count()) {
            m_updatingTabsCombo = true;
            tabJumpCombo->setCurrentIndex(index);
            m_updatingTabsCombo = false;
        }
    }

    void saveSession() {
        QJsonArray arr;
        for (int i = 0; i < pageStackedWidget->count(); ++i) {
            if (auto *view = qobject_cast<CustomWebEngineView*>(pageStackedWidget->widget(i))) {
                arr.append(view->url().toString());
            }
        }
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataPath);
        QFile file(dataPath + "/desktop_session.json");
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(arr).toJson());
            file.close();
        }
    }

    bool restoreSession() {
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QFile file(dataPath + "/desktop_session.json");
        if (!file.open(QIODevice::ReadOnly)) return false;

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (!doc.isArray() || doc.array().isEmpty()) return false;

        QJsonArray arr = doc.array();
        int count = 0;
        for (const auto &v : arr) {
            addNewTab(v.toString());
            count++;
        }
        return count > 0;
    }

    void applyDesktopLayer() {
#if defined(Q_OS_LINUX)
        if (m_layer.isEmpty() || m_layer == "normal") return;
        Display *display = XOpenDisplay(nullptr);
        if (!display) return;
        WId winId = internalWinId();
        Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
        Atom typeAtom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);

        if (m_layer == "desktop") {
            Atom desktopType = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
            XChangeProperty(display, winId, typeAtom, XA_ATOM, 32, PropModeReplace, (unsigned char *)&desktopType, 1);
        } else if (m_layer == "below") {
            Atom stateBelow = XInternAtom(display, "_NET_WM_STATE_BELOW", False);
            XChangeProperty(display, winId, wmState, XA_ATOM, 32, PropModeReplace, (unsigned char *)&stateBelow, 1);
        } else if (m_layer == "above") {
            Atom stateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
            XChangeProperty(display, winId, wmState, XA_ATOM, 32, PropModeReplace, (unsigned char *)&stateAbove, 1);
        }
        XFlush(display);
        XCloseDisplay(display);
#endif
    }

    void addCurrentBookmark() {
        if (!currentWebView()) return;
        QString url = currentWebView()->url().toString();
        QString title = currentWebView()->title();
        if (title.isEmpty()) title = url;

        bool ok = false;
        QString name = QInputDialog::getText(this, "Add Bookmark", "Name:", QLineEdit::Normal, title, &ok);
        if (ok && !name.isEmpty()) {
            m_bookmarks.append(BookmarkItem{name, url});
            saveBookmarks();
            updateBookmarkCombo();
        }
    }

    QString getBookmarksFilePath() {
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataPath);
        return dataPath + "/bookmarks.json";
    }

    void loadBookmarks() {
        m_bookmarks.clear();
        QFile file(getBookmarksFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                QJsonArray arr = doc.array();
                for (const auto &v : arr) {
                    QJsonObject obj = v.toObject();
                    m_bookmarks.append(BookmarkItem{obj["title"].toString(), obj["url"].toString()});
                }
            }
            file.close();
        }

        if (m_bookmarks.isEmpty()) {
            m_bookmarks.append(BookmarkItem{"DuckDuckGo", "https://duckduckgo.com"});
            saveBookmarks();
        }
    }

    void saveBookmarks() {
        QJsonArray arr;
        for (const auto &b : m_bookmarks) {
            QJsonObject obj;
            obj["title"] = b.title;
            obj["url"] = b.url;
            arr.append(obj);
        }
        QFile file(getBookmarksFilePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(arr).toJson());
            file.close();
        }
    }

    void updateBookmarkCombo() {
        bookmarkCombo->clear();
        // Mandatory first item: "Bookmarks" (static/non-functional label)
        bookmarkCombo->addItem("Bookmarks", "");
        bookmarkCombo->addItem("★ Add Current Page", "");
        bookmarkCombo->addItem("✏ Edit Bookmarks...", "");
        for (const auto &b : m_bookmarks) {
            bookmarkCombo->addItem(b.title, b.url);
        }
        bookmarkCombo->setCurrentIndex(0);
    }

    QString getPasswordsFilePath() {
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataPath);
        return dataPath + "/passwords.enc";
    }

    void savePasswords() {
        if (!m_passUnlocked || m_masterPassword.isEmpty()) return;
        QJsonArray arr;
        for (const auto &p : m_passwords) {
            QJsonObject obj;
            obj["title"] = p.titleUrl;
            obj["username"] = p.username;
            obj["password"] = p.password;
            arr.append(obj);
        }
        QByteArray jsonBytes = QJsonDocument(arr).toJson();
        QByteArray encrypted = SimpleCipher::encrypt(jsonBytes, m_masterPassword);

        QFile file(getPasswordsFilePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(encrypted);
            file.close();
        }
    }

    void updatePasswordCombo() {
        passCombo->clear();
        passCombo->addItem("Passwords", "");
        for (const auto &p : m_passwords) {
            passCombo->addItem(p.titleUrl, p.username);
        }
        passCombo->setCurrentIndex(0);
    }

    QString getStylesFilePath() {
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataPath);
        return dataPath + "/styles.json";
    }

    void loadStyleRules() {
        m_styleRules.clear();
        QFile file(getStylesFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                for (const auto &val : doc.array()) {
                    QJsonObject obj = val.toObject();
                    m_styleRules.append(StyleRule{obj["pattern"].toString(), obj["css"].toString()});
                }
            }
            file.close();
        }

        if (m_styleRules.isEmpty()) {
            // Default rules: google.com layout fix & gemini.google.com fix
            m_styleRules.append(StyleRule{
                "google.com",
                "/* Shift main content area by the width of the sidebar (approx. 72px collapsed / 260px expanded) */\nmain, div[role=\"main\"], #center_col {\n    margin-left: 80px !important;\n    width: calc(100% - 100px) !important;\n    box-sizing: border-box !important;\n}"
            });
            m_styleRules.append(StyleRule{
                "gemini.google.com",
                "/* Gemini expand button layout fix */ button[aria-label*='Expand'], button[aria-label*='expand'] { z-index: 9999 !important; position: relative !important; visibility: visible !important; display: inline-block !important; }"
            });
            saveStyleRules();
        }
    }

    void saveStyleRules() {
        QJsonArray arr;
        for (const auto &r : m_styleRules) {
            QJsonObject obj;
            obj["pattern"] = r.pattern;
            obj["css"] = r.cssCode;
            arr.append(obj);
        }
        QFile file(getStylesFilePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(arr).toJson());
            file.close();
        }
    }

    QString getUserAgentsFilePath() {
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataPath);
        return dataPath + "/user_agents.json";
    }

    QString getAdBlockExceptionsFilePath() {
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataPath);
        return dataPath + "/adblock_exceptions.json";
    }

    void loadAdBlockExceptions() {
        m_adBlockExceptions.clear();
        QFile file(getAdBlockExceptionsFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                for (const auto &val : doc.array()) {
                    QString ex = val.toString().trimmed();
                    if (!ex.isEmpty()) m_adBlockExceptions.append(ex);
                }
            }
            file.close();
        }
        if (m_interceptor) m_interceptor->setAdBlockExceptions(m_adBlockExceptions);
    }

    void saveAdBlockExceptions() {
        QJsonArray arr;
        for (const QString &ex : m_adBlockExceptions) arr.append(ex);
        QFile file(getAdBlockExceptionsFilePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(arr).toJson());
            file.close();
        }
    }

    void loadUserAgentRules() {
        m_uaRules.clear();
        QFile file(getUserAgentsFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                for (const auto &val : doc.array()) {
                    QJsonObject obj = val.toObject();
                    m_uaRules.append(UserAgentRule{
                        obj["pattern"].toString(),
                        obj["mode"].toString(),
                        obj["customUA"].toString()
                    });
                }
            }
            file.close();
        }

        // Built-in Defaults MERGEN (nicht nur bei leerer Liste):
        // Wer schon eine gespeicherte Regelliste hat (z.B. nur Google +
        // Facebook), bekam neue Defaults wie GMX sonst nie — GMX fiel
        // dann auf den Default-Zweig ohne passende Client Hints zurück
        // und lehnte mit "technical error" ab. Eigene Regeln bleiben
        // erhalten; fehlende Defaults werden ans Ende angehängt.
        auto hasRule = [&](const QString &pattern) {
            for (const auto &r : m_uaRules) {
                if (r.pattern.compare(pattern, Qt::CaseInsensitive) == 0) return true;
            }
            return false;
        };
        bool defaultsAdded = false;
        if (!hasRule("google.com")) {
            m_uaRules.append(UserAgentRule{
                "google.com",
                "Custom",
                "Mozilla/5.0 (X11; Linux x86_64; rv:131.0) Gecko/20100101 Firefox/131.0"
            });
            defaultsAdded = true;
        }
        // Facebook: NO fixed rule. Facebook follows the GLOBAL Mobile/Desktop
        // toggle (interceptor fallback). A saved facebook.com rule (e.g.
        // from older versions that forced Facebook to Mobile) is REMOVED
        // here — otherwise the toggle would never work for Facebook,
        // because a matching rule always wins over the fallback.
        // ("Mobile toggle not working in Facebook".) The user can still
        // add their own facebook.com rule in Settings if wanted.
        for (int i = m_uaRules.size() - 1; i >= 0; --i) {
            if (m_uaRules[i].pattern.compare("facebook.com", Qt::CaseInsensitive) == 0) {
                m_uaRules.removeAt(i);
                defaultsAdded = true;
            }
        }
        // GMX bewusst Desktop, NIE Mobile: Mobile-UA bricht Ordnerbaum / Session von GMX.
        // Eine fälschlich auf Mobile (oder Custom) gesetzte GMX-Regel
        // wird korrigiert — sonst serviert GMX falschen Code und der
        // Login-Flow bricht mit cross-origin SecurityError ab
        // (dl.gmx.net <-> navigator.gmx.net).
        auto ensureGmxDesktop = [&](const QString &pattern) {
            for (auto &r : m_uaRules) {
                if (r.pattern.compare(pattern, Qt::CaseInsensitive) == 0) {
                    if (r.mode != "Desktop") {
                        r.mode = "Desktop";
                        r.customUA.clear();
                        defaultsAdded = true;
                    }
                    return;
                }
            }
            m_uaRules.append(UserAgentRule{pattern, "Desktop", ""});
            defaultsAdded = true;
        };
        ensureGmxDesktop("gmx.net");
        ensureGmxDesktop("gmx.de");
        if (defaultsAdded) saveUserAgentRules();

        if (m_interceptor) {
            m_interceptor->setRules(m_uaRules);
        }
    }

    void saveUserAgentRules() {
        QJsonArray arr;
        for (const auto &r : m_uaRules) {
            QJsonObject obj;
            obj["pattern"] = r.pattern;
            obj["mode"] = r.mode;
            obj["customUA"] = r.customUA;
            arr.append(obj);
        }
        QFile file(getUserAgentsFilePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(arr).toJson());
            file.close();
        }
    }

    QWebEngineProfile *m_profile;
    MuRequestInterceptor *m_interceptor;

    QHBoxLayout *outerLayout;
    SideToolbarWidget *sideToolbarWidget;
    QVBoxLayout *sideLayout;
    QPushButton *btnWinMenu, *btnSideSettings;
    QPushButton *btnSideReload, *btnSideBack, *btnSideForward, *btnSideHome;
    QPushButton *btnSideUser, *btnSidePass, *btnSideEditPass;
    QPushButton *btnSidePassList, *btnSideBookmarks;
    int m_sideBarPosition = 0; // 0 = links, 1 = rechts
    QPushButton *btnSideMobile, *btnSideNotes, *btnSideNewTab;

    QWidget *centerContentWidget;
    QVBoxLayout *mainLayout;
    QWidget *toolbarContainer;
    QHBoxLayout *toolsLayout;
    QWidget *tabBarContainerWidget;
    QHBoxLayout *tabBarLayout;
    QTabBar *tabBarWidget;
    QPushButton *btnTabNewTab;
    QStackedWidget *pageStackedWidget;

    QPushButton *btnBack, *btnForward, *btnReload, *btnHome, *btnGo, *btnAddBookmark, *btnNewTab;
    QLineEdit *urlEdit;
    QComboBox *bookmarkCombo;
    QComboBox *tabJumpCombo;
    QComboBox *zoomCombo;

    // Password Manager UI
    ClickableComboBox *passCombo;
    QPushButton *btnCopyUser, *btnCopyPass, *btnEditPass;
    QList<PasswordEntry> m_passwords;
    bool m_passUnlocked;
    QString m_masterPassword;

    // Mobile Toggle UI
    QPushButton *btnMobileToggle;

    // Clock Widget
    ClickableLabel *clockLabel;
    QTimer *clockTimer;

    // Notes Button
    QPushButton *btnNotes;

    QList<BookmarkItem> m_bookmarks;
    QList<StyleRule> m_styleRules;
    QList<UserAgentRule> m_uaRules;

    bool m_isFrameless;
    bool m_hideToolbar;
    bool m_darkMode;
    bool m_strictPrivacy;

    bool m_adBlocker = true; // Generic ad blocker (interceptor).
    bool m_mobileMode = false; // Global Mobile/Desktop toggle (saved in config).
    QStringList m_adBlockExceptions; // Ad blocker exceptions.
    bool m_unicodeIcons;
    bool m_useButtonIcons;
    // Transparenz (Tech-HUD Style): 7 Werte, je ein Alpha pro Bereich.
    int m_transTabBarBg = 100, m_transTabBarControls = 100;
    int m_transToolbarBg = 100, m_transToolbarControls = 100;
    int m_transSideBg = 100, m_transSideControls = 100;
    int m_transWebpage = 100;
    QByteArray m_windowGeometry;
    int m_urlBarMinWidth;
    int m_urlBarSpacerMinWidth = 0; // Spacer hinter der URL-Bar (transparent)
    QWidget *urlBarSpacer = nullptr;
    int m_reloadButtonMinWidth;
    int m_defaultZoom;
    int m_uiZoom;
    int m_tabZoom;
    
    bool m_showNav;
    bool m_showReload;
    bool m_showHome;
    bool m_showUrl;
    bool m_showGo;
    bool m_showBookmarks;
    bool m_showAddBookmark;
    bool m_showTabJump;
    bool m_showNewTab;
    bool m_showZoom;
    bool m_showPassList;
    bool m_showPassButtons;
    bool m_showMobileToggle;
    bool m_showClock;
    bool m_showNotes;

    int m_toolbarPosition; // 0 = Top, 1 = Middle/Below Tabbar, 2 = Bottom
    int m_tabBarPosition;  // 0 = Top, 1 = Bottom
    bool m_showSideToolbar;
    bool m_showTabNewTab;
    bool m_showSideNewTab;
    bool m_showSideSettings;
    bool m_clock24h;
    bool m_showSideWinMenu;
    bool m_showSideReload;
    bool m_showSideNav;
    bool m_showSideHome;
    bool m_showSidePassManager;
    bool m_showSideMobile;
    bool m_showSideNotes;

    QString m_clockCommand;
    QString m_notesCommand;

    bool m_updatingTabsCombo;
    bool m_updatingZoomCombo;
    QString m_homepageUrl;
    QString m_layer;
};

// CustomWebEngineView::toggleMobileMode() — defined AFTER BrowserWindow,
// because BrowserWindow is only forward-declared inside CustomWebEngineView.
// Uses the view's window pointer (m_browserWindow — Tabs: m_win,
// Desktop/Main: m_browserWindow).
void CustomWebEngineView::toggleMobileMode() {
    if (m_browserWindow) {
        m_browserWindow->toggleMobileMode();
        return;
    }
    m_isMobileMode = !m_isMobileMode;
    if (m_isMobileMode) page()->profile()->setHttpUserAgent(muMobileUA());
    else page()->profile()->setHttpUserAgent(muChromeUA());
    reload();
}

int main(int argc, char *argv[]) {
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString configPath = dataPath + "/desktop_config.json";
    bool strictModeSaved = false;
    
    QFile cfgFile(configPath);
    if (cfgFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(cfgFile.readAll());
        if (doc.isObject()) {
            strictModeSaved = doc.object()["strictPrivacy"].toBool(false);
        }
        cfgFile.close();
    }

    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--strict-privacy") {
            strictModeSaved = true;
            break;
        }
    }

    if (strictModeSaved) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", 
            "--disable-breakpad "
            "--disable-component-update "
            "--disable-domain-reliability "
            "--disable-sync "
            "--disable-background-networking "
            "--disable-features=Translate,AutofillServerCommunication "
            "--no-pings "
            "--enable-features=NetworkServiceInProcess"
        );
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mu"));
    QCoreApplication::setApplicationName(QStringLiteral("mu-desktop-browser"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Mu WebEngine Unrestricted Desktop Browser");
    parser.addHelpOption();

    QCommandLineOption urlOption(QStringList() << "u" << "url", "Start URL", "url", "https://schinkenpizza.de");
    QCommandLineOption framelessOption("frameless", "Show borderless window");
    QCommandLineOption hideToolbarOption("hide-toolbar", "Hide control toolbar on start");
    QCommandLineOption layerOption("layer", "Desktop layer", "layer", "normal");
    QCommandLineOption profileDirOption("profile-dir", "Persistent profile dir", "dir", "");
    QCommandLineOption strictPrivacyOption("strict-privacy", "Enable strict privacy flags");
    QCommandLineOption socketOption("socket", "Socket path for IPC", "socket", "");

    parser.addOption(urlOption);
    parser.addOption(framelessOption);
    parser.addOption(hideToolbarOption);
    parser.addOption(layerOption);
    parser.addOption(profileDirOption);
    parser.addOption(strictPrivacyOption);
    parser.addOption(socketOption);
    parser.process(app);

    if (!parser.value(profileDirOption).isEmpty()) {
        dataPath = parser.value(profileDirOption);
    }
    QDir().mkpath(dataPath + "/storage");
    QDir().mkpath(dataPath + "/cache");

    auto *profile = new QWebEngineProfile(QStringLiteral("MuDesktopProfile"), &app);
    profile->setHttpUserAgent(muChromeUA());
    
    profile->setPersistentStoragePath(dataPath + "/storage");
    profile->setCachePath(dataPath + "/cache");
    profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    auto *interceptor = new MuRequestInterceptor(&app);
    profile->setUrlRequestInterceptor(interceptor);

    QObject::connect(profile, &QWebEngineProfile::downloadRequested, [](QWebEngineDownloadRequest *download) {
        QString path = QFileDialog::getSaveFileName(nullptr, "Save File", download->suggestedFileName());
        if (!path.isEmpty()) {
            download->setDownloadFileName(path);
            download->accept();
        } else {
            download->cancel();
        }
    });

    DesktopBrowserWindow window(
        parser.value(urlOption), 
        profile, 
        interceptor,
        parser.isSet(framelessOption),
        parser.isSet(hideToolbarOption), 
        parser.value(layerOption)
    );

    QIcon appIcon;
    QStringList iconPaths = {
        "/usr/share/icons/hicolor/scalable/apps/mu-browser.svg",
        "/usr/share/pixmaps/mu-browser.svg",
        "/usr/local/share/icons/hicolor/scalable/apps/mu-browser.svg",
        "/usr/local/share/pixmaps/mu-browser.svg",
        "mu-browser.svg"
    };
    for (const QString &path : iconPaths) {
        if (QFile::exists(path)) {
            appIcon = QIcon(path);
            if (!appIcon.isNull()) break;
        }
    }
    if (appIcon.isNull()) {
        appIcon = QIcon("mu-browser.svg");
    }
    app.setWindowIcon(appIcon);
    window.setWindowIcon(appIcon);

    if (!window.hasSavedGeometry()) {
        window.resize(1280, 720);
    }
    window.show();
    return app.exec();
}

#include "main_desktop.moc"
