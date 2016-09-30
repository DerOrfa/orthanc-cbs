// kate: space-indent on; replace-tabs on; tab-indents off; indent-width 2; indent-mode cstyle;

/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2015 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * In addition, as a special exception, the copyright holders of this
 * program give permission to link the code of its release with the
 * OpenSSL project's "OpenSSL" library (or with modified versions of it
 * that use the same license as the "OpenSSL" library), and distribute
 * the linked executables. You must obey the GNU General Public License
 * in all respects for all of the code used other than "OpenSSL". If you
 * modify file(s) with this exception, you may extend this exception to
 * your version of the file(s), but you are not obligated to do so. If
 * you do not wish to do so, delete this exception statement from your
 * version. If you delete this exception statement from all source files
 * in the program, then also delete it here.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#include "../PrecompiledHeadersServer.h"
#include "OrthancRestApi.h"

#include "../DicomDirWriter.h"
#include "../../Core/FileStorage/StorageAccessor.h"
#include "../../Core/Compression/HierarchicalZipWriter.h"
#include "../../Core/Compression/TarStreamWriter.h"
#include "../../Core/FileStorage/ShadowWriter.h"
#include "../../Core/HttpServer/FilesystemHttpSender.h"
#include "../../Core/Logging.h"
#include "../../Core/Uuid.h"
#include "../../OrthancServer/OrthancInitialization.h"
#include "../ServerContext.h"


#include <stdio.h>

#if defined(_MSC_VER)
#define snprintf _snprintf
#endif

static const uint64_t MEGA_BYTES = 1024 * 1024;
static const uint64_t GIGA_BYTES = 1024 * 1024 * 1024;

namespace Orthanc
{
  // Download of ZIP files ----------------------------------------------------
 
  static bool IsZip64Required(uint64_t uncompressedSize,
                              unsigned int countInstances)
  {
    static const uint64_t  SAFETY_MARGIN = 64 * MEGA_BYTES;

    /**
     * Determine whether ZIP64 is required. Original ZIP format can
     * store up to 2GB of data (some implementation supporting up to
     * 4GB of data), and up to 65535 files.
     * https://en.wikipedia.org/wiki/Zip_(file_format)#ZIP64
     **/

    const bool isZip64 = (uncompressedSize >= 2 * GIGA_BYTES - SAFETY_MARGIN ||
                          countInstances >= 65535);

    LOG(INFO) << "Creating a ZIP file with " << countInstances << " files of size "
              << (uncompressedSize / MEGA_BYTES) << "MB using the "
              << (isZip64 ? "ZIP64" : "ZIP32") << " file format";

    return isZip64;
  }


  namespace
  {
    class ResourceIdentifiers
    {
    private:
      ResourceType   level_;
      std::string    patient_;
      std::string    study_;
      std::string    series_;
      std::string    instance_;

      static void GoToParent(ServerIndex& index,
                             std::string& current)
      {
        std::string tmp;

        if (index.LookupParent(tmp, current))
        {
          current = tmp;
        }
        else
        {
          throw OrthancException(ErrorCode_UnknownResource);
        }
      }


    public:
      ResourceIdentifiers(ServerIndex& index,
                          const std::string& publicId)
      {
        if (!index.LookupResourceType(level_, publicId))
        {
          throw OrthancException(ErrorCode_UnknownResource);
        }

        std::string current = publicId;;
        switch (level_)  // Do not add "break" below!
        {
          case ResourceType_Instance:
            instance_ = current;
            GoToParent(index, current);
            
          case ResourceType_Series:
            series_ = current;
            GoToParent(index, current);

          case ResourceType_Study:
            study_ = current;
            GoToParent(index, current);

          case ResourceType_Patient:
            patient_ = current;
            break;

          default:
            throw OrthancException(ErrorCode_InternalError);
        }
      }

      ResourceType GetLevel() const
      {
        return level_;
      }

