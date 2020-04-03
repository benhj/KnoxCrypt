/*
  Copyright (c) <2013-2015>, <BenHJ>
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
#include "knoxcrypt/FileEntryException.hpp"
#include "knoxcrypt/detail/DetailKnoxCrypt.hpp"
#include "knoxcrypt/detail/DetailFileBlock.hpp"
#include "test/SimpleTest.hpp"
#include "test/TestHelpers.hpp"
#include "utility/MakeKnoxCrypt.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/stream.hpp>

#include <cassert>
#include <sstream>

using namespace simpletest;

class FileTest
{
  public:
    FileTest() : m_uniquePath(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path())
    {
        boost::filesystem::create_directories(m_uniquePath);
        testFileSizeReportedCorrectly();
        testBlocksAllocated();
        testFileUnlink();
        testReadingFromNonReadableThrows();
        testWritingToNonWritableThrows();
        testBigWriteFollowedByRead();
        testBigWriteFollowedBySmallAppend();
        testBigWriteFollowedBySmallOverwriteAtStart();
        testBigWriteFollowedBySmallOverwriteAtEnd();
        testBigWriteFollowedBySmallOverwriteAtEndThatGoesOverOriginalLength();
        testBigWriteFollowedByBigOverwriteAtEndThatGoesOverOriginalLength();
        testSmallWriteFollowedByBigAppend();
        testSeekAndReadSmallFile();
        testWriteBigDataAppendSmallStringSeekToAndReadAppendedString();
        testSeekingFromEnd();
        testSeekingFromCurrentNegative();
        testSeekingFromCurrentNegative_bigSeek();
        testSeekingFromCurrentPositive();
        testSeekingFromCurrentPositive_bigSeek();
        testEdgeCaseEndOfBlockOverWrite();
        testEdgeCaseEndOfBlockAppend();
    }

    ~FileTest()
    {
        boost::filesystem::remove_all(m_uniquePath);
    }

  private:

    boost::filesystem::path m_uniquePath;

    void testFileSizeReportedCorrectly()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write get file size from same entry
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
            ASSERT_EQUAL(BIG_SIZE, entry.fileSize(), "testFileSizeReportedCorrectly A");
        }

        // test get file size different entry but same data
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            ASSERT_EQUAL(BIG_SIZE, entry.fileSize(), "testFileSizeReportedCorrectly B");
        }
    }

    void testBlocksAllocated()
    {
        long const blocks = 2048;
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write get file size from same entry
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();

            uint64_t startBlock = entry.getStartVolumeBlockIndex();
            knoxcrypt::ContainerImageStream in(io, std::ios::in | std::ios::out | std::ios::binary);
            ASSERT_EQUAL(true, knoxcrypt::detail::isBlockInUse(startBlock, blocks, in), "testBlocksAllocated: blockAllocated");
            knoxcrypt::FileBlock block(io, startBlock,
                                     knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            uint64_t nextBlock = block.getNextIndex();
            while (nextBlock != startBlock) {
                startBlock = nextBlock;
                ASSERT_EQUAL(true, knoxcrypt::detail::isBlockInUse(startBlock, blocks, in), "testBlocksAllocated: blockAllocated");
                knoxcrypt::FileBlock block(io, startBlock,
                                         knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
                nextBlock = block.getNextIndex();
            }
        }
    }

    void testFileUnlink()
    {
        long const blocks = 2048;
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // for storing block indices to make sure they've been deallocated after unlink
        std::vector<uint64_t> blockIndices;

        // test write followed by unlink
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();

            // get allocated block indices
            uint64_t startBlock = entry.getStartVolumeBlockIndex();
            knoxcrypt::ContainerImageStream in(io, std::ios::in | std::ios::out | std::ios::binary);
            blockIndices.push_back(startBlock);

            knoxcrypt::FileBlock block(io, startBlock,
                                     knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            uint64_t nextBlock = block.getNextIndex();
            while (nextBlock != startBlock) {
                startBlock = nextBlock;
                knoxcrypt::FileBlock block(io, startBlock,
                                         knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
                blockIndices.push_back(startBlock);
                nextBlock = block.getNextIndex();
            }
            in.close();

            // no unlink and assert that file size is 0
            entry.unlink();
            ASSERT_EQUAL(0, entry.fileSize(), "testFileUnlink A");
        }

        // test that filesize is 0 when read back in
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            ASSERT_EQUAL(0, entry.fileSize(), "testFileUnlink B");

            // test that blocks deallocated after unlink
            knoxcrypt::ContainerImageStream in(io, std::ios::in | std::ios::out | std::ios::binary);
            for (auto const & it : blockIndices) {
                ASSERT_EQUAL(false, knoxcrypt::detail::isBlockInUse(it, blocks, in), "testFileUnlink: blockDeallocatedTest");
            }
            in.close();
        }
    }

    void testReadingFromNonReadableThrows()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write get file size from same entry
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
        }

        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildWriteOnlyDisposition());
            std::vector<uint8_t> vec(entry.fileSize());

            bool pass = false;
            // assert correct exception was thrown
            try {
                entry.read((char*)&vec.front(), entry.fileSize());
            } catch (knoxcrypt::FileEntryException const &e) {
                ASSERT_EQUAL(e, knoxcrypt::FileEntryException(knoxcrypt::FileEntryError::NotReadable), "testReadingFromNonReadableThrows A");
                pass = true;
            }
            // assert that any exception was thrown
            ASSERT_EQUAL(pass, true, "testReadingFromNonReadableThrows B");
        }
    }

    void testWritingToNonWritableThrows()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write get file size from same entry
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
        }

        // write some more
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());

            bool pass = false;
            // assert correct exception was thrown
            try {
                entry.write((char*)&vec.front(), BIG_SIZE);
            } catch (knoxcrypt::FileEntryException const &e) {
                ASSERT_EQUAL(e, knoxcrypt::FileEntryException(knoxcrypt::FileEntryError::NotWritable), "testWritingToNonWritableThrows A");
                pass = true;
            }
            // assert that any exception was thrown
            ASSERT_EQUAL(pass, true, "testWritingToNonWritableThrows B");
        }
    }

    void testBigWriteFollowedByRead()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), testData.length());
            entry.flush();
        }

        // test read
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());

            std::string expected(createLargeStringToWrite());
            std::vector<char> vec;
            vec.resize(entry.fileSize());
            entry.read(&vec.front(), entry.fileSize());
            std::string recovered(vec.begin(), vec.begin() + entry.fileSize());
            ASSERT_EQUAL(recovered, expected, "testWriteFollowedByRead");
        }
    }

    void testBigWriteFollowedBySmallAppend()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // initial big write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
        }

        // small append
        std::string appendString("appended!");
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildAppendDisposition());
            entry.write(appendString.c_str(), appendString.length());
            entry.flush();
        }

        // test read
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", 1,
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::string expected(createLargeStringToWrite());
            expected.append(appendString);
            std::vector<char> vec;
            vec.resize(BIG_SIZE + appendString.length());
            entry.read(&vec.front(), BIG_SIZE + appendString.length());
            std::string recovered(vec.begin(), vec.begin() + BIG_SIZE + appendString.length());
            ASSERT_EQUAL(recovered, expected, "testBigWriteFollowedBySmallAppend");
        }
    }

    void testBigWriteFollowedBySmallOverwriteAtStart()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // initial big write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
        }

        // secondary overwrite
        std::string testData("goodbye...!");
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildOverwriteDisposition());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), testData.length());
            entry.flush();
            // ensure correct file size
            ASSERT_EQUAL(entry.fileSize(), BIG_SIZE, "testBigWriteFollowedBySmallOverwriteAtStart correct file size");
        }
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", 1,
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> readBackIn;
            readBackIn.resize(testData.length());
            entry.read((char*)&readBackIn.front(), testData.length());
            std::string result(readBackIn.begin(), readBackIn.end());
            ASSERT_EQUAL(testData, result, "testBigWriteFollowedBySmallOverwriteAtStart correct content");
        }
    }

    void testBigWriteFollowedBySmallOverwriteAtEnd()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // initial big write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
        }

        // secondary overwrite
        std::string testData("goodbye...!");
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildOverwriteDisposition());
            entry.seek(BIG_SIZE - testData.length());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), testData.length());
            entry.flush();
            // ensure correct file size
            ASSERT_EQUAL(entry.fileSize(), BIG_SIZE, "testBigWriteFollowedBySmallOverwriteAtEnd correct file size");
        }
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", 1,
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> readBackIn;
            readBackIn.resize(testData.length());
            entry.seek(BIG_SIZE - testData.length());
            entry.read((char*)&readBackIn.front(), testData.length());
            std::string result(readBackIn.begin(), readBackIn.end());
            ASSERT_EQUAL(testData, result, "testBigWriteFollowedBySmallOverwriteAtEnd correct content");
        }
    }

    void testBigWriteFollowedBySmallOverwriteAtEndThatGoesOverOriginalLength()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // initial big write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
        }


        // secondary overwrite
        std::string testData("goodbye...!");
        std::string testDataB("final bit!");
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                  knoxcrypt::OpenDisposition::buildOverwriteDisposition());
            assert(entry.seek(BIG_SIZE - testData.length()) != -1);
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), testData.length());
            std::vector<uint8_t> vecb(testDataB.begin(), testDataB.end());
            entry.write((char*)&vecb.front(), testDataB.length());
            entry.flush();
        }


        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", 1,
                                  knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> readBackIn;
            readBackIn.resize(testData.length() + testDataB.length());
            assert(entry.seek(BIG_SIZE - testData.length()) != -1);
            entry.read((char*)&readBackIn.front(), testData.length() + testDataB.length());
            std::string result(readBackIn.begin(), readBackIn.end());
            ASSERT_EQUAL(entry.fileSize(), BIG_SIZE + testDataB.length(), "testBigWriteFollowedBySmallOverwriteAtEndThatGoesOverOriginalLength correct file size");
            ASSERT_EQUAL(testData.append(testDataB), result, "testBigWriteFollowedBySmallOverwriteAtEndThatGoesOverOriginalLength correct content");
        }
    }

    void testBigWriteFollowedByBigOverwriteAtEndThatGoesOverOriginalLength()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // initial big write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
        }

        // secondary overwrite
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                  knoxcrypt::OpenDisposition::buildOverwriteDisposition());
            entry.seek(BIG_SIZE - 50);
            std::string testData(createLargeStringToWrite("abcdefghijklm"));
            entry.write(testData.c_str(), testData.length());
            entry.flush();
        }

        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", 1,
                                  knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> readBackIn;
            readBackIn.resize(BIG_SIZE + BIG_SIZE - 50);
            entry.read((char*)&readBackIn.front(), BIG_SIZE + BIG_SIZE - 50);
            std::string result(readBackIn.begin(), readBackIn.end());
            ASSERT_EQUAL(entry.fileSize(), BIG_SIZE + BIG_SIZE - 50, "testBigWriteFollowedByBigOverwriteAtEndThatGoesOverOriginalLength correct file size");
            std::string orig_(createLargeStringToWrite());
            std::string orig(orig_.begin(), orig_.end()-50);
            orig.append(createLargeStringToWrite("abcdefghijklm"));
            ASSERT_EQUAL(orig, result, "testBigWriteFollowedByBigOverwriteAtEndThatGoesOverOriginalLength correct content");
        }
    }

    void testSmallWriteFollowedByBigAppend()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // initial small write
        std::string testData("small string");
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), testData.length());
            entry.flush();
        }

        // big append
        std::string appendString(createLargeStringToWrite());
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildAppendDisposition());
            entry.write(appendString.c_str(), appendString.length());
            entry.flush();
        }

        // test read
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::string expected(testData.append(appendString));
            std::vector<char> vec;
            vec.resize(entry.fileSize());
            entry.read(&vec.front(), entry.fileSize());
            std::string recovered(vec.begin(), vec.begin() + entry.fileSize());
            ASSERT_EQUAL(recovered, expected, "testSmallWriteFollowedByBigAppend");
        }
    }

    void testSeekAndReadSmallFile()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string const testString("Hello and goodbye!");
            std::string testData(testString);
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
        }

        // test seek and read
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::string expected("goodbye!");
            std::vector<char> vec;
            vec.resize(expected.size());
            entry.seek(10);
            entry.read(&vec.front(), expected.size());
            std::string recovered(vec.begin(), vec.end());
            ASSERT_EQUAL(recovered, expected, "testSeekAndReadSmallFile");
        }
    }

    void testWriteBigDataAppendSmallStringSeekToAndReadAppendedString()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), BIG_SIZE);
            entry.flush();
        }

        // test seek of big file
        std::string appendString("appended!");
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildAppendDisposition());
            entry.write(appendString.c_str(), appendString.length());
            entry.flush();
        }

        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildAppendDisposition());
            std::vector<char> vec;
            vec.resize(appendString.length());
            entry.seek(BIG_SIZE);
            entry.read(&vec.front(), appendString.length());
            std::string recovered(vec.begin(), vec.end());
            ASSERT_EQUAL(recovered, appendString, "testWriteBigDataAppendSmallStringSeekToAndReadAppendedString");
        }

    }

    void testSeekingFromEnd()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), testData.length());
            entry.flush();
        }

        std::string testData("goodbye!");
        std::streamoff off = -548;
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildOverwriteDisposition());
            entry.seek(off, std::ios_base::end);
            entry.write(testData.c_str(), testData.length());
            entry.flush();
        }
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> vec;
            vec.resize(entry.fileSize());
            entry.read((char*)&vec.front(), entry.fileSize());
            std::string recovered(vec.begin()+entry.fileSize()+off,
                                  vec.begin() + entry.fileSize() + off + testData.length());
            ASSERT_EQUAL(recovered, testData, "FileTest::testSeekingFromEnd()");
        }
    }

    boost::filesystem::path 
    seekingFromCurrentTestSetup(std::streamoff const off,
                                std::streamoff const initialSeek)
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createLargeStringToWrite());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), testData.length());
            entry.flush();
        }

        std::string testData("goodbye!");
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildOverwriteDisposition());
            entry.seek(initialSeek);
            entry.seek(off, std::ios_base::cur);
            entry.write(testData.c_str(), testData.length());
            entry.flush();
        }

        return testPath;
    }

    void testSeekingFromCurrentNegative()
    {
        std::streamoff const off = -5876;
        std::streamoff const initialSeek = 12880;
        boost::filesystem::path testPath = 
           seekingFromCurrentTestSetup(off, initialSeek);

        {
            std::string testData("goodbye!");
            std::streamoff finalPosition = initialSeek + off;
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> vec;
            vec.resize(entry.fileSize());
            entry.read((char*)&vec.front(), entry.fileSize());
            std::string recovered(vec.begin() + finalPosition,
                                  vec.begin() + finalPosition + testData.length());
            ASSERT_EQUAL(recovered, testData, "FileTest::testSeekingFromCurrentNegative()");
        }
    }

    void testSeekingFromCurrentNegative_bigSeek()
    {
        std::streamoff const off = -19476;
        std::streamoff const initialSeek = 27980;
        boost::filesystem::path testPath = 
           seekingFromCurrentTestSetup(off, initialSeek);

        {
            std::string testData("goodbye!");
            std::streamoff finalPosition = initialSeek + off;
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> vec;
            vec.resize(entry.fileSize());
            entry.read((char*)&vec.front(), entry.fileSize());
            std::string recovered(vec.begin() + finalPosition,
                                  vec.begin() + finalPosition + testData.length());
            ASSERT_EQUAL(recovered, testData, "FileTest::testSeekingFromCurrentNegative_bigSeek()");
        }
    }

    void testSeekingFromCurrentPositive()
    {
        std::streamoff const off = 2176;
        std::streamoff const initialSeek = 3267;
        boost::filesystem::path testPath = 
           seekingFromCurrentTestSetup(off, initialSeek);

        {
            std::string testData("goodbye!");
            std::streamoff finalPosition = initialSeek + off;
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> vec;
            vec.resize(entry.fileSize());
            entry.read((char*)&vec.front(), entry.fileSize());
            std::string recovered(vec.begin() + finalPosition,
                                  vec.begin() + finalPosition + testData.length());
            ASSERT_EQUAL(recovered, testData, "FileTest::testSeekingFromCurrentPositive()");
        }
    }

    void testSeekingFromCurrentPositive_bigSeek()
    {
        std::streamoff const off = 128176;
        std::streamoff const initialSeek = 267;
        boost::filesystem::path testPath = 
           seekingFromCurrentTestSetup(off, initialSeek);

        {
            std::string testData("goodbye!");
            std::streamoff finalPosition = initialSeek + off;
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> vec;
            vec.resize(entry.fileSize());
            entry.read((char*)&vec.front(), entry.fileSize());
            std::string recovered(vec.begin() + finalPosition,
                                  vec.begin() + finalPosition + testData.length());
            ASSERT_EQUAL(recovered, testData, "FileTest::testSeekingFromCurrentPositive_bigSeek()");
        }
    }

    void testEdgeCaseEndOfBlockOverWrite()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createAString());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), testData.length());
            entry.flush();
        }

        int seekPos = 499; // overwrite to begin at very end
        std::string testData("goodbye!");
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildOverwriteDisposition());
            entry.seek(seekPos);
            entry.write(testData.c_str(), testData.length());
            entry.flush();
        }

        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());
            std::vector<uint8_t> vec;
            vec.resize(entry.fileSize());
            entry.read((char*)&vec.front(), entry.fileSize());
            std::string recovered(vec.begin() + seekPos,
                                  vec.begin() + seekPos + testData.length());
            ASSERT_EQUAL(recovered, testData, "FileTest::testEdgeCaseEndOfBlockOverwrite()");
        }
    }

    void testEdgeCaseEndOfBlockAppend()
    {
        boost::filesystem::path testPath = buildImage(m_uniquePath);

        // test write
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt");
            std::string testData(createAString());
            std::vector<uint8_t> vec(testData.begin(), testData.end());
            entry.write((char*)&vec.front(), testData.length());
            entry.flush();
        }

        std::string testData("goodbye!");
        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "test.txt", 1,
                                       knoxcrypt::OpenDisposition::buildAppendDisposition());
            entry.write(testData.c_str(), testData.length());
            entry.flush();
        }

        {
            knoxcrypt::SharedCoreIO io(createTestIO(testPath));
            knoxcrypt::File entry(io, "entry", uint64_t(1),
                                       knoxcrypt::OpenDisposition::buildReadOnlyDisposition());

            ASSERT_EQUAL(entry.fileSize(), A_STRING_SIZE + testData.length(), "FileTest::testEdgeCaseEndOfBlockAppend() filesize");
            std::vector<uint8_t> vec;
            vec.resize(A_STRING_SIZE + testData.length());
            entry.read((char*)&vec.front(), A_STRING_SIZE + testData.length());
            std::string recovered(vec.begin() + 998,
                                  vec.begin() + 998 + testData.length());
            ASSERT_EQUAL(recovered, testData, "FileTest:: testEdgeCaseEndOfBlockAppend() content");
        }
    }
};
