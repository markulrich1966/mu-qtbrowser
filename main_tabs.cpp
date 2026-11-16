#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QTabBar>
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
#include <QLabel>
#include <QSpinBox>
#include <QSlider>
#include <QGroupBox>
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

#define MAX_TABS 10

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
    QString pattern;
    QString cssCode;
};

struct UserAgentRule {
    QString pattern;
    QString mode;
    QString customUA;
};

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
        result.append("MUPASS01");
        result.append(salt);
        result.append(checksum);
        result.append(ciphertext);
        return result.toBase64();
    }

    static bool decrypt(const QByteArray &base64Input, const QString &masterPassword, QByteArray &outPlaintext) {
        QByteArray data = QByteArray::fromBase64(base64Input);
        if (data.size() < 40 || !data.startsWith("MUPASS01")) return false;
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
        if (actualChecksum != expectedChecksum) return false;
        outPlaintext = plaintext;
        return true;
    }
};

class ClickableLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickableLabel(QWidget *parent = nullptr) : QLabel(parent) { setCursor(Qt::PointingHandCursor); }
signals:
    void clicked();
protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) emit clicked();
        QLabel::mousePressEvent(event);
    }
};

class ClickableComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit ClickableComboBox(QWidget *parent = nullptr) : QComboBox(parent) {}
    std::function<bool()> onBeforePopup;
    void showPopup() override {
        if (onBeforePopup && !onBeforePopup()) return;
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
            if (text.isEmpty()) text = clipboard->text(QClipboard::Clipboard);
            if (!text.isEmpty()) { insert(text); event->accept(); return; }
        }
        QLineEdit::mousePressEvent(event);
    }
};

static void installSameHostTargetFixOnPage(QWebEnginePage *page) {
    if (!page) return;
    QObject::connect(page, &QWebEnginePage::loadFinished, page, [page](bool) {
        page->runJavaScript(QStringLiteral(R"JS(
(function(){
  if (window.__muSameHostFixInstalled) return;
  window.__muSameHostFixInstalled = true;
  document.addEventListener('click', function(e){
    var a = (e.target && e.target.closest) ? e.target.closest('a') : null;
    if (!a || !a.target) return;
    try {
      var linkUrl = new URL(a.href, location.href);
      if (linkUrl.hostname === location.hostname) a.removeAttribute('target');
    } catch (_) {}
  }, true);
})();
)JS"));
    });
}

class MuRequestInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit MuRequestInterceptor(QObject *parent = nullptr) : QWebEngineUrlRequestInterceptor(parent) {}
    void setRules(const QList<UserAgentRule> &rules) { m_rules = rules; }
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
        if (host.endsWith("gstatic.com")) return;

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

        // Check if a per-domain user agent rule applies (GMX fix:
        // gmx.net/gmx.de brauchen Desktop-UA + passende Client Hints,
        // sonst lehnt GMX mit "technical error" ab).
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

class CustomWebPage : public QWebEnginePage {
    Q_OBJECT
public:
    explicit CustomWebPage(QWebEngineProfile *profile, QWebEngineView *view) : QWebEnginePage(profile, view), m_view(view) {
        settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
        settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
        settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
        settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
        settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        settings()->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
        settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
        settings()->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
        settings()->setAttribute(QWebEngineSettings::ErrorPageEnabled, true);
        connect(this, &QWebEnginePage::fullScreenRequested, this, [view](QWebEngineFullScreenRequest req) {
            if (req.toggleOn()) view->showFullScreen(); else view->showNormal();
            req.accept();
        });
    }
    void setNewPageFactory(std::function<QWebEnginePage*()> factory) { m_newPageFactory = std::move(factory); }
protected:
    QWebEnginePage *createWindow(WebWindowType) override {
        if (m_newPageFactory) return m_newPageFactory();
        return nullptr;
    }
private:
    QWebEngineView *m_view;
    std::function<QWebEnginePage*()> m_newPageFactory;
};

class TabbedBrowserWindow;

class CustomWebEngineView : public QWebEngineView {
    Q_OBJECT
public:
    explicit CustomWebEngineView(TabbedBrowserWindow *win, QWidget *parent = nullptr) : QWebEngineView(parent), m_win(win), m_useUnicodeIcons(true), m_isMobileMode(false) {}
    void setUseUnicodeIcons(bool enable) { m_useUnicodeIcons = enable; }
    bool isMobileMode() const { return m_isMobileMode; }
    // Sync the view's flag with the global mode (no toggle, no reload).
    void setMobileMode(bool mobile) { m_isMobileMode = mobile; }
    // Mobile/Desktop toggle: delegates to the BrowserWindow's GLOBAL
    // mobile mode. DECLARED here, DEFINED after BrowserWindow —
    // BrowserWindow is only forward-declared at this point, so calling
    // m_win->toggleMobileMode() here would be an
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
        menu.addAction(lbl("\u25C0", "Back"), this, &QWebEngineView::back)->setEnabled(history()->canGoBack());
        menu.addAction(lbl("\u25B6", "Forward"), this, &QWebEngineView::forward)->setEnabled(history()->canGoForward());
        menu.addAction(lbl("\u27F3", "Reload"), this, [this]() { page()->triggerAction(QWebEnginePage::Reload); });
        menu.addSeparator();
        if (hasSelection()) {
            QString selText = selectedText().trimmed();
            if (!selText.isEmpty()) {
                menu.addAction(lbl("\u2197", "Open selected text in new tab"), this, [this, selText]() {
                    QString targetUrl = selText;
                    if (!targetUrl.startsWith("http://") && !targetUrl.startsWith("https://") && !targetUrl.startsWith("file://")) {
                        if (targetUrl.contains(".") && !targetUrl.contains(" ")) targetUrl = "https://" + targetUrl;
                        else targetUrl = "https://duckduckgo.com/?q=" + QUrl::toPercentEncoding(selText);
                    }
                    emit openUrlInNewTabRequested(targetUrl);
                });
                menu.addSeparator();
            }
        }
        menu.addAction(lbl("\u29C9", "Copy"), this, [this]() { triggerPageAction(QWebEnginePage::Copy); })->setEnabled(hasSelection());
        menu.addAction(lbl("\U0001F4CB", "Paste"), this, [this]() { triggerPageAction(QWebEnginePage::Paste); });
        auto req = lastContextMenuRequest();
        if (req && !req->linkUrl().isEmpty()) {
            menu.addSeparator();
            menu.addAction(lbl("\u2197", "Open Link in New Tab"), this, [this, req]() { emit openUrlInNewTabRequested(req->linkUrl().toString()); });
        }
        if (req && req->mediaType() == QWebEngineContextMenuRequest::MediaTypeImage) {
            QUrl mediaUrl = req->mediaUrl();
            if (mediaUrl.isValid()) {
                menu.addSeparator();
                menu.addAction(lbl("\U0001F5BC", "Open Image in New Tab"), this, [this, mediaUrl]() { emit openUrlInNewTabRequested(mediaUrl.toString()); });
                auto openInExternalEditor = [mediaUrl](const QString &program) {
                    QString targetPath;
                    if (mediaUrl.isLocalFile()) targetPath = mediaUrl.toLocalFile();
                    else if (mediaUrl.scheme() == "http" || mediaUrl.scheme() == "https") {
                        QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + QFileInfo(mediaUrl.path()).fileName();
                        if (tempPath.endsWith("/")) tempPath += "downloaded_image.png";
                        QNetworkAccessManager manager; QNetworkRequest request(mediaUrl);
                        QNetworkReply *reply = manager.get(request); QEventLoop loop;
                        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit); loop.exec();
                        if (reply->error() == QNetworkReply::NoError) {
                            QFile file(tempPath);
                            if (file.open(QIODevice::WriteOnly)) { file.write(reply->readAll()); file.close(); targetPath = tempPath; }
                        }
                        reply->deleteLater();
                    }
                    if (!targetPath.isEmpty() && QFile::exists(targetPath)) QProcess::startDetached(program, QStringList() << targetPath);
                    else QMessageBox::warning(nullptr, "Error", QString("Could not open image in %1.").arg(program));
                };
                menu.addAction(lbl("\U0001F3A8", "Open in GIMP"), this, [openInExternalEditor]() { openInExternalEditor("gimp"); });
                menu.addAction(lbl("\U0001F58C", "Open in mtPaint"), this, [openInExternalEditor]() { openInExternalEditor("mtpaint"); });
            }
        }
        menu.addSeparator();
        menu.addAction(lbl("\U0001F4F1", m_isMobileMode ? "Switch to Desktop Mode" : "Switch to Mobile Mode"), this, [this]() { toggleMobileMode(); emit mobileModeToggled(m_isMobileMode); });
        menu.addAction(lbl("\u2605", "Add to Bookmarks"), this, &CustomWebEngineView::addCurrentBookmarkRequested);
        menu.addAction(lbl("\u2302", "Set as Homepage"), this, &CustomWebEngineView::setAsHomepageRequested);
        menu.addAction(lbl("\U0001F6E0", "Browser Settings..."), this, &CustomWebEngineView::openSettingsRequested);
        menu.addSeparator();
        menu.addAction(lbl("\u26F6", "Toggle Fullscreen"), this, [this]() {
            if (auto *win = this->window()) { if (win->isFullScreen()) win->showNormal(); else win->showFullScreen(); }
        });
        menu.addAction(lbl("\u25AD", "Toggle Toolbar"), this, &CustomWebEngineView::toggleToolbarRequested);
        menu.addAction(lbl("\u21E5", "Toggle Tabbar"), this, &CustomWebEngineView::toggleTabbarRequested);
        menu.addAction(lbl("\U0001F319", "Toggle Dark Mode"), this, &CustomWebEngineView::toggleDarkModeRequested);
        menu.exec(event->globalPos());
    }
private:
    TabbedBrowserWindow *m_win;
    bool m_useUnicodeIcons;
    bool m_isMobileMode;
};

