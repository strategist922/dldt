// Copyright (C) 2018 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <mock_icnn_network.hpp>
#include "gna_matcher.hpp"
#include <gna/gna_config.hpp>
#include <gna-api-types-xnn.h>
#include <gna_plugin/gna_executable_network.hpp>
#include "gna_plugin.hpp"
#include "gna_mock_api.hpp"
#include "matchers/precision_matcher.hpp"
#include "matchers/pwl_matcher.hpp"
#include "matchers/copy_matcher.hpp"
#include "matchers/diag_matcher.hpp"
#include "matchers/pwl_quantization_metrics_matcher.hpp"
#include "matchers/conv_matcher.hpp"
#include "matchers/pool_matcher.hpp"

#include <gmock/gmock-generated-actions.h>
#include <gmock/gmock-more-actions.h>
#include "gmock/gmock.h"

using namespace std;
using namespace InferenceEngine;
using namespace GNAPluginNS;
using namespace ::testing;

class NullAllocator : public IAllocator {
 void * ptr = nullptr;
public:
    NullAllocator() {
        ptr = malloc(1);
    }
    ~NullAllocator() {
        free(ptr);
    }
    void * lock(void * handle, LockOp = LOCK_FOR_WRITE)  noexcept override {
        return ptr;
    }
    void  unlock(void * handle) noexcept override {

    }
    void * alloc(size_t size) noexcept override {
        return ptr;
    }
    virtual bool   free(void* handle) noexcept {
        return true;
    }
    virtual void Release() noexcept {
        delete this;
    }
};

