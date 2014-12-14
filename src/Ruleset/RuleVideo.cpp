/*
 * Copyright 2010-2014 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "RuleVideo.h"

namespace OpenXcom
{

Video::Video(const std::string &id) : _id(id)
{
}

Video::~Video()
{
}

void Video::load(const YAML::Node &node)
{
  if(const YAML::Node &videos = node["video"])
  {
    for(YAML::const_iterator i = videos.begin(); i != videos.end(); ++i)
      _videos.push_back((*i).as<std::string>());
  }

  // Slides
  /*if(const YAML::Node &slides = node["slides"])
  {
    for(YAML::const_iterator i = slides.begin(); i != slides.end(); ++i)
      _slides.push_back(*i).as<std::string>());
  }*/
}

const std::vector<std::string> * Video::getVideos() const
{
  return &_videos;
}

}
