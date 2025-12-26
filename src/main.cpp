/**
 * WebUI RDP Client - Main Application
 * 
 * A native C++ application using WebUI for the frontend
 * and FreeRDP for Remote Desktop connections.
 */

#include <webui.hpp>
#include <iostream>
#include <string>

#include "gui/main_window.hpp"
#include "gui/aad_auth_handler.hpp"

int main(int argc, char* argv[]) {
    std::cout << "============================================" << std::endl;
    std::cout << "  WebUI RDP Client v0.1.0" << std::endl;
    std::cout << "  Remote Desktop Interface" << std::endl;
    std::cout << "============================================" << std::endl;
    
    // Parse command line arguments
    int debug_port = 0;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--debug-port=") == 0) {
            try {
                debug_port = std::stoi(arg.substr(13));
            } catch (...) {
                std::cerr << "[RDPMAN] Invalid debug port: " << arg.substr(13) << std::endl;
            }
        } else if (arg == "-USE_MANUAL_CODE_FLOW") {
            // Enable manual AAD code flow mode
            AADAuthHandler::enable_manual_code_flow();
        }
    }
    
    // Create and initialize the main window
    MainWindow main_window(debug_port);
    
    if (!main_window.initialize()) {
        std::cerr << "[ERROR] Failed to initialize main window" << std::endl;
        return 1;
    }
    
    // Show the window
    if (!main_window.show()) {
        std::cerr << "[ERROR] Failed to show main window" << std::endl;
        return 1;
    }

    std::cout << "[RDPMAN] Systems online. Awaiting your command." << std::endl;
    
    // Wait until the window is closed
    webui::wait();
    
    std::cout << "[RDPMAN] Shutting down. Goodbye." << std::endl;
    
    return 0;
}
