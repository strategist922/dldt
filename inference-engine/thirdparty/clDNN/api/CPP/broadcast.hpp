﻿// Copyright (c) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

///////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../C/broadcast.h"
#include "primitive.hpp"


namespace cldnn
{
/// @addtogroup cpp_api C++ API
/// @{
/// @addtogroup cpp_topology Network Topology
/// @{
/// @addtogroup cpp_primitives Primitives
/// @{

/// @brief Broadcasts input to specified output size (broadcast size).
///
/// @details Takes input and copies it to output once or multiple times, until output will
///          reach the sizes specified in @p broadcast_sizes.
/// @n
/// @n Lets assume that:
/// @n      <tt>input_sizes = (in_b, in_f, in_y, in_x)</tt>
/// @n      <tt>broadcast_sizes = (bs_b, bs_f, bs_y, bs_x)</tt>
/// @n The input is broadcasted on each dimension where <tt>bs_{dim} > in_{dim}</tt> and <tt>bs_{dim}</tt>
///    is dividable by <tt>in_{dim}</tt> (input is copied <tt>bs_{dim} / in_{dim}</tt> times).
///    The dimensions where <tt>bs_{dim}</tt> is equal to <tt>in_{dim}</tt> remain unchanged.
/// @n The resulting output will have sizes equal to @p broadcast_sizes and contains values from
///    input that meet following criteria:
/// @n      <tt>output[(b, f, y, x)] = input[(b % in_b, f % in_f, y % in_y, x % in_x)]</tt>
/// @n where <tt>(b, f, y, x)</tt> is a position of value in a primitive output.
/// @n
/// @n@b Requirements:
/// @n - @p broadcast_sizes must be positive on all dimensions and compatible
///      with size of input (describe the same dimensions).
/// @n - @p broadcast_sizes must be greater than or equal to input sizes on
///      all dimensions. (For any dimension, if @p broadcast_sizes is lower
///      than input size on the dimension then @p broadcast_sizes will be replaced
///      by input size on this dimension.)
/// @n - For any dimension, if @p broadcast_sizes is greater than input size on
///      the dimension then @p broadcast_sizes must be dividable by input size
///      on this dimension.
/// @n Breaking any of these conditions will raise an exeption.
struct broadcast : public primitive_base<broadcast, CLDNN_PRIMITIVE_DESC(broadcast)>
{
    CLDNN_DECLARE_PRIMITIVE(broadcast)

    /// @brief Constructs broadcast primitive / layer.
    ///
    /// @param id              An identifier of new primitive.
    /// @param input           An identifier of primitive which is an input for newly created
    ///                        broadcast primitive.
    /// @param broadcast_sizes Sizes of broadcast. Output size of current primitive
    ///                        will match broadcast sizes (layout type will not change).
    ///                        If @p broadcast_sizes are not specified, the input sizes
    ///                        are used as @p broadcast_sizes.
    /// @param output_padding  Optional padding for output from primitive.
    broadcast(
        const primitive_id& id,
        const primitive_id& input,
        const tensor& broadcast_sizes = {0, 0, 0, 0},
        const padding& output_padding = padding()
    )
        : primitive_base(id, {input}, output_padding),
          broadcast_sizes(broadcast_sizes)
    {
    }

    /// @brief Constructs a copy from C API @CLDNN_PRIMITIVE_DESC{broadcast}
    broadcast(const dto* dto)
        : primitive_base(dto),
          broadcast_sizes(dto->broadcast_sizes)
    {
    }

    /// @brief Expected sizes of output from broadcast primitive.
    tensor broadcast_sizes;

protected:
    void update_dto(dto& dto) const override
    {
        dto.broadcast_sizes = broadcast_sizes;
    }
};
/// @}
/// @}
/// @}
}