void GNAPropagateMatcher :: match() {
    try {
        // matching gna propagate forward call.
        GNAPlugin plugin(_env.config);
        size_t inputSize = 10;
        size_t outputSize = 10;

        auto loadNetworkFromIR = [&] () {
            CNNNetReader net_reader;
            ASSERT_NO_THROW_IE_EXCEPTION(net_reader.ReadNetwork(_env.model.data(), _env.model.length()));

            auto weights_fake = make_shared<TBlob<uint8_t>>(Precision::U8, C, SizeVector({std::numeric_limits<uint32_t>::max()}), make_shared<NullAllocator>());
            net_reader.SetWeights(weights_fake);

            auto net_original = net_reader.getNetwork();
            auto input_dims = net_original.getInputsInfo().begin()->second->getTensorDesc().getDims();
            auto output = net_original.getOutputsInfo();
            // sometimes network might be created without outputs - ex memory output only
            auto output_dims = !output.empty() ? output.begin()->second->getTensorDesc().getDims() : input_dims;

            inputSize = details::product(std::begin(input_dims), std::end(input_dims));
            outputSize = details::product(std::begin(output_dims), std::end(output_dims));

            size_t weightsSize = 0;
            for (auto &layer : net_original) {
                auto w = layer->blobs["weights"];
                auto b = layer->blobs["biases"];

                if (w) {
                    weightsSize += w->byteSize();
                }
                if (b) {
                    weightsSize += b->byteSize();
                }
            }
            auto weights = make_shared_blob<uint8_t >(Precision::U8, C, {weightsSize});

            weights->allocate();
            GNATest::fillWeights(weights);
            net_reader.SetWeights(weights);

            net_reader.getNetwork().setTargetDevice(_env.target_device);

            if (_env.cb) {
                auto network = net_reader.getNetwork();
                _env.cb(network);
            }

            plugin.LoadNetwork(net_reader.getNetwork());
        };

        auto loadNetworkFromAOT = [&] () {
            plugin.ImportNetwork(_env.importedModelFileName);
        };

        TBlob<float>::Ptr input, output;
        size_t in_N = 1;
        size_t out_N = in_N;
        size_t in_C;
        size_t out_C;


        auto loadNetwork = [&]() {
            if (!_env.importedModelFileName.empty()) {
                ASSERT_NO_FATAL_FAILURE(loadNetworkFromAOT());
            } else {
                ASSERT_NO_FATAL_FAILURE(loadNetworkFromIR());
            }
            in_C = _env.matchOutput == true ? _env.input_init.size(): inputSize;
            out_C = _env.matchOutput == true ? _env.expected_output.size(): outputSize;

            input.reset(new TBlob<float>(Precision::FP32, NC, {in_C, in_N}));
            input->allocate();

            if(_env.matchOutput == true) {
                std::copy_n(_env.input_init.cbegin(), in_N * in_C, input->buffer().as<float *>());
            }

            output.reset(new TBlob<float>(Precision::FP32, NC, {out_C, out_N}));
            output->allocate();
        };


        StrictMock<GNACppApi> mockApi;
        std::vector<uint8_t> data;

        if (_env.target_device == InferenceEngine::TargetDevice::eGNA &&
                                                         !_env.matchThrows) {

            EXPECT_CALL(mockApi, GNAAlloc(_,_,_)).WillOnce(Invoke([&data](
                intel_gna_handle_t nGNADevice,   // handle to GNA accelerator
                uint32_t           sizeRequested,
                uint32_t*          sizeGranted
            ) {
                data.resize(sizeRequested);
                *sizeGranted = sizeRequested;
                return &data.front();
            }));
            EXPECT_CALL(mockApi, GNADeviceOpenSetThreads(_, _)).WillOnce(Return(1));

            if(_env.is_profiling_enabled == false) {
                EXPECT_CALL(mockApi, GNAWait(_, _, _)).WillOnce(Return(GNA_NOERROR));
            } else {
                EXPECT_CALL(mockApi, GNAWaitPerfRes(_, _, _, _)).WillOnce(Return(GNA_NOERROR));
            }

            if(_env.is_setup_of_omp_theads_expected == true) {
                EXPECT_CALL(mockApi, gmmSetThreads(_)).Times(1);
            } else {
                EXPECT_CALL(mockApi, gmmSetThreads(_)).Times(0);
            }

            std::unique_ptr<NNetComponentMatcher> combined(new NNetComponentMatcher());

            for (auto & matchWhat : _env.whatToMatch) {
                switch(matchWhat) {
                    case GnaPluginTestEnvironment::matchPrecision :
                        combined->add(new NNetPrecisionMatcher(_env.nnet_precision, INTEL_AFFINE));
                        break;
                    case GnaPluginTestEnvironment::matchProcType :
                        EXPECT_CALL(mockApi, GNAPropagateForward(_, _, _, _, _, Eq(_env.proc_type)))
                            .WillOnce(Return(GNA_NOERROR));
                        break;
                    case GnaPluginTestEnvironment::matchPwlInserted :
                        combined->add(new PWLMatcher(_env.matchInserted, _env.matchQuantity));
                        break;
                    case GnaPluginTestEnvironment::matchConvInserted:
                        combined->add(new ConvoluionLayerMatcher(_env.matchInserted, _env.matchQuantity));
                        break;
                    case GnaPluginTestEnvironment::matchMaxPoolingInserted:
                        combined->add(new PoolingLayerMatcher(_env.matchInserted, _env.matchQuantity, true));
                        break;
                    case GnaPluginTestEnvironment::matchPwlQuantizeMetrics :
                        combined->add(new PWLQuantizationMetricsMatcher(_env.type,
                                                                        _env.quantization_presicion_threshold,
                                                                        _env.quantization_segments_threshold));
                        break;
                    case GnaPluginTestEnvironment::matchCopyInserted :
                        combined->add(new CopyLayerMatcher(_env.matchInserted, _env.matchQuantity));
                        break;
                    case GnaPluginTestEnvironment::matchDiagonalInserted :
                        combined->add(new DiagLayerMatcher(_env.matchInserted, _env.matchQuantity));
                        break;
                    case GnaPluginTestEnvironment::saveArgs :
                        EXPECT_CALL(mockApi, GNAPropagateForward(_, _, _, _, _, _))
                            .WillOnce(DoAll(SaveArgPointee<1>(savedNet), Return(GNA_NOERROR)));
                        break;
                    default:
                        EXPECT_CALL(mockApi, GNAPropagateForward(_, _, _, _, _, _))
                            .WillOnce(Return(GNA_NOERROR));
                        break;
                }
            }
            if (combined && !combined->empty()) {
                EXPECT_CALL(mockApi, GNAPropagateForward(_, ::testing::MakeMatcher(combined.release()), _, _, _,_)).WillOnce(Return(GNA_NOERROR));
            }
        }

        loadNetwork();
        plugin.Infer(*input, *output);
        if(_env.matchOutput == true) {
            std::vector<float> actual_output(output->size());

            std::copy_n(output->cbuffer().as<float *>(), out_C * out_N, actual_output.begin());

            ASSERT_EQ(true,
                    std::equal(_env.expected_output.begin(), _env.expected_output.end(), actual_output.begin())
                  );
        }

        std::map<std::string, InferenceEngine::InferenceEngineProfileInfo> perfMap;
        plugin.GetPerformanceCounts(perfMap);

        if(_env.is_profiling_enabled != false) {
            ASSERT_NE(perfMap.empty(),true);
        } else {
            ASSERT_NE(perfMap.empty(),false);
        }

    }
    catch(std::exception &ex) {
        if (!_env.matchThrows) {
            FAIL() << ex.what();
        }
    }
    catch(...) {
        if (!_env.matchThrows) {
            FAIL() << "unknown exception thrown";
        }
    }

}

