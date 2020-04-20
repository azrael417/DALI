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

#include <dirent.h>
#include <errno.h>
#include <memory>

// general includes
#include "dali/core/common.h"
#include "dali/pipeline/data/views.h"

// numpy loader specific includes
#include "dali/operators/reader/loader/numpy_loader.h"
#include "dali/operators/reader/loader/utils/readslice.h"
#include "dali/util/file.h"


namespace dali {
TypeInfo TypeFromNumpyStr(const std::string &format) {
  if (format == "i8")
    return TypeInfo::Create<int64_t>();
  else if (format == "i4")
    return TypeInfo::Create<int32_t>();
  else if (format == "f4")
    return TypeInfo::Create<float>();
  else if (format == "f8")
    return TypeInfo::Create<double>();
  else
    return TypeInfo();
}

std::unique_ptr<FileStream> NumpyLoader::ParseHeader(std::unique_ptr<FileStream> file,
                                                     NumpyParseTarget& target) {
  // check if the file is actually a numpy file
  std::vector<uint8_t> token(11);
  int64_t nread = file->Read(token.data(), 10);
  DALI_ENFORCE(nread == 10, "Can not read header.");
  token[nread] = '\0';

  // check if heqder is too short
  std::string header = std::string(reinterpret_cast<char*>(token.data()));
  DALI_ENFORCE(header.find_first_of("NUMPY") != std::string::npos,
               "File is not a numpy file.");

  // extract header length
  uint16_t header_len = 0;
  memcpy(&header_len, &token[8], 2);
  DALI_ENFORCE((header_len + 10) % 16 == 0,
               "Error extracting header length.");

  // read header: the offset is a magic number
  int64 offset = (6+1+1+2);
  // the header_len can be 4GiB according to the NPYv2 file format
  // specification: https://numpy.org/neps/nep-0001-npy-format.html
  // while this allocation could be sizable, it is performed on the host.
  token.resize(header_len+1);
  file->Seek(offset);
  nread = file->Read(token.data(), header_len);
  DALI_ENFORCE(nread == header_len, "Can not read header.");
  token[header_len] = '\0';
  header = std::string(reinterpret_cast<char*>(token.data()));
  DALI_ENFORCE(header.find_first_of("{") != std::string::npos, "Header is corrupted.");
  offset += header_len;

  // prepare file for later reads
  file->Seek(offset);

  // extract dictionary info from header
  std::smatch header_match;
  DALI_ENFORCE(std::regex_search(header, header_match, header_regex_),
               "Can not parse header.");

  // now extract header information
  // type
  std::string typestring = header_match[1].str();

  // < means LE, | means N/A, = means native. In all those cases, we can read
  bool little_endian =
    (typestring[0] == '<' || typestring[0] == '|' || typestring[0] == '=');
  DALI_ENFORCE(little_endian,
    "Big Endian files are not supported.");

  std::string tid = typestring.substr(1);
  // get type in a safe way
  target.type_info = TypeFromNumpyStr(tid);

  // check for data order
  if (header_match[2].str() == "False")
    target.fortran_order = false;
  else
    target.fortran_order = true;

  // set sizes
  std::string shapestring = header_match[3].str();
  std::regex shape_regex{R"(,+)"};  // split on comma
  std::sregex_token_iterator it{shapestring.begin(), shapestring.end(), shape_regex, -1};
  std::vector<std::string> shapevec{it, {}};

  // if shapevec size is 1 and shapevec[0] is the empty string,
  // the array is actually a scalar/singleton (denoted as ())
  // and thus the size needs to be set to one:
  if ( (shapevec.size() == 1) && (shapevec[0] == "") ) shapevec[0] = "1";

  // determine shapes
  size_t shapesize = shapevec.size();
  target.shape.resize(shapesize);
  // cheapest thing to do is to define the tensor in an reversed way
  if (target.fortran_order) {
    for (size_t i = 0; i < shapesize; ++i)
      target.shape[i] = static_cast<int64_t>(stoi(shapevec[shapesize-i-1]));
  } else {
    for (size_t i = 0; i < shapesize; ++i)
      target.shape[i] = static_cast<int64_t>(stoi(shapevec[i]));
  }

  return file;
}

// sanitize slabs
void NumpyLoader::SetupSlab(TensorShape<>& slab_anchor,
                            TensorShape<>& slab_shape,
                            const TensorShape<>& sample_shape,
                            const bool& fortran_order) {
  int ndims = sample_shape.size();

  if (fortran_order) {
    // transpose the shapes:
    TensorShape<> old_anchor(slab_anchor);
    TensorShape<> old_shape(slab_shape);
    for (int i = 0; i < ndims; ++i) {
      slab_anchor[i] = old_anchor[ndims-i-1];
      slab_shape[i] = old_shape[ndims-i-1];
    }
  }

  DALI_ENFORCE((slab_anchor.size() == ndims) &&
               (slab_shape.size() == ndims),
               "The dimensions of anchor and slab have to match the sample dims.");

  for (int i = 0; i < ndims; ++i) {
    int offset = slab_anchor[i];
    DALI_ENFORCE((offset >= 0) &&
                 (offset + slab_shape[i] <= sample_shape[i]),
                 "The slab has to fit inside the sample dimensions");
  }
}

std::unique_ptr<FileStream> NumpyLoader::ReadSampleHelper(std::unique_ptr<FileStream> file,
                                                          ImageFileWrapper& imfile,
                                                          const NumpyParseTarget& target) {
  Index image_bytes = target.nbytes();
  if (copy_read_data_) {
    if (imfile.image.shares_data()) {
      imfile.image.Reset();
    }
    imfile.image.Resize(target.shape, target.type_info);
    // copy the image
    file->Read(static_cast<uint8_t*>(imfile.image.raw_mutable_data()), image_bytes);
  } else {
    auto p = file->Get(image_bytes);
    // Wrap the raw data in the Tensor object.
    imfile.image.ShareData(p, image_bytes, {image_bytes});
    imfile.image.Resize(target.shape, target.type_info);
  }
  return file;
}

// here, slab_anchor and slab_shape are passed by value deliberately, because
// we might need to transpose them. In that case, we do not want to propagate that back
// to the class members
std::unique_ptr<FileStream> NumpyLoader::ReadSampleSlabHelper(std::unique_ptr<FileStream> file,
                                                              ImageFileWrapper& imfile,
                                                              const NumpyParseTarget& target) {
  // deal with potential fortran complications
  TensorShape<> slab_anchor(slab_anchor_);
  TensorShape<> slab_shape(slab_shape_);
  SetupSlab(slab_anchor, slab_shape, TensorShape<>(target.shape), target.fortran_order);

  Index image_bytes = target.nbytes();
  if (copy_read_data_) {
    if (imfile.image.shares_data()) {
      imfile.image.Reset();
    }
    ReadSliceKernel(imfile.image, file, file->Pos(),
                    TensorShape<>(target.shape), target.type_info,
                    slab_anchor, slab_shape);
  } else {
    auto p = file->Get(image_bytes);
    // Wrap the raw data in the Tensor object.
    Tensor<CPUBackend> tmptensor;
    tmptensor.ShareData(p, image_bytes, {image_bytes});
    tmptensor.Resize(target.shape, target.type_info);

    // do the sliced read
    CopySliceKernel(imfile.image, tmptensor, slab_anchor, slab_shape);
  }
  return file;
}

void NumpyLoader::ReadSample(ImageFileWrapper& imfile) {
  auto image_file = images_[current_index_++];

  // handle wrap-around
  MoveToNextShard(current_index_);

  // metadata info
  DALIMeta meta;
  meta.SetSourceInfo(image_file);
  meta.SetSkipSample(false);

  // if image is cached, skip loading
  if (ShouldSkipImage(image_file)) {
    meta.SetSkipSample(true);
    imfile.image.Reset();
    imfile.image.SetMeta(meta);
    imfile.image.set_type(TypeInfo::Create<uint8_t>());
    imfile.image.Resize({0});
    imfile.filename = "";
    return;
  }

  // open file stream
  auto current_image = FileStream::Open(file_root_ + "/" + image_file, read_ahead_);

  // read the header
  NumpyParseTarget target;
  current_image = ParseHeader(std::move(current_image), target);
  Index image_bytes = target.nbytes();

  // read a slab or the whole file
  if (slab_anchor_.empty() || slab_shape_.empty()) {
    current_image = ReadSampleHelper(std::move(current_image), imfile, target);
  } else {
    current_image = ReadSampleSlabHelper(std::move(current_image), imfile, target);
  }

  // close the file handle
  current_image->Close();

  // set metadata
  imfile.image.SetMeta(meta);

  // set file path
  imfile.filename = file_root_ + "/" + image_file;

  // set meta
  imfile.meta = (target.fortran_order ? "transpose:true" : "transpose:false");
}

}  // namespace dali
