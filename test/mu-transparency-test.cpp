// mu-transparency-test.cpp - Tech-HUD Style Transparency (Optimierter Feinschliff)
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QTabBar>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QTextEdit>
#include <QPainter>
#include <QPixmap>
#include <QIcon>

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
    else if (kind == "reload") { p.drawArc(3, 3, 14, 14, 45 * 16, 270 * 16); QPolygonF t; t << QPointF(16, 2) << QPointF(18, 6) << QPointF(13, 5); p.drawPolygon(t); }
    else if (kind == "home") { QPolygonF t; t << QPointF(10, 3) << QPointF(3, 10) << QPointF(17, 10); p.drawPolygon(t); p.drawRect(6, 10, 8, 7); }
    else if (kind == "settings") { p.drawEllipse(5, 5, 10, 10); p.drawEllipse(8, 8, 4, 4); p.drawLine(10, 3, 10, 5); p.drawLine(10, 15, 10, 17); p.drawLine(3, 10, 5, 10); p.drawLine(15, 10, 17, 10); }
    else if (kind == "user") { p.drawEllipse(7, 3, 6, 6); p.drawArc(3, 10, 14, 10, 0, 180 * 16); }
    return QIcon(pm);
}

// Tech-HUD Style Panel mit exakter Alpha-Steuerung per QPainter
class TransparentPanel : public QWidget {
    Q_OBJECT
public:
    TransparentPanel(QWidget *parent = nullptr) : QWidget(parent) {}
    QColor bgColor = QColor(30, 30, 30, 255);

    void setBgColor(const QColor &c, int alphaPct) {
        bgColor = c;
        bgColor.setAlpha((alphaPct * 255) / 100);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Zeichnet den Hintergrund pixelgenau mit Alpha-Kanal ohne opake Qt-Standardartefakte
        p.fillRect(rect(), bgColor);
    }
};

class TestWindow : public QWidget {
    Q_OBJECT
public:
    TestWindow() {
        setWindowTitle("mu-transparency-test (Feinschliff)");
        setObjectName("mainWindow");
        resize(950, 650);

        // Echtes transparentes Hauptfenster aktivieren
        setAttribute(Qt::WA_TranslucentBackground, true);
        setStyleSheet("#mainWindow { background-color: transparent; }");

        auto *outer = new QHBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        // --- Sidebar ---
        sidePanel = new TransparentPanel(this);
        sidePanel->setObjectName("sidePanel");
        auto *sideLay = new QVBoxLayout(sidePanel);
        sideLay->setContentsMargins(2, 2, 2, 2);
        sideLay->setSpacing(2);
        for (const auto &k : QStringList{"settings", "user", "home", "reload"}) {
            auto *b = new QPushButton(this);
            b->setIcon(drawBtnIcon(k, true));
            b->setToolTip(k);
            iconButtons.append({b, k});
            sideLay->addWidget(b);
        }
        sideLay->addStretch(1);
        outer->addWidget(sidePanel, 0);

        // --- Center Container (ohne Ränder für nahtloses Ineinandergreifen) ---
        auto *centerWidget = new QWidget(this);
        centerWidget->setStyleSheet("background: transparent;");
        auto *centerLay = new QVBoxLayout(centerWidget);
        centerLay->setContentsMargins(0, 0, 0, 0);
        centerLay->setSpacing(0);

        // Toolbar
        toolbarPanel = new TransparentPanel(this);
        toolbarPanel->setObjectName("toolbarPanel");
        auto *toolsLay = new QHBoxLayout(toolbarPanel);
        toolsLay->setContentsMargins(4, 4, 4, 4);
        toolsLay->setSpacing(3);
        auto *btnBack = new QPushButton(this); btnBack->setIcon(drawBtnIcon("back", true));
        auto *btnReload = new QPushButton(this); btnReload->setIcon(drawBtnIcon("reload", true));
        auto *btnHome = new QPushButton(this); btnHome->setIcon(drawBtnIcon("home", true));
        iconButtons.append({btnBack, "back"}); iconButtons.append({btnReload, "reload"}); iconButtons.append({btnHome, "home"});
        urlEdit = new QLineEdit("https://example.com", this);
        toolsLay->addWidget(btnBack);
        toolsLay->addWidget(btnReload);
        toolsLay->addWidget(btnHome);
        toolsLay->addWidget(urlEdit, 1);
        centerLay->addWidget(toolbarPanel, 0);

        // Webseite / Content Area
        contentPanel = new TransparentPanel(this);
        contentPanel->setObjectName("contentPanel");
        auto *contentLay = new QVBoxLayout(contentPanel);
        contentLay->setContentsMargins(0, 0, 0, 0);
        contentLabel = new QLabel("WEBSEITE / CONTENT BEREICH\n(Vollständig separat regelbare Transparenz)", this);
        contentLabel->setAlignment(Qt::AlignCenter);
        contentLabel->setStyleSheet("color: #ffffff; font-size: 16px; background: transparent;");
        contentLay->addWidget(contentLabel);
        centerLay->addWidget(contentPanel, 1);

        // Tab Bar Container
        tabPanel = new TransparentPanel(this);
        tabPanel->setObjectName("tabPanel");
        auto *tabLay = new QHBoxLayout(tabPanel);
        tabLay->setContentsMargins(2, 2, 2, 2);
        tabLay->setSpacing(2);
        tabBarWidget = new QTabBar(this);
        tabBarWidget->setTabsClosable(true);
        tabBarWidget->addTab("Google");
        tabBarWidget->addTab("WhatsApp");
        tabBarWidget->addTab("rhodan4.png");
        auto *btnNewTab = new QPushButton("+", this);
        tabLay->addWidget(tabBarWidget, 1);
        tabLay->addWidget(btnNewTab, 0);
        centerLay->addWidget(tabPanel, 0);

        outer->addWidget(centerWidget, 1);

        // --- Control Panel (Rechts, opak zur Bedienung) ---
        auto *ctrl = new QGroupBox("Tech-HUD Style Transparenz-Regler", this);
        ctrl->setStyleSheet("QGroupBox { background-color: #222222; color: #ffffff; border: 1px solid #444; }");
        auto *ctrlLay = new QVBoxLayout(ctrl);
        
        auto addRow = [&](const QString &label, int value, QSlider *&slider, QSpinBox *&spin) {
            ctrlLay->addWidget(new QLabel(label, this));
            auto *row = new QHBoxLayout();
            slider = new QSlider(Qt::Horizontal, this);
            slider->setRange(0, 100);
            slider->setValue(value);
            spin = new QSpinBox(this);
            spin->setRange(0, 100);
            spin->setSuffix(" %");
            spin->setValue(value);
            row->addWidget(slider, 1);
            row->addWidget(spin);
            ctrlLay->addLayout(row);
            connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
            connect(slider, &QSlider::valueChanged, this, &TestWindow::applyTransparency);
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &TestWindow::applyTransparency);
        };

