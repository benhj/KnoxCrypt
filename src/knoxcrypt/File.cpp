/*
  Copyright (c) <2013-2016>, <BenHJ>
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

#include "knoxcrypt/ContainerImageStream.hpp"
#include "knoxcrypt/File.hpp"
#include "knoxcrypt/FileBlockBuilder.hpp"
#include "knoxcrypt/FileBlockIterator.hpp"
#include "knoxcrypt/FileEntryException.hpp"
#include "knoxcrypt/detail/DetailKnoxCrypt.hpp"
#include "knoxcrypt/detail/DetailFileBlock.hpp"

#include <stdexcept>

namespace knoxcrypt
{

    namespace {

        uint32_t blockWriteSpace()
        {
            return detail::FILE_BLOCK_SIZE - detail::FILE_BLOCK_META;
        }

    }

    // for writing a brand new entry where start block isn't known
    File::File(SharedCoreIO const &io,
                             std::string const &name,
                             bool const enforceStartBlock)
        : m_io(io)
        , m_name(name)
        , m_enforceStartBlock(enforceStartBlock)
        , m_fileSize(0)
        , m_workingBlock()
        , m_buffer()
        , m_startVolumeBlock(0)
        , m_blockIndex(0)
        , m_openDisposition(OpenDisposition::buildAppendDisposition())
        , m_pos(0)
        , m_blockCount(0)
        , m_stream()
    {
    }

    // for appending or overwriting
    File::File(SharedCoreIO const &io,
                             std::string const &name,
                             uint64_t const startBlock,
                             OpenDisposition const &openDisposition)
        : m_io(io)
        , m_name(name)
        , m_enforceStartBlock(false)
        , m_fileSize(0)
        , m_workingBlock()
        , m_buffer()
        , m_startVolumeBlock(startBlock)
        , m_blockIndex(0)
        , m_openDisposition(openDisposition)
        , m_pos(0)
        , m_blockCount(0)
        , m_stream()
    {
        // counts number of blocks and sets file size
        enumerateBlockStats();

        // sets the current working block to the very first file block
        m_workingBlock = std::make_shared<FileBlock>(io, startBlock, openDisposition, m_stream);

        m_stream = m_workingBlock->getStream();

        // set up for specific write-mode
        if (m_openDisposition.readWrite() != ReadOrWriteOrBoth::ReadOnly) {

            // if in trunc, unlink
            if (m_openDisposition.trunc() == TruncateOrKeep::Truncate) {
                this->unlink();

            } else {
                // only if in append mode do we seek to end.
                if (m_openDisposition.append() == AppendOrOverwrite::Append) {
                    this->seek(0, std::ios::end);
                }
            }
        }
    }

    std::string
    File::filename() const
    {
        return m_name;
    }

    uint64_t
    File::fileSize() const
    {
        return m_fileSize;
    }

    OpenDisposition
    File::getOpenDisposition() const
    {
        return m_openDisposition;
    }

    SharedImageStream
    File::getStream() const
    {
        return m_stream;
    }

    uint64_t
    File::getCurrentVolumeBlockIndex()
    {
        if (!m_workingBlock) {
            checkAndUpdateWorkingBlockWithNew();
        }
        return m_workingBlock->getIndex();
    }

    uint64_t
    File::getStartVolumeBlockIndex() const
    {
        if (!m_workingBlock) {
            checkAndUpdateWorkingBlockWithNew();
            m_startVolumeBlock = m_workingBlock->getIndex();
        }
        return m_startVolumeBlock;
    }

    std::streamsize
    File::readWorkingBlockBytes(uint32_t const thisMany)
    {

        // need to take into account the currently seeked-to position and
        // subtract that because we then only want to read
        // (totalBytes - toldPosition) bytes. This will allows us to read
        // from the told position up to totalBytes
        uint32_t size =  m_workingBlock->getDataBytesWritten() - m_workingBlock->tell();

        // try to read thisMany bytes
        uint32_t bytesToRead = std::min(size, thisMany);

        std::vector<uint8_t>().swap(m_buffer);
        m_buffer.resize(bytesToRead);
        (void)m_workingBlock->read((char*)&m_buffer.front(), bytesToRead);

        if (static_cast<uint64_t>(m_blockIndex + 1) < m_blockCount && bytesToRead == size) {
            ++m_blockIndex;
            m_workingBlock = std::make_shared<FileBlock>(m_io,
                                                           m_workingBlock->getNextIndex(),
                                                           m_openDisposition,
                                                           m_stream);
        }

        return bytesToRead;
    }

    void File::newWritableFileBlock() const
    {
        auto block(m_io->blockBuilder->buildWritableFileBlock(m_io,
                                                              knoxcrypt::OpenDisposition::buildAppendDisposition(),
                                                              m_stream,
                                                              m_enforceStartBlock));

        if (m_enforceStartBlock) { m_enforceStartBlock = false; }

        block.registerBlockWithVolumeBitmap();

        if (m_workingBlock) {
            m_workingBlock->setNextIndex(block.getIndex());
        }

        ++m_blockCount;
        m_blockIndex = m_blockCount - 1;
        m_workingBlock = std::make_shared<FileBlock>(block);
    }

    void File::enumerateBlockStats()
    {
        // find very first block
        FileBlockIterator block(m_io,
                                m_startVolumeBlock,
                                m_openDisposition,
                                m_stream);
        FileBlockIterator end;
        for (; block != end; ++block) {
            m_fileSize += block->getDataBytesWritten();
            ++m_blockCount;
        }
    }

    void
    File::writeBufferedDataToWorkingBlock(uint32_t const bytes)
    {
        m_workingBlock->write((char*)&m_buffer.front(), bytes);
        std::vector<uint8_t>().swap(m_buffer);

        // stream would have been initialized in block's write function
        if(!m_stream) {
            m_stream = m_workingBlock->getStream();
        }
    }

    bool
    File::workingBlockHasAvailableSpace() const
    {
        // use tell to get bytes written so far as the read/write head position
        // is always updates after reads/writes
        uint32_t const bytesWritten = m_workingBlock->tell();

        if (bytesWritten < blockWriteSpace()) {
            return true;
        }
        return false;

    }

    void
    File::checkAndUpdateWorkingBlockWithNew() const
    {
        // first case no file blocks so absolutely need one to write to
        if (!m_workingBlock) {

            newWritableFileBlock();

            // when writing the file, the working block will be empty
            // and the start volume block will be unset so need to set now
            m_startVolumeBlock = m_workingBlock->getIndex();
            return;
        }

        // in this case the current block is exhausted so we need a new one
        if (!workingBlockHasAvailableSpace()) {
            // EDGE case: if overwrite causes us to go over end, need to
            // switch to append mode
            if (static_cast<uint64_t>(this->tell()) >= m_fileSize) {
                m_openDisposition = OpenDisposition::buildAppendDisposition();
            }

            // if in overwrite mode, maybe we want to overwrite current bytes
            if (m_openDisposition.append() == AppendOrOverwrite::Overwrite) {
                // if the reported stream position in the block is less that
                // the block's total capacity, then we don't create a new block
                // we simply overwrite
                if (m_workingBlock->tell() < blockWriteSpace()) {
                    return;
                }

                // edge case; if right at the very end of the block, need to
                // iterate the block index and return if possible
                if (m_workingBlock->tell() == blockWriteSpace()) {
                    ++m_blockIndex;
                    m_workingBlock = std::make_shared<FileBlock>(m_io,
                                                                   m_workingBlock->getNextIndex(),
                                                                   m_openDisposition,
                                                                   m_stream);
                    return;
                }

            }
            newWritableFileBlock();

            return;
        }

    }

    uint32_t
    File::getBytesLeftInWorkingBlock()
    {
        // if given the stream position no more bytes can be written
        // then write out buffer
        uint32_t streamPosition(m_workingBlock->tell());

        // the stream position is subtracted since block may have already
        // had bytes written to it in which case the available size left
        // is approx. block size - stream position
        return (blockWriteSpace()) - streamPosition;
    }

    std::streamsize
    File::read(char* s, std::streamsize n)
    {
        if (m_openDisposition.readWrite() == ReadOrWriteOrBoth::WriteOnly) {
            throw FileEntryException(FileEntryError::NotReadable);
        }

        // read block data
        uint32_t read(0);
        uint64_t offset(0);
        while (read < n) {

            // there are n-read bytes left to read so try and read that many!
            uint32_t count = readWorkingBlockBytes(n - read);
            read += count;

            // optimization; use this rather than for loop
            // copies from m_buffer to s + offset
            std::copy(&m_buffer[0], &m_buffer[count], s + offset);

            offset += count;

            // edge case bug fix
            if (count == 0) { break;}
        }

        // update stream position
        m_pos += read;

        return read;
    }

    uint32_t
    File::bufferBytesForWorkingBlock(const char* s, std::streamsize n, uint32_t offset)
    {

        auto const spaceAvailable = getBytesLeftInWorkingBlock();

        // if n is smaller than space available, just copy in to buffer
        if (n < spaceAvailable) {
            m_buffer.resize(n);
            std::copy(&s[offset], &s[offset + n], &m_buffer[0]);
            return n;
        }

        // if space available > 0 copy in to the buffer spaceAvailable bytes
        if (spaceAvailable > 0) {
            m_buffer.resize(spaceAvailable);
            std::copy(&s[offset], &s[offset + spaceAvailable], &m_buffer[0]);
            return spaceAvailable;
        }
        return 0;
    }

    std::streamsize
    File::write(const char* s, std::streamsize n)
    {

        if (m_openDisposition.readWrite() == ReadOrWriteOrBoth::ReadOnly) {
            throw FileEntryException(FileEntryError::NotWritable);
        }

        std::streamsize wrote(0);
        while (wrote < n) {

            // check if the working block needs to be updated with a new one
            checkAndUpdateWorkingBlockWithNew();

            // buffers the data that will be written to the working block
            // computed as a function of the data left to write and the
            // working block's available space
            auto actualWritten = bufferBytesForWorkingBlock(s, (n-wrote), wrote);

            // does what it says
            writeBufferedDataToWorkingBlock(actualWritten);
            wrote += actualWritten;

            // update stream position
            m_pos += actualWritten;

            if (m_openDisposition.append() == AppendOrOverwrite::Append) {
                m_fileSize+=actualWritten;
            }
        }
        return wrote;
    }

    void
    File::truncate(std::ios_base::streamoff newSize)
    {
        // compute number of block required
        auto const blockSize = blockWriteSpace();

        // edge case
        if (newSize < blockSize) {
            FileBlock zeroBlock = getBlockWithIndex(0);
            zeroBlock.setSize(newSize);
            zeroBlock.setNextIndex(zeroBlock.getIndex());
            return;
        }

        boost::iostreams::stream_offset const leftOver = newSize % blockSize;

        boost::iostreams::stream_offset const roundedDown = newSize - leftOver;

        uint64_t blocksRequired = roundedDown / blockSize;

        // edge case
        SharedFileBlock block;
        if (leftOver == 0) {
            --blocksRequired;
            block = std::make_shared<FileBlock>(getBlockWithIndex(blocksRequired));
            block->setSize(blockSize);
        } else {
            block = std::make_shared<FileBlock>(getBlockWithIndex(blocksRequired));
            block->setSize(leftOver);
        }

        block->setNextIndex(block->getIndex());

        m_blockCount = blocksRequired;

    }

    using SeekPair = std::pair<int64_t, boost::iostreams::stream_offset>;
    SeekPair
    getPositionFromBegin(boost::iostreams::stream_offset off)
    {
        // find what file block the offset would relate to and set extra offset in file block
        // to that position
        auto const blockSize = blockWriteSpace();
        boost::iostreams::stream_offset casted = off;
        boost::iostreams::stream_offset const leftOver = casted % blockSize;
        int64_t block = 0;
        boost::iostreams::stream_offset blockPosition = 0;
        if (off > blockSize) {
            if (leftOver > 0) {

                // round down casted
                casted -= leftOver;

                // set the position of the stream in block to leftOver
                blockPosition = leftOver;

            } else {
                blockPosition = 0;
            }

            // get exact number of blocks after round-down
            block = casted / blockSize;
            if(leftOver == 0) {
                --block;
            }

        } else {
            // offset is smaller than the first block so keep block
            // index at 0 and the position for the zero block at offset
            blockPosition = off;
        }

        return std::make_pair(block, blockPosition);
    }

    SeekPair
    getPositionFromEnd(boost::iostreams::stream_offset off, int64_t endBlockIndex,
                       boost::iostreams::stream_offset bytesWrittenToEnd)
    {
        // treat like begin and then 'inverse'
        auto treatLikeBegin = getPositionFromBegin(std::abs(off));

        int64_t block = endBlockIndex - treatLikeBegin.first;
        auto blockPosition = bytesWrittenToEnd - treatLikeBegin.second;

        if (blockPosition < 0) {
            uint16_t const blockSize = blockWriteSpace();
            blockPosition = blockSize + blockPosition;
            --block;
        }

        return std::make_pair(block, blockPosition);

    }

    SeekPair
    getPositionFromCurrent(boost::iostreams::stream_offset off,
                           int64_t blockIndex,
                           boost::iostreams::stream_offset indexedBlockPosition)
    {
        // find what file block the offset would relate to and set extra offset in file block
        // to that position
        auto const blockSize = blockWriteSpace();
        auto addition = off + indexedBlockPosition;
        auto leftOver = std::abs(addition) % blockSize;
        auto roundedDown = std::abs(addition) - leftOver;
        auto toIncrementBy = roundedDown / blockSize;

        if (addition >= 0) {
            auto newBlockIndex = blockIndex + toIncrementBy;
            auto newPosition = leftOver;
            return std::make_pair(newBlockIndex, newPosition);
        }
        auto newBlockIndex = blockIndex - (toIncrementBy + 1);
        auto newPosition = blockSize - leftOver;
        return std::make_pair(newBlockIndex, newPosition);
    }

    boost::iostreams::stream_offset
    File::seek(boost::iostreams::stream_offset off, std::ios_base::seekdir way)
    {
        // reset any offset values to zero but only if not seeking from the current
        // position. When seeking from the current position, we need to keep
        // track of the original block offset
        if (way != std::ios_base::cur) {
            if (m_workingBlock) {
                m_workingBlock->seek(0);
            }
        }

        // if at end just seek right to end and don't do anything else
        SeekPair seekPair;
        if (way == std::ios_base::end) {

            size_t endBlock = m_blockCount - 1;
            seekPair = getPositionFromEnd(off, endBlock,
                                          getBlockWithIndex(endBlock).getDataBytesWritten());

        }

        // pass in the current offset, the current block and the current
        // block position. Note both of these latter two params will be 0
        // if seeking from the beginning

        if (way == std::ios_base::beg) {
            seekPair = getPositionFromBegin(off);
        }
        // seek relative to the current position
        if (way == std::ios_base::cur) {
            seekPair = getPositionFromCurrent(off, m_blockIndex,
                                              m_workingBlock->tell());
        }

        // check bounds and error if too big
        if (static_cast<uint64_t>(seekPair.first) >= m_blockCount || seekPair.first < 0) {
            return -1; // fail
        } else {

            // update block where we start reading/writing from
            m_blockIndex = seekPair.first;
            m_workingBlock = std::make_shared<FileBlock>(this->getBlockWithIndex(m_blockIndex));

            // set the position to seek to for given block
            // this will be the point from which we read or write
            m_workingBlock->seek(seekPair.second);

            switch (way) {
              case std::ios_base::cur:
                m_pos = m_pos + off;
                break;
              case std::ios_base::end:
                m_pos = m_fileSize + off;
                break;
              case std::ios_base::beg:
                m_pos = off;
                break;
              default:
                break;
            }

        }

        return off;
    }

    boost::iostreams::stream_offset
    File::tell() const
    {
        return m_pos;
    }

    void
    File::flush()
    {
        writeBufferedDataToWorkingBlock(m_buffer.size());
        if (m_optionalSizeCallback) {
            (*m_optionalSizeCallback)(m_fileSize);
        }
    }

    void
    File::reset()
    {
        doReset();
    }

    void
    File::doReset()
    {
        m_fileSize = 0;
        m_blockCount = 0;
        m_workingBlock = nullptr;
        m_blockIndex = 0;
    }

    void
    File::unlink()
    {
        // loop over all file blocks and update the volume bitmap indicating
        // that block is no longer in use
        FileBlockIterator it(m_io, m_startVolumeBlock, m_openDisposition, m_stream);
        FileBlockIterator end;

        for (; it != end; ++it) {
            it->unlink();
            ++m_io->freeBlocks;
        }

        doReset();
    }

    void
    File::setOptionalSizeUpdateCallback(SetEntryInfoSizeCallback callback)
    {
        m_optionalSizeCallback = OptionalSizeCallback(callback);
    }

    FileBlock
    File::getBlockWithIndex(uint64_t n) const
    {

        {
            FileBlockIterator it(m_io, m_startVolumeBlock, m_openDisposition, m_stream);
            FileBlockIterator end;
            uint64_t c(0);
            for (; it != end; ++it) {
                if (c==n) {
                    return *it;
                }
                ++c;
            }
        }

        throw std::runtime_error("Whoops! Something went wrong in File::getBlockWithIndex");
    }
}
