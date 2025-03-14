/*
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All Rights Reserved
 * Copyright 2020 The TensorFlow Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LUCI_INTERPRETER_PAL_FULLY_CONNECTED_COMMON_H
#define LUCI_INTERPRETER_PAL_FULLY_CONNECTED_COMMON_H

#include "PALUtils.h"
#include "Params.h"

namespace luci_interpreter_pal
{

template <typename InputType, typename WeightType, typename OutputType, typename BiasType>
inline void FullyConnected(const FullyConnectedParams &params, const int32_t *input_shape,
                           const InputType *input_data, const int32_t *filter_shape,
                           const WeightType *filter_data, const BiasType *bias_data,
                           const int32_t *output_shape, OutputType *output_data)
{
  const int32_t input_offset = params.input_offset;
  const int32_t filter_offset = params.weights_offset;
  const int32_t output_offset = params.output_offset;
  const int32_t output_multiplier = params.output_multiplier;
  const int output_shift = params.output_shift;
  const int32_t output_activation_min = params.quantized_activation_min;
  const int32_t output_activation_max = params.quantized_activation_max;

  const int batches = input_shape[0];
  const int output_depth = output_shape[1];
  const int accum_depth = filter_shape[1];

  for (int b = 0; b < batches; ++b)
  {
    for (int out_c = 0; out_c < output_depth; ++out_c)
    {
      BiasType acc = 0;
      for (int d = 0; d < accum_depth; ++d)
      {
        int32_t input_val = input_data[b * accum_depth + d];
        int32_t filter_val = filter_data[out_c * accum_depth + d];
        acc += (filter_val + filter_offset) * (input_val + input_offset);
      }
      if (bias_data)
      {
        acc += bias_data[out_c];
      }
      int32_t acc_scaled = multiplyByQuantizedMultiplier(acc, output_multiplier, output_shift);
      acc_scaled += output_offset;
      acc_scaled = std::max(acc_scaled, output_activation_min);
      acc_scaled = std::min(acc_scaled, output_activation_max);
      output_data[out_c + output_depth * b] = static_cast<OutputType>(acc_scaled);
    }
  }
}
template <>
inline void FullyConnected(const FullyConnectedParams &params, const int32_t *input_shape,
                           const float *input_data, const int32_t *filter_shape,
                           const float *filter_data, const float *bias_data,
                           const int32_t *output_shape, float *output_data)
{
  const float output_activation_min = params.float_activation_min;
  const float output_activation_max = params.float_activation_max;

  const int batches = input_shape[0];
  const int output_depth = output_shape[1];
  const int accum_depth = filter_shape[1];

  for (int b = 0; b < batches; ++b)
  {
    for (int out_c = 0; out_c < output_depth; ++out_c)
    {
      float total = 0.f;
      for (int d = 0; d < accum_depth; ++d)
      {
        total += input_data[b * accum_depth + d] * filter_data[out_c * accum_depth + d];
      }
      float bias_value = 0.0f;
      if (bias_data)
      {
        bias_value = bias_data[out_c];
      }
      output_data[out_c + output_depth * b] =
        std::min(std::max(total + bias_value, output_activation_min), output_activation_max);
    }
  }
}

} // namespace luci_interpreter_pal

#endif // LUCI_INTERPRETER_PAL_FULLY_CONNECTED_COMMON_H