      const std::string& GetIdentifier(ResourceType level) const
      {
        // Some sanity check to ensure enumerations are not altered
        assert(ResourceType_Patient < ResourceType_Study);
        assert(ResourceType_Study < ResourceType_Series);
        assert(ResourceType_Series < ResourceType_Instance);

        if (level > level_)
        {
          throw OrthancException(ErrorCode_InternalError);
        }

        switch (level)
        {
          case ResourceType_Patient:
            return patient_;

          case ResourceType_Study:
            return study_;

          case ResourceType_Series:
            return series_;

          case ResourceType_Instance:
            return instance_;

          default:
            throw OrthancException(ErrorCode_InternalError);
        }
      }
    };


    class IArchiveVisitor : public boost::noncopyable
    {
    public:
      virtual ~IArchiveVisitor()
      {
      }

      virtual void Open(ResourceType level,
                        const std::string& publicId) = 0;

      virtual void Close() = 0;

      virtual void AddInstance(const std::string& instanceId,
                               const FileInfo& dicom) = 0;
    };


    class ArchiveIndex
    {
    private:
      struct Instance
      {
        std::string  id_;
        FileInfo     dicom_;

        Instance(const std::string& id,
                 const FileInfo& dicom) : 
          id_(id), dicom_(dicom)
        {
        }
      };

      // A "NULL" value for ArchiveIndex indicates a non-expanded node
      typedef std::map<std::string, ArchiveIndex*>   Resources;

      ResourceType         level_;
      Resources            resources_;   // Only at patient/study/series level
      std::list<Instance>  instances_;   // Only at instance level


      void AddResourceToExpand(ServerIndex& index,
                               const std::string& id)
      {
        if (level_ == ResourceType_Instance)
        {
          FileInfo tmp;
          if (index.LookupAttachment(tmp, id, FileContentType_Dicom))
          {
            instances_.push_back(Instance(id, tmp));
          }
        }
        else
        {
          resources_[id] = NULL;
        }
      }


    public:
      ArchiveIndex(ResourceType level) :
        level_(level)
      {
      }

      ~ArchiveIndex()
      {
        for (Resources::iterator it = resources_.begin();
             it != resources_.end(); ++it)
        {
          delete it->second;
        }
      }


      void Add(ServerIndex& index,
               const ResourceIdentifiers& resource)
      {
        const std::string& id = resource.GetIdentifier(level_);
        Resources::iterator previous = resources_.find(id);

        if (level_ == ResourceType_Instance)
        {
          AddResourceToExpand(index, id);
        }
        else if (resource.GetLevel() == level_)
        {
          // Mark this resource for further expansion
          if (previous != resources_.end())
          {
            delete previous->second;
          }

          resources_[id] = NULL;
        }
        else if (previous == resources_.end())
        {
          // This is the first time we meet this resource
          std::auto_ptr<ArchiveIndex> child(new ArchiveIndex(GetChildResourceType(level_)));
          child->Add(index, resource);
          resources_[id] = child.release();
        }
        else if (previous->second != NULL)
        {
          previous->second->Add(index, resource);
        }
        else
        {
          // Nothing to do: This item is marked for further expansion
        }
      }


      void Expand(ServerIndex& index)
      {
        if (level_ == ResourceType_Instance)
        {
          // Expanding an instance node makes no sense
          return;
        }

        for (Resources::iterator it = resources_.begin();
             it != resources_.end(); ++it)
        {
          if (it->second == NULL)
          {
            // This is resource is marked for expansion
            std::list<std::string> children;
            index.GetChildren(children, it->first);

            std::auto_ptr<ArchiveIndex> child(new ArchiveIndex(GetChildResourceType(level_)));

            for (std::list<std::string>::const_iterator 
                   it2 = children.begin(); it2 != children.end(); ++it2)
            {
              child->AddResourceToExpand(index, *it2);
            }

            it->second = child.release();
          }

          assert(it->second != NULL);
          it->second->Expand(index);
        }        
      }


      void Apply(IArchiveVisitor& visitor) const
      {
        if (level_ == ResourceType_Instance)
        {
          for (std::list<Instance>::const_iterator 
                 it = instances_.begin(); it != instances_.end(); ++it)
          {
            visitor.AddInstance(it->id_, it->dicom_);
          }          
        }
        else
        {
          for (Resources::const_iterator it = resources_.begin();
               it != resources_.end(); ++it)
          {
            assert(it->second != NULL);  // There must have been a call to "Expand()"
            visitor.Open(level_, it->first);
            it->second->Apply(visitor);
            visitor.Close();
          }
        }
      }
    };


