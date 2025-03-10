/*
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All Rights Reserved
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

#include "luci/Pass/ForwardReshapeToUnaryOpPass.h"

#include <luci/IR/CircleNodes.h>
#include <luci/IR/CircleNodeVisitor.h>
#include <luci/Log.h>
#include <luci/Profile/CircleNodeOrigin.h>
#include <luci/Service/CircleShapeInference.h>
#include <luci/Service/Nodes/CircleConst.h>
#include <luci/Service/CircleNodeClone.h>

namespace
{

luci::CircleReshape *as_reshape(loco::Node *node)
{
  return dynamic_cast<luci::CircleReshape *>(node);
}

luci::CircleConst *clone_shape(luci::CircleReshape *reshape)
{
  const auto shape = dynamic_cast<luci::CircleConst *>(reshape->shape());
  // only support CircleConst for now
  if (shape == nullptr)
    return nullptr;

  // NOTE tflite and circle only supports S32
  // TODO just check with assert() after import handles this
  auto dtype = shape->dtype();
  if (dtype != loco::DataType::S32)
    return nullptr;

  return luci::clone(shape);
}

void copy_shape(luci::CircleReshape *reshape, luci::CircleReshape *new_reshape)
{
  auto ns_rank = reshape->newShape()->rank();
  new_reshape->newShape()->rank(ns_rank);
  for (uint32_t r = 0; r < ns_rank; ++r)
    new_reshape->newShape()->dim(r) = reshape->newShape()->dim(r);
}

luci::CircleReshape *create_cloned_reshape(luci::CircleReshape *reshape)
{
  assert(reshape != nullptr); // FIX_CALLER_UNLESS

  luci::CircleConst *cloned_shape = clone_shape(reshape);
  if (cloned_shape == nullptr)
    return nullptr;

  auto cloned_node = luci::clone_node(reshape, reshape->graph());
  if (cloned_node == nullptr)
    return nullptr;

  auto new_reshape = loco::must_cast<luci::CircleReshape *>(cloned_node);
  new_reshape->shape(cloned_shape);
  new_reshape->name(reshape->name() + "_C");
  luci::add_origin(new_reshape, luci::get_origin(reshape));

  return new_reshape;
}

bool forward_reshape(luci::CircleReshape *reshape, luci::CircleAbs *abs)
{
  assert(reshape != nullptr); // FIX_CALLER_UNLESS
  assert(abs != nullptr);     // FIX_CALLER_UNLESS

  auto new_reshape = create_cloned_reshape(reshape);
  if (not new_reshape)
    return false;

  // reconnect network
  loco::replace(abs).with(new_reshape);
  abs->x(reshape->tensor());
  new_reshape->tensor(abs);

  // Do shape inference for this node again.
  abs->shape_status(luci::ShapeStatus::UNDEFINED);

  return true;
}

bool forward_reshape(luci::CircleReshape *reshape, luci::CircleNeg *neg)
{
  assert(reshape != nullptr);
  assert(neg != nullptr);

  luci::CircleConst *cloned_shape = clone_shape(reshape);
  if (cloned_shape == nullptr)
    return false;

  auto name = reshape->name();
  assert(name.length() > 0);
  loco::Graph *graph = neg->graph();
  // create reshape placed after neg
  luci::CircleReshape *new_reshape = graph->nodes()->create<luci::CircleReshape>();
  copy_shape(reshape, new_reshape);
  new_reshape->shape(cloned_shape);
  new_reshape->name(name + "_C");
  luci::add_origin(new_reshape, luci::get_origin(reshape));

  // reconnect network
  loco::replace(neg).with(new_reshape);
  neg->x(reshape->tensor());
  new_reshape->tensor(neg);

  // Do shape inference for this node again.
  neg->shape_status(luci::ShapeStatus::UNDEFINED);

  return true;
}

bool forward_reshape(luci::CircleReshape *reshape, luci::CircleLogistic *logit)
{
  assert(reshape != nullptr); // FIX_CALLER_UNLESS
  assert(logit != nullptr);   // FIX_CALLER_UNLESS

  auto new_reshape = create_cloned_reshape(reshape);
  if (not new_reshape)
    return false;

  // reconnect network
  loco::replace(logit).with(new_reshape);
  logit->x(reshape->tensor());
  new_reshape->tensor(logit);

  // Do shape inference for this node again.
  logit->shape_status(luci::ShapeStatus::UNDEFINED);

  return true;
}

class ForwardReshape final : public luci::CircleNodeMutableVisitor<bool>
{
protected:
  bool visit(luci::CircleNode *node)
  {
    LOGGER(l);
    INFO(l) << "ForwardReshape: Unsupported operator: " << node->name() << std::endl;
    return false;
  }

  bool visit(luci::CircleAbs *node)
  {
    auto reshape = as_reshape(node->x());
    if (reshape == nullptr)
      return false;
    return forward_reshape(reshape, node);
  }

  bool visit(luci::CircleNeg *node)
  {
    auto reshape = as_reshape(node->x());
    if (reshape == nullptr)
      return false;
    return forward_reshape(reshape, node);
  }

  bool visit(luci::CircleLogistic *node)
  {
    auto reshape = as_reshape(node->x());
    if (reshape == nullptr)
      return false;

    return forward_reshape(reshape, node);
  }
  // TODO add more unary operators
};

} // namespace

namespace luci
{

/**
 * BEFORE
 *                       |
 *                  [CircleNode]  [CircleConst]
 *                       |       /
 *                 [CircleReshape]
 *                /      |
 *     [CircleNode]  [(UnaryOp)]
 *          |            |     \
 *          |            |      [CircleNode]
 *          |            |           |
 *
 *   UnaryOp: CircleNeg, ...
 *
 * AFTER
 *                       |
 *   [CircleConst]  [CircleNode]
 *         |       /     |
 *  [CircleReshape] [(UnaryOp)] [CircleConst]
 *         |             |      /
 *   [CircleNode] [CircleReshape]
 *         |             |      \
 *         |             |       [CircleNode]
 *         |             |            |
 *
 *   Note: new [CircleReshape] after [(UnaryOp)] added
 */
bool ForwardReshapeToUnaryOpPass::run(loco::Graph *g)
{
  bool changed = false;
  ForwardReshape forward;
  for (auto node : loco::active_nodes(loco::output_nodes(g)))
  {
    auto circle_node = loco::must_cast<luci::CircleNode *>(node);
    if (circle_node->accept(&forward))
      changed = true;
  }
  return changed;
}

} // namespace luci
