#include "caffe2/core/operator.h"
#include "caffe2/core/tensor.h"

namespace caffe2 {
namespace {

class GatherPaddingOp final : public Operator<CPUContext> {
 public:
  GatherPaddingOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator(operator_def, ws),
        startPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("padding_width", 1)),
        endPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("end_padding_width", -1)) {
    CHECK_GE(startPaddingWidth_, 0);
    if (endPaddingWidth_ < 0) {
      endPaddingWidth_ = startPaddingWidth_;
    }
  }

  bool RunOnDevice() override {
    if (startPaddingWidth_ == 0 && endPaddingWidth_ == 0) {
      Output(0)->Resize(std::vector<TIndex>(0));
      if (OutputSize() == 2) {
        Output(1)->Resize(std::vector<TIndex>(0));
      }
      return true;
    }
    return DispatchHelper<TensorTypes<float, double, int, int64_t, bool>>::call(
        this, Input(0));
  }

  template <typename T>
  bool DoRunWithType() {
    const auto& in = Input(0);
    CHECK_GE(in.ndim(), 1);
    const auto outer_size = in.dims()[0];
    const auto block_size = std::accumulate(
        in.dims().begin() + 1, in.dims().end(), 1, std::multiplies<TIndex>());
    const auto pad_width = startPaddingWidth_ + endPaddingWidth_;

    // if no lengths is provided, assume it is a single full-span entry
    const int64_t* lengths_ptr = &outer_size;
    int64_t lengths_size = 1;
    if (InputSize() > 1) {
      const auto& lengths = Input(1);
      lengths_ptr = lengths.data<int64_t>();
      lengths_size = lengths.size();
    }

    std::vector<TIndex> padShape(in.dims().begin() + 1, in.dims().end());
    // output will contain accumulator over paddings
    Output(0)->Resize(padShape);
    T* padding_start_ptr = Output(0)->mutable_data<T>();
    memset(padding_start_ptr, 0, sizeof(T) * block_size);

    // if no end_padding is provided, assume it's the same as start_padding
    T* padding_end_ptr = padding_start_ptr;
    if (OutputSize() == 2) {
      Output(1)->Resize(padShape);
      padding_end_ptr = Output(1)->mutable_data<T>();
      memset(padding_end_ptr, 0, sizeof(T) * block_size);
    }

    const auto* in_ptr = in.data<T>();
    int64_t total_length = 0;
    for (int i = 0; i < lengths_size; ++i) {
      // check total length consistency
      const auto length = lengths_ptr[i];
      total_length += length;
      CHECK_LE(total_length, outer_size);

      // accumulate start paddings
      for (int j = 0; j < startPaddingWidth_; ++j) {
        for (int k = 0; k < block_size; ++k) {
          padding_start_ptr[k] += in_ptr[k];
        }
        in_ptr += block_size;
      }
      in_ptr += block_size * (length - pad_width);
      // accumulate end paddings
      for (int j = 0; j < endPaddingWidth_; ++j) {
        for (int k = 0; k < block_size; ++k) {
          padding_end_ptr[k] += in_ptr[k];
        }
        in_ptr += block_size;
      }
    }
    return true;
  }

 private:
  int startPaddingWidth_;
  int endPaddingWidth_;
};

