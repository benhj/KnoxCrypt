/*
  Copyright (c) <2014-present>, <BenHJ>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
  3. Neither the name of the copyright holder nor the names of its contributors
  may be used to endorse or promote products derived from this software without
  specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/// For recursively copying a ContentFolder to some physical disk location

#pragma once

#include "knoxcrypt/CoreFS.hpp"
#include "knoxcrypt/EntryInfo.hpp"
#include "knoxcrypt/EntryType.hpp"
#include "knoxcrypt/CompoundFolderEntryIterator.hpp"
#include "utility/ContentFolderVisitor.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/iostreams/copy.hpp>

#include <fstream>
#include <vector>

namespace knoxcrypt
{

    namespace utility
    {

        inline
        void recursiveExtract(ContentFolderVisitor &visitor,
                              CoreFS &theBfs,
                              std::string const &teaPath)
        {

            // get the parent folder
            auto folder = theBfs.getFolder(teaPath);

            // iterate over entries in folder
            for (auto const & it : folder) {
                // If folder, create a folder at whereToWrite and recurse
                // in to recurseExtract
                if(it->type() == EntryType::FolderType) {
                    visitor.enterFolder(*it);
                    boost::filesystem::path teaLoc(teaPath);
                    teaLoc /= it->filename();
                    recursiveExtract(visitor, theBfs, teaLoc.string());
                    visitor.exitFolder(*it);
                } else {
                    visitor.enterFile(*it);
                }
            }
        }
    }
}

