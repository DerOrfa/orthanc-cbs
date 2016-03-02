// kate: space-indent on; replace-tabs on; tab-indents off; indent-width 2; indent-mode cstyle;
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

#include "TarStreamWriter.h"
#include <boost/filesystem/fstream.hpp>
#include "../../Core/Logging.h"
#include <stdio.h>

namespace fs=boost::filesystem;

TarStreamWriter::TarStreamWriter(std::string cmd):root("/tmp")
{
  cmd=std::string("cd ") + root.native() + " \&\& "+cmd;
  f=popen(cmd.c_str(), "w" );
  if(f){
      LOG(INFO) << "Opened a storing pipe with \"" << cmd << "\"";
  } else {
      LOG(ERROR) << "Failed to open a storing pipe with \"" << cmd << "\"";
  }
}
TarStreamWriter::~TarStreamWriter()
{
  pclose(f);//we cannot handle errors, because other waitpids will screw ours up
  for(std::set<boost::filesystem::path>::iterator i=files.begin();i!=files.end();i++){
    boost::system::error_code ec;
    fs::remove(*i,ec);
    if(ec)
      LOG(WARNING) << "Failed to remove " << *i << ":" << ec.message();
  }
}

void TarStreamWriter::OpenDirectory(const std::string& name)
{
  currentDir/=name;
  boost::system::error_code ec;
  fs::create_directory(root/currentDir,ec);
  if(ec){
    LOG(ERROR) << "Failed to create " << root/currentDir << ":" << ec.message();
  }
}
void TarStreamWriter::CloseDirectory()
{
  currentDir=currentDir.parent_path();
}
void TarStreamWriter::AddFile(const std::string& filename, const std::string& content)
{
  fs::path file = currentDir/filename;
  {
    fs::ofstream stream(root/file);
    stream << content;
  }
  files.insert(root/file);
  fputs((file.native()+"\n").c_str(),f);
}
