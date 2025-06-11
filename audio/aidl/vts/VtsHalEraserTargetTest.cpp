/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#define LOG_TAG "VtsHalEraserTest"
#include <aidl/Gtest.h>
#include <aidl/android/hardware/audio/effect/IEffect.h>
#include <aidl/android/hardware/audio/effect/IFactory.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_interface_utils.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <gtest/gtest.h>

#include "EffectHelper.h"
#include "TestUtils.h"

using namespace android;

using aidl::android::hardware::audio::effect::Descriptor;
using aidl::android::hardware::audio::effect::Eraser;
using aidl::android::hardware::audio::effect::getEffectTypeUuidEraser;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::hardware::audio::effect::IFactory;
using aidl::android::hardware::audio::effect::Parameter;
using aidl::android::media::audio::common::AudioFormatType;
using aidl::android::media::audio::common::PcmType;
using aidl::android::media::audio::eraser::Mode;
using android::hardware::audio::common::testing::detail::TestExecutionTracer;

class EraserTestHelper : public EffectHelper {
  public:
    EraserTestHelper(std::pair<std::shared_ptr<IFactory>, Descriptor> descPair)
        : mFactory(descPair.first) {
        mDescriptor = descPair.second;
    }

    void SetUpEraser() {
        ASSERT_NE(nullptr, mFactory);
        ASSERT_NO_FATAL_FAILURE(create(mFactory, mEffect, mDescriptor));
    }

    void TearDownEraser() {
        ASSERT_NO_FATAL_FAILURE(destroy(mFactory, mEffect));
        mOpenEffectReturn = IEffect::OpenEffectReturn{};
    }

    bool isModeSupported(Mode mode) {
        if (!mEffect) return false;

        Parameter param;
        Eraser::Id eraserId = Eraser::Id::make<Eraser::Id::commonTag>(Eraser::capability);
        Parameter::Id capId = Parameter::Id::make<Parameter::Id::eraserTag>(eraserId);
        EXPECT_IS_OK(mEffect->getParameter(capId, &param));

        const auto specific = param.get<Parameter::specific>();
        const auto eraser = specific.get<Parameter::Specific::eraser>();
        const auto cap = eraser.get<Eraser::capability>();
        return std::find(cap.modes.begin(), cap.modes.end(), mode) != cap.modes.end();
    }

    bool setEraserMode(Mode mode) {
        if (!mEffect) return false;

        using EraserConfiguration = aidl::android::media::audio::eraser::Configuration;
        Eraser eraser = Eraser::make<Eraser::configuration>(EraserConfiguration({.mode = mode}));
        Parameter::Specific specific =
                Parameter::Specific::make<Parameter::Specific::eraser>(eraser);
        Parameter param = Parameter::make<Parameter::specific>(specific);
        EXPECT_IS_OK(mEffect->setParameter(param));
        return true;
    }

    static const long kInputFrameCount = 0x100, kOutputFrameCount = 0x100;
    const std::shared_ptr<IFactory> mFactory;
    std::shared_ptr<IEffect> mEffect;
    IEffect::OpenEffectReturn mOpenEffectReturn;
    const AudioChannelLayout kMonoChannel =
            AudioChannelLayout::make<AudioChannelLayout::layoutMask>(
                    AudioChannelLayout::LAYOUT_MONO);

  private:
    std::vector<std::pair<Eraser::Tag, Eraser>> mTags;
    void CleanUp() { mTags.clear(); }
};

enum ParamName { PARAM_INSTANCE_NAME, PARAM_AUDIO_FILE };

using EraserParamTestParam = std::tuple<std::pair<std::shared_ptr<IFactory>, Descriptor>>;
class EraserParamTest : public ::testing::TestWithParam<EraserParamTestParam>,
                        public EraserTestHelper {
  public:
    EraserParamTest() : EraserTestHelper(std::get<PARAM_INSTANCE_NAME>(GetParam())) {}

    void SetUp() override { ASSERT_NO_FATAL_FAILURE(SetUpEraser()); }

    void TearDown() override { ASSERT_NO_FATAL_FAILURE(TearDownEraser()); }
};

TEST_P(EraserParamTest, OpenFailWithUnsupportedFormats) {
    Eraser::Id eraserId = Eraser::Id::make<Eraser::Id::commonTag>(Eraser::capability);
    Parameter::Id capId = Parameter::Id::make<Parameter::Id::eraserTag>(eraserId);
    Parameter capParam;

    IEffect::OpenEffectReturn ret;
    const Parameter::Common supported = createParamCommon();
    Parameter::Common unsupported = supported;
    unsupported.input.base.sampleRate = 48000;
    ASSERT_STATUS(EX_NULL_POINTER, mEffect->open(unsupported, std::nullopt, &ret));

    unsupported = supported;
    unsupported.input.base.channelMask = AudioChannelLayout::make<AudioChannelLayout::layoutMask>(
            AudioChannelLayout::LAYOUT_STEREO);
    unsupported.output.base.channelMask = AudioChannelLayout::make<AudioChannelLayout::layoutMask>(
            AudioChannelLayout::LAYOUT_STEREO);
    ASSERT_STATUS(EX_NULL_POINTER, mEffect->open(unsupported, std::nullopt, &ret));

    unsupported = supported;
    unsupported.input.base.format.type = AudioFormatType::NON_PCM;
    ASSERT_STATUS(EX_NULL_POINTER, mEffect->open(unsupported, std::nullopt, &ret));
}

