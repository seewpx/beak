/*
 Copyright (C) 2017 Fredrik Öhrström

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

#include "filesystem.h"

#include "log.h"

#include <assert.h>
#include <map>

using namespace std;

static ComponentId FILESYSTEM = registerLogComponent("filesystem");

struct FileSystemFuseAPIImplementation : FileSystem
{
    bool readdir(Path *p, vector<Path*> *vec);
    ssize_t pread(Path *p, char *buf, size_t count, off_t offset);
    void recurse(function<void(Path *p)> cb);
    bool stat(Path *p, FileStat *fs);
    Path *mkTempDir(string prefix);
    Path *mkDir(Path *p, string name);

    FileSystemFuseAPIImplementation(FuseAPI *api);
    private:

    FuseAPI *api_;
};

unique_ptr<FileSystem> newFileSystem(FuseAPI *api)
{
    return unique_ptr<FileSystem>(new FileSystemFuseAPIImplementation(api));
}

FileSystemFuseAPIImplementation::FileSystemFuseAPIImplementation(FuseAPI *api)
{
    api_ = api;
}

static void filler(char *buf, char *path, void *x, int y)
{
}

bool FileSystemFuseAPIImplementation::readdir(Path *p, vector<Path*> *vec)
{
    //char buf[2048];
    //api_->readdirCB(p->c_str(), buf, filler, 0, 0);
    return true;
}

ssize_t FileSystemFuseAPIImplementation::pread(Path *p, char *buf, size_t size, off_t offset)
{
    //int rc = api_->readCB(p->c_str(), char *buf, size_t size, off_t offset, 0);
    return 0; // rc;
}

void FileSystemFuseAPIImplementation::recurse(function<void(Path *p)> cb)
{
}

bool FileSystemFuseAPIImplementation::stat(Path *p, FileStat *fs)
{
    return false;
}

Path *FileSystemFuseAPIImplementation::mkTempDir(string prefix)
{
    return NULL;
}

Path *FileSystemFuseAPIImplementation::mkDir(Path *p, string name)
{
    return NULL;
}

size_t basepos(string &s)
{
    return s.find_last_of('/');
}

string basename_(string &s)
{
    if (s.length() == 0 || s == "")
    {
        return "";
    }
    size_t e = s.length() - 1;
    if (s[e] == '/')
    {
        e--;
    }
    size_t p = s.find_last_of('/', e);
    return s.substr(p + 1, e - p + 1);
}

/**
 * dirname_("/a") return "" ie the root
 * dirname_("/a/") return "" ie the root
 * dirname_("/a/b") return "/a"
 * dirname_("/a/b/") return "/a"
 * dirname_("a/b") returns "a"
 * dirname_("a/b/") returns "a"
 * dirname_("") returns NULL
 * dirname_("/") returns NULL
 * dirname_("a") returns NULL
 * dirname_("a/") returns NULL
 *
 * For winapi, there is always a hidden root below the drive letter.
 * I.e. the drive letter is the first subdirectory.
 * dirname_("Z:") return "" ie the root
 * dirname_("Z:/") return "" ie the root
 * dirname_("Z:/b") returns "Z:"
 * dirname_("Z:/b/c") returns "Z:/b"
 * dirname_("/Z:/") not valid string, but still returns "" ie the root
 */
static pair<string, bool> dirname_(string &s)
{
    // Drop trailing slashes!
    if (s.length() > 0 && s.back() == '/')
    {
        s = s.substr(0, s.length() - 1);
    }
    if (s.length() == 0)
    {
        return pair<string, bool>("", false);
    }
    size_t p = s.find_last_of('/');
    if (p == string::npos) {
        #ifdef PLATFORM_WINAPI
        if (s.length()==2 && s[1] == ':' && ( (s[0]>='A' && s[0]<='Z') || (s[0]>='a' && s[0]<='z')))
        {
            // This was a drive letter. Insert an implicit root above it!
            return pair<string, bool>("", true);
        }
        #endif
        return pair<string, bool>("", false);
    }
    if (p == 0) {
        return pair<string, bool>("", true);
    }
    return pair<string, bool>(s.substr(0, p), true);
}

#define NO_ANSWER 0
#define YES_LESS_THAN 1
#define YES_GREATER_THAN 2

