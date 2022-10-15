// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/font_list_async.h"

#include <utility>

#include "base/bind.h"
#include "base/task/task_runner_util.h"
#include "base/values.h"
#include "content/common/font_list.h"

namespace content {

void GetFontListAsync(base::OnceCallback<void(base::Value::List)> callback) {
  base::PostTaskAndReplyWithResult(GetFontListTaskRunner().get(), FROM_HERE,
                                   base::BindOnce(&GetFontList_SlowBlocking),
                                   std::move(callback));
}

}  // namespace content
