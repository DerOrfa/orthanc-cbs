# Orthanc - A Lightweight, RESTful DICOM Store
# Copyright (C) 2012-2015 Sebastien Jodogne, Medical Physics
# Department, University Hospital of Liege, Belgium
#
# This program is free software: you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.


if (STANDALONE_BUILD)
  add_definitions(-DORTHANC_PLUGIN_STANDALONE=1)

  set(AUTOGENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/AUTOGENERATED")
  set(AUTOGENERATED_SOURCES "${AUTOGENERATED_DIR}/EmbeddedResources.cpp")

  file(MAKE_DIRECTORY ${AUTOGENERATED_DIR})
  include_directories(${AUTOGENERATED_DIR})

  set(TARGET_BASE "${AUTOGENERATED_DIR}/EmbeddedResources")
  add_custom_command(
    OUTPUT
    "${AUTOGENERATED_DIR}/EmbeddedResources.h"
    "${AUTOGENERATED_DIR}/EmbeddedResources.cpp"
    COMMAND 
    python
    "${CMAKE_CURRENT_SOURCE_DIR}/Framework/EmbedResources.py"
    "${AUTOGENERATED_DIR}/EmbeddedResources"
    STATIC_RESOURCES
    ${RESOURCES_ROOT}
    DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Framework/EmbedResources.py"
    ${STATIC_RESOURCES}
    )

else()
  add_definitions(
    -DORTHANC_PLUGIN_STANDALONE=0
    -DORTHANC_PLUGIN_RESOURCES_ROOT="${RESOURCES_ROOT}"
    )
endif()


list(APPEND AUTOGENERATED_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/Framework/Plugin.cpp"
  )
