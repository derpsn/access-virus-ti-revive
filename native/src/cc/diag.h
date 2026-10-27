// diag.h - Control Center "Nerd mode", part 1: TI Revive's logs and debug switches without a terminal.
// Only local files and the broker's log are touched; nothing is sent to the Virus.
#pragma once
#include <string>
#include <vector>

namespace vti::cc {

// The plug-in's folder (diag.on / diag.log live there): Windows %LOCALAPPDATA%\virus-ti-native, macOS
// ~/Library/Application Support/virus-ti-native, Linux ~/.local/share/virus-ti-native.
std::string plugin_dir();
std::string plugin_log_path();

// The plug-in writes diag.log while diag.on exists (checked when the plug-in starts).
bool plugin_log_enabled();
bool set_plugin_log(bool on, std::string &error);

// The options of nerd mode (A/B tests against the original): a flag file each in plugin_dir(), read when a plug-in
// instance starts. `name` is what tirevive-cc takes, `label` what the Control Center shows.
struct PluginOption {
    const char *name, *file, *label;
};
const std::vector<PluginOption> &plugin_options();
bool plugin_option(const std::string &name);                                  // false for an unknown name
bool set_plugin_option(const std::string &name, bool on, std::string &error); // refuses an unknown name

// Where the broker logs: a file on Windows (%TEMP%\vtibroker.log) and macOS (/tmp/tirevive-vtibroker.log), the user
// journal on Linux (systemd unit tirevive-broker). For display.
std::string broker_log_location();

// The last `n` lines of a text file / of the broker's log (empty if there is none).
std::vector<std::string> tail_file(const std::string &path, size_t n);
std::vector<std::string> broker_log_tail(size_t n);

// Replaces the home folder with "~" and the user name with "<user>" (logs end up in public bug reports).
std::string anonymize(const std::string &text);

// One text file for a bug report: system, TI Revive version, the end of the plug-in and broker logs, anonymized.
bool collect_logs(const std::string &out_path, const std::string &version, std::string &error);

} // namespace vti::cc
