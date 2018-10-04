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

#include "always.h"

#include "configuration.h"
#include "statistics.h"
#include "system.h"
#include "tarfile.h"

#include <map>
#include <string>
#include <vector>

RC rcloneListBeakFiles(Storage *storage,
                       std::vector<TarFileName> *files,
                       std::vector<TarFileName> *bad_files,
                       std::vector<std::string> *other_files,
                       std::map<Path*,FileStat> *contents,
                       ptr<System> sys);

RC rcloneFetchFiles(Storage *storage,
                    std::vector<Path*> *files,
                    Path *dir,
                    System *sys,
                    FileSystem *local_fs);

RC rcloneSendFiles(Storage *storage,
                   std::vector<Path*> *files,
                   Path *dir,
                   StoreStatistics *st,
                   FileSystem *local_fs,
                   ptr<System> sys);
