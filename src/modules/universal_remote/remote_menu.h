#pragma once
#include "core/display.h"

// loopOptions executes one action then returns. Keep local remote submenus open
// after an action, without recursive re-entry or replacing the global options.
inline void remoteMenu(std::vector<Option> opts, const char *title) {
    bool selected = false;
    for (auto &opt : opts) {
        auto action = opt.operation;
        opt.operation = [&, action]() { selected = true; action(); };
    }
    int index = 0;
    do {
        selected = false;
        index = loopOptions(opts, MENU_TYPE_SUBMENU, title, index);
    } while (selected && !returnToMenu);
}
