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

#include "ShadowWriter.h"

#include <boost/filesystem/fstream.hpp>
#include "../../Core/Logging.h"
#include <stdio.h>

namespace fs=boost::filesystem;

ShadowWriter::ShadowWriter(boost::filesystem::path shadow_root, boost::filesystem::path storage_root)
:m_shadow_root(shadow_root), m_storage_root(storage_root){}

void ShadowWriter::OpenDirectory(const std::string& name)
{
  currentDir/=name;
  boost::system::error_code ec;
  fs::create_directory(m_shadow_root/currentDir,ec);
  fs::permissions(m_shadow_root/currentDir, fs::add_perms|fs::group_read|fs::group_exe|fs::others_read|fs::others_exe);
  if(ec){
    LOG(ERROR) << "Failed to create " << m_shadow_root/currentDir << ":" << ec.message();
  }
}
void ShadowWriter::CloseDirectory()
{
  currentDir=currentDir.parent_path();
}
void ShadowWriter::AddFile(const Orthanc::FileInfo& dicom, std::string target_name)
{
  const std::string uuid=dicom.GetUuid();
  fs::path path = m_storage_root / std::string(&uuid[0], &uuid[2]) / std::string(&uuid[2], &uuid[4]) / uuid;
  boost::system::error_code ec;
  if(fs::exists(path)){
    const fs::path p=m_shadow_root/currentDir/target_name;
    if(fs::exists(p)){
      skipped++;
      return;
    }
    
    if(!symlink){
      fs::create_hard_link(path,p,ec);
      if(ec==boost::system::errc::cross_device_link){ // cant do hardlink, try again with symlink
        symlink=true;
        AddFile(dicom,target_name);
        return;
      } 
    } else 
      fs::create_symlink(fs::canonical(path),p,ec);

    if(ec) // error handling for both
      LOG(ERROR) << "Failed to link " << path << " to " << p << ":" << ec.message();
    else{
      instances++;
      fs::permissions(p, fs::add_perms|fs::group_read|fs::others_read);
    }
    
  } else
    LOG(ERROR) << path.native() << " does not exist";
}
