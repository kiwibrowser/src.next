/*
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2007 Apple Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAGE_PRINT_CONTEXT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAGE_PRINT_CONTEXT_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"
#include "ui/gfx/geometry/rect.h"

namespace gfx {
class SizeF;
}

namespace blink {

class Element;
class GraphicsContext;
class LocalFrame;
class Node;

class CORE_EXPORT PrintContext : public GarbageCollected<PrintContext> {
 public:
  // By shrinking to a width of 75%, we will render the correct physical
  // dimensions in paged media (i.e. cm, pt,). The shrinkage used
  // to be 80% to match other browsers - they have since moved on.
  // Wide pages will be scaled down more than this.
  // This value is the percentage inverted.
  static constexpr float kPrintingMinimumShrinkFactor = 1.33333333f;

  // This number determines how small we are willing to reduce the page content
  // in order to accommodate the widest line. If the page would have to be
  // reduced smaller to make the widest line fit, we just clip instead (this
  // behavior matches MacIE and Mozilla, at least).
  // TODO(rhogan): Decide if this quirk is still required.
  static constexpr float kPrintingMaximumShrinkFactor = 2;

  PrintContext(LocalFrame*, bool use_printing_layout);
  virtual ~PrintContext();

  LocalFrame* GetFrame() const { return frame_; }

  // Break up a page into rects without relayout.
  // FIXME: This means that CSS page breaks won't be on page boundary if the
  // size is different than what was passed to BeginPrintMode(). That's probably
  // not always desirable.
  virtual void ComputePageRects(const gfx::SizeF& print_size);

  // Deprecated. Page size computation is already in this class, clients
  // shouldn't be copying it.
  virtual void ComputePageRectsWithPageSize(
      const gfx::SizeF& page_size_in_pixels);

  // These are only valid after page rects are computed.
  wtf_size_t PageCount() const { return page_rects_.size(); }
  const gfx::Rect& PageRect(wtf_size_t page_number) const {
    return page_rects_[page_number];
  }
  const Vector<gfx::Rect>& PageRects() const { return page_rects_; }

  // Enter print mode, updating layout for new page size.
  // This function can be called multiple times to apply new print options
  // without going back to screen mode.
  virtual void BeginPrintMode(float width, float height = 0);

  // Return to screen mode.
  virtual void EndPrintMode();

  // The following static methods are used by web tests:

  // Returns -1 if page isn't found.
  static int PageNumberForElement(Element*,
                                  const gfx::SizeF& page_size_in_pixels);
  static String PageProperty(LocalFrame*,
                             const char* property_name,
                             uint32_t page_number);
  static bool IsPageBoxVisible(LocalFrame*, uint32_t page_number);
  static String PageSizeAndMarginsInPixels(LocalFrame*,
                                           uint32_t page_number,
                                           int width,
                                           int height,
                                           int margin_top,
                                           int margin_right,
                                           int margin_bottom,
                                           int margin_left);
  static int NumberOfPages(LocalFrame*, const gfx::SizeF& page_size_in_pixels);

  virtual void Trace(Visitor*) const;

  bool use_printing_layout() const;

 protected:
  friend class PrintContextTest;

  void OutputLinkedDestinations(GraphicsContext&, const gfx::Rect& page_rect);
  bool IsFrameValid() const;

  Member<LocalFrame> frame_;
  Vector<gfx::Rect> page_rects_;

 private:
  void ComputePageRectsWithPageSizeInternal(
      const gfx::SizeF& page_size_in_pixels);
  void CollectLinkedDestinations(Node*);

  // Used to prevent misuses of BeginPrintMode() and EndPrintMode() (e.g., call
  // EndPrintMode() without BeginPrintMode()).
  bool is_printing_;

  // True when printing layout needs to be applied.
  bool use_printing_layout_;

  HeapHashMap<String, Member<Node>> linked_destinations_;
  bool linked_destinations_valid_;
};

class CORE_EXPORT ScopedPrintContext {
  STACK_ALLOCATED();

 public:
  explicit ScopedPrintContext(LocalFrame*);
  ScopedPrintContext(const ScopedPrintContext&) = delete;
  ScopedPrintContext& operator=(const ScopedPrintContext&) = delete;
  ~ScopedPrintContext();

  PrintContext* operator->() const { return context_; }

 private:
  PrintContext* context_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAGE_PRINT_CONTEXT_H_