static int compareSameLengthPaths(Path *a, Path *b)
{
    if (a == b)
    {
        return NO_ANSWER;
    }
    assert(a->depth() == b->depth());
    int rc = compareSameLengthPaths(a->parent(), b->parent());

    if (rc == NO_ANSWER)
    {
        if (a->name() == b->name())
        {
            return NO_ANSWER;
        }
        if (Atom::lessthan(a->name(), b->name()))
        {
            return YES_LESS_THAN;
        }
        return YES_GREATER_THAN;
    }
    return rc;
}

bool depthFirstSortPath::lessthan(Path *a, Path *b)
{
    if (a == b)
    {
        return false;
    }
    if (a->depth() > b->depth())
    {
        return true;
    }
    if (a->depth() < b->depth())
    {
        return false;
    }

    bool rc = compareSameLengthPaths(a, b) == YES_LESS_THAN;
    return rc;
}

/**
 Special path comparison operator that sorts file names and directories in this order:
 This is the default order for tar files, the directory comes first,
 then subdirs, then content, then hard links.
 TEXTS/filter
 TEXTS/filter/alfa
 TEXTS/filter.zip
 */
bool TarSort::lessthan(Path *a, Path *b)
{
    if (a == b) {
        // Same path!
        return false;
    }
    int d = min(a->depth(), b->depth());
    Path *ap = a->parentAtDepth(d);
    Path *bp = b->parentAtDepth(d);
    if (ap == bp) {
        // Identical stem, one is simply deeper.
        if (a->depth() < b->depth()) {
            return true;
        }
        return false;
    }
    // Stem is not identical, compare the contents.
    return compareSameLengthPaths(ap, bp) == YES_LESS_THAN;
}

unsigned djb_hash(const char *key, int len)
{
    const unsigned char *p = reinterpret_cast<const unsigned char*>(key);
    unsigned h = 0;
    int i;

    for (i = 0; i < len; i++)
    {
        h = 33 * h + p[i];
    }

    return h;
}

