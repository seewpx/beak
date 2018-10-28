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

#include "tarfile.h"

#include <openssl/sha.h>
#include <string.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <iterator>
#include <stdlib.h>

#include "log.h"
#include "tar.h"
#include "tarentry.h"
#include "util.h"

using namespace std;

ComponentId TARFILE = registerLogComponent("tarfile");
ComponentId HASHING = registerLogComponent("hashing");

TarFile::TarFile(TarContents tc, TarEntry *te)
{
    size_ = 0;
    tar_contents_ = tc;
    tar_entry_ = te;
    memset(&mtim_, 0, sizeof(mtim_));
    disk_update = NoUpdate;
    num_parts_ = 1;
}

void TarFile::addEntryLast(TarEntry *entry)
{
    entry->updateMtim(&mtim_);

    entry->registerTarFile(this, current_tar_offset_);
    contents_[current_tar_offset_] = entry;
    offsets.push_back(current_tar_offset_);
    debug(TARFILE, "%s: added %s at %zu\n", "GURKA",
          entry->path()->c_str(), current_tar_offset_);
    current_tar_offset_ += entry->blockedSize();
}

void TarFile::addEntryFirst(TarEntry *entry)
{
    entry->updateMtim(&mtim_);

    entry->registerTarFile(this, 0);
    map<size_t, TarEntry*> newc;
    vector<size_t> newo;

    newc[0] = entry;
    newo.push_back(0);
    entry->registerTarFile(this, 0);

    for (auto & a : contents_)
    {
        size_t o = a.first + entry->blockedSize();
        newc[o] = a.second;
        newo.push_back(o);
        a.second->registerTarFile(this, o);
    }
    contents_ = newc;
    offsets = newo;

    debug(TARFILE, "    %s    Added FIRST %s at %zu with blocked size %zu\n",
          "GURKA", entry->path()->c_str(), current_tar_offset_,
          entry->blockedSize());
    current_tar_offset_ += entry->blockedSize();
}

pair<TarEntry*, size_t> TarFile::findTarEntry(size_t offset)
{
    if (offset > size_)
    {
        return pair<TarEntry*, size_t>(NULL, 0);
    }
    debug(TARFILE, "Looking for offset %zu\n", offset);
    size_t o = 0;

    vector<size_t>::iterator i = lower_bound(offsets.begin(), offsets.end(),
                                             offset, less_equal<size_t>());

    if (i == offsets.end())
    {
        o = *offsets.rbegin();
    }
    else
    {
        i--;
        o = *i;
    }
    TarEntry *te = contents_[o];

    debug(TARFILE, "Found it %s\n", te->path()->c_str());
    return pair<TarEntry*, size_t>(te, o);
}

void TarFile::calculateHash()
{
    calculateSHA256Hash();
}

void TarFile::calculateHash(vector<pair<TarFile*,TarEntry*>> &tars, string &content)
{
    calculateSHA256Hash(tars, content);
}

vector<char> &TarFile::hash() {
    return sha256_hash_;
}

void TarFile::calculateSHA256Hash()
{
    SHA256_CTX sha256ctx;
    SHA256_Init(&sha256ctx);

    for (auto & a : contents_)
    {
        TarEntry *te = a.second;
        SHA256_Update(&sha256ctx, &te->hash()[0], te->hash().size());
    }
    sha256_hash_.resize(SHA256_DIGEST_LENGTH);
    SHA256_Final((unsigned char*)&sha256_hash_[0], &sha256ctx);
}

void TarFile::calculateSHA256Hash(vector<pair<TarFile*,TarEntry*>> &tars, string &content)
{
    SHA256_CTX sha256ctx;
    SHA256_Init(&sha256ctx);

    // SHA256 all other tar and gz file hashes! This is the hash of this state!
    for (auto & p : tars)
    {
        TarFile *tf = p.first;
        if (tf == this) continue;

        SHA256_Update(&sha256ctx, &tf->hash()[0], tf->hash().size());
    }

    // SHA256 the detailed file listing too!
    SHA256_Update(&sha256ctx, &content[0], content.length());

    sha256_hash_.resize(SHA256_DIGEST_LENGTH);
    SHA256_Final((unsigned char*)&sha256_hash_[0], &sha256ctx);
}

