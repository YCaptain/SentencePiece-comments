// Copyright 2016 Google Inc.
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

#include "trainer_factory.h"

#include "bpe_model_trainer.h"
#include "char_model_trainer.h"
#include "unigram_model_trainer.h"
#include "util.h"
#include "word_model_trainer.h"

namespace sentencepiece {

// 从训练器特性和正规器特性初始化训练器。
// 
// 参数：
//       trainer_spec ---- 训练器特性
//       normalizer_spec ---- 正规器特性
//
// 返回：
//       训练器的唯一指针
// Instantiate Trainer instance from trainer_spec and normalization_spec
std::unique_ptr<TrainerInterface> TrainerFactory::Create(
    const TrainerSpec &trainer_spec, const NormalizerSpec &normalizer_spec) {
  switch (trainer_spec.model_type()) {
    case TrainerSpec::UNIGRAM:
      return port::MakeUnique<unigram::Trainer>(trainer_spec, normalizer_spec);
      break;
    case TrainerSpec::BPE:
      return port::MakeUnique<bpe::Trainer>(trainer_spec, normalizer_spec);
      break;
    case TrainerSpec::WORD:
      return port::MakeUnique<word::Trainer>(trainer_spec, normalizer_spec);
      break;
    case TrainerSpec::CHAR:
      return port::MakeUnique<character::Trainer>(trainer_spec,
                                                  normalizer_spec);
      break;
    default:
      LOG(FATAL) << "Unknown model_type: " << trainer_spec.model_type();
      break;
  }

  return port::MakeUnique<unigram::Trainer>(trainer_spec, normalizer_spec);
}
}  // namespace sentencepiece
