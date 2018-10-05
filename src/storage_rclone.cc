/*
 Copyright (C) 2018 Fredrik Öhrström

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "storage_rclone.h"

#include "log.h"
#include "statistics.h"

using namespace std;

static ComponentId RCLONE = registerLogComponent("rclone");

RC rcloneListBeakFiles(Storage *storage,
                       vector<TarFileName> *files,
                       vector<TarFileName> *bad_files,
                       vector<string> *other_files,
                       map<Path*,FileStat> *contents,
                       ptr<System> sys)
{
    assert(storage->type == RCloneStorage);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;

    args.push_back("ls");
    args.push_back(storage->storage_location->c_str());
    rc = sys->invoke("rclone", args, &out);

    if (rc.isErr()) return RC::ERR;

    auto i = out.begin();
    bool eof = false, err = false;

    for (;;) {
	// Example line:
	// 12288 z01_001506595429.268937346_0_7eb62d8e0097d5eaa99f332536236e6ba9dbfeccf0df715ec96363f8ddd495b6_0.gz
        eatWhitespace(out, i, &eof);
        if (eof) break;
        string size = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        string file_name = eatTo(out, i, '\n', 4096, &eof, &err);
        if (err) break;
        TarFileName tfn;
        bool ok = tfn.parseFileName(file_name);
        // Only files that have proper beakfs names are included.
        if (ok) {
            // Check that the remote size equals the content. If there is a mismatch,
            // then for sure the file must be overwritte/updated. Perhaps there was an earlier
            // transfer interruption....
            size_t siz = (size_t)atol(size.c_str());
            if ( (tfn.type != REG_FILE && tfn.size == siz) ||
                 (tfn.type == REG_FILE && tfn.size == 0) )
            {
                files->push_back(tfn);
                Path *p = tfn.path->prepend(storage->storage_location);
                FileStat fs;
                fs.st_size = (off_t)siz;
                fs.st_mtim.tv_sec = tfn.secs;
                fs.st_mtim.tv_nsec = tfn.nsecs;
                fs.st_mode |= S_IRUSR;
                fs.st_mode |= S_IFREG;
                (*contents)[p] = fs;
            }
            else
            {
                bad_files->push_back(tfn);
            }
        } else {
            other_files->push_back(file_name);
        }
    }
    if (err) return RC::ERR;

    return RC::OK;
}


RC rcloneFetchFiles(Storage *storage,
                    vector<Path*> *files,
                    Path *dir,
                    System *sys,
                    FileSystem *local_fs)
{
    Path *target_dir = storage->storage_location->prepend(dir);

    string files_to_fetch;
    for (auto& p : *files) {
        // Rclone does not like to have a leading /, we have to remove it.
        Path *n = p->subpath(1);
        files_to_fetch.append(n->str());
        files_to_fetch.append("\n");
    }

    Path *tmp = local_fs->mkTempFile("beak_fetching_", files_to_fetch);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    args.push_back("copy");
    args.push_back("--include-from");
    args.push_back(tmp->c_str());
    args.push_back(storage->storage_location->c_str());
    args.push_back(target_dir->c_str());
    rc = sys->invoke("rclone", args, &out);

    local_fs->deleteFile(tmp);

    return rc;
}

void parse_rclone_verbose_output(StoreStatistics *st,
                                 Storage *storage,
                                 char *buf,
                                 size_t len)
{
    // Parse verbose output and look for:
    // 2018/01/29 20:05:36 INFO  : code/src/s01_001517180913.689221661_11659264_b6f526ca4e988180fe6289213a338ab5a4926f7189dfb9dddff5a30ab50fc7f3_0.tar: Copied (new)

    size_t from, to;
    // Find the beginning of the file path.
    for (from=1; from<len-1; ++from) {
        if (buf[from-1] == ' ' && buf[from] == ':' && buf[from+1] == ' ') {
            from = from+2;
            break;
        }
    }
    // Find the end of the file path.
    for (to=len-2; to>from; --to) {
        if (buf[to] == ':' && buf[to+1] == ' ') {
            break;
        }
    }
    string file = storage->storage_location->str()+"/"+string(buf+from, to-from);
    TarFileName tfn;
    if (tfn.parseFileName(file)) {
        Path *path = tfn.path;
        size_t size = 0;

        debug(RCLONE, "copied: %ju \"%s\"\n", st->stats.file_sizes.count(path), path->c_str());

        if (st->stats.file_sizes.count(path)) {
            size = st->stats.file_sizes[path];
            st->stats.size_files_stored += size;
            st->stats.num_files_stored++;
            st->updateProgress();
        }
    }
}

RC rcloneSendFiles(Storage *storage,
                   vector<Path*> *files,
                   Path *dir,
                   StoreStatistics *st,
                   FileSystem *local_fs,
                   ptr<System> sys)
{
    string files_to_fetch;
    for (auto& p : *files) {
        Path *n = p->subpath(1); // storage->storage_location->depth());
        fprintf(stderr, "FROM %s TO %s\n", p->c_str(), n->c_str());
        files_to_fetch.append(n->str());
        files_to_fetch.append("\n");
    }
    Path *tmp = local_fs->mkTempFile("beak_sending_", files_to_fetch);

    vector<string> args;
    args.push_back("copy");
    args.push_back("-v");
    args.push_back("--include-from");
    args.push_back(tmp->c_str());
    args.push_back(dir->c_str());
    args.push_back(storage->storage_location->str());
    vector<char> output;
    RC rc = sys->invoke("rclone", args, &output, CaptureBoth,
                        [&st, storage](char *buf, size_t len) {
                            parse_rclone_verbose_output(st,
                                                        storage,
                                                        buf,
                                                        len);
                        });

    local_fs->deleteFile(tmp);

    return rc;
}
