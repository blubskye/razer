#pragma once
#include <QWidget>
#include <string>
extern "C" {
#include "librazerd.h"
}

class ButtonsTab : public QWidget {
    Q_OBJECT
public:
    explicit ButtonsTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);
};
