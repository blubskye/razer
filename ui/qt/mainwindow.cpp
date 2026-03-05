#include "mainwindow.h"
#include "dpitab.h"
#include "ledtab.h"
#include "freqtab.h"
#include "profiletab.h"
#include "buttonstab.h"

#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Razer Device Configuration");
    resize(640, 520);

    m_r = razerd_open();
    if (!m_r) {
        QMessageBox::critical(this, "razercfg", "razerd is not running.");
        QTimer::singleShot(0, qApp, &QApplication::quit);
        return;
    }

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(8, 8, 8, 8);
    vbox->setSpacing(6);

    m_devCombo = new QComboBox(this);
    vbox->addWidget(m_devCombo);

    m_tabs = new QTabWidget(this);
    vbox->addWidget(m_tabs);

    /* Hot-plug notifications */
    int fd = razerd_get_notify_fd(m_r);
    if (fd >= 0) {
        m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated,
                this, &MainWindow::onHotplugEvent);
    }

    connect(m_devCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onDeviceChanged);

    refreshDeviceList();
}

MainWindow::~MainWindow()
{
    if (m_r) razerd_close(m_r);
}

void MainWindow::refreshDeviceList()
{
    char **mice = nullptr;
    size_t mc   = 0;
    if (razerd_get_mice(m_r, &mice, &mc) != 0) return;

    m_mice.clear();
    m_devCombo->blockSignals(true);
    m_devCombo->clear();
    for (size_t i = 0; i < mc; i++) {
        m_mice.push_back(mice[i]);
        m_devCombo->addItem(QString::fromUtf8(mice[i]));
    }
    m_devCombo->blockSignals(false);
    razerd_free_mice(mice, mc);

    if (!m_mice.empty())
        onDeviceChanged(0);
    else
        m_tabs->clear();
}

void MainWindow::onDeviceChanged(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= m_mice.size()) return;
    populateTabs(m_mice[static_cast<size_t>(index)]);
}

void MainWindow::onHotplugEvent()
{
    razerd_event_t ev;
    while (razerd_read_event(m_r, &ev) == 0)
        ; /* drain */
    refreshDeviceList();
}

void MainWindow::populateTabs(const std::string &idstr)
{
    /* Replace all tabs with fresh widgets for the selected device */
    m_tabs->blockSignals(true);
    while (m_tabs->count() > 0) {
        QWidget *w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete w;
    }

    m_tabs->addTab(new DpiTab(m_r, idstr, this),     "DPI");
    m_tabs->addTab(new LedTab(m_r, idstr, this),     "LEDs");
    m_tabs->addTab(new FreqTab(m_r, idstr, this),    "Frequency");
    m_tabs->addTab(new ProfileTab(m_r, idstr, this), "Profiles");
    m_tabs->addTab(new ButtonsTab(m_r, idstr, this), "Buttons");
    m_tabs->blockSignals(false);
}