void TarFile::updateMtim(struct timespec *mtim) {
    if (isInTheFuture(&mtim_)) {
        fprintf(stderr, "Virtual tarfile %s has a future timestamp! Ignoring the timstamp.\n",
                "PATHHERE");
    } else {
        if (mtim_.tv_sec > mtim->tv_sec ||
            (mtim_.tv_sec == mtim->tv_sec && mtim_.tv_nsec > mtim->tv_nsec)) {
            memcpy(mtim, &mtim_, sizeof(*mtim));
        }
    }
}

TarFileName::TarFileName(TarFile *tf, uint partnr)
{
    type = tf->type();
    version = 2;
    sec = tf->mtim()->tv_sec;
    nsec = tf->mtim()->tv_nsec;
    size = tf->size(partnr);
    header_hash = toHex(tf->hash());
    part_nr = partnr;
    num_parts = tf->numParts();
}

bool TarFileName::isIndexFile(Path *p)
{
    // Example file name:
    // foo/bar/dir/z01_(001501080787).(579054757)_(0)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(0).gz

    size_t len = p->name()->str().length();
    if (len < 20) return false;
    const char *s = p->name()->c_str();

    return 0==strncmp(s, "z01_", 3) && 0==strncmp(s+len-3, ".gz", 3);
}

bool TarFileName::parseFileName(string &name, string *dir)
{
    bool k;
    // Example file name:
    // foo/bar/dir/(l)01_(001501080787).(579054757)_(1119232)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(0fe).(tar)
    // or
    // foo/bar/dir/(l)02_(001501080787).(579054757)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(0fe-1ff)_(1119232).(tar)

    if (name.size() == 0) return false;

    size_t p0 = name.rfind('/');
    if (p0 == string::npos) { p0=0; } else { p0++; }

    if (dir) {
        *dir = name.substr(0, p0);
    }
    k = typeFromChar(name[p0], &type);
    if (!k) return false;

    size_t p1 = name.find('_', p0); if (p1 == string::npos) return false;
    string versions;
    k = digitsOnly(&name[p0+1], p1-p0-1, &versions);
    if (!k) return false;
    version = atoi(versions.c_str());

    if (version == 1) {
        return parseFileNameVersion1_(name, p0, p1);
    } else if (version == 2) {
        return parseFileNameVersion2_(name, p0, p1);
    } else {
        error(TARFILE, "Unsupported beak file version. %s\n", name.c_str());
        assert(0);
    }
    return false;
}

bool TarFileName::parseFileNameVersion1_(string &name, size_t p0, size_t p1)
{
    bool k;
    size_t p2 = name.find('.', p1+1); if (p2 == string::npos) return false;
    size_t p3 = name.find('_', p2+1); if (p3 == string::npos) return false;
    size_t p4 = name.find('_', p3+1); if (p4 == string::npos) return false;
    size_t p5 = name.find('_', p4+1); if (p5 == string::npos) return false;
    size_t p6 = name.find('.', p5+1); if (p6 == string::npos) return false;

    string secss;
    k = digitsOnly(&name[p1+1], p2-p1-1, &secss);
    if (!k) return false;
    sec = atol(secss.c_str());

    string nsecss;
    k = digitsOnly(&name[p2+1], p3-p2-1, &nsecss);
    if (!k) return false;
    nsec = atol(nsecss.c_str());

    string sizes;
    k = digitsOnly(&name[p3+1], p4-p3-1, &sizes);
    if (!k) return false;
    size = atol(sizes.c_str());

    k = hexDigitsOnly(&name[p4+1], p5-p4-1, &header_hash);
    if (!k) return false;

    string partnrs;
    k = hexDigitsOnly(&name[p5+1], p6-p5-1, &partnrs);
    if (!k) return false;
    part_nr = strtol(partnrs.c_str(), NULL, 16);

    string suffix = name.substr(p6+1);
    if (suffixtype(type) != suffix) {
        return false;
    }

    return true;
}