TEST_P(EraserParamTest, OpenCloseSeq) {
    Parameter::Common common = createParamCommon();
    ASSERT_NO_FATAL_FAILURE(open(mEffect, common, std::nullopt, &mOpenEffectReturn, EX_NONE));
    ASSERT_NE(nullptr, mEffect);

    ASSERT_NO_FATAL_FAILURE(close(mEffect));
}

TEST_P(EraserParamTest, SetClassifierMode) {
    Parameter::Common common = createParamCommon();
    ASSERT_NO_FATAL_FAILURE(open(mEffect, common, std::nullopt, &mOpenEffectReturn, EX_NONE));
    ASSERT_NE(nullptr, mEffect);

    // eraser effect must support CLASSIFIER mode
    ASSERT_TRUE(isModeSupported(Mode::CLASSIFIER));
    ASSERT_TRUE(setEraserMode(Mode::CLASSIFIER));

    ASSERT_NO_FATAL_FAILURE(close(mEffect));
}

TEST_P(EraserParamTest, SetEraserModeIfSupported) {
    Parameter::Common common = createParamCommon();
    ASSERT_NO_FATAL_FAILURE(open(mEffect, common, std::nullopt, &mOpenEffectReturn, EX_NONE));
    ASSERT_NE(nullptr, mEffect);

    if (isModeSupported(Mode::ERASER)) {
        ASSERT_TRUE(setEraserMode(Mode::ERASER));
    } else {
        GTEST_SKIP() << "Eraser mode not supported, skipping test";
    }

    ASSERT_NO_FATAL_FAILURE(close(mEffect));
}

std::vector<std::pair<std::shared_ptr<IFactory>, Descriptor>> kDescPair;
INSTANTIATE_TEST_SUITE_P(EraserParamTest, EraserParamTest,
                         ::testing::Combine(testing::ValuesIn(
                                 kDescPair = EffectFactoryHelper::getAllEffectDescriptors(
                                         IFactory::descriptor, getEffectTypeUuidEraser()))),
                         [](const testing::TestParamInfo<EraserParamTest::ParamType>& info) {
                             auto descriptor = std::get<PARAM_INSTANCE_NAME>(info.param).second;
                             std::string name = getPrefix(descriptor);
                             std::replace_if(
                                     name.begin(), name.end(),
                                     [](const char c) { return !std::isalnum(c); }, '_');
                             return name;
                         });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(EraserParamTest);

using EraserDataTestParam = std::tuple<std::pair<std::shared_ptr<IFactory>, Descriptor>>;
class EraserDataTest : public ::testing::TestWithParam<EraserDataTestParam>,
                       public EraserTestHelper {
  public:
    EraserDataTest() : EraserTestHelper(std::get<PARAM_INSTANCE_NAME>(GetParam())) {}

    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(SetUpEraser());

        Parameter::Common common = createParamCommon();
        ASSERT_NO_FATAL_FAILURE(open(mEffect, common, std::nullopt, &mOpenEffectReturn, EX_NONE));
        ASSERT_NE(nullptr, mEffect);
    }

    void TearDown() override {
        ASSERT_NO_FATAL_FAILURE(close(mEffect));
        ASSERT_NO_FATAL_FAILURE(TearDownEraser());
    }
};

TEST_P(EraserDataTest, ClassifyAudio) {
    // eraser effect must support CLASSIFIER mode
    ASSERT_TRUE(isModeSupported(Mode::CLASSIFIER));
    ASSERT_TRUE(setEraserMode(Mode::CLASSIFIER));
}

INSTANTIATE_TEST_SUITE_P(EraserDataTest, EraserDataTest,
                         ::testing::Combine(testing::ValuesIn(
                                 kDescPair = EffectFactoryHelper::getAllEffectDescriptors(
                                         IFactory::descriptor, getEffectTypeUuidEraser()))),
                         [](const testing::TestParamInfo<EraserDataTest::ParamType>& info) {
                             auto descriptor = std::get<PARAM_INSTANCE_NAME>(info.param).second;
                             std::string name = getPrefix(descriptor);
                             std::replace_if(
                                     name.begin(), name.end(),
                                     [](const char c) { return !std::isalnum(c); }, '_');
                             return name;
                         });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(EraserDataTest);

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new TestExecutionTracer());
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();
    return RUN_ALL_TESTS();
}
