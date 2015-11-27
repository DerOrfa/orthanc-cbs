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
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#pragma once

#include <orthanc/OrthancCPlugin.h>

#include "GdcmImageDecoder.h"

namespace OrthancPlugins
{
  class OrthancImageWrapper
  {
  private:
    OrthancPluginContext*  context_;
    OrthancPluginImage*    image_;
    double                 slope_;
    double                 intercept_;
    double                 rowPixelSpacing_;
    double                 columnPixelSpacing_;

  public:
    OrthancImageWrapper(OrthancPluginContext* context,
                        OrthancPluginPixelFormat format,
                        uint32_t width,
                        uint32_t height);

    OrthancImageWrapper(OrthancPluginContext* context,
                        GdcmImageDecoder& decoder,
                        unsigned int frameIndex);

    ~OrthancImageWrapper();

    OrthancPluginImage* Release();

    uint32_t GetWidth();

    uint32_t GetHeight();

    uint32_t GetPitch();

    OrthancPluginPixelFormat GetFormat();

    char* GetBuffer();
  };
}