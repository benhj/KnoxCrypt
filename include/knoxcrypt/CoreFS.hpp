/*
  Copyright (c) <2013-present>, <BenHJ>
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

#pragma once

#include "knoxcrypt/CoreIO.hpp"
#include "knoxcrypt/FileDevice.hpp"
#include "knoxcrypt/CompoundFolder.hpp"
#include "knoxcrypt/FolderRemovalType.hpp"
#include "knoxcrypt/OpenDisposition.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/optional.hpp>

#include <map>
#include <memory>
#include <string>
#include <mutex>

#include <sys/statvfs.h>

namespace knoxcrypt
{
    class CoreFS;
    using Sharedknoxcrypt = std::shared_ptr<CoreFS>;

    class CoreFS
    {
        using SharedCompoundFolder = std::shared_ptr<CompoundFolder>;

      public:
        CoreFS() = delete;
        explicit CoreFS(SharedCoreIO const &io);

        /**
         * Retrieve the designated file block size
         */
        long getBlockSize() const;

        /**
         * @brief  retrieves folder entry for given path
         * @param  path the path to retrieve entry for
         * @return the CompoundFolder
         * @throw  knoxcryptException if path cannot be found
         */
        CompoundFolder getFolder(std::string const &path);

        /**
         * @brief  retrieves metadata for given path
         * @param  path the path to retrieve metadata for
         * @return the meta data
         * @throw  knoxcryptException if path cannot be found
         */
        EntryInfo getInfo(std::string const &path);

        /**
         * @brief  file existence check
         * @param  path the path to check
         * @return true if path exists, false otherwise
         */
        bool fileExists(std::string const &path) const;

        /**
         * @brief  folder existence check
         * @param  path the path to check
         * @return true if path exists, false otherwise
         */
        bool folderExists(std::string const &path);

        /**
         * @brief adds an empty file
         * @param path the path of new file
         * @throw knoxcryptException illegal filename if path has '/' on end
         * @throw knoxcryptException if parent path cannot be found
         * @throw knoxcryptException AlreadyExists if path exists
         */
        void addFile(std::string const &path);

        /**
         * @brief creates a new folder
         * @param path the path of new folder
         * @throw knoxcryptException if parent path cannot be found
         * @throw knoxcryptException AlreadyExists if path exists
         */
        void addFolder(std::string const &path) const;

        /**
         * @brief for renaming
         * @param src entry to rename from
         * @param dst entry to rename to
         * @throw knoxcryptException NotFound if src or parent of dst cannot be found
         * @throw knoxcryptException AlreadyExists if dst already present
         */
        void renameEntry(std::string const &src, std::string const &dst);

        /**
         * @brief removes a file
         * @param path file to remove
         * @throw knoxcryptException NotFound if not found
         */
        void removeFile(std::string const &path);

        /**
         * @brief removes a folder
         * @param path folder to remove
         * @throw knoxcryptException NotFound if not found
         * @throw knoxcryptException NotEmpty if removalType is MustBeEmpty and
         * folder isn't empty
         */
        void removeFolder(std::string const &path, FolderRemovalType const &removalType);

        /**
         * @brief  opens a file
         * @param  path the file to open
         * @param  openMode the open mode
         * @return a seekable device to the opened file
         * @throw  knoxcryptException not found if can't be found
         */
        FileDevice openFile(std::string const &path, OpenDisposition const &openMode);

        /**
         * @brief chops off end a file at given offset
         * @param path the file to truncate
         * @param offset the position at which to 'chop' the file
         */
        void truncateFile(std::string const &path, std::ios_base::streamoff offset);

        /**
         * @brief gets file system info; used when a 'df' command is issued
         * @param buf stores the filesystem stats data
         */
        void statvfs(struct statvfs *buf);

      private:

        // the core knoxcrypt io (path, blocks, password)
        SharedCoreIO m_io;

        // the root of the tea safe filesystem
        mutable SharedCompoundFolder m_rootFolder;

        // so that folders don't have to be consistently rebuilt store
        // them as they are built in map and prefer to query map in future
        using FolderCache = std::map<std::string, SharedCompoundFolder>;
        mutable FolderCache m_folderCache;

        using StateMutex = std::mutex;
        using StateLock = std::lock_guard<StateMutex>;
        mutable StateMutex m_stateMutex;

        // so that a new file doesn't need to be created each time the same file is opened
        // hold on to just the last opened file, rather than a cache 13/02/16
        using FileAndPathPair = std::pair<std::string, SharedFile>;
        using UniqueFileAndPathPair = std::unique_ptr<FileAndPathPair>;
        mutable UniqueFileAndPathPair m_cachedFileAndPath;

        void throwIfAlreadyExists(std::string const &path) const;

        bool doFileExists(std::string const &path) const;

        bool doFolderExists(std::string const &path) const;

        SharedCompoundFolder doGetParentCompoundFolder(std::string const &path) const;

        bool doExistanceCheck(std::string const &path, EntryType const &entryType) const;

        /**
         * @brief when a folder is deleted, need to remove it from the cache
         * if it exists. Use this function to do so
         * @param path the folder to remove
         */
        void removeFolderFromCache(::boost::filesystem::path const &path);
        void removeAllChildFoldersToo(::boost::filesystem::path const &path, 
                                      SharedCompoundFolder const &f);

        /**
         * @brief  updates the cached file
         * @param  path the path of the file to update / retrieve
         * @param  parentEntry the parent of the entry
         * @param  openMode the mode to open the file in
         */
        void setCachedFile(std::string const &path,
                           SharedCompoundFolder const &parentEntry,
                           OpenDisposition openMode) const;

        void resetCachedFile(::boost::filesystem::path const &path);
    };
}