    class StatisticsVisitor : public IArchiveVisitor
    {
    private:
      uint64_t       size_;
      unsigned int   instances_;
      
    public:
      StatisticsVisitor() : size_(0), instances_(0)
      {
      }

      uint64_t GetUncompressedSize() const
      {
        return size_;
      }

      unsigned int GetInstancesCount() const
      {
        return instances_;
      }

      virtual void Open(ResourceType level,
                        const std::string& publicId)
      {
      }

      virtual void Close()
      {
      }

      virtual void AddInstance(const std::string& instanceId,
                               const FileInfo& dicom)
      {
        instances_ ++;
        size_ += dicom.GetUncompressedSize();
      }
    };


    class PrintVisitor : public IArchiveVisitor
    {
    private:
      std::ostream& out_;
      std::string   indent_;

    public:
      PrintVisitor(std::ostream& out) : out_(out)
      {
      }

      virtual void Open(ResourceType level,
                        const std::string& publicId)
      {
        switch (level)
        {
          case ResourceType_Patient:  indent_ = "";       break;
          case ResourceType_Study:    indent_ = "  ";     break;
          case ResourceType_Series:   indent_ = "    ";   break;
          default:
            throw OrthancException(ErrorCode_InternalError);
        }

        out_ << indent_ << publicId << std::endl;
      }

      virtual void Close()
      {
      }

      virtual void AddInstance(const std::string& instanceId,
                               const FileInfo& dicom)
      {
        out_ << "      " << instanceId << std::endl;
      }
    };


    class ArchiveWriterVisitor : public IArchiveVisitor
    {
    private:
      HierarchicalZipWriter&  writer_;
      ServerContext&            context_;
      char                    instanceFormat_[24];
      unsigned int            countInstances_;

      static std::string GetTag(const DicomMap& tags,
                                const DicomTag& tag)
      {
        const DicomValue* v = tags.TestAndGetValue(tag);
        if (v != NULL &&
            !v->IsBinary() &&
            !v->IsNull())
        {
          return v->GetContent();
        }
        else
        {
          return "";
        }
      }

    public:
      ArchiveWriterVisitor(HierarchicalZipWriter& writer,
                           ServerContext& context) :
        writer_(writer),
        context_(context),
        countInstances_(0)
      {
        snprintf(instanceFormat_, sizeof(instanceFormat_) - 1, "%%08d.dcm");
      }

      virtual void Open(ResourceType level,
                        const std::string& publicId)
      {
        std::string path;

        DicomMap tags;
        if (context_.GetIndex().GetMainDicomTags(tags, publicId, level, level))
        {
          switch (level)
          {
            case ResourceType_Patient:
              path = GetTag(tags, DICOM_TAG_PATIENT_ID);
              break;

            case ResourceType_Study:
              path = GetTag(tags, DICOM_TAG_ACCESSION_NUMBER) + " " + GetTag(tags, DICOM_TAG_STUDY_DESCRIPTION);
              break;

            case ResourceType_Series:
            {
              std::string modality = GetTag(tags, DICOM_TAG_MODALITY);
              path = modality + " " + GetTag(tags, DICOM_TAG_SERIES_DESCRIPTION);

              if (modality.size() == 0)
              {
                snprintf(instanceFormat_, sizeof(instanceFormat_) - 1, "%%08d.dcm");
              }
              else if (modality.size() == 1)
              {
                snprintf(instanceFormat_, sizeof(instanceFormat_) - 1, "%c%%07d.dcm", 
                         toupper(modality[0]));
              }
              else if (modality.size() >= 2)
              {
                snprintf(instanceFormat_, sizeof(instanceFormat_) - 1, "%c%c%%06d.dcm", 
                         toupper(modality[0]), toupper(modality[1]));
              }

              countInstances_ = 0;

              break;
            }

            default:
              throw OrthancException(ErrorCode_InternalError);
          }
        }

        path = Toolbox::StripSpaces(Toolbox::ConvertToAscii(path));

        if (path.empty())
        {
          path = std::string("Unknown ") + EnumerationToString(level);
        }

        writer_.OpenDirectory(path.c_str());
      }