class RemovePaddingOp final : public Operator<CPUContext> {
 public:
  RemovePaddingOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator(operator_def, ws),
        startPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("padding_width", 1)),
        endPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("end_padding_width", -1)) {
    CHECK_GE(startPaddingWidth_, 0);
    if (endPaddingWidth_ < 0) {
      endPaddingWidth_ = startPaddingWidth_;
    }
  }

  bool RunOnDevice() override {
    if (startPaddingWidth_ == 0 && endPaddingWidth_ == 0) {
      Output(0)->CopyFrom(Input(0));
      if (OutputSize() == 2) {
        Output(1)->CopyFrom(Input(1));
      }
      return true;
    }
    return DispatchHelper<TensorTypes<float, double, int, int64_t, bool>>::call(
        this, Input(0));
  }

  template <typename T>
  bool DoRunWithType() {
    const auto& in = Input(0);
    CHECK_GE(in.ndim(), 1);
    const auto outer_size = in.dims()[0];
    const auto block_size = std::accumulate(
        in.dims().begin() + 1, in.dims().end(), 1, std::multiplies<TIndex>());
    const auto pad_width = startPaddingWidth_ + endPaddingWidth_;

    // if no lengths is provided, assume it is a single full-span entry
    const int64_t* lengths_ptr = &outer_size;
    int64_t lengths_size = 1;
    if (InputSize() > 1) {
      const auto& lengths = Input(1);
      lengths_ptr = lengths.data<int64_t>();
      lengths_size = lengths.size();
    }

    auto* out = Output(0);
    {
      auto out_dims = in.dims();
      out_dims[0] -= pad_width * lengths_size;
      out->Resize(std::move(out_dims));
    }
    const auto* in_ptr = in.data<T>();
    auto* out_ptr = out->mutable_data<T>();
    int64_t total_length = 0;
    for (int i = 0; i < lengths_size; ++i) {
      // check that total length is consistent
      const auto length = lengths_ptr[i];
      total_length += length;
      CHECK_LE(total_length, outer_size);
      std::copy(
          in_ptr + block_size * startPaddingWidth_,
          in_ptr + block_size * (length - endPaddingWidth_),
          out_ptr);
      in_ptr += block_size * length;
      out_ptr += block_size * (length - pad_width);
    }
    if (OutputSize() == 1) {
      return true;
    }
    auto* lengths_out = Output(1);
    lengths_out->Resize(lengths_size);
    std::transform(
        lengths_ptr,
        lengths_ptr + lengths_size,
        lengths_out->mutable_data<int64_t>(),
        [pad_width](int64_t x) { return x - pad_width; });
    return true;
  }

 private:
  int startPaddingWidth_;
  int endPaddingWidth_;
};

