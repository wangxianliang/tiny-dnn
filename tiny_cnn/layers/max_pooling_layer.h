/*
    Copyright (c) 2015, Taiga Nomi
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once

#include <string>
#include <vector>
#include <algorithm>

#include "tiny_cnn/core/backend_tiny.h"
#include "tiny_cnn/core/backend_nnp.h"
#include "tiny_cnn/core/backend_dnn.h"
#ifdef CNN_USE_AVX
#include "tiny_cnn/core/backend_avx.h"
#endif

#include "tiny_cnn/util/util.h"
#include "tiny_cnn/util/image.h"
#include "tiny_cnn/activations/activation_function.h"

namespace tiny_cnn {

/**
 * applies max-pooing operaton to the spatial data
 **/
template <typename Activation = activation::identity>
class max_pooling_layer : public feedforward_layer<Activation> {
 public:
    CNN_USE_LAYER_MEMBERS;
    typedef feedforward_layer<Activation> Base;

    /**
     * @param in_width     [in] width of input image
     * @param in_height    [in] height of input image
     * @param in_channels  [in] the number of input image channels(depth)
     * @param pooling_size [in] factor by which to downscale
     **/
    max_pooling_layer(cnn_size_t     in_width,
                      cnn_size_t     in_height,
                      cnn_size_t     in_channels,
                      cnn_size_t     pooling_size,
                      backend_t      backend_type = backend_t::tiny_cnn,
                      backend_params b_params = backend_params())
            : Base({ vector_type::data }) {
        if ((in_width % pooling_size) || (in_height % pooling_size)) {
            pooling_size_mismatch(in_width, in_height, pooling_size);
        }
        set_maxpool_params(shape3d(in_width, in_height, in_channels),
                           shape3d(in_width  / pooling_size,
                                   in_height / pooling_size,
                                   in_channels),
                           pooling_size, pooling_size);

        init_connection();
        init_backend(backend_type);
    }

    /**
     * @param in_width     [in] width of input image
     * @param in_height    [in] height of input image
     * @param in_channels  [in] the number of input image channels(depth)
     * @param pooling_size [in] factor by which to downscale
     * @param stride       [in] interval at which to apply the filters to the input
    **/
    max_pooling_layer(cnn_size_t     in_width,
                      cnn_size_t     in_height,
                      cnn_size_t     in_channels,
                      cnn_size_t     pooling_size,
                      cnn_size_t     stride,
                      backend_t      backend_type = backend_t::tiny_cnn,
                      backend_params b_params = backend_params())
            : Base({ vector_type::data }) {
        set_maxpool_params(
            shape3d(in_width, in_height, in_channels),
            shape3d(pool_out_dim(in_width, pooling_size, stride),
                    pool_out_dim(in_height, pooling_size, stride),
                    in_channels),
            pooling_size, stride);

        init_connection();
        init_backend(backend_type);
    }

    // move constructor
    max_pooling_layer(max_pooling_layer&& other)  // NOLINT
            : Base(std::move(other))
            , params_(std::move(other.params_))
            , out2in_(std::move(other.out2in_))
            , in2out_(std::move(other.in2out_))
            , max_pooling_layer_worker_storage_(
                std::move(other.max_pooling_layer_worker_storage_)) {
        init_connection();
        init_backend(std::move(Base::get_backend_type()));
    }

    size_t fan_in_size() const override {
        return out2in_[0].size();
    }

    size_t fan_out_size() const override {
        return 1;
    }

    void forward_propagation(const std::vector<tensor_t*>& in_data,
                             std::vector<tensor_t*>&       out_data) {
        // launch maxpool kernel
        Base::backend_->maxpool(in_data, out_data);

        // activations
        for_i(in_data[0]->size(), [&](int sample) {
            vec_t& out     = (*out_data[0])[sample];
            const vec_t& a = (*out_data[1])[sample];

            for (cnn_size_t i = 0; i < params_.out_.size(); i++) {
                out[i] = this->h_.f(a, i);
            };
        });
    }

    void back_propagation(const std::vector<tensor_t*>& in_data,
                          const std::vector<tensor_t*>& out_data,
                          std::vector<tensor_t*>&       out_grad,
                          std::vector<tensor_t*>&       in_grad) {
        // launch maxpool kernel
        Base::backend_->maxpool(in_data, out_data, out_grad, in_grad);
    }

    std::vector<index3d<cnn_size_t>>
    in_shape() const override { return { params_.in_ }; }

    std::vector<index3d<cnn_size_t>>
    out_shape() const override { return { params_.out_, params_.out_ }; }