      virtual void Close()
      {
        writer_.CloseDirectory();
      }

      virtual void AddInstance(const std::string& instanceId,
                               const FileInfo& dicom)
      {
        std::string content;
        context_.ReadFile(content, dicom);

        char filename[24];
        snprintf(filename, sizeof(filename) - 1, instanceFormat_, countInstances_);
        countInstances_ ++;

        writer_.OpenFile(filename);
        writer_.Write(content);
      }

      static void Apply(RestApiOutput& output,
                        ServerContext& context,
                        ArchiveIndex& archive,
                        const std::string& filename)
      {
        archive.Expand(context.GetIndex());

        StatisticsVisitor stats;
        archive.Apply(stats);

        const bool isZip64 = IsZip64Required(stats.GetUncompressedSize(), stats.GetInstancesCount());

        // Create a RAII for the temporary file to manage the ZIP file
        Toolbox::TemporaryFile tmp;

        {
          // Create a ZIP writer
          HierarchicalZipWriter writer(tmp.GetPath().c_str());
          writer.SetZip64(isZip64);

          ArchiveWriterVisitor v(writer, context);
          archive.Apply(v);
        }

        // Prepare the sending of the ZIP file
        FilesystemHttpSender sender(tmp.GetPath());
        sender.SetContentType("application/zip");
        sender.SetContentFilename(filename);

        // Send the ZIP
        output.AnswerStream(sender);

        // The temporary file is automatically removed thanks to the RAII
      }
    };

    
    class TarStreamWriterVisitor : public IArchiveVisitor
    {
    private:
      TarStreamWriter&  writer_;
      ServerContext&            context_;

      static std::string GetTag(const DicomMap& tags,
                                const DicomTag& tag)
      {
        const DicomValue* v = tags.TestAndGetValue(tag);
        if (v != NULL &&
            !v->IsBinary() &&
            !v->IsNull())
        {
          return v->GetContent();
        }
        else
        {
          return "";
        }
      }

    public:
      TarStreamWriterVisitor(TarStreamWriter& writer, ServerContext& context) :
        writer_(writer),
        context_(context)
      {}

      virtual void Open(ResourceType level, const std::string& publicId)
      {
        std::string path;

        DicomMap tags;
        if (context_.GetIndex().GetMainDicomTags(tags, publicId, level, level))
        {
          switch (level)
          {
            case ResourceType_Patient:
              path = GetTag(tags, DICOM_TAG_PATIENT_ID);
              break;

            case ResourceType_Study:
              path = GetTag(tags, DICOM_TAG_STUDY_DATE).substr(2)+"_"+GetTag(tags, DICOM_TAG_STUDY_TIME);
              break;

            case ResourceType_Series:
            {
              path = std::string("S")+ GetTag(tags,DicomTag(0x0020,0x0011)) + "_" + GetTag(tags, DICOM_TAG_SERIES_DESCRIPTION);
              break;
            }

            default:
              throw OrthancException(ErrorCode_InternalError);
          }
        }
        
        static const char forbidden[]="/ ";
        path = Toolbox::StripSpaces(Toolbox::ConvertToAscii(path));
        for(std::string::size_type found=path.find_first_of(forbidden);
            found!=std::string::npos;
            found=path.find_first_of(forbidden,found)
        ){
            path.replace(found,1,"_");
        }

        if (path.empty())
        {
          path = std::string("Unknown ") + EnumerationToString(level);
        }

        writer_.OpenDirectory(path.c_str());
      }

      virtual void Close()
      {
        writer_.CloseDirectory();
      }

      virtual void AddInstance(const std::string& instanceId, const FileInfo& dicom)
      {
        std::string content;
        context_.ReadFile(content, dicom);
        writer_.AddFile(dicom.GetUuid()+".ima",content);
      }