void GNAPluginCreationMatcher :: match() {
    if (_env.matchThrows) {
        ASSERT_ANY_THROW(GNAPlugin(_env.config));
        return;
    }
    GNAPlugin(_env.config);
}


void GNAPluginAOTMatcher :: match() {
    // matching gna_propagate forward call.
    MockICNNNetwork net;
    CNNNetReader net_reader;
    ASSERT_NO_THROW_IE_EXCEPTION(net_reader.ReadNetwork(_env.model.data(), _env.model.length()));

    size_t weightsSize = 440*3;

    auto weights = make_shared_blob<uint8_t >(Precision::U8, C, {weightsSize});
    weights->allocate();
    GNATest::fillWeights(weights);
    net_reader.SetWeights(weights);

    GNAPlugin plugin(_env.config);

    TBlob<float> input(Precision::FP32, NC, {10, 1});
    input.allocate();


    TBlob<float> output(Precision::FP32, NC, {10, 1});
    output.allocate();

    net_reader.getNetwork().setTargetDevice(TargetDevice::eGNA);

    if (_env.cb) {
        auto network = net_reader.getNetwork();
        _env.cb(network);
    }

    GNACppApi mockApi;
    std::vector<uint8_t> data(10000);
    EXPECT_CALL(mockApi, GNAAlloc(_,_,_)).WillOnce(DoAll(SetArgPointee<2>(10000), Return(&data.front())));
    EXPECT_CALL(mockApi, GNADeviceOpenSetThreads(_, _)).WillOnce(Return(1));

    plugin.LoadNetwork(net_reader.getNetwork());
    plugin.Export(_env.exportedModelFileName);
}


void GNADumpXNNMatcher::load(GNAPlugin & plugin) {

    // matching gna DumpXNN forward call.
    plugin = GNAPlugin(_env.config);

    auto loadNetworkFromIR = [&]() {
        MockICNNNetwork net;
        CNNNetReader net_reader;
        ASSERT_NO_THROW_IE_EXCEPTION(net_reader.ReadNetwork(_env.model.data(), _env.model.length()));

        size_t weightsSize = 440 * 3;

        auto weights = make_shared_blob<uint8_t>(Precision::U8, C, {weightsSize});
        weights->allocate();
        GNATest::fillWeights(weights);
        net_reader.SetWeights(weights);

        net_reader.getNetwork().setTargetDevice(TargetDevice::eGNA);

        if (_env.cb) {
            auto network = net_reader.getNetwork();
            _env.cb(network);
        }

        plugin.LoadNetwork(net_reader.getNetwork());
    };

    auto loadNetworkFromAOT = [&]() {
        plugin.ImportNetwork(_env.importedModelFileName);
    };

    auto loadNetwork = [&]() {
        if (!_env.importedModelFileName.empty()) {
            loadNetworkFromAOT();
        } else {
            loadNetworkFromIR();
        }
    };

    loadNetwork();
}

