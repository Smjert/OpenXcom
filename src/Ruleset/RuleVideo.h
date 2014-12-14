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
#ifndef OPENXCOM_VIDEO_H
#define OPENXCOM_VIDEO_H

#include <yaml-cpp/yaml.h>
#include <vector>
#include <string>
#include <map>

namespace OpenXcom
{
class Video
{
private:
  std::string _id;
  std::vector<std::string> _videos;
  //std::vector<std::string> _slides;
public:
	Video(const std::string &type);
	~Video();
	void load(const YAML::Node &node);
  const std::vector<std::string> * getVideos() const;

};

}
#endif