bool TarFileName::parseFileNameVersion2_(string &name, size_t p0, size_t p1)
{
    // foo/bar/dir/(l)02_(001501080787).(579054757)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(0fe-1ff)_(1119232).(tar)
    bool k;
    size_t p2 = name.find('.', p1+1); if (p2 == string::npos) return false;
    size_t p3 = name.find('_', p2+1); if (p3 == string::npos) return false;
    size_t p4 = name.find('_', p3+1); if (p4 == string::npos) return false;
    size_t p5 = name.find('-', p4+1); if (p5 == string::npos) return false;
    size_t p6 = name.find('_', p5+1); if (p6 == string::npos) return false;
    size_t p7 = name.find('.', p6+1); if (p7 == string::npos) return false;

    string secss;
    k = digitsOnly(&name[p1+1], p2-p1-1, &secss);
    if (!k) return false;
    sec = atol(secss.c_str());

    string nsecss;
    k = digitsOnly(&name[p2+1], p3-p2-1, &nsecss);
    if (!k) return false;
    nsec = atol(nsecss.c_str());

    k = hexDigitsOnly(&name[p3+1], p4-p3-1, &header_hash);
    if (!k) return false;

    string partnrs;
    k = hexDigitsOnly(&name[p4+1], p5-p4-1, &partnrs);
    if (!k) return false;
    part_nr = strtol(partnrs.c_str(), NULL, 16);

    string numparts;
    k = hexDigitsOnly(&name[p4+1], p6-p5-1, &numparts);
    if (!k) return false;
    num_parts = strtol(numparts.c_str(), NULL, 16);

    string sizes;
    k = digitsOnly(&name[p6+1], p7-p6-1, &sizes);
    if (!k) return false;
    size = atol(sizes.c_str());

    string suffix = name.substr(p7+1);
    if (suffixtype(type) != suffix) {
        return false;
    }

    return true;
}

void TarFileName::writeTarFileNameIntoBuffer(char *buf, size_t buf_len, Path *dir)
{
    if (version == 1) {
        writeTarFileNameIntoBufferVersion1_(buf, buf_len, dir);
    } else if (version == 2) {
        writeTarFileNameIntoBufferVersion2_(buf, buf_len, dir);
    } else {
        assert(0);
    }
}

void TarFileName::writeTarFileNameIntoBufferVersion1_(char *buf, size_t buf_len, Path *dir)
{
    // dirprefix/(l)01_(1501080787).(579054757)_(1119232)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(0).(tar)
    char sizes[32];
    memset(sizes, 0, sizeof(sizes));
    snprintf(sizes, 32, "%zu", size);

    char secs_and_nanos[32];
    memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
    snprintf(secs_and_nanos, 32, "%012" PRINTF_TIME_T "u.%09lu", sec, nsec);

    const char *suffix = suffixtype(type);

    if (dir == NULL) {
        snprintf(buf, buf_len, "%c01_%s_%s_%s_%d.%s",
                 TarFileName::chartype(type),
                 secs_and_nanos,
                 sizes,
                 header_hash.c_str(),
                 0, // version 1 cannot handle parts
                 suffix);
    } else {
        snprintf(buf, buf_len, "%s/%c01_%s_%s_%s_%d.%s",
                 dir->c_str(),
                 TarFileName::chartype(type),
                 secs_and_nanos,
                 sizes,
                 header_hash.c_str(),
                 0, // version 1 cannot handle parts
                 suffix);
    }
}

void TarFileName::writeTarFileNameIntoBufferVersion2_(char *buf, size_t buf_len, Path *dir)
{
    // dirprefix/(l)02_(1501080787).(579054757)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(07)_(1119232).(tar)
    char sizes[32];
    memset(sizes, 0, sizeof(sizes));
    snprintf(sizes, 32, "%zu", size);

    char secs_and_nanos[32];
    memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
    snprintf(secs_and_nanos, 32, "%" PRINTF_TIME_T "u.%09lu", sec, nsec);

    string partnr = toHex(part_nr, num_parts);
    const char *suffix = suffixtype(type);

    if (dir == NULL) {
        snprintf(buf, buf_len, "%c02_%s_%s_%s-%x_%s.%s",
                 TarFileName::chartype(type),
                 secs_and_nanos,
                 header_hash.c_str(),
                 partnr.c_str(),
                 num_parts,
                 sizes,
                 suffix);
    } else {
        snprintf(buf, buf_len, "%s/%c02_%s_%s_%s-%x_%s.%s",
                 dir->c_str(),
                 TarFileName::chartype(type),
                 secs_and_nanos,
                 header_hash.c_str(),
                 partnr.c_str(),
                 num_parts,
                 sizes,
                 suffix);
    }
}