void GNADumpXNNMatcher::match() {

    GNACppApi mockApi;
    std::vector<uint8_t> data(10000);
    if (!_env.matchThrows) {

        EXPECT_CALL(mockApi, GNAAlloc(_,_,_)).WillOnce(DoAll(SetArgPointee<2>(10000), Return(&data.front())));
        EXPECT_CALL(mockApi, GNADeviceOpenSetThreads(_, _)).WillOnce(Return(1));
        intel_gna_model_header header = {};
        header.model_size = 1;
        EXPECT_CALL(mockApi, GNADumpXnn(_, _, _, _, _,_)).WillOnce(DoAll(SetArgPointee<3>(header), Return((void*)::operator new(1))));
        EXPECT_CALL(mockApi, GNAFree(_)).WillOnce(Return(GNA_NOERROR));
        EXPECT_CALL(mockApi, GNADeviceClose(_)).WillOnce(Return(GNA_NOERROR));
    }

    try {
        // matching gna DumpXNN forward call.
        GNAPluginNS::GNAPlugin plugin;
        load(plugin);
    }
    catch(std::exception &ex) {
        if (!_env.matchThrows) {
            FAIL() << ex.what();
        }
    }
    catch(...) {
        if (!_env.matchThrows) {
            FAIL() << "unknown exception thrown";
        }
    }

}

void GNAQueryStateMatcher :: match() {

   //  TODO : avoid copy pastes
    GNACppApi mockApi;
    std::vector<uint8_t> data(10000);

    std::shared_ptr<IExecutableNetworkInternal> executer;
    auto loadNetworkFromIR = [&]() {
        MockICNNNetwork net;
        CNNNetReader net_reader;
        ASSERT_NO_THROW_IE_EXCEPTION(net_reader.ReadNetwork(_env.model.data(), _env.model.length()));

        size_t weightsSize = 440 * 3;

        auto weights = make_shared_blob<uint8_t>(Precision::U8, C, {weightsSize});
        weights->allocate();
        GNATest::fillWeights(weights);
        net_reader.SetWeights(weights);

        net_reader.getNetwork().setTargetDevice(TargetDevice::eGNA);

        if (_env.cb) {
            auto network = net_reader.getNetwork();
            _env.cb(network);
        }

        executer.reset(new GNAExecutableNetwork(net_reader.getNetwork(), _env.config));
    };

    auto loadNetworkFromAOT = [&]() {
        executer.reset(new GNAExecutableNetwork(_env.importedModelFileName, _env.config));
    };

    auto loadNetwork = [&]() {
        if (!_env.importedModelFileName.empty()) {
            return loadNetworkFromAOT();
        } else {
            return loadNetworkFromIR();
        }
    };


    EXPECT_CALL(mockApi, GNAAlloc(_,_,_)).WillOnce(DoAll(SetArgPointee<2>(10000), Return(&data.front())));
    EXPECT_CALL(mockApi, GNADeviceOpenSetThreads(_, _)).WillOnce(Return(1));
    EXPECT_CALL(mockApi, GNAFree(_)).WillOnce(Return(GNA_NOERROR));
    EXPECT_CALL(mockApi, GNADeviceClose(_)).WillOnce(Return(GNA_NOERROR));

    try {
        loadNetwork();
        if (GnaPluginTestEnvironment::kAnyNotNull == _env.numberOfStates) {
            auto states = executer->QueryState();
            ASSERT_NE(states.size(), 0);
            // usually states are callable
            for (auto & state : states) {
                state->Reset();
            }
        } else if (_env.numberOfStates >= 0) {
            ASSERT_EQ(executer->QueryState().size(), _env.numberOfStates);
        } else {
            FAIL() << "number of memory states expectation not set";
        }

    }
    catch(std::exception &ex) {
        FAIL() << ex.what();
    }
    catch(...) {
        FAIL() << "unknown exception thrown";
    }
}