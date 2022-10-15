// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/critical_closure.h"

#import <UIKit/UIKit.h>

namespace base {
namespace internal {

ImmediateCriticalClosure::ImmediateCriticalClosure(StringPiece task_name,
                                                   OnceClosure closure)
    : critical_action_(task_name), closure_(std::move(closure)) {}

ImmediateCriticalClosure::~ImmediateCriticalClosure() {}

void ImmediateCriticalClosure::Run() {
  std::move(closure_).Run();
}

PendingCriticalClosure::PendingCriticalClosure(StringPiece task_name,
                                               OnceClosure closure)
    : task_name_(task_name), closure_(std::move(closure)) {}

PendingCriticalClosure::~PendingCriticalClosure() {}

void PendingCriticalClosure::Run() {
  critical_action_.emplace(task_name_);
  std::move(closure_).Run();
}

}  // namespace internal
}  // namespace base
