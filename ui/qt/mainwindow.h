#pragma once

#include <QMainWindow>
#include <QComboBox>
#include <QTabWidget>
#include <QSocketNotifier>
#include <QString>
#include <vector>
#include <string>

extern "C" {
#include "librazerd.h"
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onDeviceChanged(int index);
    void onHotplugEvent();

private:
    void refreshDeviceList();
    void populateTabs(const std::string &idstr);

    razerd_t        *m_r        = nullptr;
    QComboBox       *m_devCombo = nullptr;
    QTabWidget      *m_tabs     = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    std::vector<std::string> m_mice;
};
