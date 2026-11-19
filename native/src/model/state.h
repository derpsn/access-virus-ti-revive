// state.h - the plug-in's project state (the original's "MIDI" container of SysEx/MIDI records), its editor-only
// records and the meta parameter value mapping of editordevice.xml. Pure functions, unit tested (tests/).
#pragma once
#include "model/remotedevice.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vti {

// "MIDI" + u32 BE length, then records of u32 BE length + message; a truncated record ends the list
std::vector<std::vector<uint8_t>> parse_container(const std::vector<uint8_t> &d);
std::vector<uint8_t> build_container(const std::vector<std::vector<uint8_t>> &recs);
// a 524-byte single dump of `part` (F0 .. 10 00 <part>: edit buffer of that part)
bool is_part_dump(const std::vector<uint8_t> &m, int part);
// a truncated project lacks part dumps: each part without one gets the dump from `defaults` (the plug-in's default
// project), appended like the default keeps them after its multi; returns how many were added (audit #22)
int add_missing_part_dumps(std::vector<std::vector<uint8_t>> &recs, const std::vector<std::vector<uint8_t>> &defaults);
// values per part (-1: globals) rebuilt from the state: those the state does not carry (the System Clock the Virus
// reports, E445) are taken over from `prev`, else they fell back to defaults on screen at every patch load (audit #24)
void keep_unset_values(std::map<int, std::map<std::string, int>> &next,
                       const std::map<int, std::map<std::string, int>> &prev);
// E174: part dumps (edit buffers, bank byte 0) older than the current format are upgraded like the Virus does on
// receipt, so the editor shows and saves what the Virus plays; returns how many were upgraded
int upgrade_state_dumps(std::vector<std::vector<uint8_t>> &recs);
// E445: the globals System Clock/SA Freq, Frequency, Mode (0x0F, 0x10, 0x11, any part byte). Sent to the Virus they reach
// its USB chip, which can switch its audio clock behind the driver's back (E257); the rate belongs to the DAW's
// session, never to a project, and the original sends none of them (E327). So a project does not keep them
bool is_system_clock_global(const std::vector<uint8_t> &m);
// removes them from a project's records (a saved one may hold them), returns how many
int drop_system_clock_globals(std::vector<std::vector<uint8_t>> &recs);
// editor-only per-part settings (link buttons): F0 7D 'V' 'T' 'N' 01 <part> <path> 00 <value> F7
std::vector<uint8_t> editor_record(const std::string &path, int part, int value);
bool parse_editor_record(const std::vector<uint8_t> &m, std::string &path, int &part, int &value);
// meta value -> client value (maps first, else a * meta + b) and back (false: no map entry / not invertible)
int meta_to_client(const MetaClient &c, int mv);
bool client_to_meta(const MetaClient &c, int cv, int &mv);

} // namespace vti
