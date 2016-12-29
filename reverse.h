/*  
    Copyright (C) 2016 Fredrik Öhrström

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

#ifndef REVERSE_H
#define REVERSE_H

#include<assert.h>

#include"defs.h"
#include"tarfile.h"
#include"tarentry.h"
#include"libtar.h"
#include"util.h"

#include<fuse.h>
#include<regex.h>

#include<map>
#include<string>
#include<vector>

using namespace std;

struct Taz {
    TAR *tar;

    Taz(TAR *tar_) : tar(tar_) { }
    Taz() { }
};

struct Entry {
    mode_t mode_bits;
    time_t secs, nanos;
    size_t size, offset;
    string pathandfile;
    string file;
    string path;
    string tar;
    vector<Entry*> dir;
    bool loaded;
    string symlink;
    
    bool isLnk() { return (bool)S_ISLNK(mode_bits); }
    bool isDir() { return (bool)S_ISDIR(mode_bits); }

    Entry(mode_t m, size_t s, size_t o, string f) : mode_bits(m), size(s), offset(o) {
        if (f.length() > 1 && f.back()=='/') {
            f = f.substr(0,f.length()-1);
        }
        pathandfile = f;
        file = basename(f);
        path = dirname(f);
        if (path.length() > 1 && path.back() == '/') {
            path = path.substr(0,path.length()-1);
        }
        loaded = false;
    }
    
    Entry() { }
};

struct ReverseTarredFS {
    pthread_mutex_t global;
    
    string root_dir;
    string mount_dir;
    
    map<string,Entry> entries;
    map<string,Taz> tazs;
    
    int getattrCB(const char *path, struct stat *stbuf);
    int readdirCB(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi);
    int readCB(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
    int readlinkCB(const char *path, char *buf, size_t s);
    
    int parseTarredfsContent(vector<char> &v, string taz_path);
    void loadTaz(string taz, string path);
    void loadCache(string path);

    ReverseTarredFS() {
        mode_t m = S_IFDIR | S_IRUSR | S_IXUSR;            
        entries["/"] = Entry(m,0,0,"/");
    }
};

#endif