        addRow("Tab Bar Hintergrund:", 85, tabBgSlider, tabBgSpin);
        addRow("Toolbar Hintergrund:", 85, toolbarBgSlider, toolbarBgSpin);
        addRow("Sidebar Hintergrund:", 85, sideBgSlider, sideBgSpin);
        addRow("Webseite / Content Hintergrund:", 25, contentBgSlider, contentBgSpin);
        addRow("Buttons & Controls Alpha:", 75, ctrlAlphaSlider, ctrlAlphaSpin);

        outer->addWidget(ctrl, 0);
        ctrl->setFixedWidth(340);

        applyTransparency();
    }

private:
    void applyTransparency() {
        QColor baseCol(22, 22, 26);
        
        tabPanel->setBgColor(baseCol, tabBgSlider->value());
        toolbarPanel->setBgColor(baseCol, toolbarBgSlider->value());
        sidePanel->setBgColor(baseCol, sideBgSlider->value());
        
        // Webseite bekommt einen eigenen Farbton mit eigenem Alpha für echtes Durchschimmern
        QColor webCol(15, 15, 35);
        contentPanel->setBgColor(webCol, contentBgSlider->value());

        // Controls (Buttons, Inputs, Tabs) einheitlich mit flexiblem Alpha stylen
        int cAlpha = ctrlAlphaSlider->value();
        QString btnStyle = QString(
            "QPushButton, QLineEdit, QComboBox, QTabBar::tab {"
            "  background-color: rgba(55, 55, 65, %1);"
            "  color: #ffffff;"
            "  border: 1px solid rgba(90, 90, 100, %1);"
            "  border-radius: 3px;"
            "  padding: 4px;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(80, 80, 100, %1);"
            "}"
            "QTabBar::tab:selected {"
            "  background-color: rgba(90, 90, 120, %1);"
            "}"
            "QTabBar::tab {"
            "  border-top-left-radius: 3px;"
            "  border-top-right-radius: 3px;"
            "}"
        ).arg(cAlpha / 100.0);

        setStyleSheet(btnStyle);
        update();
    }

    TransparentPanel *sidePanel = nullptr;
    TransparentPanel *toolbarPanel = nullptr;
    TransparentPanel *contentPanel = nullptr;
    TransparentPanel *tabPanel = nullptr;
    QTabBar *tabBarWidget = nullptr;
    QLineEdit *urlEdit = nullptr;
    QLabel *contentLabel = nullptr;

    QSlider *tabBgSlider = nullptr, *toolbarBgSlider = nullptr, *sideBgSlider = nullptr, *contentBgSlider = nullptr, *ctrlAlphaSlider = nullptr;
    QSpinBox *tabBgSpin = nullptr, *toolbarBgSpin = nullptr, *sideBgSpin = nullptr, *contentBgSpin = nullptr, *ctrlAlphaSpin = nullptr;
    QList<QPair<QPushButton*, QString>> iconButtons;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    TestWindow w;
    w.show();
    return app.exec();
}

#include "mu-transparency-test.moc"