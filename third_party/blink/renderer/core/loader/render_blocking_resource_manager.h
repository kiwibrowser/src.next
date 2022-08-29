// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_RENDER_BLOCKING_RESOURCE_MANAGER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_RENDER_BLOCKING_RESOURCE_MANAGER_H_

#include "base/time/time.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/timer.h"

namespace blink {

class Document;
class FontFace;
class PendingLinkPreload;
class Node;
class ScriptElementBase;

// https://html.spec.whatwg.org/#render-blocking-mechanism with some extensions.
class CORE_EXPORT RenderBlockingResourceManager final
    : public GarbageCollected<RenderBlockingResourceManager> {
 public:
  explicit RenderBlockingResourceManager(Document&);
  ~RenderBlockingResourceManager() = default;

  RenderBlockingResourceManager(const RenderBlockingResourceManager&) = delete;
  RenderBlockingResourceManager& operator=(
      const RenderBlockingResourceManager&) = delete;

  bool HasRenderBlockingResources() const {
    return pending_stylesheet_owner_nodes_.size() || pending_scripts_.size() ||
           pending_preloads_.size() || imperative_font_loading_count_;
  }

  bool HasPendingStylesheets() const {
    return pending_stylesheet_owner_nodes_.size();
  }
  // Returns true if the sheet is successfully added as a render-blocking
  // resource.
  bool AddPendingStylesheet(const Node& owner_node);
  // If the sheet is a render-blocking resource, removes it and returns true;
  // otherwise, returns false with no operation.
  bool RemovePendingStylesheet(const Node& owner_node);

  void AddPendingScript(const ScriptElementBase& script);
  void RemovePendingScript(const ScriptElementBase& script);

  // We additionally allow font preloading (via <link rel="preload"> or Font
  // Loading API) to block rendering for a short period, so that preloaded fonts
  // have a higher chance to be used by the first paint.
  // Design doc: https://bit.ly/36E8UKB
  // TODO(crbug.com/1271296): `kRegular` is no longer in use. Clean up the code.
  enum class PreloadType { kRegular, kShortBlockingFont };
  void AddPendingPreload(const PendingLinkPreload& link, PreloadType type);
  void RemovePendingPreload(const PendingLinkPreload& link);

  void AddImperativeFontLoading(FontFace*);
  void RemoveImperativeFontLoading();
  void EnsureStartFontPreloadTimer();
  void FontPreloadingTimerFired(TimerBase*);

  void Trace(Visitor* visitor) const;

 private:
  friend class RenderBlockingResourceManagerTest;

  // Exposed to unit tests only.
  void SetFontPreloadTimeoutForTest(base::TimeDelta timeout);
  void DisableFontPreloadTimeoutForTest();
  bool FontPreloadTimerIsActiveForTest() const;

  Member<Document> document_;

  // Tracks the currently loading top-level stylesheets which block
  // rendering from starting. Sheets loaded using the @import directive are not
  // directly included in this set. See:
  // https://html.spec.whatwg.org/multipage/links.html#link-type-stylesheet
  // https://html.spec.whatwg.org/multipage/semantics.html#update-a-style-block
  HeapHashSet<WeakMember<const Node>> pending_stylesheet_owner_nodes_;

  // Tracks the currently pending render-blocking script elements.
  HeapHashSet<WeakMember<const ScriptElementBase>> pending_scripts_;

  // Tracks the currently pending render-blocking preload and modulepreload
  // links, including short-blocking font preloads.
  HeapHashMap<WeakMember<const PendingLinkPreload>, PreloadType>
      pending_preloads_;

  unsigned imperative_font_loading_count_ = 0;

  HeapTaskRunnerTimer<RenderBlockingResourceManager> font_preload_timer_;
  base::TimeDelta font_preload_timeout_;
  bool font_preload_timer_has_fired_ = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_RENDER_BLOCKING_RESOURCE_MANAGER_H_