      static void Apply(RestApiOutput& output, ServerContext& context,
                        ArchiveIndex& archive, const std::string& filename)
      {
        
        const std::string placeholder="{}";
        std::string cmd=Configuration::GetGlobalStringParameter("tar-stream-command", "");
        for(std::string::size_type found=cmd.find(placeholder);
            found!=std::string::npos;
            found=cmd.find(placeholder)
        ){
            cmd.replace(found,placeholder.length(),filename);
        }


        archive.Expand(context.GetIndex());

        {
          // Create a TAR stream writer
          TarStreamWriter writer(cmd);
          TarStreamWriterVisitor v(writer, context);
		  // add all instances
          archive.Apply(v);
        }

        output.AnswerJson(Json::Value());
      }
    };

    class ShadowWriterVisitor : public IArchiveVisitor
    {
    private:
      ShadowWriter&  writer_;
      ServerContext&            context_;

      static std::string GetTag(const DicomMap& tags, const DicomTag& tag)
      {
        const DicomValue* v = tags.TestAndGetValue(tag);
        if (v != NULL && !v->IsBinary() && !v->IsNull())
        {
          return v->GetContent();
        }
        else
        {
          return "";
        }
      }


    public:
      ShadowWriterVisitor(ShadowWriter& writer, ServerContext& context) : writer_(writer), context_(context)
      {}

      virtual void Open(ResourceType level, const std::string& publicId)
      {
        std::string path;

        DicomMap tags;
        if (context_.GetIndex().GetMainDicomTags(tags, publicId, level, level))
        {
          switch (level)
          {
            case ResourceType_Patient:
              path = GetTag(tags, DICOM_TAG_PATIENT_ID);
              break;

            case ResourceType_Study:
              path = GetTag(tags, DICOM_TAG_STUDY_DATE).substr(2)+"_"+GetTag(tags, DICOM_TAG_STUDY_TIME).substr(0,6);
              break;

            case ResourceType_Series:
            {
              path = std::string("S")+ GetTag(tags,DicomTag(0x0020,0x0011)) + "_" + GetTag(tags, DICOM_TAG_SERIES_DESCRIPTION);
              break;
            }
            default:
              LOG(ERROR) << "level " << EnumerationToString(level) << " invalid, raising internal error";
              throw OrthancException(ErrorCode_InternalError);
          }
        }
        
        static const char forbidden[]="/ ";
        path = Toolbox::StripSpaces(Toolbox::ConvertToAscii(path));
        for(std::string::size_type found=path.find_first_of(forbidden);
            found!=std::string::npos;
            found=path.find_first_of(forbidden,found)
        ){
            path.replace(found,1,"_");
        }

        if (path.empty())
        {
          path = std::string("Unknown ") + EnumerationToString(level);
        }

        writer_.OpenDirectory(path.c_str());
      }

      virtual void Close()
      {
        writer_.CloseDirectory();
      }

      virtual void AddInstance(const std::string& instanceId, const FileInfo& dicom)
      {
        if(dicom.GetCompressionType()!=CompressionType_None){
          LOG(ERROR) << "instance " << instanceId << " is comressed, can't be shadowed, raising internal error";
          throw OrthancException(ErrorCode_InternalError);
        }
        
        if(dicom.GetContentType()!=FileContentType_Dicom){
          LOG(ERROR) << "instance " << instanceId << " is no dicom, can't be shadowed, raising internal error";
          throw OrthancException(ErrorCode_InternalError);
        }
        
        DicomMap tags;
        context_.GetIndex().GetMainDicomTags(tags, instanceId, ResourceType_Instance, ResourceType_Instance);
        
        writer_.AddFile(dicom,GetTag(tags,DICOM_TAG_SOP_INSTANCE_UID)+".ima");
      }

      static void Apply(RestApiOutput& output, ServerContext& context, ArchiveIndex& archive)
      {
        archive.Expand(context.GetIndex());
        const std::string storageDirectoryStr = Configuration::GetGlobalStringParameter("StorageDirectory", "OrthancStorage");
        const std::string shadowDirectoryStr = Configuration::GetGlobalStringParameter("shadow-root", "shadowStorage");
        Json::Value answer;

        {
          // Create a TAR stream writer
          ShadowWriter writer(shadowDirectoryStr,storageDirectoryStr);
          ShadowWriterVisitor v(writer, context);
		  // add all instances
          archive.Apply(v);
          answer["hardlinked"]= !writer.symlink;
          answer["instances"]=Json::Value::UInt64(writer.instances);
          answer["skipped"]=Json::Value::UInt64(writer.skipped);
        }

        output.AnswerJson(answer);
      }
    };

