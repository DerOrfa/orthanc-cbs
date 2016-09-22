// kate: space-indent on; replace-tabs on; tab-indents off; indent-width 2; indent-mode cstyle;
/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2016  <copyright holder> <email>
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

#ifndef SHADOWWRITER_H
#define SHADOWWRITER_H

#include <boost/filesystem.hpp>
#include "FileInfo.h"

class ShadowWriter
{
  boost::filesystem::path currentDir,m_shadow_root,m_storage_root;
  bool symlink=false;
  size_t instances=0,skipped=0;
public:
  ShadowWriter(boost::filesystem::path shadow_root, boost::filesystem::path storage_root);
  void CloseDirectory();
  void OpenDirectory(const std::string &name);
  void AddFile(const Orthanc::FileInfo& dicom,std::string target_name);
};

#endif // SHADOWWRITER_H
