#include "caffe2/mpi/mpi_ops.h"
#include "caffe2/core/context_gpu.h"
#include "caffe2/operators/operator_fallback_gpu.h"


namespace caffe2 {

// Here is a bunch of MPI macro definitions that allow us to see if the MPI
// version supports CUDA aware MPI functions or not.

#if OPEN_MPI
  #define CAFFE2_OMPI_VERSION \
    OMPI_MAJOR_VERSION * 10000 + OMPI_MINOR_VERSION * 100 + \
    OMPI_RELEASE_VERSION
  #if CAFFE2_OMPI_VERSION >= 20000
    // openmpi 2.x now supports compile time check whether cuda support is
    // built with openmpi.
    #include "mpi-ext.h" /* Needed for CUDA-aware check */
    #if MPIX_CUDA_AWARE_SUPPORT
      #define CAFFE2_HAS_CUDA_MPI_BROADCAST 1
      #define CAFFE2_HAS_CUDA_MPI_ALLREDUCE 1
    #endif  // MPIX_CUDA_AWARE_SUPPORT
  #else  // CAFFE2_OMPI_VERSION >= 2000
    // In the case of openmpi 1.x, we don't have compile-time flags to figure
    // out if cuda is built; as a result, we will assume that the user has built
    // openmpi with cuda.
    // CUDA-aware MPIBroadcast is introduced after openmpi 1.7.
    #if CAFFE2_OMPI_VERSION >= 10700
    #define CAFFE2_HAS_CUDA_MPI_BROADCAST 1
    #else  // CAFFE2_OMPI_VERSION >= 10700
    #define CAFFE2_HAS_CUDA_MPI_BROADCAST 0
    #endif  // CAFFE2_OMPI_VERSION >= 10700

    // CUDA-aware MPIAllreduce is introduced after openmpi 1.8.5.
    #if CAFFE2_OMPI_VERSION >= 10805
    #define CAFFE2_HAS_CUDA_MPI_ALLREDUCE 1
    #else  // CAFFE2_OMPI_VERSION >= 10805
    #define CAFFE2_HAS_CUDA_MPI_ALLREDUCE 0
    #endif  // CAFFE2_OMPI_VERSION >= 10805
  #endif  // CAFFE2_OMPI_VERSION >= 2000
#else  // !OPEN_MPI
  // We have not really tested against other MPI environments, so let's go for a
  // safe path and basically say we don't have cuda-aware functions.
  #define CAFFE2_HAS_CUDA_MPI_BROADCAST 0
  #define CAFFE2_HAS_CUDA_MPI_ALLREDUCE 0
#endif  // OPEN_MPI

// We allow a macro to force using fallback functions.
#ifdef CAFFE2_FORCE_FALLBACK_CUDA_MPI
#undef CAFFE2_HAS_CUDA_MPI_BROADCAST
#undef CAFFE2_HAS_CUDA_MPI_ALLREDUCE
#define CAFFE2_HAS_CUDA_MPI_BROADCAST 0
#define CAFFE2_HAS_CUDA_MPI_ALLREDUCE 0
#endif  // CAFFE2_FORCE_FALLBACK_CUDA_MPI

namespace {

REGISTER_CUDA_OPERATOR_WITH_ENGINE(
    CreateCommonWorld,
    MPI,
    MPICreateCommonWorldOp<CUDAContext>);
#if CAFFE2_HAS_CUDA_MPI_BROADCAST
REGISTER_CUDA_OPERATOR_WITH_ENGINE(Broadcast, MPI, MPIBroadcastOp<CUDAContext>);
REGISTER_CUDA_OPERATOR_WITH_ENGINE(
    Reduce,
    MPI,
    MPIReduceOp<float, CUDAContext>);
REGISTER_CUDA_OPERATOR_WITH_ENGINE(
    Allgather,
    MPI,
    MPIAllgatherOp<float, CUDAContext>);
#else
REGISTER_CUDA_OPERATOR_WITH_ENGINE(
    Broadcast,
    MPI,
    GPUFallbackOp<MPIBroadcastOp<CPUContext>>);
REGISTER_CUDA_OPERATOR_WITH_ENGINE(
    Reduce,
    MPI,
    GPUFallbackOp<MPIReduceOp<float, CPUContext>>);
REGISTER_CUDA_OPERATOR_WITH_ENGINE(
    Allgather,
    MPI,
    GPUFallbackOp<MPIAllgatherOp<float, CPUContext>>);
#endif

#if CAFFE2_HAS_CUDA_MPI_ALLREDUCE
REGISTER_CUDA_OPERATOR_WITH_ENGINE(
    Allreduce,
    MPI,
    MPIAllreduceOp<float, CUDAContext>);
#else
REGISTER_CUDA_OPERATOR_WITH_ENGINE(
    Allreduce,
    MPI,
    GPUFallbackOp<MPIAllreduceOp<float, CPUContext>>);
#endif
}  // namespace

}  // namespace caffe2
