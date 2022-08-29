/*
 * Copyright (C) 2006, 2007, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_WINDOW_OR_WORKER_GLOBAL_SCOPE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_WINDOW_OR_WORKER_GLOBAL_SCOPE_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class EventTarget;
class ExceptionState;
class StructuredSerializeOptions;
class ScriptState;
class ScriptValue;
class V8Function;

class CORE_EXPORT WindowOrWorkerGlobalScope {
  STATIC_ONLY(WindowOrWorkerGlobalScope);

 public:
  static void reportError(ScriptState*, EventTarget&, const ScriptValue&);

  static String btoa(EventTarget&,
                     const String& string_to_encode,
                     ExceptionState&);
  static String atob(EventTarget&,
                     const String& encoded_string,
                     ExceptionState&);

  static int setTimeout(ScriptState*,
                        EventTarget&,
                        V8Function* handler,
                        int timeout,
                        const HeapVector<ScriptValue>& arguments);
  static int setTimeout(ScriptState*,
                        EventTarget&,
                        const String& handler,
                        int timeout,
                        const HeapVector<ScriptValue>&);
  static int setInterval(ScriptState*,
                         EventTarget&,
                         V8Function* handler,
                         int timeout,
                         const HeapVector<ScriptValue>&);
  static int setInterval(ScriptState*,
                         EventTarget&,
                         const String& handler,
                         int timeout,
                         const HeapVector<ScriptValue>&);
  static void clearTimeout(EventTarget&, int timeout_id);
  static void clearInterval(EventTarget&, int timeout_id);

  static bool crossOriginIsolated(const ExecutionContext&);
  static String crossOriginEmbedderPolicy(const ExecutionContext&);

  static ScriptValue structuredClone(ScriptState*,
                                     EventTarget&,
                                     const ScriptValue& message,
                                     const StructuredSerializeOptions*,
                                     ExceptionState&);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_WINDOW_OR_WORKER_GLOBAL_SCOPE_H_