class BookmarkEditorDialog : public QDialog {
    Q_OBJECT
public:
    BookmarkEditorDialog(const QList<BookmarkItem> &bookmarks, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Edit Bookmarks");
        resize(500, 350);
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("Format per line: Title | URL", this));
        textEdit = new QTextEdit(this);
        QString content;
        for (const auto &b : bookmarks) content += b.title + " | " + b.url + "\n";
        textEdit->setText(content);
        layout->addWidget(textEdit);
        auto *btnLayout = new QHBoxLayout();
        auto *btnSave = new QPushButton("Save", this);
        auto *btnCancel = new QPushButton("Cancel", this);
        btnLayout->addStretch(); btnLayout->addWidget(btnSave); btnLayout->addWidget(btnCancel);
        layout->addLayout(btnLayout);
        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }
    QList<BookmarkItem> getBookmarks() const {
        QList<BookmarkItem> list;
        for (const QString &line : textEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
            int idx = line.indexOf(" | ");
            if (idx != -1) list.append(BookmarkItem{line.left(idx).trimmed(), line.mid(idx + 3).trimmed()});
        }
        return list;
    }
private:
    QTextEdit *textEdit;
};

class PasswordEditorDialog : public QDialog {
    Q_OBJECT
public:
    PasswordEditorDialog(const QList<PasswordEntry> &entries, std::function<void(const QString&)> onChangeMasterPass, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Encrypted Password Manager Editor");
        resize(550, 420);
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("Format per line: Title | Username | Password", this));
        textEdit = new QTextEdit(this);
        QString content;
        for (const auto &e : entries) content += e.titleUrl + " | " + e.username + " | " + e.password + "\n";
        textEdit->setText(content);
        layout->addWidget(textEdit);
        auto *btnLayout = new QHBoxLayout();
        auto *btnChangeMaster = new QPushButton("Change Master Password...", this);
        auto *btnSave = new QPushButton("Save", this);
        auto *btnCancel = new QPushButton("Cancel", this);
        btnLayout->addWidget(btnChangeMaster); btnLayout->addStretch(); btnLayout->addWidget(btnSave); btnLayout->addWidget(btnCancel);
        layout->addLayout(btnLayout);
        connect(btnChangeMaster, &QPushButton::clicked, this, [this, onChangeMasterPass]() {
            bool ok = false;
            QString p = QInputDialog::getText(this, "Master Password", "New Master Password:", QLineEdit::Password, "", &ok);
            if (ok && !p.isEmpty()) { onChangeMasterPass(p); QMessageBox::information(this, "Success", "Master password updated!"); }
        });
        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }
    QList<PasswordEntry> getEntries() const {
        QList<PasswordEntry> list;
        for (const QString &line : textEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
            QStringList parts = line.split(" | ");
            if (parts.size() >= 3) list.append(PasswordEntry{parts[0].trimmed(), parts[1].trimmed(), parts.mid(2).join(" | ").trimmed()});
        }
        return list;
    }
private:
    QTextEdit *textEdit;
};

class CssEditorDialog : public QDialog {
    Q_OBJECT
public:
    CssEditorDialog(const QList<StyleRule> &rules, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("User CSS Styles Editor");
        resize(600, 450);
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("Format per line: Domain | CSS Code", this));
        textEdit = new QTextEdit(this);
        QString content;
        for (const auto &r : rules) content += r.pattern + " | " + r.cssCode + "\n";
        textEdit->setText(content);
        layout->addWidget(textEdit);
        auto *btnLayout = new QHBoxLayout();
        auto *btnSave = new QPushButton("Save", this);
        auto *btnCancel = new QPushButton("Cancel", this);
        btnLayout->addStretch(); btnLayout->addWidget(btnSave); btnLayout->addWidget(btnCancel);
        layout->addLayout(btnLayout);
        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }
    QList<StyleRule> getRules() const {
        QList<StyleRule> list;
        for (const QString &line : textEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
            int idx = line.indexOf(" | ");
            if (idx != -1) list.append(StyleRule{line.left(idx).trimmed(), line.mid(idx + 3).trimmed()});
        }
        return list;
    }
private:
    QTextEdit *textEdit;
};

class UserAgentEditorDialog : public QDialog {
    Q_OBJECT
public:
    UserAgentEditorDialog(const QList<UserAgentRule> &rules, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("User-Agent Rules");
        resize(650, 450);
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("Format per line: Domain | Mode (Desktop/Mobile/Custom) | [UA]", this));
        textEdit = new QTextEdit(this);
        QString content;
        for (const auto &r : rules) content += r.pattern + " | " + r.mode + (r.customUA.isEmpty() ? "" : (" | " + r.customUA)) + "\n";
        textEdit->setText(content);
        layout->addWidget(textEdit);
        auto *btnLayout = new QHBoxLayout();
        auto *btnSave = new QPushButton("Save", this);
        auto *btnCancel = new QPushButton("Cancel", this);
        btnLayout->addStretch(); btnLayout->addWidget(btnSave); btnLayout->addWidget(btnCancel);
        layout->addLayout(btnLayout);
        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }
    QList<UserAgentRule> getRules() const {
        QList<UserAgentRule> list;
        for (const QString &line : textEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
            QStringList parts = line.split(" | ");
            if (parts.size() >= 2) list.append(UserAgentRule{parts[0].trimmed(), parts[1].trimmed(), parts.size() >= 3 ? parts.mid(2).join(" | ").trimmed() : ""});
        }
        return list;
    }
private:
    QTextEdit *textEdit;
};