    std::string layer_type() const override { return "max-pool"; }
    size_t pool_size() const { return params_.pool_size_; }

    void set_sample_count(cnn_size_t sample_count) override {
        max_pooling_layer_worker_storage_.out2inmax_.resize(sample_count, std::vector<cnn_size_t>(params_.out_.size()));
    }

private:
    maxpool_params params_;

    /* mapping out => in (1:N) */
    std::vector<std::vector<cnn_size_t> > out2in_;
    /* mapping in => out (N:1) */
    std::vector<cnn_size_t> in2out_;

    max_pooling_layer_worker_specific_storage
    max_pooling_layer_worker_storage_;

    static cnn_size_t pool_out_dim(cnn_size_t in_size,
                                   cnn_size_t pooling_size,
                                   cnn_size_t stride) {
        float_t tmp = static_cast<float_t>(in_size - pooling_size) / stride;
        return static_cast<cnn_size_t>(std::ceil(tmp) + float_t(1.0));
    }

    void connect_kernel(cnn_size_t pooling_size,
                        cnn_size_t outx,
                        cnn_size_t outy,
                        cnn_size_t c) {
        cnn_size_t dxmax = static_cast<cnn_size_t>(
            std::min(static_cast<size_t>(pooling_size),
                     params_.in_.width_ - outx * params_.stride_));

        cnn_size_t dymax = static_cast<cnn_size_t>(
            std::min(static_cast<size_t>(pooling_size),
                     params_.in_.height_ - outy * params_.stride_));

        for (cnn_size_t dy = 0; dy < dymax; dy++) {
            for (cnn_size_t dx = 0; dx < dxmax; dx++) {
                cnn_size_t in_index = params_.in_.get_index(
                    static_cast<cnn_size_t>(outx * params_.stride_ + dx),
                    static_cast<cnn_size_t>(outy * params_.stride_ + dy), c);
                cnn_size_t out_index = params_.out_.get_index(outx, outy, c);

                if (in_index >= in2out_.size()) {
                    throw nn_error("index overflow");
                }
                if (out_index >= out2in_.size()) {
                    throw nn_error("index overflow");
                }
                in2out_[in_index] = out_index;
                out2in_[out_index].push_back(in_index);
            }
        }
    }

    void init_connection() {
        in2out_.resize(params_.in_.size());
        out2in_.resize(params_.out_.size());
        //max_pooling_layer_worker_storage_.out2inmax_.resize(params_.out_.size());

        for (cnn_size_t c = 0; c < params_.in_.depth_; ++c) {
            for (cnn_size_t y = 0; y < params_.out_.height_; ++y) {
                for (cnn_size_t x = 0; x < params_.out_.width_; ++x) {
                    connect_kernel(static_cast<cnn_size_t>(params_.pool_size_),
                                   x, y, c);
                }
            }
        }
    }

    void init_backend(backend_t backend_type) {
        std::shared_ptr<core::backend> backend = nullptr;

        // allocate new backend
        if (backend_type == backend_t::tiny_cnn) {
            backend = std::make_shared<core::tiny_backend>(
                &out2in_,
                &in2out_,
                [this](const tensor_t& p_delta,
                       const tensor_t& out, tensor_t& c_delta) {
                    return Base::backward_activation(p_delta, out, c_delta);
                },
                &max_pooling_layer_worker_storage_);
        } else if (backend_type == backend_t::nnpack) {
            backend = std::make_shared<core::nnp_backend>(&params_);
        } else if (backend_type == backend_t::libdnn) {
            backend = std::make_shared<core::dnn_backend>();
#ifdef CNN_USE_AVX
        } else if (backend_type == backend_t::avx) {
            backend = std::make_shared<core::avx_backend>(
                &out2in_,
                &in2out_,
                [this](const tensor_t& p_delta,
                       const tensor_t& out, tensor_t& c_delta) {
                    return Base::backward_activation(p_delta, out, c_delta);
                },
                &max_pooling_layer_worker_storage_);
#endif
        } else {
            throw nn_error("Not supported backend type.");
        }

        if (backend) {
            Base::set_backend(backend);
            Base::backend_->set_layer(this);
            Base::backend_->set_type(backend_type);
        } else {
            throw nn_error("Could not allocate the backend.");
        }
    }

    void set_maxpool_params(const shape3d& in,
                            const shape3d& out,
                            cnn_size_t pooling_size,
                            cnn_size_t stride) {
        params_.in_        = in;
        params_.out_       = out;
        params_.pool_size_ = pooling_size;
        params_.stride_    = stride;
    }
};

}  // namespace tiny_cnn
