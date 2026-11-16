// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Changed files and mod packages. The editor keeps the original of every file it overwrites as <file>.bak. A mod package
// is a ZIP with the files that differ from their backups (KB\<n>.vga, TORE\<name>); applying one writes the files into a
// game folder (with .bak copies of what it replaces). No user interface in here.
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "formats/binary.hpp"

namespace em::editor {

struct ChangedFile {
    std::string path;  // relative with forward slashes, e.g. "KB/80.vga"
    size_t size = 0;
};

// Files in KB\ and TORE\ whose .bak differs from the file
std::vector<ChangedFile> changed_files(const std::filesystem::path& data);
// Puts the original (.bak) back; the .bak stays
bool restore_file(const std::filesystem::path& data, const std::string& rel);

using ZipEntries = std::vector<std::pair<std::string, Bytes>>;
Bytes make_zip(const ZipEntries& entries);  // stored (no compression)
ZipEntries read_zip(const Bytes& zip);      // stored and deflated entries; throws FormatError otherwise
// "KB/80.vga" or "TORE/2.ht": the only paths a package may contain
bool allowed_mod_path(const std::string& path);

// Writes the package; returns the number of files (0 = nothing changed, no file written)
size_t create_modpack(const std::filesystem::path& data, const std::filesystem::path& zip, std::string* error = nullptr);
// Extracts the package into the game folder; returns the number of files written (-1 on error)
int apply_modpack(const std::filesystem::path& data, const std::filesystem::path& zip, std::string* error = nullptr);

}  // namespace em::editor
