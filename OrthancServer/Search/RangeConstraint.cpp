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
#include "RangeConstraint.h"

#include "../../Core/Toolbox.h"

namespace Orthanc
{
  RangeConstraint::RangeConstraint(const DicomTag& tag, 
                                   const std::string& lower,
                                   const std::string& upper,
                                   bool isCaseSensitive) : 
    IFindConstraint(tag),
    lower_(lower),
    upper_(upper),
    isCaseSensitive_(isCaseSensitive)
  {
    if (isCaseSensitive_)
    {
      Toolbox::ToUpperCase(lower_);
      Toolbox::ToUpperCase(upper_);
    }
  }


  void RangeConstraint::Setup(LookupIdentifierQuery& lookup) const
  {
    lookup.AddConstraint(GetTag(), IdentifierConstraintType_GreaterOrEqual, lower_);
    lookup.AddConstraint(GetTag(), IdentifierConstraintType_SmallerOrEqual, upper_);
  }


  bool RangeConstraint::Match(const std::string& value) const
  {
    std::string v = value;

    if (isCaseSensitive_)
    {
      Toolbox::ToUpperCase(v);
    }

    if (lower_.size() == 0 && 
        upper_.size() == 0)
    {
      return false;
    }

    if (lower_.size() == 0)
    {
      return v <= upper_;
    }

    if (upper_.size() == 0)
    {
      return v >= lower_;
    }
    
    return (v >= lower_ && v <= upper_);
  }
}
