/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2016  Enrico Reimer <reimer@cbs.mpg.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef TARSTREAMWRITER_H
#define TARSTREAMWRITER_H

#include <string>
#include <set>
#include <boost/filesystem.hpp>

class TarStreamWriter
{
  FILE *f;
  boost::filesystem::path currentDir,root;
  std::set<boost::filesystem::path> files;
public:
  TarStreamWriter(std::string command);
  ~TarStreamWriter();
  void CloseDirectory();
  void OpenDirectory(const std::string &name);
  void AddFile(const std::string &filename,const std::string &content);
};

#endif // TARSTREAMWRITER_H