class NotesDialog : public QDialog {
    Q_OBJECT
public:
    explicit NotesDialog(const QString &filePath, QWidget *parent = nullptr) : QDialog(parent), m_filePath(filePath) {
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
        auto *btnSave = new QPushButton("Save", this);
        auto *btnClose = new QPushButton("Close", this);
        btnLayout->addStretch(); btnLayout->addWidget(btnSave); btnLayout->addWidget(btnClose);
        layout->addLayout(btnLayout);
        connect(btnSave, &QPushButton::clicked, this, &NotesDialog::save);
        connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    }
private:
    void save() {
        QFile file(m_filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(textEdit->toPlainText().toUtf8());
            file.close();
            QMessageBox::information(this, "Saved", "Notes saved successfully!");
        }
    }
    QString m_filePath;
    QTextEdit *textEdit;
};

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
        setWindowTitle("mu-browser-tabs Setup & Settings");
        resize(560, 620);
        auto *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(8, 8, 8, 8);
        tabWidget = new QTabWidget(this);

        // General
        auto *tabGeneral = new QWidget(this);
        auto *layGeneral = new QVBoxLayout(tabGeneral);
        layGeneral->addWidget(new QLabel("<b>Homepage URL:</b>", this));
        homepageEdit = new QLineEdit(homepage, this);
        layGeneral->addWidget(homepageEdit);
        layGeneral->addWidget(new QLabel("<b>Clock Command:</b>", this));
        clockCmdEdit = new QLineEdit(clockCmd, this);
        layGeneral->addWidget(clockCmdEdit);
        chkClock24h = new QCheckBox("Use 24-Hour Clock Format", this); chkClock24h->setChecked(clock24h);
        layGeneral->addWidget(chkClock24h);
        layGeneral->addWidget(new QLabel("<b>Notes Command:</b>", this));
        notesCmdEdit = new QLineEdit(notesCmd, this);
        layGeneral->addWidget(notesCmdEdit);
        layGeneral->addWidget(new QLabel("<b>Default Zoom (%):</b>", this));
        zoomSpin = new QSpinBox(this); zoomSpin->setRange(40, 250); zoomSpin->setValue(defaultZoom);
        layGeneral->addWidget(zoomSpin);
        layGeneral->addWidget(new QLabel("<b>UI Scaling (%):</b>", this));
        uiZoomSpin = new QSpinBox(this); uiZoomSpin->setRange(50, 250); uiZoomSpin->setValue(uiZoom);
        layGeneral->addWidget(uiZoomSpin);
        // Spacer hinter der URL-Bar: Mindestbreite (transparentes Loch).
        layGeneral->addWidget(new QLabel("URL Bar Spacer Minimum Width (transparent gap)", this));
        urlBarSpacerMinWidthSpin = new QSpinBox(this);
        urlBarSpacerMinWidthSpin->setRange(0, 800);
        urlBarSpacerMinWidthSpin->setValue(urlBarSpacerMinWidth);
        layGeneral->addWidget(urlBarSpacerMinWidthSpin);
        darkModeCheck = new QCheckBox("Enable Dark Mode", this);
        darkModeCheck->setChecked(darkMode);
        layGeneral->addWidget(darkModeCheck);
        unicodeIconsCheck = new QCheckBox("Unicode Symbols", this);
        unicodeIconsCheck->setChecked(unicodeIcons);
        layGeneral->addWidget(unicodeIconsCheck);
        chkUseButtonIcons = new QCheckBox("Drawn Icons Instead of Letters for Buttons (u/p/e/m/n ...)", this);
        chkUseButtonIcons->setChecked(useButtonIcons);
        layGeneral->addWidget(chkUseButtonIcons);
        layGeneral->addStretch(1);
        tabWidget->addTab(tabGeneral, "🌐 General");

        // Layout
        auto *tabLayout = new QWidget(this);
        auto *layLayout = new QVBoxLayout(tabLayout);
        layLayout->addWidget(new QLabel("<b>Main Toolbar Position:</b>", this));
        comboToolbarPos = new QComboBox(this);
        comboToolbarPos->addItem("Top", 0); comboToolbarPos->addItem("Middle", 1); comboToolbarPos->addItem("Bottom", 2);
        comboToolbarPos->setCurrentIndex(qBound(0, toolbarPosition, 2));
        layLayout->addWidget(comboToolbarPos);
        layLayout->addWidget(new QLabel("<b>Tab Bar Position:</b>", this));
        comboTabBarPos = new QComboBox(this);
        comboTabBarPos->addItem("Top (Above Web Content)", 0); comboTabBarPos->addItem("Bottom (Below Web Content)", 1);
        comboTabBarPos->setCurrentIndex(qBound(0, tabBarPosition, 1));
        layLayout->addWidget(comboTabBarPos);
        // Sidebar-Position (links/rechts).
        layLayout->addWidget(new QLabel("Sidebar Position", this));
        comboSideBarPos = new QComboBox(this);
        comboSideBarPos->addItem("Left", 0);
        comboSideBarPos->addItem("Right", 1);
        comboSideBarPos->setCurrentIndex(qBound(0, sideBarPosition, 1));
        layLayout->addWidget(comboSideBarPos);
        chkTabNewTab = new QCheckBox("Show New Tab Button Beside Tabs (+)", this); chkTabNewTab->setChecked(showTabNewTab);
        layLayout->addWidget(chkTabNewTab);
        chkShowSideToolbar = new QCheckBox("Add Vertical Side Toolbar", this); chkShowSideToolbar->setChecked(showSideToolbar);
        layLayout->addWidget(chkShowSideToolbar);
        chkFrameless = new QCheckBox("Frameless Window", this); chkFrameless->setChecked(frameless);
        layLayout->addWidget(chkFrameless);
        layLayout->addStretch(1);
        tabWidget->addTab(tabLayout, "📐 Layout & Positioning");

        // Toolbar Elements
        auto *tabToolbarBtns = new QWidget(this);
        auto *scrollToolbar = new QScrollArea(tabToolbarBtns);
        scrollToolbar->setWidgetResizable(true);
        auto *contentToolbar = new QWidget(scrollToolbar);
        auto *layToolbarBtns = new QVBoxLayout(contentToolbar);
        chkNav = new QCheckBox("Show Back/Forward", this); chkNav->setChecked(showNav);
        chkReload = new QCheckBox("Show Reload", this); chkReload->setChecked(showReload);
        chkHome = new QCheckBox("Show Home", this); chkHome->setChecked(showHome);
        chkUrl = new QCheckBox("Show URL Bar", this); chkUrl->setChecked(showUrl);
        chkGo = new QCheckBox("Show Go", this); chkGo->setChecked(showGo);
        chkBookmarks = new QCheckBox("Show Bookmarks", this); chkBookmarks->setChecked(showBookmarks);
        chkAddBookmark = new QCheckBox("Show Add Bookmark", this); chkAddBookmark->setChecked(showAddBookmark);
        chkTabJump = new QCheckBox("Show Tab Navigator", this); chkTabJump->setChecked(showTabJump);
        chkNewTab = new QCheckBox("Show New Tab Button (Max 10)", this); chkNewTab->setChecked(showNewTab);
        chkZoom = new QCheckBox("Show Zoom", this); chkZoom->setChecked(showZoom);
        chkPassList = new QCheckBox("Show Passwords List", this); chkPassList->setChecked(showPassList);
        chkPassButtons = new QCheckBox("Show Password Buttons (u, p, e)", this); chkPassButtons->setChecked(showPassButtons);
        chkMobileToggle = new QCheckBox("Show Mobile Toggle", this); chkMobileToggle->setChecked(showMobileToggle);
        chkClock = new QCheckBox("Show Clock", this); chkClock->setChecked(showClock);
        chkNotes = new QCheckBox("Show Notes", this); chkNotes->setChecked(showNotes);

        layToolbarBtns->addWidget(chkNav); layToolbarBtns->addWidget(chkReload); layToolbarBtns->addWidget(chkHome);
        layToolbarBtns->addWidget(chkUrl); layToolbarBtns->addWidget(chkGo); layToolbarBtns->addWidget(chkBookmarks);
        layToolbarBtns->addWidget(chkAddBookmark); layToolbarBtns->addWidget(chkTabJump); layToolbarBtns->addWidget(chkNewTab);
        layToolbarBtns->addWidget(chkZoom); layToolbarBtns->addWidget(chkPassList); layToolbarBtns->addWidget(chkPassButtons);
        layToolbarBtns->addWidget(chkMobileToggle); layToolbarBtns->addWidget(chkClock); layToolbarBtns->addWidget(chkNotes);
        layToolbarBtns->addStretch(1);
        scrollToolbar->setWidget(contentToolbar);
        auto *layScrollWrap1 = new QVBoxLayout(tabToolbarBtns);
        layScrollWrap1->setContentsMargins(0,0,0,0); layScrollWrap1->addWidget(scrollToolbar);
        tabWidget->addTab(tabToolbarBtns, "👁️ Toolbar Elements");

        // Side Toolbar Buttons
        auto *tabSideBtns = new QWidget(this);
        auto *laySideBtns = new QVBoxLayout(tabSideBtns);
        chkSideWinMenu = new QCheckBox("Show Window Menu (⚙)", this); chkSideWinMenu->setChecked(showSideWinMenu);
        chkSideSettings = new QCheckBox("Show Settings (🛠)", this); chkSideSettings->setChecked(showSideSettings);
        chkSideReload = new QCheckBox("Show Reload", this); chkSideReload->setChecked(showSideReload);
        chkSideNav = new QCheckBox("Show Back/Forward", this); chkSideNav->setChecked(showSideNav);
        chkSideHome = new QCheckBox("Show Home", this); chkSideHome->setChecked(showSideHome);
        chkSidePassManager = new QCheckBox("Show Password Manager Buttons", this); chkSidePassManager->setChecked(showSidePassManager);
        chkSideMobile = new QCheckBox("Show Mobile Switcher", this); chkSideMobile->setChecked(showSideMobile);
        chkSideNotes = new QCheckBox("Show Notes", this); chkSideNotes->setChecked(showSideNotes);
        chkSideNewTab = new QCheckBox("Show New Tab (t)", this); chkSideNewTab->setChecked(showSideNewTab);

        laySideBtns->addWidget(chkSideWinMenu); laySideBtns->addWidget(chkSideSettings); laySideBtns->addWidget(chkSideReload);
        laySideBtns->addWidget(chkSideNav); laySideBtns->addWidget(chkSideHome); laySideBtns->addWidget(chkSidePassManager);
        laySideBtns->addWidget(chkSideMobile); laySideBtns->addWidget(chkSideNotes); laySideBtns->addWidget(chkSideNewTab);
        laySideBtns->addStretch(1);
        tabWidget->addTab(tabSideBtns, "🛡️ Side Toolbar");

        // Privacy
        auto *tabPrivacy = new QWidget(this);
        auto *layPrivacy = new QVBoxLayout(tabPrivacy);
        strictPrivacyCheck = new QCheckBox("Enable Strict Privacy Flags", this); strictPrivacyCheck->setChecked(strictPrivacy);
        adBlockerCheck = new QCheckBox("Ad Blocker: Block ads & trackers (generic, all pages)", this); adBlockerCheck->setChecked(adBlocker);
        layPrivacy->addWidget(strictPrivacyCheck);
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
        layPrivacy->addStretch(1);
        tabWidget->addTab(tabPrivacy, "🔒 Privacy");
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
        auto *btnSave = new QPushButton("Save Settings", this);
        auto *btnCancel = new QPushButton("Cancel", this);
        btnLayout->addStretch(); btnLayout->addWidget(btnSave); btnLayout->addWidget(btnCancel);
        mainLayout->addLayout(btnLayout);
        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QString getHomepage() const { return homepageEdit->text().trimmed(); }
    QString getClockCommand() const { return clockCmdEdit->text().trimmed(); }
    QString getNotesCommand() const { return notesCmdEdit->text().trimmed(); }
    int getDefaultZoom() const { return zoomSpin->value(); }
    int getUiZoom() const { return uiZoomSpin->value(); }
    int getTabZoom() const { return 100; }
    int getUrlMinWidth() const { return 300; }
    int getUrlBarSpacerMinWidth() const { return urlBarSpacerMinWidthSpin->value(); }
    int getReloadMinWidth() const { return 30; }
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
    QLineEdit *homepageEdit, *clockCmdEdit, *notesCmdEdit;
    QSpinBox *zoomSpin, *uiZoomSpin;
    QSpinBox *urlBarSpacerMinWidthSpin;
    QCheckBox *strictPrivacyCheck, *darkModeCheck, *unicodeIconsCheck, *chkFrameless, *chkClock24h;
    QCheckBox *adBlockerCheck;
    QPlainTextEdit *adBlockExceptionsEdit = nullptr;
    QCheckBox *chkUseButtonIcons;
    QSlider *transTabBarBgSlider = nullptr, *transTabBarControlsSlider = nullptr;
    QSlider *transToolbarBgSlider = nullptr, *transToolbarControlsSlider = nullptr;
    QSlider *transSideBgSlider = nullptr, *transSideControlsSlider = nullptr;
    QSlider *transWebpageSlider = nullptr;
    QSpinBox *transTabBarBgSpin = nullptr, *transTabBarControlsSpin = nullptr;
    QSpinBox *transToolbarBgSpin = nullptr, *transToolbarControlsSpin = nullptr;
    QSpinBox *transSideBgSpin = nullptr, *transSideControlsSpin = nullptr;
    QSpinBox *transWebpageSpin = nullptr;
    QComboBox *comboToolbarPos, *comboTabBarPos;
    QComboBox *comboSideBarPos;
    QCheckBox *chkTabNewTab, *chkShowSideToolbar;
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

// --- Tabbed Browser Main Window ---

