#include <iostream>
#include "../browser/browser_window.hpp"

int main(int argc, char** argv) {
    SetProcessDPIAware();
    std::string url = "about:blank";
    if (argc > 1) url = argv[1];

    browser::BrowserWindow browser;
    auto r = browser.initialize();
    if (r.is_err()) {
        std::cerr << "Failed to initialize: " << r.unwrap_err() << std::endl;
        return 1;
    }
    browser.navigate(url);
    browser.run();
    return 0;
}
