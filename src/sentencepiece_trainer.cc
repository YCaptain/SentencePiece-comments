// Copyright 2018 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.!

#include "sentencepiece_trainer.h"
#include <string>
#include <vector>

#include "builder.h"
#include "common.h"
#include "flags.h"
#include "normalizer.h"
#include "sentencepiece.pb.h"
#include "sentencepiece_model.pb.h"
#include "trainer_factory.h"
#include "util.h"

namespace sentencepiece {
namespace {
    //设置默认的规范化器（Normalizer）对象的名称
static constexpr char kDefaultNormalizerName[] = "nmt_nfkc";
}  // namespace

// this header is automatically generated.
#include "spec_parser.h"

// static
//DOC：
//  用默认的规范化器设置与指定的训练方法训练SentencePiece模型
// 参数:
//      const TrainerSpec &trainer_spec -- 用以训练SentencePiece模型的指定训练方法
// 返回:
//      util::Status  -- 用于表示训练的状态与结果
util::Status SentencePieceTrainer::Train(const TrainerSpec &trainer_spec) {
  NormalizerSpec normalizer_spec;
  return Train(trainer_spec, normalizer_spec);
}

// static
// DOC：
//  用指定的规范化器设置与指定的训练方法训练SentencePiece模型
// 参数:
//      const TrainerSpec &trainer_spec -- 用以训练SentencePiece模型的指定训练方法
//      const NormalizerSpec &normalizer_spec -- 用以训练SentencePiece模型的指定规范化器设置
// 返回:
//      util::Status  -- 用于表示训练的状态与结果
util::Status SentencePieceTrainer::Train(
    const TrainerSpec &trainer_spec, const NormalizerSpec &normalizer_spec) {
  auto copied_normalizer_spec = normalizer_spec;
  RETURN_IF_ERROR(PopulateNormalizerSpec(&copied_normalizer_spec));
  auto trainer = TrainerFactory::Create(trainer_spec, copied_normalizer_spec);

  LOG(INFO) << "Starts training with : \n"
            << PrintProto(trainer_spec) << PrintProto(copied_normalizer_spec);

  return trainer->Train();
}

// static
// 用于从已有的的规范化器名称生成规范化器设置
// 参数:
//      util::min_string_view name -- 用以生成规范化器设置的已有的的规范化器名称字符串
// 返回:
//      NormalizerSpec spec -- 生成的规范化器设置
NormalizerSpec SentencePieceTrainer::GetNormalizerSpec(
    util::min_string_view name) {
  NormalizerSpec spec;
  spec.set_name(name.data(), name.size());
  CHECK_OK(normalizer::Builder::GetPrecompiledCharsMap(
      spec.name(), spec.mutable_precompiled_charsmap()));
  return spec;
}

// static
//DOC:
//  根据输入的命令行字符串指令重新配置SentencePiece模型的训练方法的设置与规范化器的设置
// 参数:
//      util::min_string_view args -- 输入的命令行字符串
//      TrainerSpec *trainer_spec -- 需要被重新配置的SentencePiece模型的训练方法的设置
//      NormalizerSpec *normalizer_spec -- 需要被重新配置的SentencePiece模型的规范化器的设置
// 返回:
//      util::Status  -- 用于表示操作的状态与结果
util::Status SentencePieceTrainer::MergeSpecsFromArgs(
    util::min_string_view _args, TrainerSpec *trainer_spec,
    NormalizerSpec *normalizer_spec) {
  CHECK_OR_RETURN(trainer_spec) << "`trainer_spec` must not be null.";
  CHECK_OR_RETURN(normalizer_spec) << "`normalizer_spec` must not be null.";

  absl::string_view args(_args.data(), _args.size());
  if (args.empty()) return util::OkStatus();

  for (auto arg : string_util::SplitPiece(args, " ")) {
    string_util::ConsumePrefix(&arg, "--");
    std::string key, value;
    auto pos = arg.find("=");
    if (pos == absl::string_view::npos) {
      key = std::string(arg);
    } else {
      key = std::string(arg.substr(0, pos));
      value = std::string(arg.substr(pos + 1));
    }

    // Exception.
    if (key == "normalization_rule_name") {
      normalizer_spec->set_name(value);
      continue;
    }

    if (key == "minloglevel") {
      flags::SetMinLogLevel(atoi(value.c_str()));
      continue;
    }

    const auto status_train = SetProtoField(key, value, trainer_spec);
    if (status_train.ok()) continue;
    if (!util::IsNotFound(status_train)) return status_train;

    const auto status_norm = SetProtoField(key, value, normalizer_spec);
    if (status_norm.ok()) continue;
    if (!util::IsNotFound(status_norm)) return status_norm;

    // Not found both in trainer_spec and normalizer_spec.
    if (util::IsNotFound(status_train) && util::IsNotFound(status_norm)) {
      return status_train;
    }
  }

  return util::OkStatus();
}

// static
//DOC:
//  根据输入的命令行字符串指令训练sentencepiece模型
// 参数:
//      util::min_string_view args -- 指定如何训练SentencePiece模型的命令行字符串
// 返回:
//      util::Status  -- 用于表示训练的状态与结果
util::Status SentencePieceTrainer::Train(util::min_string_view args) {
  LOG(INFO) << "Running command: " << args.data();
  TrainerSpec trainer_spec;
  NormalizerSpec normalizer_spec;
  RETURN_IF_ERROR(MergeSpecsFromArgs(args, &trainer_spec, &normalizer_spec));
  return Train(trainer_spec, normalizer_spec);
}

// static
//DOC:
// 用于对规范化器的规范增添数据与信息
// 参数:
//      NormalizerSpec &normalizer_spec -- 用以加入规范化器的规范中的数据与信息
// 返回:
//      util::Status  -- 用于表示操作的状态与结果
util::Status SentencePieceTrainer::PopulateNormalizerSpec(
    NormalizerSpec *normalizer_spec) {
  CHECK_OR_RETURN(normalizer_spec);

  if (!normalizer_spec->normalization_rule_tsv().empty()) {
    CHECK_OR_RETURN(normalizer_spec->precompiled_charsmap().empty())
        << "precompiled_charsmap is already defined.";
    normalizer::Builder::CharsMap chars_map;
    RETURN_IF_ERROR(normalizer::Builder::LoadCharsMap(
        normalizer_spec->normalization_rule_tsv(), &chars_map));
    RETURN_IF_ERROR(normalizer::Builder::CompileCharsMap(
        chars_map, normalizer_spec->mutable_precompiled_charsmap()));
    normalizer_spec->set_name("user_defined");
  } else {
    if (normalizer_spec->name().empty()) {
      normalizer_spec->set_name(kDefaultNormalizerName);
    }
    if (normalizer_spec->precompiled_charsmap().empty()) {
      RETURN_IF_ERROR(normalizer::Builder::GetPrecompiledCharsMap(
          normalizer_spec->name(),
          normalizer_spec->mutable_precompiled_charsmap()));
    }
  }

  return util::OkStatus();
}

}  // namespace sentencepiece