class TabbedBrowserWindow : public QWidget {
    Q_OBJECT
public:
    TabbedBrowserWindow(const QString &initialUrl, QWebEngineProfile *profile, MuRequestInterceptor *interceptor,
                        bool frameless, bool hideToolbar, const QString &layer, QWidget *parent = nullptr)
        : QWidget(parent), m_profile(profile), m_interceptor(interceptor), m_isFrameless(frameless), m_hideToolbar(hideToolbar), m_layer(layer),
          m_darkMode(false), m_strictPrivacy(false), m_unicodeIcons(true), m_urlBarMinWidth(300), m_reloadButtonMinWidth(30),
          m_defaultZoom(100), m_uiZoom(100), m_tabZoom(100),
          m_showNav(true), m_showReload(true), m_showHome(true), m_showUrl(true), m_showGo(true),
          m_showBookmarks(true), m_showAddBookmark(true), m_showTabJump(true), m_showNewTab(true), m_showZoom(true),
          m_showPassList(true), m_showPassButtons(true), m_showMobileToggle(true), m_showClock(true), m_showNotes(true),
          m_toolbarPosition(0), m_tabBarPosition(0), m_showSideToolbar(false),
          m_showTabNewTab(true), m_showSideNewTab(true), m_showSideSettings(true), m_clock24h(true),
          m_showSideWinMenu(true), m_showSideReload(true), m_showSideNav(true), m_showSideHome(true),
          m_showSidePassManager(true), m_showSideMobile(true), m_showSideNotes(true),
          m_clockCommand("mu-calendar"), m_notesCommand("mu-notepad ~/notes.txt"),
          m_updatingTabsCombo(false), m_updatingZoomCombo(false), m_passUnlocked(false)
    {
        loadConfig();
        // Werbeblocker-Einstellung an den Interceptor weitergeben.
        if (m_interceptor) m_interceptor->setAdBlockerEnabled(m_adBlocker);
        // Objektnamen: #mainWindow-Regel in applyTransparency greift nur, wenn das Fenster wirklich so heisst (Geminis Technik).
        setObjectName("mainWindow");
        // Fenster-Geometrie wiederherstellen (Position + Groesse + maximiert).
        if (!m_windowGeometry.isEmpty()) restoreGeometry(m_windowGeometry);
        setAttribute(Qt::WA_TranslucentBackground, anyTransparency());
        if (m_isFrameless) setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        loadBookmarks(); loadStyleRules(); loadUserAgentRules(); loadAdBlockExceptions(); if (m_interceptor) m_interceptor->setMobileMode(m_mobileMode);

        outerLayout = new QHBoxLayout(this);
        outerLayout->setContentsMargins(0,0,0,0); outerLayout->setSpacing(0);

        sideToolbarWidget = new SideToolbarWidget(this);
        sideToolbarWidget->setObjectName("sideToolbarWidget");
        sideLayout = new QVBoxLayout(sideToolbarWidget);
        sideLayout->setContentsMargins(3,3,3,3); sideLayout->setSpacing(0);

        btnWinMenu = new QPushButton("⚙", this); btnWinMenu->setToolTip("Window Menu");
        connect(btnWinMenu, &QPushButton::clicked, this, &TabbedBrowserWindow::showWindowMenu);
        btnSideSettings = new QPushButton("🛠", this); btnSideSettings->setToolTip("Browser Settings");
        connect(btnSideSettings, &QPushButton::clicked, this, &TabbedBrowserWindow::openSettingsDialog);
        btnSideReload = new QPushButton("⟳", this); btnSideReload->setToolTip("Reload Page");
        connect(btnSideReload, &QPushButton::clicked, this, [this]() { if (auto *v = currentWebView()) v->reload(); });
        btnSideBack = new QPushButton("◀", this); btnSideBack->setToolTip("Go Back");
        btnSideForward = new QPushButton("▶", this); btnSideForward->setToolTip("Go Forward");
        connect(btnSideBack, &QPushButton::clicked, this, [this]() { if (auto *v = currentWebView()) v->back(); });
        connect(btnSideForward, &QPushButton::clicked, this, [this]() { if (auto *v = currentWebView()) v->forward(); });
        btnSideHome = new QPushButton("⌂", this); btnSideHome->setToolTip("Go to Homepage");
        connect(btnSideHome, &QPushButton::clicked, this, [this]() { if (auto *v = currentWebView()) v->load(QUrl(m_homepageUrl)); });
        btnSideUser = new QPushButton("u", this); btnSideUser->setToolTip("Copy Username");
        btnSidePass = new QPushButton("p", this); btnSidePass->setToolTip("Copy Password");
        btnSideEditPass = new QPushButton("e", this); btnSideEditPass->setToolTip("Edit Passwords");
        // Erster Passwort-Button: Domainliste als Popup.
        btnSidePassList = new QPushButton("D", this);
        connect(btnSidePassList, &QPushButton::clicked, this, &TabbedBrowserWindow::showPasswordDomainPopup);
        // Bookmark-Button: Bookmarkliste als Popup (direkt unter Home).
        btnSideBookmarks = new QPushButton("★", this);
        btnSideBookmarks->setToolTip("Bookmarks Menu");
        connect(btnSideBookmarks, &QPushButton::clicked, this, &TabbedBrowserWindow::showBookmarksPopup);
        connect(btnSideUser, &QPushButton::clicked, this, &TabbedBrowserWindow::copyPasswordUsername);
        connect(btnSidePass, &QPushButton::clicked, this, &TabbedBrowserWindow::copyPasswordValue);
        connect(btnSideEditPass, &QPushButton::clicked, this, &TabbedBrowserWindow::editPasswordsDialog);
        btnSideMobile = new QPushButton("m", this); btnSideMobile->setToolTip("Toggle Mobile / Desktop");
        connect(btnSideMobile, &QPushButton::clicked, this, [this]() { if (auto *v = currentWebView()) { v->toggleMobileMode(); updateMobileToggleButton(v->isMobileMode()); } });
        btnSideNotes = new QPushButton("n", this); btnSideNotes->setToolTip("Open Quick Notes");
        connect(btnSideNotes, &QPushButton::clicked, this, &TabbedBrowserWindow::onNotesClicked);
        btnSideNewTab = new QPushButton("t", this); btnSideNewTab->setToolTip("Open New Tab");
        connect(btnSideNewTab, &QPushButton::clicked, this, [this]() { addNewTab(m_homepageUrl); });

        rebuildSideToolbarLayout();
        outerLayout->addWidget(sideToolbarWidget, 0);
        sideToolbarWidget->setVisible(m_showSideToolbar);

        centerContentWidget = new QWidget(this);
        mainLayout = new QVBoxLayout(centerContentWidget);
        mainLayout->setContentsMargins(0,0,0,0); mainLayout->setSpacing(0);
        outerLayout->addWidget(centerContentWidget, 1);
        applySideBarPosition(); // Sidebar links/rechts aus Config.

        toolbarContainer = new QWidget(this);
        toolbarContainer->setObjectName("toolbarContainer");
        toolsLayout = new QHBoxLayout(toolbarContainer);
        toolsLayout->setContentsMargins(2,2,2,2); toolsLayout->setSpacing(3);

        btnBack = new QPushButton("◀", this); btnBack->setToolTip("Go Back");
        btnForward = new QPushButton("▶", this); btnForward->setToolTip("Go Forward");
        btnReload = new QPushButton("⟳", this); btnReload->setToolTip("Reload Page");
        btnReload->setMinimumWidth(m_reloadButtonMinWidth);
        btnHome = new QPushButton("⌂", this); btnHome->setToolTip("Go to Homepage");
        urlEdit = new MiddleClickLineEdit(this); urlEdit->setToolTip("URL Address Bar");
        // Spacer hinter der URL-Bar: transparentes Loch in der Toolbar,
        // durch das der (transparente) Hintergrund sichtbar wird.
        // Mindestbreite ist in den Einstellungen setzbar.
        urlBarSpacer = new DraggableWidget(this); // Ziehbar wie die Sidebar (inkl. IceWM-Doppelklick-Fix).
        urlBarSpacer->setObjectName("urlBarSpacer");
        urlBarSpacer->setStyleSheet("#urlBarSpacer { background-color: transparent; border: 0px; }");
        urlBarSpacer->setMinimumWidth(m_urlBarSpacerMinWidth);
        urlEdit->setMinimumWidth(100); urlEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        btnGo = new QPushButton("Go", this); btnGo->setToolTip("Go to URL");
        bookmarkCombo = new QComboBox(this); bookmarkCombo->setToolTip("Bookmarks Menu"); bookmarkCombo->setMaximumWidth(140);
        btnAddBookmark = new QPushButton("+", this); btnAddBookmark->setToolTip("Add current page as bookmark");
        tabJumpCombo = new QComboBox(this); tabJumpCombo->setToolTip("Jump to tab"); tabJumpCombo->setMaximumWidth(120);
        zoomCombo = new QComboBox(this); zoomCombo->setToolTip("Zoom Level"); zoomCombo->setMaximumWidth(75);
        for (int z = 40; z <= 250; z += 10) zoomCombo->addItem(QString("%1%").arg(z), z);
        updateZoomComboSelection(m_defaultZoom);

        btnNewTab = new QPushButton("+ Tab", this); btnNewTab->setToolTip("Open new tab (Max 10)");
        passCombo = new ClickableComboBox(this); passCombo->setToolTip("Saved Logins"); passCombo->setMaximumWidth(130);
        passCombo->onBeforePopup = [this]() { return unlockPasswords(); };
        btnCopyUser = new QPushButton("u", this); btnCopyUser->setToolTip("Copy Username");
        btnCopyPass = new QPushButton("p", this); btnCopyPass->setToolTip("Copy Password");
        btnEditPass = new QPushButton("e", this); btnEditPass->setToolTip("Edit Passwords");
        btnMobileToggle = new QPushButton("m", this); btnMobileToggle->setToolTip("Toggle Mobile / Desktop");
        clockLabel = new ClickableLabel(this); clockLabel->setToolTip("Current Time");
        clockTimer = new QTimer(this); connect(clockTimer, &QTimer::timeout, this, &TabbedBrowserWindow::updateClock);
        clockTimer->start(1000); updateClock();
        connect(clockLabel, &ClickableLabel::clicked, this, &TabbedBrowserWindow::onClockClicked);
        btnNotes = new QPushButton("n", this); btnNotes->setToolTip("Quick Notes");
        connect(btnNotes, &QPushButton::clicked, this, &TabbedBrowserWindow::onNotesClicked);

        tabWidget = new QTabWidget(this);
        tabWidget->setTabsClosable(true);
        tabWidget->setMovable(true);
        tabWidget->setTabPosition(m_tabBarPosition == 1 ? QTabWidget::South : QTabWidget::North);
        connect(tabWidget->tabBar(), &QTabBar::tabMoved, this, [this](int from, int to) {
            QWidget *w = tabWidget->widget(from);
            QString t = tabWidget->tabText(from);
            QIcon icon = tabWidget->tabIcon(from);
            tabWidget->removeTab(from);
            tabWidget->insertTab(to, w, icon, t);
            tabWidget->setCurrentIndex(to);
            updateTabsCombo();
        });

        btnTabNewTab = new QPushButton("+", this);
        btnTabNewTab->setToolTip("Open New Tab");
        connect(btnTabNewTab, &QPushButton::clicked, this, [this]() { addNewTab(m_homepageUrl); });
        tabWidget->setCornerWidget(btnTabNewTab, m_tabBarPosition == 1 ? Qt::BottomRightCorner : Qt::TopRightCorner);
        btnTabNewTab->setVisible(m_showTabNewTab);

        updatePasswordCombo();
        rebuildToolbarLayout();

        connect(tabWidget, &QTabWidget::tabCloseRequested, this, &TabbedBrowserWindow::closeTab);
        connect(tabWidget, &QTabWidget::currentChanged, this, &TabbedBrowserWindow::onTabChanged);

        updateBookmarkCombo();
        applyUiScaling(m_uiZoom);
        applyTabScaling(m_tabZoom);
        applyTheme(m_darkMode);

        connect(btnBack, &QPushButton::clicked, this, [this]() { if (auto *v = currentWebView()) v->back(); });
        connect(btnForward, &QPushButton::clicked, this, [this]() { if (auto *v = currentWebView()) v->forward(); });
        connect(btnReload, &QPushButton::clicked, this, [this]() { if (auto *v = currentWebView()) v->page()->triggerAction(QWebEnginePage::Reload); });
        connect(btnHome, &QPushButton::clicked, this, [this]() { if (auto *v = currentWebView()) v->load(QUrl(m_homepageUrl)); else addNewTab(m_homepageUrl); });

        auto loadUrl = [this]() {
            QString in = urlEdit->text().trimmed();
            if (in.isEmpty()) return;
            if (!in.startsWith("http://") && !in.startsWith("https://") && !in.startsWith("file://")) in = "https://" + in;
            if (auto *v = currentWebView()) v->load(QUrl(in));
        };
        connect(btnGo, &QPushButton::clicked, loadUrl);
        connect(urlEdit, &QLineEdit::returnPressed, loadUrl);

        connect(bookmarkCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (index == 0) { bookmarkCombo->setCurrentIndex(0); return; }
            if (index == 1) { addCurrentBookmark(); bookmarkCombo->setCurrentIndex(0); return; }
            if (index == 2) { editBookmarksDialog(); bookmarkCombo->setCurrentIndex(0); return; }
            QString u = bookmarkCombo->itemData(index).toString();
            if (!u.isEmpty() && currentWebView()) currentWebView()->load(QUrl(u));
            bookmarkCombo->setCurrentIndex(0);
        });