class AddPaddingOp final : public Operator<CPUContext> {
 public:
  AddPaddingOp(const OperatorDef& operator_def, Workspace* ws)
      : Operator(operator_def, ws),
        startPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("padding_width", 1)),
        endPaddingWidth_(
            OperatorBase::GetSingleArgument<int>("end_padding_width", -1)) {
    CHECK_GE(startPaddingWidth_, 0);
    if (endPaddingWidth_ < 0) {
      endPaddingWidth_ = startPaddingWidth_;
    }
  }

  bool RunOnDevice() override {
    if (startPaddingWidth_ == 0 && endPaddingWidth_ == 0) {
      Output(0)->CopyFrom(Input(0));
      if (OutputSize() == 2) {
        Output(1)->CopyFrom(Input(1));
      }
      return true;
    }
    return DispatchHelper<TensorTypes<float, double, int, int64_t, bool>>::call(
        this, Input(0));
  }

  template <typename T>
  bool DoRunWithType() {
    const auto& in = Input(0);
    CHECK_GE(in.ndim(), 1);
    const auto outer_size = in.dims()[0];
    const auto block_size = std::accumulate(
        in.dims().begin() + 1, in.dims().end(), 1, std::multiplies<TIndex>());

    // if no lengths is provided, assume it is a single full-span entry
    const int64_t* lengths_ptr = &outer_size;
    int64_t lengths_size = 1;
    if (InputSize() > 1) {
      const auto& lengths = Input(1);
      lengths_ptr = lengths.data<int64_t>();
      lengths_size = lengths.size();
    }

    // fetch paddings
    // input_size == 2 : pad with zeros
    // input_size == 3 : start and end paddings are the same
    // input_size == 4 : different start and end paddings
    const T* padding_start_ptr = nullptr;
    const T* padding_end_ptr = nullptr;
    if (InputSize() >= 3) {
      auto& padding_start = Input(2);
      CHECK_EQ(block_size, padding_start.size());
      padding_start_ptr = padding_start.data<T>();
    }
    if (InputSize() == 4) {
      auto& padding_end = Input(3);
      CHECK_EQ(block_size, padding_end.size());
      padding_end_ptr = padding_end.data<T>();
    } else {
      padding_end_ptr = padding_start_ptr;
    }

    auto* out = Output(0);
    {
      auto out_dims = in.dims();
      out_dims[0] += (startPaddingWidth_ + endPaddingWidth_) * lengths_size;
      out->Resize(std::move(out_dims));
    }
    const auto* in_ptr = in.data<T>();
    auto* out_ptr = out->mutable_data<T>();
    int64_t total_length = 0;
    for (int i = 0; i < lengths_size; ++i) {
      // check that total length is consistent
      const auto length = lengths_ptr[i];
      total_length += length;
      CHECK_LE(total_length, outer_size);
      // copy padding before
      if (!padding_start_ptr) {
        memset(out_ptr, 0, block_size * startPaddingWidth_ * sizeof(T));
        out_ptr += block_size * startPaddingWidth_;
      } else {
        for (int j = 0; j < startPaddingWidth_; ++j) {
          std::copy(padding_start_ptr, padding_start_ptr + block_size, out_ptr);
          out_ptr += block_size;
        }
      }
      // copy payload
      const auto num_elems = block_size * length;
      std::copy(in_ptr, in_ptr + num_elems, out_ptr);
      in_ptr += num_elems;
      out_ptr += num_elems;
      // copy padding after
      if (!padding_end_ptr) {
        memset(out_ptr, 0, block_size * endPaddingWidth_ * sizeof(T));
        out_ptr += block_size * endPaddingWidth_;
      } else {
        for (int j = 0; j < endPaddingWidth_; ++j) {
          std::copy(padding_end_ptr, padding_end_ptr + block_size, out_ptr);
          out_ptr += block_size;
        }
      }
    }
    if (OutputSize() == 1) {
      return true;
    }
    auto* lengths_out = Output(1);
    lengths_out->Resize(lengths_size);
    const auto pad_width = startPaddingWidth_ + endPaddingWidth_;
    std::transform(
        lengths_ptr,
        lengths_ptr + lengths_size,
        lengths_out->mutable_data<int64_t>(),
        [pad_width](int64_t x) { return x + pad_width; });
    return true;
  }

 private:
  int startPaddingWidth_;
  int endPaddingWidth_;
};

REGISTER_CPU_OPERATOR(AddPadding, AddPaddingOp);
REGISTER_CPU_OPERATOR(RemovePadding, RemovePaddingOp);
REGISTER_CPU_OPERATOR(GatherPadding, GatherPaddingOp);

struct GetAddPadingGradient : public GradientMakerBase {
  using GradientMakerBase::GradientMakerBase;
  vector<OperatorDef> GetGradientDefs() override {
    // whether to provide lengths as input to gradient
    vector<std::string> g_inputs{GO(0)};
    if (Def().input_size() > 1) {
      CAFFE_ENFORCE(Def().output_size() > 1);
      g_inputs.push_back(O(1));
    }

    vector<OperatorDef> ops;
    // gradient on the data
    ops.push_back(CreateOperatorDef(
        "RemovePadding", "", g_inputs, vector<string>{GI(0)}, Def().arg()));
    // gradient on the start_padding (and end_padding)
    if (Def().input_size() >= 3) {
      std::vector<string> padding_grads{GI(2)};
      if (Def().input_size() == 4) {
        padding_grads.push_back(GI(3));
      }
      auto g_inputs2 = g_inputs;
      ops.push_back(CreateOperatorDef(
          "GatherPadding", "", g_inputs2, padding_grads, Def().arg()));
    }
    return ops;
  }
};
REGISTER_GRADIENT(AddPadding, GetAddPadingGradient);

