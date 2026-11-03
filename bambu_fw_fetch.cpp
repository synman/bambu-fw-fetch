// bambu_fw_fetch.cpp
// Calls bambu_network_get_printer_firmware (and get_user_print_info) via
// libbambu_networking.dylib, reusing the cached BambuStudio session token.
//
// Usage:
//   ./bambu_fw_fetch <DEV_ID>   -- fetch firmware for a specific device
//   ./bambu_fw_fetch --info      -- fetch user print info (no device required)
//
// Prerequisites:
//   - BambuStudio installed at /Applications/BambuStudio.app
//   - User is currently logged in via BambuStudio (token is cached)

#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <iostream>
#include <string>

// ── function pointer typedefs (from NetworkAgent.hpp) ──────────────────────

typedef void* (*func_create_agent)(std::string log_dir);
typedef int   (*func_destroy_agent)(void* agent);
typedef int   (*func_set_config_dir)(void* agent, std::string config_dir);
typedef int   (*func_init_log)(void* agent);
typedef int   (*func_set_cert_file)(void* agent, std::string folder, std::string filename);
typedef int   (*func_set_country_code)(void* agent, std::string country_code);
typedef int   (*func_start)(void* agent);
typedef bool  (*func_is_user_login)(void* agent);
typedef int   (*func_get_user_print_info)(void* agent,
                                          unsigned* http_code, std::string* http_body);
typedef int   (*func_get_printer_firmware)(void* agent, std::string dev_id,
                                            unsigned* http_code, std::string* http_body);

// ── helpers ─────────────────────────────────────────────────────────────────

static std::string home() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}

template<typename T>
static T require_sym(void* lib, const char* name) {
    auto sym = reinterpret_cast<T>(dlsym(lib, name));
    if (!sym) {
        std::cerr << "ERROR: symbol not found: " << name << "\n";
        std::exit(1);
    }
    return sym;
}

// ── main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <DEV_ID>\n";
        std::cerr << "       " << argv[0] << " --info\n";
        std::cerr << "  DEV_ID: printer serial number (e.g. 0948AD5B2700913)\n";
        std::cerr << "  --info: call get_user_print_info (no device binding required)\n";
        return 1;
    }
    const std::string arg    = argv[1];
    const bool info_mode     = (arg == "--info");
    const std::string dev_id = info_mode ? "" : arg;

    // ── paths ────────────────────────────────────────────────────────────────
    const std::string lib_path  = home() + "/Library/Application Support/"
                                  "BambuStudio/plugins/libbambu_networking.dylib";
    const std::string config_dir = home() + "/Library/Application Support/BambuStudio";
    const std::string cert_dir   = "/Applications/BambuStudio.app/Contents/Resources/cert";
    const std::string cert_file  = "slicer_base64.cer";
    const std::string log_dir    = "/tmp/bambu_fw_fetch_log";
    const std::string country    = "US";

    // ── load library ─────────────────────────────────────────────────────────
    void* lib = dlopen(lib_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!lib) {
        std::cerr << "ERROR: cannot load library: " << dlerror() << "\n";
        return 1;
    }

    // ── resolve symbols ───────────────────────────────────────────────────────
    auto fn_create   = require_sym<func_create_agent>       (lib, "bambu_network_create_agent");
    auto fn_destroy  = require_sym<func_destroy_agent>      (lib, "bambu_network_destroy_agent");
    auto fn_cfg_dir  = require_sym<func_set_config_dir>     (lib, "bambu_network_set_config_dir");
    auto fn_init_log = require_sym<func_init_log>           (lib, "bambu_network_init_log");
    auto fn_cert     = require_sym<func_set_cert_file>      (lib, "bambu_network_set_cert_file");
    auto fn_country  = require_sym<func_set_country_code>   (lib, "bambu_network_set_country_code");
    auto fn_start    = require_sym<func_start>              (lib, "bambu_network_start");
    auto fn_login_q   = require_sym<func_is_user_login>      (lib, "bambu_network_is_user_login");
    auto fn_print_info = require_sym<func_get_user_print_info>(lib, "bambu_network_get_user_print_info");
    auto fn_firmware  = require_sym<func_get_printer_firmware>(lib, "bambu_network_get_printer_firmware");

    // ── init sequence (mirrors GUI_App.cpp:3484-3501) ────────────────────────
    // The library creates a log/ subdirectory inside log_dir; ensure it exists.
    mkdir(log_dir.c_str(), 0755);
    mkdir((log_dir + "/log").c_str(), 0755);

    void* agent = fn_create(log_dir);
    if (!agent) {
        std::cerr << "ERROR: bambu_network_create_agent returned null\n";
        return 1;
    }

    fn_cfg_dir(agent, config_dir);
    fn_init_log(agent);
    fn_cert(agent, cert_dir, cert_file);
    fn_country(agent, country);
    fn_start(agent);

    // Allow background init threads to settle
    sleep(1);

    // ── check login ───────────────────────────────────────────────────────────
    if (!fn_login_q(agent)) {
        std::cerr << "ERROR: not logged in.\n"
                  << "  Open BambuStudio, log in, then re-run this tool.\n";
        fn_destroy(agent);
        return 1;
    }

    // ── fetch ─────────────────────────────────────────────────────────────────
    unsigned http_code = 0;
    std::string http_body;
    int ret;

    if (info_mode) {
        std::cerr << "Fetching user print info ...\n";
        ret = fn_print_info(agent, &http_code, &http_body);
    } else {
        std::cerr << "Fetching firmware info for device: " << dev_id << " ...\n";
        ret = fn_firmware(agent, dev_id, &http_code, &http_body);
    }

    if (ret != 0) {
        std::cerr << "ERROR: API call returned " << ret << "\n";
        fn_destroy(agent);
        return 1;
    }

    std::cerr << "HTTP " << http_code << "\n";
    std::cout << http_body << "\n";

    fn_destroy(agent);
    return (http_code == 200) ? 0 : 1;
}