    class MediaWriterVisitor : public IArchiveVisitor
    {
    private:
      HierarchicalZipWriter&  writer_;
      DicomDirWriter          dicomDir_;
      ServerContext&          context_;
      unsigned int            countInstances_;

    public:
      MediaWriterVisitor(HierarchicalZipWriter& writer,
                         ServerContext& context) :
        writer_(writer),
        context_(context),
        countInstances_(0)
      {
      }

      void EncodeDicomDir(std::string& result)
      {
        dicomDir_.Encode(result);
      }

      virtual void Open(ResourceType level,
                        const std::string& publicId)
      {
      }

      virtual void Close()
      {
      }

      virtual void AddInstance(const std::string& instanceId,
                               const FileInfo& dicom)
      {
        // "DICOM restricts the filenames on DICOM media to 8
        // characters (some systems wrongly use 8.3, but this does not
        // conform to the standard)."
        std::string filename = "IM" + boost::lexical_cast<std::string>(countInstances_);
        writer_.OpenFile(filename.c_str());

        std::string content;
        context_.ReadFile(content, dicom);
        writer_.Write(content);

        ParsedDicomFile parsed(content);
        dicomDir_.Add("IMAGES", filename, parsed);

        countInstances_ ++;
      }

      static void Apply(RestApiOutput& output,
                        ServerContext& context,
                        ArchiveIndex& archive,
                        const std::string& filename)
      {
        archive.Expand(context.GetIndex());

        StatisticsVisitor stats;
        archive.Apply(stats);

        const bool isZip64 = IsZip64Required(stats.GetUncompressedSize(), stats.GetInstancesCount());

        // Create a RAII for the temporary file to manage the ZIP file
        Toolbox::TemporaryFile tmp;

        {
          // Create a ZIP writer
          HierarchicalZipWriter writer(tmp.GetPath().c_str());
          writer.SetZip64(isZip64);
          writer.OpenDirectory("IMAGES");

          // Create the DICOMDIR writer
          DicomDirWriter dicomDir;

          MediaWriterVisitor v(writer, context);
          archive.Apply(v);

          // Add the DICOMDIR
          writer.CloseDirectory();
          writer.OpenFile("DICOMDIR");
          std::string s;
          v.EncodeDicomDir(s);
          writer.Write(s);
        }

        // Prepare the sending of the ZIP file
        FilesystemHttpSender sender(tmp.GetPath());
        sender.SetContentType("application/zip");
        sender.SetContentFilename(filename);

        // Send the ZIP
        output.AnswerStream(sender);

        // The temporary file is automatically removed thanks to the RAII
      }
    };
  }


  static bool AddResourcesOfInterest(ArchiveIndex& archive,
                                     RestApiPostCall& call)
  {
    ServerIndex& index = OrthancRestApi::GetIndex(call);

    Json::Value resources;
    if (call.ParseJsonRequest(resources) &&
        resources.type() == Json::arrayValue)
    {
      for (Json::Value::ArrayIndex i = 0; i < resources.size(); i++)
      {
        if (resources[i].type() != Json::stringValue)
        {
          return false;   // Bad request
        }

        ResourceIdentifiers resource(index, resources[i].asString());
        archive.Add(index, resource);
      }

      return true;
    }
    else
    {
      return false;
    }
  }


  static void CreateBatchArchive(RestApiPostCall& call)
  {
    ArchiveIndex archive(ResourceType_Patient);  // root

    if (AddResourcesOfInterest(archive, call))
    {
      ArchiveWriterVisitor::Apply(call.GetOutput(),
                                  OrthancRestApi::GetContext(call),
                                  archive,
                                  "Archive.zip");
    }
  }  