struct GetRemovePaddingGradient : public GradientMakerBase {
  using GradientMakerBase::GradientMakerBase;
  vector<OperatorDef> GetGradientDefs() override {
    // whether to provide lengths as input to gradient
    vector<std::string> g_inputs{GO(0)};
    if (Def().input_size() > 1) {
      CAFFE_ENFORCE(Def().output_size() > 1);
      g_inputs.push_back(O(1));
    }

    return SingleGradientDef(
        "AddPadding", "", g_inputs, vector<string>{GI(0)}, Def().arg());
  }
};
REGISTER_GRADIENT(RemovePadding, GetRemovePaddingGradient);

OPERATOR_SCHEMA(AddPadding)
    .NumInputs(1, 4)
    .NumOutputs(1, 2)
    .SetDoc(R"DOC(
Given a partitioned tensor T<N, D1..., Dn>, where the partitions are
defined as ranges on its outer-most (slowest varying) dimension N,
with given range lengths, return a tensor T<N + 2*pad_width, D1 ..., Dn>
with paddings added to the start and end of each range.
Optionally, different paddings can be provided for beginning and end. Paddings
provided must be a tensor T<D1..., Dn>.

If no padding is provided, add zero padding.
If no lengths vector is provided, add padding only once,
at the start and end of data.
)DOC")
    .Arg("pad_width", "Number of copies of padding to add around each range.")
    .Arg("end_pad_width", "(Optional) Specifies a different end-padding width.")
    .Input(0, "data_in", "(T<N, D1..., Dn>) Input data")
    .Input(
        1,
        "lengths",
        "(i64) Num of elements in each range. sum(lengths) = N.")
    .Input(2, "start_padding", "T<D1..., Dn> Padding data for range start.")
    .Input(
        3,
        "end_padding",
        "T<D1..., Dn> (optional) Padding for range end. "
        "If not provided, start_padding is used as end_padding as well.")
    .Output(0, "data_out", "(T<N + 2*pad_width, D1..., Dn>) Padded data.")
    .Output(1, "lengths_out", "(i64, optional) Lengths for each padded range.");

OPERATOR_SCHEMA(RemovePadding)
    .NumInputs(1, 2)
    .NumOutputs(1, 2)
    .SetDoc(R"DOC(
Remove padding around the edges of each segment of the input data. This is
the reverse opration of AddPadding, and uses the same arguments and conventions
for input and output data format.
)DOC")
    .Arg("pad_width", "Outer-size of padding to remove around each range.")
    .Arg("end_pad_width", "(Optional) Specifies a different end-padding width.")
    .Input(0, "data_in", "T<N, D1..., Dn> Input data")
    .Input(
        1,
        "lengths",
        "(i64) Num of elements in each range. sum(lengths) = N. "
        "If not provided, considers all data as a single segment.")
    .Output(0, "data_out", "(T<N - 2*pad_width, D1..., Dn>) Unpadded data.")
    .Output(
        1,
        "lengths_out",
        "(i64, optional) Lengths for each unpadded range.");

OPERATOR_SCHEMA(GatherPadding)
    .NumInputs(2)
    .NumOutputs(1, 2)
    .SetDoc(R"DOC(
Gather the sum of start and end paddings in a padded input sequence. Used in
order to compute the gradients of AddPadding w.r.t the padding tensors.
)DOC")
    .Arg("pad_width", "Outer-size of padding present around each range.")
    .Arg("end_pad_width", "(Optional) Specifies a different end-padding width.")
    .Input(0, "data_in", "T<N, D1..., Dn> Padded input data")
    .Input(
        1,
        "lengths",
        "(i64) Num of elements in each range. sum(lengths) = N. "
        "If not provided, considers all data as a single segment.")
    .Output(
        0,
        "padding_sum",
        "Sum of all start paddings, or of all "
        "paddings if end_padding_sum is not provided.")
    .Output(
        1,
        "end_padding_sum",
        "T<D1..., Dn> Sum of all end paddings, if provided.");
}
}
