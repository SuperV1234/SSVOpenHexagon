// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#pragma once

#include <SSVUtils/Core/FileSystem/FileSystem.hpp>

#include <string>

namespace hg {

struct PackInfo
{
    std::string id;
    ssvufs::Path path;
};

} // namespace hg