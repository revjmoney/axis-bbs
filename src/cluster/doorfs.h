// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// DOS doors on a board without the door files (a cluster follower, a headless node). The door runs
// on the follower; its file system is a RemoteHostFs that sends every call (open, read, write,
// seek, find, ...) to the main's DoorFsServer, which serves the real door folder on the SD card.
// Record locks go to the main's LockTable too (RemoteLockTable), so the same door played on two
// boards at once keeps its files whole. Requests ride the cluster link like the file areas'
// (remotefiles.h): "H", an operation letter, fields split by 0x1F (a write's data last, whole);
// replies are '1' or '0', then data.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "door/hostfs.h"

namespace axis::cluster {

class DoorFsServer {
public:
    using Ask = std::function<bool(const std::string& request, std::string& reply)>;
    using Open = std::function<std::unique_ptr<door::HostFs>(const std::string& doorId)>;

    DoorFsServer(Open open, door::LockTable& locks) : open_(std::move(open)), locks_(locks) {}
    std::string handle(const std::string& request);  // request without the leading 'H'... or with it
    void        drop();  // the follower went away: its files close, its locks come free

private:
    Open                          open_;
    door::LockTable&              locks_;
    std::unique_ptr<door::HostFs> fs_;
    std::string                   door_;
    std::vector<int>              owners_;  // nodes that locked through here
};

class RemoteHostFs : public door::HostFs {
public:
    RemoteHostFs(DoorFsServer::Ask ask, std::string doorId) : ask_(std::move(ask)), door_(std::move(doorId)) {}
    int         open(const std::string& p, int access, bool create, bool truncate, bool exclusive = false) override;
    int         read(int h, void* buf, int n) override;
    int         write(int h, const void* buf, int n) override;
    int64_t     seek(int h, int64_t off, int whence) override;
    int         close(int h) override;
    bool        stat(const std::string& p, door::FileInfo& out) override;
    int         remove(const std::string& p) override;
    int         rename(const std::string& from, const std::string& to) override;
    int         mkdir(const std::string& p) override;
    int         rmdir(const std::string& p) override;
    bool        isDir(const std::string& p) override;
    bool        find(const std::string& pattern, uint8_t attr, door::FindState& st) override;
    int         fileKey(int h) override;
    bool        fileTime(int h, uint16_t& dosDate, uint16_t& dosTime) override;
    void        setCwd(const std::string& dosDir) override { cwd_ = dosDir; }
    std::string cwd() const override { return cwd_; }
    void        dropCaches();  // someone may have changed the files (a record lock was taken)

private:
    // Each handle keeps its own position (seeks cost nothing) and the last 2 KB read from it, so a
    // door reading a byte or a line at a time asks the main once per 2 KB, not once per call.
    struct Handle {
        int64_t     pos = 0, cacheAt = -1;
        std::string cache;
    };
    std::map<int, Handle> handles_;
    bool        call(std::initializer_list<std::string> fields, std::string& data);
    long long   number(std::initializer_list<std::string> fields, long long fail);  // a call that answers a number
    DoorFsServer::Ask ask_;
    std::string       door_, cwd_;
    bool              begun_ = false;
};

class RemoteLockTable : public door::LockTable {
public:
    // fs: its cached reads are dropped whenever a lock is taken (the records may have changed)
    explicit RemoteLockTable(DoorFsServer::Ask ask, RemoteHostFs* fs = nullptr) : ask_(std::move(ask)), fs_(fs) {}
    bool lock(int key, uint32_t off, uint32_t len, int owner) override;
    bool unlock(int key, uint32_t off, uint32_t len, int owner) override;
    void releaseOwner(int owner) override;

private:
    DoorFsServer::Ask ask_;
    RemoteHostFs*     fs_;
};

// LORD for another node: its NODE1.DAT with the drop file moved to C:\NODEn\ (so nodes on several
// boards never share one DOOR.SYS).
std::string lordNodeFile(const std::string& node1Dat, int node);
// a door's command tail with %N made the node number ("%N /DREW" -> "3 /DREW")
std::string doorArgs(const std::string& args, int node);

}  // namespace axis::cluster
