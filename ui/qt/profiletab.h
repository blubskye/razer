#pragma once
#include <QWidget>
#include <string>
extern "C" {
#include "librazerd.h"
}

class ProfileTab : public QWidget {
    Q_OBJECT
public:
    explicit ProfileTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);
};
