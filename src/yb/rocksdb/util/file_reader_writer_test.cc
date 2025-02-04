//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#include <vector>
#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/util/testharness.h"

#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

class WritableFileWriterTest : public RocksDBTest {};

const uint32_t kMb = 1 << 20;

TEST_F(WritableFileWriterTest, RangeSync) {
  class FakeWF : public WritableFile {
   public:
    FakeWF() : size_(0), last_synced_(0) {}
    ~FakeWF() {}

    Status Append(const Slice& data) override {
      size_ += data.size();
      return Status::OK();
    }
    Status Truncate(uint64_t size) override {
      return Status::OK();
    }
    Status Close() override {
      EXPECT_GE(size_, last_synced_ + kMb);
      EXPECT_LT(size_, last_synced_ + 2 * kMb);
      // Make sure random writes generated enough writes.
      EXPECT_GT(size_, 10 * kMb);
      return Status::OK();
    }
    Status Flush() override { return Status::OK(); }
    Status Sync() override { return Status::OK(); }
    Status Fsync() override { return Status::OK(); }
    void SetIOPriority(yb::IOPriority pri) override {}
    uint64_t GetFileSize() override { return size_; }
    void GetPreallocationStatus(size_t* block_size,
                                size_t* last_allocated_block) override {}
    size_t GetUniqueId(char* id) const override { return 0; }
    Status InvalidateCache(size_t offset, size_t length) override {
      return Status::OK();
    }

   protected:
    Status Allocate(uint64_t offset, uint64_t len) override { return Status::OK(); }
    Status RangeSync(uint64_t offset, uint64_t nbytes) override {
      EXPECT_EQ(offset % 4096, 0u);
      EXPECT_EQ(nbytes % 4096, 0u);

      EXPECT_EQ(offset, last_synced_);
      last_synced_ = offset + nbytes;
      EXPECT_GE(size_, last_synced_ + kMb);
      if (size_ > 2 * kMb) {
        EXPECT_LT(size_, last_synced_ + 2 * kMb);
      }
      return Status::OK();
    }

    uint64_t size_;
    uint64_t last_synced_;
  };

  EnvOptions env_options;
  env_options.bytes_per_sync = kMb;
  unique_ptr<FakeWF> wf(new FakeWF);
  unique_ptr<WritableFileWriter> writer(
      new WritableFileWriter(std::move(wf), env_options));
  Random r(301);
  std::unique_ptr<char[]> large_buf(new char[10 * kMb]);
  for (int i = 0; i < 1000; i++) {
    int skew_limit = (i < 700) ? 10 : 15;
    uint32_t num = r.Skewed(skew_limit) * 100 + r.Uniform(100);
    writer->Append(Slice(large_buf.get(), num));

    // Flush in a chance of 1/10.
    if (r.Uniform(10) == 0) {
      writer->Flush();
    }
  }
  writer->Close();
}

TEST_F(WritableFileWriterTest, AppendStatusReturn) {
  class FakeWF : public WritableFile {
   public:
    FakeWF() : use_os_buffer_(true), io_error_(false) {}

    bool UseOSBuffer() const override { return use_os_buffer_; }
    Status Append(const Slice& data) override {
      if (io_error_) {
        return STATUS(IOError, "Fake IO error");
      }
      return Status::OK();
    }
    Status PositionedAppend(const Slice& data, uint64_t) override {
      if (io_error_) {
        return STATUS(IOError, "Fake IO error");
      }
      return Status::OK();
    }
    Status Close() override { return Status::OK(); }
    Status Flush() override { return Status::OK(); }
    Status Sync() override { return Status::OK(); }
    void SetUseOSBuffer(bool val) { use_os_buffer_ = val; }
    void SetIOError(bool val) { io_error_ = val; }

   protected:
    bool use_os_buffer_;
    bool io_error_;
  };
  unique_ptr<FakeWF> wf(new FakeWF());
  wf->SetUseOSBuffer(false);
  unique_ptr<WritableFileWriter> writer(
      new WritableFileWriter(std::move(wf), EnvOptions()));

  ASSERT_OK(writer->Append(std::string(2 * kMb, 'a')));

  // Next call to WritableFile::Append() should fail
  dynamic_cast<FakeWF*>(writer->writable_file())->SetIOError(true);
  ASSERT_NOK(writer->Append(std::string(2 * kMb, 'b')));
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
