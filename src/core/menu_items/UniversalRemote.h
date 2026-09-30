#pragma once
#include <MenuItemInterface.h>

class UniversalRemoteMenu : public MenuItemInterface {
public:
    UniversalRemoteMenu() : MenuItemInterface("Universal Remote") {}
    void optionsMenu(void);
    void drawIcon(float scale);
};