  static void CreateBatchMedia(RestApiPostCall& call)
  {
    ArchiveIndex archive(ResourceType_Patient);  // root

    if (AddResourcesOfInterest(archive, call))
    {
      MediaWriterVisitor::Apply(call.GetOutput(),
                                OrthancRestApi::GetContext(call),
                                archive,
                                "Archive.zip");
    }
  }  


  static void CreateArchive(RestApiGetCall& call)
  {
    ServerIndex& index = OrthancRestApi::GetIndex(call);

    std::string id = call.GetUriComponent("id", "");
    ResourceIdentifiers resource(index, id);

    ArchiveIndex archive(ResourceType_Patient);  // root
    archive.Add(OrthancRestApi::GetIndex(call), resource);

    ArchiveWriterVisitor::Apply(call.GetOutput(),
                                OrthancRestApi::GetContext(call),
                                archive,
                                id + ".zip");
  }

  static void CreateTarArchive(RestApiPutCall& call)
  {
    ServerIndex& index = OrthancRestApi::GetIndex(call);

    std::string id = call.GetUriComponent("id", "");
    ResourceIdentifiers resource(index, id);
    
            
    DicomMap tags;
    std::string filename;
    ServerIndex &idx=OrthancRestApi::GetContext(call).GetIndex();
    Orthanc::ResourceType rs_type;
    idx.LookupResourceType(rs_type,id);
    
    if (rs_type>=Orthanc::ResourceType_Patient && idx.GetMainDicomTags(tags, id, rs_type, ResourceType_Patient)){
      filename=
        tags.GetValue(DICOM_TAG_PATIENT_ID).GetContent();
      if(rs_type>=Orthanc::ResourceType_Study && idx.GetMainDicomTags(tags, id, rs_type, ResourceType_Study)){
        filename+="_"+
          tags.GetValue(DICOM_TAG_STUDY_DATE).GetContent().substr(2)+"_"+
          tags.GetValue(DICOM_TAG_STUDY_TIME).GetContent().substr(0,6);
      }
    } else {
      filename=id;
    }



    ArchiveIndex archive(ResourceType_Patient);  // root
    archive.Add(OrthancRestApi::GetIndex(call), resource);

    TarStreamWriterVisitor::Apply(call.GetOutput(),
                                OrthancRestApi::GetContext(call),
                                archive,filename);
  }

  static void CreateShadow(RestApiPutCall& call)
  {
    ServerIndex& index = OrthancRestApi::GetIndex(call);

    std::string id = call.GetUriComponent("id", "");
    ResourceIdentifiers resource(index, id);
    
    ArchiveIndex archive(ResourceType_Patient);  // root
    archive.Add(OrthancRestApi::GetIndex(call), resource);

    ShadowWriterVisitor::Apply(call.GetOutput(), OrthancRestApi::GetContext(call), archive);
    
  }

  static void CreateMedia(RestApiGetCall& call)
  {
    ServerIndex& index = OrthancRestApi::GetIndex(call);

    std::string id = call.GetUriComponent("id", "");
    ResourceIdentifiers resource(index, id);

    ArchiveIndex archive(ResourceType_Patient);  // root
    archive.Add(OrthancRestApi::GetIndex(call), resource);

    MediaWriterVisitor::Apply(call.GetOutput(),
                              OrthancRestApi::GetContext(call),
                              archive,
                              id + ".zip");
  }


  void OrthancRestApi::RegisterArchive()
  {
    Register("/patients/{id}/archive", CreateArchive);
    Register("/studies/{id}/archive", CreateArchive);
    Register("/series/{id}/archive", CreateArchive);

    Register("/patients/{id}/stream-archive", CreateTarArchive);
    Register("/studies/{id}/stream-archive", CreateTarArchive);
    Register("/series/{id}/stream-archive", CreateTarArchive);

    Register("/studies/{id}/make-shadow", CreateShadow);
    Register("/series/{id}/make-shadow", CreateShadow);

    Register("/patients/{id}/media", CreateMedia);
    Register("/studies/{id}/media", CreateMedia);
    Register("/series/{id}/media", CreateMedia);

    Register("/tools/create-archive", CreateBatchArchive);
    Register("/tools/create-media", CreateBatchMedia);

  }
}