uint32_t jenkins_one_at_a_time_hash(char *key, size_t len)
{
    uint32_t hash, i;
    for (hash = i = 0; i < len; ++i)
    {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

uint32_t hashString(string a)
{
    return djb_hash(a.c_str(), a.length());
}

static map<string, Atom*> interned_atoms;

Atom *Atom::lookup(string n)
{
    auto l = interned_atoms.find(n);
    if (l != interned_atoms.end())
    {
        return l->second;
    }
    Atom *na = new Atom(n);
    interned_atoms[n] = na;
    return na;
}

bool Atom::lessthan(Atom *a, Atom *b)
{
    if (a == b)
    {
        return 0;
    }
    // We are not interested in any particular locale dependent sort order here,
    // byte-wise is good enough for the map keys.
    int rc = strcmp(a->literal_.c_str(), b->literal_.c_str());
    return rc < 0;
}

static map<string, Path*> interned_paths;

Path *Path::lookup(string p)
{
    assert(p.back() != '\n' && (p.back() != 0 || p.length() == 0));
    #ifdef PLATFORM_WINAPI
    char *c = &p[0];
    while (c < &p[p.length()]) {
        if (*c == '\\') {
            *c = '/';
        }
        c++;
    }
    #endif
    if (p.back() == '/')
    {
        p = p.substr(0, p.length() - 1);
    }
    auto pl = interned_paths.find(p);
    if (pl != interned_paths.end())
    {
        return pl->second;
    }
    auto s = dirname_(p);
    if (s.second)
    {
        Path *parent = lookup(s.first);
        Path *np = new Path(parent, Atom::lookup(basename_(p)), p);
        interned_paths[p] = np;
        return np;
    }
    Path *np = new Path(NULL, Atom::lookup(basename_(p)), p);
    interned_paths[p] = np;
    return np;
}

Path *Path::lookupRoot()
{
    return lookup("");
}

deque<Path*> Path::nodes()
{
    deque<Path*> v;
    Path *p = this;
    while (p)
    {
        v.push_front(p);
        p = p->parent();
    }
    return v;
}

Path *Path::appendName(Atom *n) {
    string s = str()+"/"+n->str();
    return lookup(s);
}

Path *Path::parentAtDepth(int i)
{
    int d = depth_;
    Path *p = this;
    assert(d >= i);
    while (d > i && p) {
        p = p->parent_;
        d--;
    }
    return p;
}

/*
string &Path::str()
{
    if (path_cache_)
    {
        return string(path_cache_);
    }

    string rs;
    int i = 0;
    auto v = nodes();
    for (auto p : v)
    {
        if (i > 0)
            rs += "/";
        rs += p->name()->literal();
        i++;
    }
    path_cache_ = new char[rs.length() + 1];
    memcpy(path_cache_, rs.c_str(), rs.length() + 1);
    path_cache_len_ = rs.length();

    return rs;
    }*/

Path *Path::reparent(Path *parent)
{
    string s = parent->str()+"/"+atom_->str();
    return new Path(parent, atom_, s);
}

Path* Path::subpath(int from, int len)
{
    if (len == 0)
    {
        return NULL;
    }
    string rs;
    auto v = nodes();
    int i = 0, to = v.size();
    if (len != -1)
    {
        to = from + len;
    }
    for (auto p : v)
    {
        if (i >= from && i < to)
        {
            if (i > from)
                rs += "/";
            rs += p->name()->str();
        }
        i++;
    }
    return lookup(rs);
}

Path* Path::prepend(Path *p)
{
    string concat;
    if (str().front() == '/') {
        concat = p->str() + str();
    } else {
        concat = p->str() + "/" + str();
    }
    Path *pa = lookup(concat);
    return pa;
}

Path* Path::append(string p)
{
    string concat;
    printf("CONCAT >%s< >%s<\n", str().c_str(), p.c_str());
    if (p.front() == '/') {
        concat = str() + p;
    } else {
        concat = str() + "/" + p;
    }
    Path *pa = lookup(concat);
    return pa;
}

Path* Path::commonPrefix(Path *a, Path *b)
{
    auto av = a->nodes();
    auto bv = b->nodes();
    auto ai = av.begin();
    auto bi = bv.begin();
    int i = 0;

    while (ai != av.end() && bi != bv.end() && (*ai)->name() == (*bi)->name())
    {
        i++;
        ai++;
        bi++;
    }
    return a->subpath(0, i);
}

Path::Initializer::Initializer()
{
    Atom *root = Atom::lookup("");
    string s = string("");
    interned_paths[""] = new Path(NULL, root, s);
}

Path::Initializer Path::initializer_s;

/*
bool Path::splitInto(size_t name_len, Path **name, size_t prefix_len, Path **prefx)
{
    char *s = c_str();
    char *e = c_str()+c_str_len();

    while (e > s && *e != '/') {
	e--;
    }
    size_t nlen = c_str_len() - (e-s);
    // Ouch, filename did not fit in the name field...
    if (nlen > name_len) return false;

    while (e > s) {
    }
}
*/

ssize_t readlink(const char *path, char *dest, size_t len)
{
    return -1;
}

string permissionString(FileStat *fs)
{
    string s;

    if (fs->isDirectory()) s.append("d");
    else if (fs->isSymbolicLink()) s.append("l");
    else if (fs->isCharacterDevice()) s.append("c");
    else if (fs->isBlockDevice()) s.append("b");
    else if (fs->isFIFO()) s.append("p");
    else if (fs->isSOCK()) s.append("s");
    else
    {
        assert(fs->isRegularFile());
        s.append("-");
    }
    if (fs->isIRUSR()) s.append("r");
    else s.append("-");
    if (fs->isIWUSR()) s.append("w");
    else s.append("-");

    if (fs->isISUID()) s.append("s");
    else {
        if (fs->isIXUSR()) s.append("x");
        else               s.append("-");
    }
    if (fs->isIRGRP()) s.append("r");
    else               s.append("-");

    if (fs->isIWGRP()) s.append("w");
    else               s.append("-");

    if (fs->isISGID()) s.append("s");
    else {
        if (fs->isIXGRP()) s.append("x");
        else               s.append("-");
    }
    if (fs->isIROTH()) s.append("r");
    else               s.append("-");

    if (fs->isIWOTH()) s.append("w");
    else               s.append("-");

    if (fs->isISVTX()) s.append("t");
    else {
        if (fs->isIXOTH()) s.append("x");
        else               s.append("-");
    }
    return s;
}

mode_t stringToPermission(string s)
{
    mode_t rc = 0;

    if (s[0] == 'd')
        rc |= S_IFDIR;
    else if (s[0] == 'l')
        rc |= S_IFLNK;
    else if (s[0] == 'c')
        rc |= S_IFCHR;
    else if (s[0] == 'b')
        rc |= S_IFBLK;
    else if (s[0] == 'p')
        rc |= S_IFIFO;
    else if (s[0] == 's')
        rc |= S_IFSOCK;
    else if (s[0] == '-')
        rc |= S_IFREG;
    else
        goto err;

    if (s[1] == 'r')
        rc |= S_IRUSR;
    else if (s[1] != '-')
        goto err;
    if (s[2] == 'w')
        rc |= S_IWUSR;
    else if (s[2] != '-')
        goto err;

    if (s[3] == 'x')
        rc |= S_IXUSR;
    else
    if (s[3] == 's') {
        rc |= S_IXUSR;
        rc |= S_ISUID;
    }
    else if (s[3] != '-')
        goto err;

    if (s[4] == 'r')
        rc |= S_IRGRP;
    else if (s[4] != '-')
        goto err;
    if (s[5] == 'w')
        rc |= S_IWGRP;
    else if (s[5] != '-')
        goto err;
    if (s[6] == 'x')
        rc |= S_IXGRP;
    else
    if (s[6] == 's') {
        rc |= S_IXGRP;
        rc |= S_ISGID;
    }
    else if (s[6] != '-')
        goto err;

    if (s[7] == 'r')
        rc |= S_IROTH;
    else if (s[7] != '-')
        goto err;
    if (s[8] == 'w')
        rc |= S_IWOTH;
    else if (s[8] != '-')
        goto err;
    if (s[9] == 'x')
        rc |= S_IXOTH;
    else
    if (s[9] == 't') {
        rc |= S_IXOTH;
        rc |= S_ISVTX;
    }
    else if (s[9] != '-')
        goto err;

    return rc;

    err:

    return 0;
}


void FileStat::loadFrom(const struct stat *sb)
{
    memset(this, 0,sizeof(FileStat));
    st_ino = sb->st_ino;
    st_mode = sb->st_mode;
    st_nlink = sb->st_nlink;
    st_uid = sb->st_uid;
    st_gid = sb->st_gid;
    st_rdev = sb->st_rdev;
    st_size = sb->st_size;
#if HAS_ST_MTIM
    st_atim = sb->st_atim;
    st_mtim = sb->st_mtim;
    st_ctim = sb->st_ctim;
#elif HAS_ST_MTIME
    st_atim.tv_sec = sb->st_atime;
    st_mtim.tv_sec = sb->st_mtime;
    st_ctim.tv_sec = sb->st_ctime;
    st_atim.tv_nsec = 0;
    st_mtim.tv_nsec = 0;
    st_ctim.tv_nsec = 0;
#else
#error HAS_ST_MTIM or HAS_ST_TIME must be true!
#endif
}

void FileStat::storeIn(struct stat *sb)
{
    memset(sb, 0, sizeof(struct stat));
    sb->st_ino = st_ino;
    sb->st_mode = st_mode;
    sb->st_nlink = st_nlink;
    sb->st_uid = st_uid;
    sb->st_gid = st_gid;
    sb->st_rdev = st_rdev;
    sb->st_size = st_size;
#if HAS_ST_MTIM
    sb->st_atim = st_atim;
    sb->st_mtim = st_mtim;
    sb->st_ctim = st_ctim;
#elif HAS_ST_MTIME
    sb->st_atime = st_atim.tv_sec;
    sb->st_mtime = st_mtim.tv_sec;
    sb->st_ctime = st_ctim.tv_sec;
#else
#error HAS_ST_MTIM or HAS_ST_TIME must be true!
#endif
}

bool Path::mkdir()
{
    if (parent() && parent()->str().length() > 0) {
        bool rc = parent()->mkdir();
        if (!rc) return false;
    }
    #ifdef PLATFORM_WINAPI
    if (isDrive()) return true;
    #endif
    return makeDirHelper(c_str());
}