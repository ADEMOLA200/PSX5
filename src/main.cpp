#include "runtime/emulator.h"
#include "core/logger.h"
#include "debugger.h"
#ifdef PSX5_ENABLE_QT_GUI
#include "gui/main_window.h"
#include <QApplication>
#endif
#include <fstream>
#include <vector>
#include <iostream>

static std::vector<uint8_t> read_file(const std::string& p){ std::ifstream f(p, std::ios::binary|std::ios::ate); if(!f) return {}; auto n=(size_t)f.tellg(); std::vector<uint8_t> b(n); f.seekg(0); f.read((char*)b.data(), n); return b; }

int main(int argc, char** argv){
#ifdef PSX5_ENABLE_QT_GUI
    // Check for GUI mode (default) vs command line mode
    bool useGui = true;
    for(int i=1;i<argc;++i) {
        if(std::string(argv[i])=="--nogui" || std::string(argv[i])=="--cli") {
            useGui = false;
            break;
        }
    }
    
    if (useGui) {
        QApplication app(argc, argv);
        app.setApplicationName("PSX5 Emulator");
        app.setApplicationVersion("1.0");
        app.setOrganizationName("PSX5");
        app.setOrganizationDomain("psx5.dev");
        
        MainWindow window;
        window.show();
        
        return app.exec();
    }
#endif
    
    // Original command line interface
    bool nogui = false;
    for(int i=1;i<argc;++i) if(std::string(argv[i])=="--nogui") nogui=true;
    log::set_level(log::Level::Info);
    if(argc < 2){ std::cout<<"Usage: "<<argv[0]<<" <path-to-blob> [base-addr] [--nogui|--cli]\n"; return 1; }
    auto bytes = read_file(argv[1]); if(bytes.empty()){ std::cerr<<"Failed to read "<<argv[1]<<"\n"; return 2; }
    uint64_t base = 0x1000; if(argc>=3) base = std::stoull(argv[2], nullptr, 0);
    Emulator emu(1<<24);
    if(!emu.load_module(bytes, base)){ std::cerr<<"load_module failed\n"; return 3; }
    Debugger dbg(emu);
    dbg.repl();
    return 0;
}