string TarFileName::asStringWithDir(Path *dir) {
    char buf[1024];
    writeTarFileNameIntoBuffer(buf, sizeof(buf), dir);
    return buf;
}

Path *TarFileName::asPathWithDir(Path *dir)
{
    char buf[1024];
    writeTarFileNameIntoBuffer(buf, sizeof(buf), dir);
    return Path::lookup(buf);
}

size_t TarFile::copy(char *buf, size_t bufsize, off_t offset, FileSystem *fs, uint partnr)
{
    size_t copied = 0;

    if (offset < 0) return 0;
    size_t from = (size_t)offset;
    if (from >= size(partnr)) return 0;

    while (bufsize>0)
    {
        //fprintf(stderr, "partnr=%u buf=%p from=%zu bufsize=%zu headersize=%zu\n", partnr, buf, from, bufsize, header_size_);
        if (partnr > 0 && from < header_size_)
        {
            debug(TARFILE, "Copying max %zu from %zu, now inside header (header size=%ju)\n",
                  bufsize, from,
                  header_size_);

            char tmp[header_size_];
            memset(tmp, 0, header_size_);
            int p = 0;
            TarHeader th;

            // Deal with very long path names.
            /*
            if (th.numLongPathBlocks() > 0)
               {
               TarHeader lph;
               lph.setLongPathType(&th);
               lph.setSize(tarpath_->c_str_len()+1);
               lph.calculateChecksum();

               memcpy(tmp+p, lph.buf(), T_BLOCKSIZE);
               memcpy(tmp+p+T_BLOCKSIZE, tarpath_->c_str(), tarpath_->c_str_len());
               p += th.numLongPathBlocks()*T_BLOCKSIZE;
               debug(TARENTRY, "Wrote long path header for %s\n", tarpath_->c_str());
               }
            */

            assert(tar_entry_ != NULL);

            size_t file_offset = calculateOriginTarOffset(partnr, header_size_);
            assert(file_offset > tar_entry_->headerSize());
            file_offset -= tar_entry_->headerSize();

            assert(tar_entry_->tarpath()->str().length() < 100);

            th.setMultivolType(tar_entry_->tarpath()->c_str(), file_offset);
            th.setSize(tar_entry_->stat()->st_size-file_offset);
            th.calculateChecksum();

            memcpy(tmp+p, th.buf(), T_BLOCKSIZE);


            // Copy the header out
            size_t len = header_size_-from;
            if (len > bufsize) {
                len = bufsize;
            }
            debug(TARFILE, "multivol header out from %zu size=%zu\n", from, len);
            assert(from+len <= header_size_);
            memcpy(buf, tmp+from, len);
            bufsize -= len;
            buf += len;
            copied += len;
            from += len;
        } else {
            size_t origin_from = calculateOriginTarOffset(partnr, from);
            pair<TarEntry*,size_t> r = findTarEntry(origin_from);
            TarEntry *te = r.first;
            size_t tar_offset = r.second;
            assert(te != NULL);
            size_t len = te->copy(buf, bufsize, origin_from - tar_offset, fs);
            debug(TARFILE, "copy size=%ju result=%ju\n", bufsize, len);
            bufsize -= len;
            buf += len;
            copied += len;
            from += len;
            // No more tar entries...
            if (len==0) break;
        }
    }

    return copied;
}

bool TarFile::createFile(Path *file, FileStat *stat, uint partnr,
                         FileSystem *src_fs, FileSystem *dst_fs, size_t off,
                         function<void(size_t)> update_progress)
{
    dst_fs->createFile(file, stat, [this,file,src_fs,off,update_progress,partnr] (off_t offset, char *buffer, size_t len) {
            debug(TARFILE,"Write %ju bytes to file %s\n", len, file->c_str());
            size_t n = copy(buffer, len, off+offset, src_fs, partnr);
            debug(TARFILE, "Wrote %ju bytes from %ju to %ju.\n", n, off+offset, offset);
            update_progress(n);
            return n;
        });
    return true;
}

