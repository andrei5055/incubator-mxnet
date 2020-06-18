/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef MXNET_STORAGE_STORAGE_MANAGER_HELPERS_H_
#define MXNET_STORAGE_STORAGE_MANAGER_HELPERS_H_

#if !defined(ANDROID) && !defined(__ANDROID__)

#if MXNET_USE_CUDA
#include <cuda_runtime.h>
#include "../common/cuda_utils.h"
typedef  mxnet::common::cuda::DeviceStore CudaDeviceStore;
#endif  // MXNET_USE_CUDA

#include <sys/sysinfo.h>
#include <thread>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#else
#include <Windows.h>
#include <process.h>
#endif  // _WIN32

#include <unordered_map>
#include <vector>
#include <tuple>
#include <atomic>
#include <iostream>
#include <mutex>
#include <new>
#include <string>
#include <limits>

namespace mxnet {
namespace storage {


/*!
 * \brief Abstract class, which contains context specific methods used by PooledStorageManager.
 */
class ContextHelper {
 public:
  virtual ~ContextHelper()                              {}

  inline void set_initilal_context(const Context &ctx)  { initilal_context_ = ctx; }
  inline const Context &initilal_context() const        { return initilal_context_; }
  virtual std::tuple<size_t, size_t> getMemoryInfo() const = 0;
  virtual int Malloc(void **ppNtr, size_t size) const = 0;
  virtual void Free(void *dptr) const = 0;
  inline size_t freeMemorySize() const                  { return std::get<0>(getMemoryInfo()); }

#if MXNET_USE_CUDA
  virtual const CudaDeviceStore *SetCurrentDevice(const Context &/*ctx*/) const { return nullptr; }
#define SET_DEVICE(device_store, contextHelper, ctx, flag) \
             const auto *device_store = flag? contextHelper.get()->SetCurrentDevice(ctx) : nullptr;
#define UNSET_DEVICE(device_store)    delete device_store
#else
  // empty macros when MxNet is compile without CUDA support
  #define SET_DEVICE(...)
  #define UNSET_DEVICE(...)
#endif

 private:
  // context used by this Storage Manager
  Context initilal_context_;
};

/*!
 * \brief Class, which contains the CPU specific methods used by PooledStorageManager.
 */
class ContextHelperCPU : public ContextHelper {
 public:
  std::tuple<size_t, size_t> getMemoryInfo() const  override {
#if defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return std::make_tuple(statex.ullAvailPhys, status.ullTotalPhys)
#else   // Linux and iOS
    struct sysinfo info = {};
    if (sysinfo(&info) < 0)
      LOG(FATAL) << "Error: sysinfo failed";

    return std::make_tuple(info.freeram, info.totalram);
#endif
  }

  int Malloc(void **ppNtr, size_t size) const override {
    return (*ppNtr = std::malloc(size))? 0 : -1;
  }

  void Free(void *dptr) const override {
    std::free(dptr);
  }
};

#if MXNET_USE_CUDA
/*!
 * \brief Class, which contains the GPU specific methods used by PooledStorageManager.
 */
class ContextHelperGPU : public ContextHelper {
 public:
  std::tuple<size_t, size_t> getMemoryInfo() const override {
    size_t free, total;
    const auto cuda_status = cudaMemGetInfo(&free, &total);
    if (cudaSuccess != cuda_status)
      LOG(FATAL) << "Error: cudaMemGetInfo fails " << cudaGetErrorString(cuda_status);
    return std::make_tuple(free, total);
  }

  int Malloc(void **ppPntr, size_t size) const override {
    return cudaMalloc(ppPntr, size);
  }

  void Free(void *dptr) const override {
    CUDA_CALL(cudaFree(dptr));
  }

  const CudaDeviceStore *SetCurrentDevice(const Context &ctx) const override {
    return new CudaDeviceStore(ctx.real_dev_id(), true);
  }
};

/*!
 * \brief Class, which contains the CPU_Pinned specific methods used by PooledStorageManager.
 * When MxNet is compiled for MXNET_USE_CUDA=0, this class coincides with ContextHelperCPU
 */
class ContextHelperPinned : public ContextHelperGPU {
 public:
  int Malloc(void **ppPntr, size_t size) const override {
    // make the memory available across all devices
    return cudaHostAlloc(ppPntr, size, cudaHostAllocPortable);
  }
  void Free(void *dptr) const override {
    CUDA_CALL(cudaFreeHost(dptr));
  }
};

#else
typedef  ContextHelperCPU ContextHelperPinned;
#endif

}  // namespace storage
}  // namespace mxnet

#endif  // !defined(ANDROID) && !defined(__ANDROID__)

#endif  // MXNET_STORAGE_STORAGE_MANAGER_HELPERS_H_
