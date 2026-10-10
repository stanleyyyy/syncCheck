#pragma once

#include <atomic>
#include <string>

// Windows notification area icon of the tray app: a double click opens the web UI, the menu also
// starts the app with Windows and quits. Runs the message loop until Quit is chosen or quit is set
// elsewhere; then sets quit.
void runTray(const std::string &url, std::atomic<bool> &quit);

// Shows an error to the user of the tray app, which has no console.
void showTrayError(const std::string &message);
