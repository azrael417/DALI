// Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef DALI_OPERATORS_READER_LOADER_NUMPY_LOADER_H_
#define DALI_OPERATORS_READER_LOADER_NUMPY_LOADER_H_

#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#include <fstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <algorithm>
#include <regex>
#include <memory>

#include "dali/core/common.h"
#include "dali/pipeline/data/types.h"
#include "dali/operators/reader/loader/file_loader.h"
#include "dali/util/file.h"


namespace dali {

#define NUMPY_ALLOWED_DIMS (1, 2, 3, 4, 5, 6, 7)

#define NUMPY_ALLOWED_TYPES \
  (bool, uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, float, float16, \
  double)

TypeInfo TypeFromNumpyStr(const std::string &format);

class NumpyParseTarget{
 public:
  std::vector<int64_t> shape;
  TypeInfo type_info;
  bool fortran_order;

  size_t size() const {
    return volume(shape);
  }

  size_t nbytes() const {
    return type_info.size() * size();
  }
};

class NumpyLoader : public FileLoader {
 public:
  explicit inline NumpyLoader(
    const OpSpec& spec,
    vector<std::string> images = std::vector<std::string>(),
    bool shuffle_after_epoch = false)
    : FileLoader(spec, images, shuffle_after_epoch),
    header_regex_(R"###(^\{'descr': \'(.*?)\', 'fortran_order': (.*?), 'shape': \((.*?)\), \})###") {}

  // we want to make it possible to override this function as well
  void ReadSample(ImageFileWrapper& tensor) override;

 protected:
  // parser function, only for internal use
  std::unique_ptr<FileStream> ParseHeader(std::unique_ptr<FileStream> file,
                                          NumpyParseTarget& target);

  // sanitize shapes
  bool SetupSlab(const TensorShape<>& sample_shape, const bool& fortran_order);

  // read helpers for sliced and non-sliced reads
  std::unique_ptr<FileStream> ReadSampleHelper(std::unique_ptr<FileStream> file,
                                               ImageFileWrapper& imfile,
                                               const NumpyParseTarget& target);
  std::unique_ptr<FileStream> ReadSampleSlabHelper(std::unique_ptr<FileStream> file,
                                                   ImageFileWrapper& imfile,
                                                   const NumpyParseTarget& target);

  // regex search string
  const std::regex header_regex_;
};

}  // namespace dali

#endif  // DALI_OPERATORS_READER_LOADER_NUMPY_LOADER_H_