        connect(tabJumpCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (m_updatingTabsCombo || index < 0 || index >= tabWidget->count()) return;
            tabWidget->setCurrentIndex(index);
        });

        connect(zoomCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (m_updatingZoomCombo || index < 0) return;
            int z = zoomCombo->itemData(index).toInt();
            if (z > 0 && currentWebView()) currentWebView()->setZoomFactor(static_cast<qreal>(z) / 100.0);
        });

        connect(btnAddBookmark, &QPushButton::clicked, this, &TabbedBrowserWindow::addCurrentBookmark);
        connect(btnNewTab, &QPushButton::clicked, this, [this]() { addNewTab(m_homepageUrl); });

        // Password Manager Actions (Toolbar) - fehlten: u/p/e waren tot, Auswahl tat nichts
        connect(passCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (index > 0 && !m_passUnlocked) {
                if (!unlockPasswords()) passCombo->setCurrentIndex(0);
            }
        });
        connect(btnCopyUser, &QPushButton::clicked, this, &TabbedBrowserWindow::copyPasswordUsername);
        connect(btnCopyPass, &QPushButton::clicked, this, &TabbedBrowserWindow::copyPasswordValue);
        connect(btnEditPass, &QPushButton::clicked, this, &TabbedBrowserWindow::editPasswordsDialog);

        setupKeyboardShortcuts();

        if (!restoreSession()) {
            QString startUrl = initialUrl;
            if (startUrl == "https://schinkenpizza.de" && !m_homepageUrl.isEmpty()) startUrl = m_homepageUrl;
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
        AboutDialog dlg("mu-browser-tabs", version, this);
        connect(&dlg, &AboutDialog::openUrlRequested, this, [this](const QString &url) {
            addNewTab(url);
        });
        dlg.exec();
    }

    void showWindowMenu() {
        QMenu menu(this);
        menu.addAction("🗕 Minimize", this, &QWidget::showMinimized);
        menu.addAction(isMaximized() ? "🗗 Restore" : "🗖 Maximize", this, [this]() { if (isMaximized()) showNormal(); else showMaximized(); });
        menu.addSeparator();
        menu.addAction("ℹ About...", this, &TabbedBrowserWindow::showAboutDialog);
        menu.addSeparator();
        menu.addAction("🗙 Close", this, &QWidget::close);
        menu.exec(btnWinMenu->mapToGlobal(QPoint(0, btnWinMenu->height())));
    }

    void setupKeyboardShortcuts() {
        auto *scNewTab = new QShortcut(QKeySequence("Ctrl+T"), this);
        connect(scNewTab, &QShortcut::activated, this, [this]() { addNewTab(m_homepageUrl); });
        auto *scCloseTab = new QShortcut(QKeySequence("Ctrl+W"), this);
        connect(scCloseTab, &QShortcut::activated, this, [this]() { int idx = tabWidget->currentIndex(); if (idx >= 0) closeTab(idx); });
        auto *scReload = new QShortcut(QKeySequence("Ctrl+R"), this);
        connect(scReload, &QShortcut::activated, this, [this]() { if (auto *v = currentWebView()) v->reload(); });
        auto *scFocusUrl = new QShortcut(QKeySequence("Ctrl+L"), this);
        connect(scFocusUrl, &QShortcut::activated, this, [this]() { urlEdit->setFocus(); urlEdit->selectAll(); });
        auto *scAddBookmark = new QShortcut(QKeySequence("Ctrl+D"), this);
        connect(scAddBookmark, &QShortcut::activated, this, &TabbedBrowserWindow::addCurrentBookmark);
    }

    void updateClock() {
        QString fmt = m_clock24h ? "HH:mm" : "h:mm";
        clockLabel->setText(QDateTime::currentDateTime().toString(fmt));
    }

    void onClockClicked() { if (!m_clockCommand.isEmpty()) QProcess::startDetached(m_clockCommand, QStringList()); }
    void onNotesClicked() {
        if (!m_notesCommand.isEmpty()) {
            QString cmd = m_notesCommand; cmd.replace("~", QDir::homePath());
            QStringList args = QProcess::splitCommand(cmd);
            if (!args.isEmpty()) { QString p = args.takeFirst(); if (QProcess::startDetached(p, args)) return; }
        }
        NotesDialog dlg(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/notes.txt", this);
        dlg.exec();
    }

    void toggleDarkMode() {
        m_darkMode = !m_darkMode; applyTheme(m_darkMode); saveConfig();
        for (int i = 0; i < tabWidget->count(); ++i) {
            if (auto *v = qobject_cast<CustomWebEngineView*>(tabWidget->widget(i))) {
                if (m_darkMode) injectDarkModeToPage(v->page());
                injectUserCssToPage(v->page());
                if (auto *pg = v->page()) { pg->setBackgroundColor(m_transWebpage < 100 ? Qt::transparent : Qt::white); injectPageTransparencyToPage(pg); }
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
        for (int i = 0; i < tabWidget->count(); ++i) {
            if (auto *view = qobject_cast<CustomWebEngineView*>(tabWidget->widget(i))) {
                view->setMobileMode(m_mobileMode);
            }
        }
        if (auto *view = currentWebView()) view->reload();
        updateMobileToggleButton(m_mobileMode);
    }
    bool isMobileMode() const { return m_mobileMode; }

    void setHomepage(const QString &url) { m_homepageUrl = url; saveConfig(); QMessageBox::information(this, "Homepage", "New homepage saved:\n" + url); }
    void editBookmarksDialog() {
        BookmarkEditorDialog dlg(m_bookmarks, this);
        if (dlg.exec() == QDialog::Accepted) { m_bookmarks = dlg.getBookmarks(); saveBookmarks(); updateBookmarkCombo(); }
    }

    bool unlockPasswords() {
        if (m_passUnlocked) return true;
        bool ok = false;
        QString pass = QInputDialog::getText(this, "Master Password", "Enter Master Password:", QLineEdit::Password, "", &ok);
        if (!ok || pass.isEmpty()) return false;
        QFile file(getPasswordsFilePath());
        if (!file.exists()) { m_masterPassword = pass; m_passUnlocked = true; m_passwords.clear(); savePasswords(); updatePasswordCombo(); return true; }
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray plainJson;
            if (SimpleCipher::decrypt(file.readAll(), pass, plainJson)) {
                m_masterPassword = pass; m_passUnlocked = true;
                QJsonDocument doc = QJsonDocument::fromJson(plainJson);
                m_passwords.clear();
                if (doc.isArray()) {
                    for (const auto &val : doc.array()) {
                        QJsonObject obj = val.toObject();
                        m_passwords.append(PasswordEntry{obj["title"].toString(), obj["username"].toString(), obj["password"].toString()});
                    }
                }
                updatePasswordCombo();
                if (currentWebView()) autoMatchPasswordDomain(currentWebView()->url());
                return true;
            }
        }
        QMessageBox::warning(this, "Error", "Incorrect Password!");
        return false;
    }

    void editPasswordsDialog() {
        if (!unlockPasswords()) return;
        PasswordEditorDialog dlg(m_passwords, [this](const QString &np) { m_masterPassword = np; savePasswords(); }, this);
        if (dlg.exec() == QDialog::Accepted) { m_passwords = dlg.getEntries(); savePasswords(); updatePasswordCombo(); if (currentWebView()) autoMatchPasswordDomain(currentWebView()->url()); }
    }

    void copyPasswordUsername() {
        // Master-Passwort-Dialog auch ohne gewählte Domain zeigen:
        // Sonst passiert beim Klick auf u/p gar nichts, wenn noch
        // kein Master-Passwort eingegeben wurde.
        if (!unlockPasswords()) return;
        int idx = passCombo->currentIndex();
        if (idx > 0 && idx - 1 < m_passwords.size())
            QApplication::clipboard()->setText(m_passwords[idx - 1].username);
    }
    void copyPasswordValue() {
        if (passCombo->currentIndex() > 0 && unlockPasswords() && passCombo->currentIndex() - 1 < m_passwords.size()) {
            QString p = m_passwords[passCombo->currentIndex() - 1].password;
            QApplication::clipboard()->setText(p);
            QTimer::singleShot(15000, [p]() { if (QApplication::clipboard()->text() == p) QApplication::clipboard()->clear(); });
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
        for (int i = 0; i < m_passwords.size(); ++i) {
            if (host.contains(m_passwords[i].titleUrl.toLower())) { passCombo->setCurrentIndex(i + 1); return; }
        }
        passCombo->setCurrentIndex(0);
    }

    void editCssStylesDialog() { CssEditorDialog dlg(m_styleRules, this); if (dlg.exec() == QDialog::Accepted) { m_styleRules = dlg.getRules(); saveStyleRules(); } }
    void editUserAgentRulesDialog() { UserAgentEditorDialog dlg(m_uaRules, this); if (dlg.exec() == QDialog::Accepted) { m_uaRules = dlg.getRules(); saveUserAgentRules(); if (m_interceptor) m_interceptor->setRules(m_uaRules); } }

    void openSettingsDialog() {
        BrowserSettingsDialog dlg(m_homepageUrl, m_strictPrivacy, m_adBlocker, m_darkMode, m_unicodeIcons, m_isFrameless,
                                  m_urlBarMinWidth, m_reloadButtonMinWidth, m_urlBarSpacerMinWidth, m_defaultZoom, m_uiZoom, 100,
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
        bool oldAnyTrans = anyTransparency();
        if (dlg.exec() == QDialog::Accepted) {
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
            m_urlBarSpacerMinWidth = dlg.getUrlBarSpacerMinWidth();
            m_isFrameless = dlg.isFrameless();
            m_toolbarPosition = dlg.getToolbarPosition();
            int oldTabBarPos = m_tabBarPosition;
            m_tabBarPosition = dlg.getTabBarPosition();
            m_sideBarPosition = dlg.getSideBarPosition();
            applySideBarPosition();
            m_showTabNewTab = dlg.showTabNewTab();
            m_showSideToolbar = dlg.showSideToolbar();
            m_clock24h = dlg.isClock24h();

            m_showNav = dlg.showNav(); m_showReload = dlg.showReload(); m_showHome = dlg.showHome();
            m_showUrl = dlg.showUrl(); m_showGo = dlg.showGo(); m_showBookmarks = dlg.showBookmarks();
            m_showAddBookmark = dlg.showAddBookmark(); m_showTabJump = dlg.showTabJump(); m_showNewTab = dlg.showNewTab();
            m_showZoom = dlg.showZoom(); m_showPassList = dlg.showPassList(); m_showPassButtons = dlg.showPassButtons();
            m_showMobileToggle = dlg.showMobileToggle(); m_showClock = dlg.showClock(); m_showNotes = dlg.showNotes();

            m_showSideWinMenu = dlg.showSideWinMenu(); m_showSideSettings = dlg.showSideSettings();
            m_showSideReload = dlg.showSideReload(); m_showSideNav = dlg.showSideNav(); m_showSideHome = dlg.showSideHome();
            m_showSidePassManager = dlg.showSidePassManager(); m_showSideMobile = dlg.showSideMobile(); m_showSideNotes = dlg.showSideNotes(); m_showSideNewTab = dlg.showSideNewTab();

            sideToolbarWidget->setVisible(m_showSideToolbar);
            rebuildSideToolbarLayout();
            if (oldTabBarPos != m_tabBarPosition) {
                tabWidget->setTabPosition(m_tabBarPosition == 1 ? QTabWidget::South : QTabWidget::North);
                tabWidget->setCornerWidget(btnTabNewTab, m_tabBarPosition == 1 ? Qt::BottomRightCorner : Qt::TopRightCorner);
            }
            btnTabNewTab->setVisible(m_showTabNewTab);
            rebuildToolbarLayout();
            applyTheme(m_darkMode);
            saveConfig();
            if (oldAnyTrans != anyTransparency()) {
                hide();
                setAttribute(Qt::WA_TranslucentBackground, anyTransparency());
                show();
                applyTheme(m_darkMode);
            }
        }
    }

    void injectDarkModeToPage(QWebEnginePage *p) {
        if (!p) return;
        p->runJavaScript("document.documentElement.style.setProperty('color-scheme', 'dark', 'important');");
    }
    void injectUserCssToPage(QWebEnginePage *p) { Q_UNUSED(p); }
    void updateMobileToggleButton(bool isMobile) {
        if (m_useButtonIcons) { return; }
        if (isMobile) { btnMobileToggle->setText("m (on)"); btnSideMobile->setText("m (on)"); }
        else { btnMobileToggle->setText("m"); btnSideMobile->setText("m"); }
    }

    void addNewTab(const QString &urlStr) {
        if (tabWidget->count() >= MAX_TABS) {
            QMessageBox::information(this, "Limit Reached", QString("Maximum of %1 tabs supported in mu-browser-tabs.").arg(MAX_TABS));
            return;
        }

        auto *webView = new CustomWebEngineView(this, this);
        webView->setUseUnicodeIcons(m_unicodeIcons);
        webView->setMobileMode(m_mobileMode); // New tab inherits global Mobile/Desktop mode
        auto *page = new CustomWebPage(m_profile, webView);
        webView->setPage(page);
        webView->setZoomFactor(static_cast<qreal>(m_defaultZoom) / 100.0);

        page->setNewPageFactory([this]() -> QWebEnginePage* { return createPopupTabPage(); });
        installSameHostTargetFixOnPage(page);
        connect(page, &QWebEnginePage::loadFinished, this, [this, page](bool) {
            if (m_darkMode) injectDarkModeToPage(page);
            injectUserCssToPage(page);
            page->setBackgroundColor(m_transWebpage < 100 ? Qt::transparent : Qt::white);
            injectPageTransparencyToPage(page);
        });

        connect(webView, &QWebEngineView::titleChanged, this, [this, webView](const QString &title) {
            int idx = tabWidget->indexOf(webView);
            if (idx != -1) {
                QString t = title.trimmed();
                if (t.length() > 25) t = t.left(22) + "...";
                tabWidget->setTabText(idx, t.isEmpty() ? "New Tab" : t);
                updateTabsCombo();
            }
        });

        // Favicon ins Tab (Opera-Stil): iconChanged feuert, sobald das
        // Favicon der Seite geladen ist.
        connect(webView, &QWebEngineView::iconChanged, this, [this, webView](const QIcon &icon) {
            int idx = tabWidget->indexOf(webView);
            if (idx != -1 && !icon.isNull()) {
                tabWidget->setTabIcon(idx, icon);
            }
        });

        connect(webView, &QWebEngineView::urlChanged, this, [this, webView](const QUrl &url) {
            if (webView == currentWebView()) { urlEdit->setText(url.toString()); autoMatchPasswordDomain(url); }
        });

        connect(webView, &CustomWebEngineView::openUrlInNewTabRequested, this, [this](const QString &u) { addNewTab(u); });
        connect(webView, &CustomWebEngineView::addCurrentBookmarkRequested, this, [this]() { addCurrentBookmark(); });
        connect(webView, &CustomWebEngineView::openSettingsRequested, this, [this]() { openSettingsDialog(); });
        connect(webView, &CustomWebEngineView::toggleToolbarRequested, this, [this]() { toolbarContainer->setVisible(!toolbarContainer->isVisible()); });
        connect(webView, &CustomWebEngineView::toggleTabbarRequested, this, [this]() { tabWidget->setVisible(!tabWidget->isVisible()); });
        connect(webView, &CustomWebEngineView::toggleDarkModeRequested, this, [this]() { toggleDarkMode(); });
        connect(webView, &CustomWebEngineView::mobileModeToggled, this, [this, webView](bool isMobile) { if (webView == currentWebView()) updateMobileToggleButton(isMobile); });

        int idx = tabWidget->addTab(webView, "Loading...");
        tabWidget->setCurrentIndex(idx);
        updateTabsCombo();
        webView->load(QUrl(urlStr));
    }

    QWebEnginePage *createPopupTabPage() {
        auto *view = new CustomWebEngineView(this, this);
        auto *p = new CustomWebPage(m_profile, view);
        view->setPage(p);
        p->setNewPageFactory([this]() { return createPopupTabPage(); });
        return p;
    }

    void closeTab(int index) {
        if (tabWidget->count() <= 1) {
            if (auto *v = currentWebView()) {
                v->load(QUrl(m_homepageUrl));
                tabWidget->setTabText(0, "DuckDuckGo");
            }
            return;
        }
        auto *w = tabWidget->widget(index);
        tabWidget->removeTab(index);
        delete w;
        updateTabsCombo();
    }

    void onTabChanged(int index) {
        if (auto *v = currentWebView()) {
            urlEdit->setText(v->url().toString());
            updateMobileToggleButton(v->isMobileMode());
            autoMatchPasswordDomain(v->url());
        }
        if (!m_updatingTabsCombo && index >= 0 && index < tabJumpCombo->count()) {
            m_updatingTabsCombo = true; tabJumpCombo->setCurrentIndex(index); m_updatingTabsCombo = false;
        }
    }

protected:
    void closeEvent(QCloseEvent *event) override {
        saveSession(); // Tabs-Session (offene Tabs) sichern.
        saveConfig(); // Speichert auch die aktuelle Fenster-Geometrie.
        QWidget::closeEvent(event);
    }

public:
    void saveSession() {
        QJsonArray arr;
        for (int i = 0; i < tabWidget->count(); ++i) {
            if (auto *v = qobject_cast<CustomWebEngineView*>(tabWidget->widget(i))) arr.append(v->url().toString());
        }
        QFile file(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/tabs_session.json");
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(arr).toJson());
    }

    bool restoreSession() {
        QFile file(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/tabs_session.json");
        if (!file.open(QIODevice::ReadOnly)) return false;
        QJsonArray arr = QJsonDocument::fromJson(file.readAll()).array();
        for (const auto &v : arr) addNewTab(v.toString());
        return !arr.isEmpty();
    }

    void applyDesktopLayer() {}
    void addCurrentBookmark() {
        if (!currentWebView()) return;
        bool ok = false;
        QString name = QInputDialog::getText(this, "Add Bookmark", "Name:", QLineEdit::Normal, currentWebView()->title(), &ok);
        if (ok && !name.isEmpty()) { m_bookmarks.append(BookmarkItem{name, currentWebView()->url().toString()}); saveBookmarks(); updateBookmarkCombo(); }
    }
    QString getBookmarksFilePath() { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/bookmarks.json"; }
    void loadBookmarks() {
        QFile file(getBookmarksFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            for (const auto &v : QJsonDocument::fromJson(file.readAll()).array()) {
                QJsonObject o = v.toObject(); m_bookmarks.append(BookmarkItem{o["title"].toString(), o["url"].toString()});
            }
        }
        if (m_bookmarks.isEmpty()) {
            m_bookmarks.append(BookmarkItem{"DuckDuckGo", "https://duckduckgo.com"});
            saveBookmarks();
        }
    }
    void saveBookmarks() {
        QJsonArray arr;
        for (const auto &b : m_bookmarks) { QJsonObject o; o["title"] = b.title; o["url"] = b.url; arr.append(o); }
        QFile file(getBookmarksFilePath());
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(arr).toJson());
    }
    void updateBookmarkCombo() {
        bookmarkCombo->clear();
        bookmarkCombo->addItem("Bookmarks", ""); bookmarkCombo->addItem("★ Add Current Page", ""); bookmarkCombo->addItem("✏ Edit Bookmarks...", "");
        for (const auto &b : m_bookmarks) bookmarkCombo->addItem(b.title, b.url);
        bookmarkCombo->setCurrentIndex(0);
    }

    QString getPasswordsFilePath() { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/passwords.enc"; }
    void savePasswords() {
        if (!m_passUnlocked) return;
        QJsonArray arr;
        for (const auto &p : m_passwords) { QJsonObject o; o["title"] = p.titleUrl; o["username"] = p.username; o["password"] = p.password; arr.append(o); }
        QFile file(getPasswordsFilePath());
        if (file.open(QIODevice::WriteOnly)) file.write(SimpleCipher::encrypt(QJsonDocument(arr).toJson(), m_masterPassword));
    }
    void updatePasswordCombo() {
        passCombo->clear(); passCombo->addItem("Passwords", "");
        for (const auto &p : m_passwords) passCombo->addItem(p.titleUrl, p.username);
        passCombo->setCurrentIndex(0);
    }
    QString getStylesFilePath() {
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dataPath);
        return dataPath + "/styles.json";
    }

    // Default CSS injections (English, generated on first run).
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
    void loadUserAgentRules() {
        m_uaRules.clear();
        QFile file(getUserAgentsFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                for (const auto &val : doc.array()) {
                    QJsonObject obj = val.toObject();
                    m_uaRules.append(UserAgentRule{obj["pattern"].toString(), obj["mode"].toString(), obj["customUA"].toString()});
                }
            }
            file.close();
        }
        // Built-in Defaults MERGEN (nicht nur bei leerer Liste):
        // Wer schon eine gespeicherte Regelliste hat, bekam neue
        // Defaults wie GMX sonst nie — GMX fiel dann auf den
        // Default-Zweig ohne passende Client Hints zurück und lehnte
        // mit "technical error" ab. Eigene Regeln bleiben erhalten;
        // fehlende Defaults werden ans Ende angehängt.
        auto hasRule = [&](const QString &pattern) {
            for (const auto &r : m_uaRules) {
                if (r.pattern.compare(pattern, Qt::CaseInsensitive) == 0) return true;
            }
            return false;
        };
        bool defaultsAdded = false;
        if (!hasRule("google.com")) {
            m_uaRules.append(UserAgentRule{"google.com", "Custom", "Mozilla/5.0 (X11; Linux x86_64; rv:131.0) Gecko/20100101 Firefox/131.0"});
            defaultsAdded = true;
        }
        // GMX bewusst Desktop, NIE Mobile: Mobile-UA bricht Ordnerbaum / Session von GMX.
        // Eine fälschlich auf Mobile (oder Custom) gesetzte GMX-Regel
        // wird korrigiert — sonst serviert GMX falschen Code und der
        // Login-Flow bricht mit cross-origin SecurityError ab
        // (dl.gmx.net <-> navigator.gmx.net).
        // Facebook: NO fixed rule (see Desktop). A saved facebook.com rule
        // (e.g. from another variant) is REMOVED — otherwise the
        // toggle would never work for Facebook.
        for (int i = m_uaRules.size() - 1; i >= 0; --i) {
            if (m_uaRules[i].pattern.compare("facebook.com", Qt::CaseInsensitive) == 0) {
                m_uaRules.removeAt(i);
                defaultsAdded = true;
            }
        }
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
        if (m_interceptor) m_interceptor->setRules(m_uaRules);
    }
    void saveUserAgentRules() {
        QJsonArray arr;
        for (const auto &r : m_uaRules) {
            QJsonObject obj; obj["pattern"] = r.pattern; obj["mode"] = r.mode; obj["customUA"] = r.customUA; arr.append(obj);
        }
        QFile file(getUserAgentsFilePath());
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(arr).toJson()); file.close(); }
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

    QWebEngineProfile *m_profile;
    MuRequestInterceptor *m_interceptor;
    QHBoxLayout *outerLayout;
    SideToolbarWidget *sideToolbarWidget;
    QVBoxLayout *sideLayout;
    QPushButton *btnWinMenu, *btnSideSettings, *btnSideReload, *btnSideBack, *btnSideForward, *btnSideHome;
    QPushButton *btnSideUser, *btnSidePass, *btnSideEditPass, *btnSideMobile, *btnSideNotes, *btnSideNewTab;
    QPushButton *btnSidePassList, *btnSideBookmarks;
    QWidget *centerContentWidget;
    QVBoxLayout *mainLayout;
    QWidget *toolbarContainer;
    QHBoxLayout *toolsLayout;
    QTabWidget *tabWidget;
    QPushButton *btnTabNewTab;
    QPushButton *btnBack, *btnForward, *btnReload, *btnHome, *btnGo, *btnAddBookmark, *btnNewTab;
    QLineEdit *urlEdit;
    QComboBox *bookmarkCombo, *tabJumpCombo, *zoomCombo;
    ClickableComboBox *passCombo;
    QPushButton *btnCopyUser, *btnCopyPass, *btnEditPass;
    QList<PasswordEntry> m_passwords;
    bool m_passUnlocked;
    QString m_masterPassword;
    QPushButton *btnMobileToggle;
    ClickableLabel *clockLabel;
    QTimer *clockTimer;
    QPushButton *btnNotes;
    QList<BookmarkItem> m_bookmarks;
    QList<StyleRule> m_styleRules;
    QList<UserAgentRule> m_uaRules;
    bool m_isFrameless, m_hideToolbar, m_darkMode, m_unicodeIcons, m_strictPrivacy;
    bool m_adBlocker = true; // Generic ad blocker (interceptor).
    bool m_mobileMode = false; // Global Mobile/Desktop toggle (saved in config).
    QStringList m_adBlockExceptions; // Ad blocker exceptions.
    bool m_useButtonIcons;
    int m_transTabBarBg = 100, m_transTabBarControls = 100;
    int m_transToolbarBg = 100, m_transToolbarControls = 100;
    int m_transSideBg = 100, m_transSideControls = 100;
    int m_transWebpage = 100;
    QByteArray m_windowGeometry;
    int m_urlBarMinWidth, m_reloadButtonMinWidth, m_defaultZoom, m_uiZoom, m_tabZoom, m_tabBarPosition, m_toolbarPosition;
    int m_urlBarSpacerMinWidth = 0; // Spacer hinter der URL-Bar (transparent)
    int m_sideBarPosition = 0; // 0 = links, 1 = rechts
    QWidget *urlBarSpacer = nullptr;
    bool m_showNav, m_showReload, m_showHome, m_showUrl, m_showGo, m_showBookmarks, m_showAddBookmark, m_showTabJump, m_showNewTab, m_showZoom;
    bool m_showPassList, m_showPassButtons, m_showMobileToggle, m_showClock, m_showNotes;
    bool m_showSideToolbar, m_showTabNewTab, m_showSideNewTab, m_showSideSettings, m_clock24h;
    bool m_showSideWinMenu, m_showSideReload, m_showSideNav, m_showSideHome, m_showSidePassManager, m_showSideMobile, m_showSideNotes;
    QString m_clockCommand, m_notesCommand, m_homepageUrl, m_layer, m_profileDir;
    bool m_updatingTabsCombo, m_updatingZoomCombo;

    CustomWebEngineView* currentWebView() const {
        return qobject_cast<CustomWebEngineView*>(tabWidget->currentWidget());
    }

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
        if (btnCopyUser) { btnCopyUser->setFont(defaultFont); btnCopyUser->setMaximumHeight(scaledHeight); btnCopyUser->setMinimumWidth(scaledHeight); }
        if (btnCopyPass) { btnCopyPass->setFont(defaultFont); btnCopyPass->setMaximumHeight(scaledHeight); btnCopyPass->setMinimumWidth(scaledHeight); }
        if (btnEditPass) { btnEditPass->setFont(defaultFont); btnEditPass->setMaximumHeight(scaledHeight); btnEditPass->setMinimumWidth(scaledHeight); }
        if (btnMobileToggle) { btnMobileToggle->setFont(defaultFont); btnMobileToggle->setMaximumHeight(scaledHeight); }
        if (clockLabel) { clockLabel->setFont(defaultFont); clockLabel->setMaximumHeight(scaledHeight); }
        if (btnNotes) { btnNotes->setFont(defaultFont); btnNotes->setMaximumHeight(scaledHeight); btnNotes->setMinimumWidth(scaledHeight); }
    }
    void applyTabScaling(int p) { Q_UNUSED(p); }

    void rebuildMainLayout() {
        mainLayout->removeWidget(toolbarContainer);
        mainLayout->removeWidget(tabWidget);
        if (m_toolbarPosition == 0) {
            mainLayout->addWidget(toolbarContainer);
            mainLayout->addWidget(tabWidget, 1);
        } else {
            mainLayout->addWidget(tabWidget, 1);
            mainLayout->addWidget(toolbarContainer);
        }
    }

    void rebuildSideToolbarLayout() {
        QLayoutItem *i;
        while ((i = sideLayout->takeAt(0)) != nullptr) delete i;
        if (m_showSideWinMenu) sideLayout->addWidget(btnWinMenu);
        if (m_showSideSettings) sideLayout->addWidget(btnSideSettings);
        sideLayout->addStretch(1);
        if (m_showSideReload) sideLayout->addWidget(btnSideReload);
        sideLayout->addStretch(1);
        if (m_showSideNav) { sideLayout->addWidget(btnSideBack); sideLayout->addWidget(btnSideForward); }
        sideLayout->addStretch(1);
        if (m_showSideHome) sideLayout->addWidget(btnSideHome);
        // Bookmark-Button direkt unter Home, ohne extra Space
        // (kein Stretch dazwischen, Spacing ist 0).
        sideLayout->addWidget(btnSideBookmarks);
        sideLayout->addStretch(1);
        if (m_showSidePassManager) { sideLayout->addWidget(btnSidePassList); sideLayout->addWidget(btnSideUser); sideLayout->addWidget(btnSidePass); sideLayout->addWidget(btnSideEditPass); }
        sideLayout->addStretch(1);
        if (m_showSideMobile) sideLayout->addWidget(btnSideMobile);
        if (m_showSideNotes) sideLayout->addWidget(btnSideNotes);
        sideLayout->addStretch(1);
        if (m_showSideNewTab) sideLayout->addWidget(btnSideNewTab);
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
        QLayoutItem *i;
        while ((i = toolsLayout->takeAt(0)) != nullptr) delete i;
        if (m_showNav) { toolsLayout->addWidget(btnBack); toolsLayout->addWidget(btnForward); }
        if (m_showReload) toolsLayout->addWidget(btnReload);
        if (m_showHome) toolsLayout->addWidget(btnHome);
        if (m_showUrl) toolsLayout->addWidget(urlEdit, 1);
        // Spacer hinter der URL-Bar (transparent, Mindestbreite einstellbar).
        urlBarSpacer->setMinimumWidth(m_urlBarSpacerMinWidth);
        toolsLayout->addWidget(urlBarSpacer);
        if (m_showGo) toolsLayout->addWidget(btnGo);
        if (m_showZoom) toolsLayout->addWidget(zoomCombo);
        if (m_showBookmarks) toolsLayout->addWidget(bookmarkCombo);
        if (m_showAddBookmark) toolsLayout->addWidget(btnAddBookmark);
        if (m_showPassList) toolsLayout->addWidget(passCombo);
        if (m_showPassButtons) { toolsLayout->addWidget(btnCopyUser); toolsLayout->addWidget(btnCopyPass); toolsLayout->addWidget(btnEditPass); }
        if (m_showMobileToggle) toolsLayout->addWidget(btnMobileToggle);
        if (m_showNotes) toolsLayout->addWidget(btnNotes);
        if (m_showTabJump) toolsLayout->addWidget(tabJumpCombo);
        if (m_showNewTab) toolsLayout->addWidget(btnNewTab);
        if (m_showClock) toolsLayout->addWidget(clockLabel);
        passCombo->setVisible(m_showPassList);
        btnCopyUser->setVisible(m_showPassButtons); btnCopyPass->setVisible(m_showPassButtons); btnEditPass->setVisible(m_showPassButtons);
        toolbarContainer->setVisible(!m_hideToolbar);
    }

    void applyTheme(bool dark) {
        if (anyTransparency()) {
            // Transparenz: KEIN opakes App-Stylesheet - Hintergruende kommen nur aus applyTransparency (rgba pro Bereich).
            // Behalten: nur color (keine opaken Hintergruende).
            if (dark) setStyleSheet("QWidget { color: #e0e0e0; }");
            else setStyleSheet("");
        } else if (dark) {
            setStyleSheet("QWidget { background-color: #121212; color: #e0e0e0; } #toolbarContainer, #sideToolbarWidget { background-color: #1a1a1a; }");
        } else {
            setStyleSheet("");
        }
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
        setAttribute(Qt::WA_TranslucentBackground, anyTransparency());
        if (anyTransparency()) {
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
            tabWidget->setStyleSheet("");
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
            QString tabText = darkUi ? "#e0e0e0" : "#202020";
            QString tabTextSel = darkUi ? "#ffffff" : "#000000";
            tabWidget->setStyleSheet(
                QString("QTabWidget::pane { background-color: transparent; }"
                        "QTabWidget::tab-bar { background-color: %1; }"
                        "QTabBar { background-color: transparent; }"
                        "QTabBar::tab { background: %2; color: %4; padding: 5px 10px; margin: 2px 3px; border-radius: 4px; }"
                        "QTabBar::tab:selected { background: %3; color: %5; }"
                        "QTabWidget QPushButton { background-color: %2; }"
                        "QTabWidget QPushButton:hover { background-color: %3; }")
                    .arg(muRgba(bg, m_transTabBarBg)).arg(muRgba(ctrl, m_transTabBarControls)).arg(muRgba(ctrlHover, m_transTabBarControls))
                    .arg(tabText).arg(tabTextSel));
        }
        // Erzwungenes, SOFORTIGES Neuzeichnen (Ghosting auf transluzentem Fenster).
        // update() plant nur ein Neuzeichnen ein — auf einem transluzenten
        // Fenster bleibt der alte Inhalt sonst im Backing-Store sichtbar
        // (verwaschene Textreste, z.B. in der Seitenleiste). repaint() ist
        // synchron und loescht den Backing-Store wirklich.
        repaint();
        if (toolbarContainer) toolbarContainer->repaint();
        if (sideToolbarWidget) sideToolbarWidget->repaint();
        if (tabWidget) tabWidget->repaint();
        for (int i = 0; i < tabWidget->count(); ++i) {
            if (auto *view = qobject_cast<CustomWebEngineView*>(tabWidget->widget(i))) {
                if (auto *pp = view->page()) {
                    pp->setBackgroundColor(m_transWebpage < 100 ? Qt::transparent : Qt::white);
                    injectPageTransparencyToPage(pp);
                }
            }
        }
    }

    void injectPageTransparencyToPage(QWebEnginePage *page) {
        if (!page) return;
        if (m_transWebpage >= 100) {
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

    QString getConfigFilePath() { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/tabs_config.json"; }
    void loadConfig() {
        m_useButtonIcons = false;
        m_transTabBarBg = 100; m_transTabBarControls = 100;
        m_transToolbarBg = 100; m_transToolbarControls = 100;
        m_transSideBg = 100; m_transSideControls = 100;
        m_transWebpage = 100;
        m_homepageUrl = "https://duckduckgo.com";
        QFile file(getConfigFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
            if (obj.contains("homepage")) m_homepageUrl = obj["homepage"].toString();
            if (obj.contains("tabBarPosition")) m_tabBarPosition = obj["tabBarPosition"].toInt(0);
            if (obj.contains("showTabNewTab")) m_showTabNewTab = obj["showTabNewTab"].toBool(true);
            if (obj.contains("clock24h")) m_clock24h = obj["clock24h"].toBool(true);
            if (obj.contains("useButtonIcons")) m_useButtonIcons = obj["useButtonIcons"].toBool(false);
            if (obj.contains("adBlocker")) m_adBlocker = obj["adBlocker"].toBool(true);
                if (obj.contains("mobileMode")) m_mobileMode = obj["mobileMode"].toBool(false); // Global Mobile/Desktop toggle
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
            m_transTabBarBg = qBound(10, m_transTabBarBg, 100);
            m_transTabBarControls = qBound(10, m_transTabBarControls, 100);
            m_transToolbarBg = qBound(10, m_transToolbarBg, 100);
            m_transToolbarControls = qBound(10, m_transToolbarControls, 100);
            m_transSideBg = qBound(10, m_transSideBg, 100);
            m_transSideControls = qBound(10, m_transSideControls, 100);
            m_transWebpage = qBound(10, m_transWebpage, 100);
        }
    }
    void saveConfig() {
        QJsonObject obj;
        obj["homepage"] = m_homepageUrl;
        obj["tabBarPosition"] = m_tabBarPosition;
        obj["showTabNewTab"] = m_showTabNewTab;
        obj["clock24h"] = m_clock24h;
        obj["useButtonIcons"] = m_useButtonIcons;
        obj["adBlocker"] = m_adBlocker;
        obj["mobileMode"] = m_mobileMode; // Global Mobile/Desktop toggle
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
        QFile file(getConfigFilePath());
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(obj).toJson());
    }

    void updateTabsCombo() {
        m_updatingTabsCombo = true;
        tabJumpCombo->clear();
        for (int i = 0; i < tabWidget->count(); ++i) {
            tabJumpCombo->addItem(QString("[%1] %2").arg(i + 1).arg(tabWidget->tabText(i)));
        }
        tabJumpCombo->setCurrentIndex(tabWidget->currentIndex());
        m_updatingTabsCombo = false;
    }

    void updateZoomComboSelection(int zoomVal) {
        m_updatingZoomCombo = true;
        for (int i = 0; i < zoomCombo->count(); ++i) {
            if (zoomCombo->itemData(i).toInt() == zoomVal) { zoomCombo->setCurrentIndex(i); break; }
        }
        m_updatingZoomCombo = false;
    }
};

// CustomWebEngineView::toggleMobileMode() — defined AFTER BrowserWindow,
// because BrowserWindow is only forward-declared inside CustomWebEngineView.
// Uses the view's window pointer (m_win — Tabs: m_win,
// Desktop/Main: m_browserWindow).
void CustomWebEngineView::toggleMobileMode() {
    if (m_win) {
        m_win->toggleMobileMode();
        return;
    }
    m_isMobileMode = !m_isMobileMode;
    if (m_isMobileMode) page()->profile()->setHttpUserAgent(muMobileUA());
    else page()->profile()->setHttpUserAgent(muChromeUA());
    reload();
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mu"));
    QCoreApplication::setApplicationName(QStringLiteral("mu-tabs-browser"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Mu WebEngine Low-RAM Tabbed Browser (max 10 tabs)");
    parser.addHelpOption();
    QCommandLineOption urlOption(QStringList() << "u" << "url", "Start URL", "url", "https://schinkenpizza.de");
    QCommandLineOption framelessOption("frameless", "Show borderless window");
    QCommandLineOption hideToolbarOption("hide-toolbar", "Hide control toolbar on start");
    QCommandLineOption layerOption("layer", "Desktop layer", "layer", "normal");
    QCommandLineOption profileDirOption("profile-dir", "Persistent profile dir", "dir", "");
    QCommandLineOption socketOption("socket", "Socket path for IPC", "socket", "");

    parser.addOption(urlOption);
    parser.addOption(framelessOption);
    parser.addOption(hideToolbarOption);
    parser.addOption(layerOption);
    parser.addOption(profileDirOption);
    parser.addOption(socketOption);
    parser.process(app);

    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!parser.value(profileDirOption).isEmpty()) dataPath = parser.value(profileDirOption);
    QDir().mkpath(dataPath + "/storage"); QDir().mkpath(dataPath + "/cache");

    auto *profile = new QWebEngineProfile(QStringLiteral("MuTabsProfile"), &app);
    profile->setPersistentStoragePath(dataPath + "/storage");
    profile->setCachePath(dataPath + "/cache");
    profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    auto *interceptor = new MuRequestInterceptor(&app);
    profile->setUrlRequestInterceptor(interceptor);

    TabbedBrowserWindow window(
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

    window.resize(1280, 720);
    window.show();
    return app.exec();
}

#include "main_tabs.moc"
