#pragma once
#include <QWidget>
#include <string>
extern "C" {
#include "librazerd.h"
}

class LedTab : public QWidget {
    Q_OBJECT
public:
    explicit LedTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);
};
