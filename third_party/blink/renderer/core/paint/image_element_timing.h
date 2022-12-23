// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_IMAGE_ELEMENT_TIMING_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_IMAGE_ELEMENT_TIMING_H_

#include <utility>

#include "base/time/time.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/supplementable.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class ImageResourceContent;
class PropertyTreeStateOrAlias;
class StyleFetchedImage;

// ImageElementTiming is responsible for tracking the paint timings for <img>
// elements for a given window.
class CORE_EXPORT ImageElementTiming final
    : public GarbageCollected<ImageElementTiming>,
      public Supplement<LocalDOMWindow> {
 public:
  static const char kSupplementName[];

  // The maximum amount of characters included in Element Timing and Largest
  // Contentful Paint for inline images.
  static constexpr const unsigned kInlineImageMaxChars = 100;

  explicit ImageElementTiming(LocalDOMWindow&);
  ImageElementTiming(const ImageElementTiming&) = delete;
  ImageElementTiming& operator=(const ImageElementTiming&) = delete;
  virtual ~ImageElementTiming() = default;

  static ImageElementTiming& From(LocalDOMWindow&);

  void NotifyImageFinished(const LayoutObject&, const ImageResourceContent*);

  void NotifyBackgroundImageFinished(const StyleFetchedImage*);
  base::TimeTicks GetBackgroundImageLoadTime(const StyleFetchedImage*);

  // Called when the LayoutObject has been painted. This method might queue a
  // presentation promise to compute and report paint timestamps.
  void NotifyImagePainted(
      const LayoutObject&,
      const ImageResourceContent& cached_image,
      const PropertyTreeStateOrAlias& current_paint_chunk_properties,
      const gfx::Rect& image_border);

  void NotifyBackgroundImagePainted(
      Node&,
      const StyleFetchedImage& background_image,
      const PropertyTreeStateOrAlias& current_paint_chunk_properties,
      const gfx::Rect& image_border);

  void NotifyImageRemoved(const LayoutObject*,
                          const ImageResourceContent* image);

  void Trace(Visitor*) const override;

 private:
  friend class ImageElementTimingTest;

  void NotifyImagePaintedInternal(
      Node&,
      const LayoutObject&,
      const ImageResourceContent& cached_image,
      const PropertyTreeStateOrAlias& current_paint_chunk_properties,
      base::TimeTicks load_time,
      const gfx::Rect& image_border);

  // Callback for the presentation promise. Reports paint timestamps.
  void ReportImagePaintPresentationTime(base::TimeTicks timestamp);

  // Class containing information about image element timing.
  class ElementTimingInfo final : public GarbageCollected<ElementTimingInfo> {
   public:
    ElementTimingInfo(const String& url,
                      const gfx::RectF& rect,
                      const base::TimeTicks& response_end,
                      const AtomicString& identifier,
                      const gfx::Size& intrinsic_size,
                      const AtomicString& id,
                      Element* element)
        : url(url),
          rect(rect),
          response_end(response_end),
          identifier(identifier),
          intrinsic_size(intrinsic_size),
          id(id),
          element(element) {}
    ElementTimingInfo(const ElementTimingInfo&) = delete;
    ElementTimingInfo& operator=(const ElementTimingInfo&) = delete;
    ~ElementTimingInfo() = default;

    void Trace(Visitor* visitor) const { visitor->Trace(element); }

    String url;
    gfx::RectF rect;
    base::TimeTicks response_end;
    AtomicString identifier;
    gfx::Size intrinsic_size;
    AtomicString id;
    Member<Element> element;
  };

  // Vector containing the element timing infos that will be reported during the
  // next presentation promise callback.
  HeapVector<Member<ElementTimingInfo>> element_timings_;
  struct ImageInfo {
    ImageInfo() {}

    base::TimeTicks load_time_;
    bool is_painted_ = false;
  };
  typedef std::pair<const LayoutObject*, const ImageResourceContent*> RecordId;
  // Hashmap of pairs of elements, LayoutObjects (for the elements) and
  // ImageResourceContent (for the src) which correspond to either images or
  // background images whose paint has been observed. For background images,
  // only the |is_painted_| bit is used, as the timestamp needs to be tracked by
  // |background_image_timestamps_|.
  WTF::HashMap<RecordId, ImageInfo> images_notified_;

  // Hashmap of background images which contain information about the load time
  // of the background image.
  HeapHashMap<WeakMember<const StyleFetchedImage>, base::TimeTicks>
      background_image_timestamps_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_IMAGE_ELEMENT_TIMING_H_
