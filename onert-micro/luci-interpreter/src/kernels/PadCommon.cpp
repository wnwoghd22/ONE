/*
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All Rights Reserved
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

#include "Builders.h"
#include "kernels/Utils.h"

#include "PadCommon.h"
#include "PALPad.h"

namespace luci_interpreter
{
void configure_kernel_CirclePadCommon(const circle::Operator *cur_op,
                                      BaseRuntimeGraph *runtime_graph)
{
  const auto num_inputs = cur_op->inputs()->size();

  const auto input1_index = cur_op->inputs()->operator[](0);
  const auto input2_index = cur_op->inputs()->operator[](1);
  const auto input3_index = num_inputs == 3 ? cur_op->inputs()->operator[](2) : -1;
  const auto output_index = cur_op->outputs()->operator[](0);

  assert(input1_index != -1);
  assert(input2_index != -1);
  assert(input3_index != -1 or num_inputs == 2);
  assert(output_index != -1);

  const auto input1_tensor = runtime_graph->getCircleTensorByIndex(input1_index);
  const auto input2_tensor = runtime_graph->getCircleTensorByIndex(input2_index);
  const auto input3_tensor =
    num_inputs == 3 ? runtime_graph->getCircleTensorByIndex(input3_index) : nullptr;
  const auto output_tensor = runtime_graph->getCircleTensorByIndex(output_index);

  assert(input1_tensor != nullptr);
  assert(input2_tensor != nullptr);
  assert(input3_tensor != nullptr or num_inputs == 2);
  assert(output_tensor != nullptr);

  LUCI_INTERPRETER_CHECK(Tensor::element_type(input2_tensor) == DataType::S32);
  LUCI_INTERPRETER_CHECK(Tensor::element_type(input1_tensor) ==
                         Tensor::element_type(output_tensor));
  if (input3_tensor != nullptr)
  {
    LUCI_INTERPRETER_CHECK(Tensor::element_type(input3_tensor) ==
                           Tensor::element_type(input1_tensor));
    // Value is scalar
    LUCI_INTERPRETER_CHECK(Tensor::num_elements(input3_tensor) == 1);
  }

  // Check shapes
  const int32_t *paddings_data =
    kernels::getTensorData<int32_t>(runtime_graph->getConstDataByTensor(input2_tensor));
  for (int i = 0; i < Tensor::num_dims(output_tensor); i++)
  {
    int output_dim = Tensor::dim(output_tensor, i);
    int expected_dim =
      Tensor::dim(input1_tensor, i) + paddings_data[i * 2] + paddings_data[i * 2 + 1];
    LUCI_INTERPRETER_CHECK(output_dim == expected_dim);
  }
}

void execute_kernel_CirclePadCommon(const circle::Operator *cur_op, BaseRuntimeGraph *runtime_graph)
{
  const auto num_inputs = cur_op->inputs()->size();

  const auto input1_index = cur_op->inputs()->operator[](0);
  const auto input2_index = cur_op->inputs()->operator[](1);
  const auto input3_index = num_inputs == 3 ? cur_op->inputs()->operator[](2) : -1;
  const auto output_index = cur_op->outputs()->operator[](0);

  assert(input1_index != -1);
  assert(input2_index != -1);
  assert(input3_index != -1 or num_inputs == 2);
  assert(output_index != -1);

  const auto input1_tensor = runtime_graph->getCircleTensorByIndex(input1_index);
  const auto input2_tensor = runtime_graph->getCircleTensorByIndex(input2_index);
  const auto input3_tensor =
    num_inputs == 3 ? runtime_graph->getCircleTensorByIndex(input3_index) : nullptr;
  const auto output_tensor = runtime_graph->getCircleTensorByIndex(output_index);

  assert(input1_tensor != nullptr);
  assert(input2_tensor != nullptr);
  assert(input3_tensor != nullptr or num_inputs == 2);
  assert(output_tensor != nullptr);

  luci_interpreter_pal::PadParams pad_params;
  const int num_input_dimensions = Tensor::num_dims(input1_tensor);
  pad_params.left_padding_count = num_input_dimensions;
  pad_params.right_padding_count = num_input_dimensions;

  const int32_t *paddings_data =
    kernels::getTensorData<int32_t>(runtime_graph->getConstDataByTensor(input2_tensor));
  for (int idx = num_input_dimensions - 1; idx >= 0; --idx)
  {
    pad_params.left_padding[idx] = paddings_data[idx * 2];
    pad_params.right_padding[idx] = paddings_data[idx * 2 + 1];
  }

  auto *input1_data = runtime_graph->getDataByTensor(input1_tensor);
  if (input1_data == nullptr)
    input1_data = runtime_graph->getConstDataByTensor(input1_tensor);
  assert(input1_data);

  auto *input2_data = runtime_graph->getConstDataByTensor(input2_tensor);
  assert(input2_data);

  auto *output_data = runtime_graph->getDataByTensor(output_tensor);
  assert(output_data);

  switch (Tensor::element_type(input1_tensor))
  {
#ifndef DIS_FLOAT
    case DataType::FLOAT32:
    {
      float pad_value =
        input3_tensor == nullptr
          ? 0.f
          : *kernels::getTensorData<float>(runtime_graph->getConstDataByTensor(input3_tensor));
      luci_interpreter_pal::Pad(pad_params, kernels::getTensorShape(input1_tensor),
                                kernels::getTensorData<float>(input1_data), &pad_value,
                                kernels::getTensorShape(output_tensor),
                                kernels::getTensorData<float>(output_data));
    }
    break;
#endif // DIS_FLOAT
    default:
      assert(false && "Unsupported type");
  }
}

} // namespace luci_interpreter
