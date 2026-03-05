#pragma once
#include <QWidget>
#include <string>
extern "C" {
#include "librazerd.h"
}

class FreqTab : public QWidget {
    Q_OBJECT
public:
    explicit FreqTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);
};