void splitParts_(size_t file_size,
                 size_t split_size,
                 TarHeaderStyle ths,
                 uint *num_parts,
                 size_t *part_size,
                 size_t *last_part_size,
                 size_t *mv_header_size)
{
    // The size_ is already rounded up to the nearest 512 byte block.
    *part_size = split_size;
    if (ths == None) {
        *mv_header_size = 0;
    } else {
        // Multivol header size
        *mv_header_size = 512;
    }
    assert(*part_size > *mv_header_size);
    // To make the multivol parts the exact same size (except the last)
    // take into account that there is no multivol header in the first
    // part, therefore the space for the multivol header in the first part
    // is instead used for the tarentry content (that has its own header).
    // Which is why we subtract the header_size from the total size before
    // dividing with what can be stored in each part.
    // For example: Total size of tarentry = 13, header size = 1, split size = 5
    // 13 content c fits exactly in three tar splits a size 5. H = multivol header
    // [c c c c c] [H c c c c] [H c c c c]
    // 3 = (13-1)/(5-1) = 12/4 = 3 and 5+(3-1)*(5-1) == 13 => perfect fit
    //
    // Whereas total size of tarentry = 14, header size = 1, split size = 5
    // 14 content c fits in four tar splits a size 5. H = multivol header
    // [c c c c c] [H c c c c] [H c c c c] [H c]
    // 3 = (14-1)/(5-1) = 13/4 = 3 but 5+(3-1)*(5-1) == 13 => not equals 14
    // we need another multivol part, which will have size 1+14-13

    *num_parts = (file_size - *mv_header_size) / (*part_size - *mv_header_size);
    size_t stores = *part_size + (*num_parts -1)*(*part_size - *mv_header_size);
    if (stores == file_size)
    {
        *last_part_size = *part_size;
        debug(TARFILE, "Splitting file into same sized parts %u parts partsize=%zu lastpartsize=%zu\n",
              *num_parts, *part_size, *last_part_size);
    }
    else
    {
        // The size was not a multiple of what can be stored in the parts.
        // We need an extra final part.
        (*num_parts)++;
        *last_part_size = *mv_header_size + file_size - stores;
        size_t stored_in_first_part = *part_size;
        size_t stored_in_middle_parts = *part_size - *mv_header_size;
        size_t stored_in_last_part = file_size - stored_in_first_part - (*num_parts-2)*(stored_in_middle_parts);
        assert(*last_part_size == stored_in_last_part+*mv_header_size);
        debug(TARFILE, "Splitting file with tarentry size %zu into %u parts partsize=%zu lastpartsize=%zu "
              "with first=%zu middles=%zu last=%zu.\n",
              file_size,
              *num_parts, *part_size, *last_part_size,
              stored_in_first_part, stored_in_middle_parts, stored_in_last_part);
    }
}

void TarFile::fixSize(size_t split_size, TarHeaderStyle ths)
{
    size_ = current_tar_offset_;
    if (size_ <= split_size) {
        // No splitting needed.
        num_parts_ = 1;
        part_size_ = size_;
        header_size_ = 0;
        return;
    }

    splitParts_(size_,
                split_size,
                ths,
                &num_parts_,
                &part_size_,
                &last_part_size_,
                &header_size_);
}
size_t TarFile::size(uint partnr)
{
    assert(partnr < num_parts_);
    if (num_parts_ == 1) {
        return size_;
    }
    if (partnr < num_parts_-1) {
        return part_size_;
    }
    // This is the last part, it can be shorter than part_size_.
    return last_part_size_;
}

size_t TarFile::calculateOriginTarOffset(uint partnr, size_t offset)
{
    assert(partnr < num_parts_);
    if (partnr == 0) {
        // Easy, this first part has the same offset, since there is no multivol header here.
        return offset;
    }
    // For all other parts, we might have a multivol header in this part, drop that
    // If the offset points into the multivol header, then fail hard! We cannot
    // calcualte the origin offset when we are looking inside the multivol header!
    assert(offset >= header_size_);
    // Given file with size 14. split size 5 and header size 1.
    // For examble partnr=2 and offset = 3 should give origin offset 11
    // Part 0      Part 1      Part 2      Part 3
    // [c c c c c] [H c c c c] [H c c c c] [H c]
    // Remove header from offset 3 => 2
    offset -= header_size_;

    // Now add the first part size, and the content (part_size_-header_size_) for
    // each multivol part before this part.
    // origin offset = 2 + 5 + (2-1)*(5-1) = 3+5+1*4 = 12
    return offset + part_size_ + (partnr-1)*(part_size_-header_size_);
}
