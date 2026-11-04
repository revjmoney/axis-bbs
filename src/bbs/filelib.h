// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What the BBS knows about file areas: the files in an area and a way to read a text file.
// The board reads its SD card (/sd/files/<id>/); tests use a pretend store.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace axis::bbs {

struct FileEntry {
    std::string name;
    uint32_t    size = 0;
    std::string desc;  // from the area's FILES.BBS
    uint32_t    date = 0;  // unix time it landed (0 = unknown)
};

// A file opened for reading anywhere in it (a ZIP's directory sits at its end).
class RandomFile {
public:
    virtual uint32_t size() const = 0;
    virtual bool     readAt(uint32_t offset, uint8_t* buf, size_t n) = 0;
    virtual ~RandomFile() = default;
};

// A new file being written; nothing is visible until finish(true).
class WriteFile {
public:
    virtual bool write(const uint8_t* p, size_t n) = 0;
    virtual bool finish(bool keep) = 0;  // keep: make it part of the area; else throw it away
    virtual ~WriteFile() = default;
};

class FileLibrary {
public:
    virtual std::vector<FileEntry> list(const std::string& areaId) = 0;
    // up to `max` bytes of a file; false if it isn't there
    virtual bool read(const std::string& areaId, const std::string& name, std::string& out, size_t max) = 0;
    // nullptr if it isn't there. The default reads the whole file into memory (fine for tests).
    virtual std::unique_ptr<RandomFile> open(const std::string& areaId, const std::string& name);
    // uploads: nullptr / false where the store is read-only
    virtual std::unique_ptr<WriteFile> create(const std::string&, const std::string&) { return nullptr; }
    virtual bool describe(const std::string&, const std::string&, const std::string&) { return false; }  // add to FILES.BBS
    virtual bool remove(const std::string&, const std::string&) { return false; }
    virtual bool rename(const std::string&, const std::string& /*from*/, const std::string& /*to: must not exist*/) { return false; }
    virtual ~FileLibrary() = default;
};

// An area listing: files in FILES.BBS order with their descriptions ("NAME.EXT  description" per
// line), then any other files in the folder. Names match in any case and keep the folder's spelling;
// FILES.BBS itself and described files that are gone are left out.
std::vector<FileEntry> mergeFilesBbs(const std::string& filesBbs,
                                     const std::vector<std::pair<std::string, uint32_t>>& folder);

std::string fileSizeText(uint32_t bytes);  // "512", "48K", "21.0M"
bool        isTextFile(const std::string& name);  // .TXT .NFO .DIZ .ANS .ASC .BBS .DOC .MD .1ST

}  // namespace axis::bbs
