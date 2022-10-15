// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_MEDIA_VALUES_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_MEDIA_VALUES_H_

#include "services/device/public/mojom/device_posture_provider.mojom-blink-forward.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/css/preferred_color_scheme.mojom-blink-forward.h"
#include "third_party/blink/public/mojom/css/preferred_contrast.mojom-blink-forward.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom-shared.h"
#include "third_party/blink/public/mojom/webpreferences/web_preferences.mojom-blink-forward.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_length_resolver.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/text/writing_mode.h"
#include "ui/base/pointer/pointer_device.h"

namespace blink {

class CSSPrimitiveValue;
class Document;
class Element;
class LocalFrame;
enum class CSSValueID;
enum class ColorSpaceGamut;
enum class ForcedColors;
enum class NavigationControls;

mojom::blink::PreferredColorScheme CSSValueIDToPreferredColorScheme(
    CSSValueID id);
mojom::blink::PreferredContrast CSSValueIDToPreferredContrast(CSSValueID);
ForcedColors CSSValueIDToForcedColors(CSSValueID id);

class CORE_EXPORT MediaValues : public GarbageCollected<MediaValues>,
                                public CSSLengthResolver {
 public:
  MediaValues() : CSSLengthResolver(1.0f /* zoom */) {}
  virtual ~MediaValues() = default;
  virtual void Trace(Visitor* visitor) const {}

  static MediaValues* CreateDynamicIfFrameExists(LocalFrame*);

  template <typename T>
  bool ComputeLength(double value,
                     CSSPrimitiveValue::UnitType type,
                     T& result) const {
    double temp_result;
    if (!ComputeLengthImpl(value, type, temp_result))
      return false;
    result = ClampTo<T>(temp_result);
    return true;
  }

  absl::optional<double> InlineSize() const;
  absl::optional<double> BlockSize() const;
  virtual absl::optional<double> Width() const { return ViewportWidth(); }
  virtual absl::optional<double> Height() const { return ViewportHeight(); }
  virtual int DeviceWidth() const = 0;
  virtual int DeviceHeight() const = 0;
  virtual float DevicePixelRatio() const = 0;
  virtual bool DeviceSupportsHDR() const = 0;
  virtual int ColorBitsPerComponent() const = 0;
  virtual int MonochromeBitsPerComponent() const = 0;
  virtual mojom::blink::PointerType PrimaryPointerType() const = 0;
  virtual int AvailablePointerTypes() const = 0;
  virtual mojom::blink::HoverType PrimaryHoverType() const = 0;
  virtual int AvailableHoverTypes() const = 0;
  virtual bool ThreeDEnabled() const = 0;
  virtual bool InImmersiveMode() const = 0;
  virtual const String MediaType() const = 0;
  virtual blink::mojom::DisplayMode DisplayMode() const = 0;
  virtual bool StrictMode() const = 0;
  virtual Document* GetDocument() const = 0;
  virtual bool HasValues() const = 0;

  virtual ColorSpaceGamut ColorGamut() const = 0;
  virtual mojom::blink::PreferredColorScheme GetPreferredColorScheme()
      const = 0;
  virtual mojom::blink::PreferredContrast GetPreferredContrast() const = 0;
  virtual bool PrefersReducedMotion() const = 0;
  virtual bool PrefersReducedData() const = 0;
  virtual ForcedColors GetForcedColors() const = 0;
  virtual NavigationControls GetNavigationControls() const = 0;
  virtual int GetHorizontalViewportSegments() const = 0;
  virtual int GetVerticalViewportSegments() const = 0;
  virtual device::mojom::blink::DevicePostureType GetDevicePosture() const = 0;
  // Returns the container element used to retrieve base style and parent style
  // when computing the computed value of a style() container query.
  virtual Element* ContainerElement() const { return nullptr; }

 protected:
  static double CalculateViewportWidth(LocalFrame*);
  static double CalculateViewportHeight(LocalFrame*);
  static double CalculateSmallViewportWidth(LocalFrame*);
  static double CalculateSmallViewportHeight(LocalFrame*);
  static double CalculateLargeViewportWidth(LocalFrame*);
  static double CalculateLargeViewportHeight(LocalFrame*);
  static double CalculateDynamicViewportWidth(LocalFrame*);
  static double CalculateDynamicViewportHeight(LocalFrame*);
  static float CalculateEmSize(LocalFrame*);
  static float CalculateExSize(LocalFrame*);
  static float CalculateChSize(LocalFrame*);
  static float CalculateIcSize(LocalFrame*);
  static int CalculateDeviceWidth(LocalFrame*);
  static int CalculateDeviceHeight(LocalFrame*);
  static bool CalculateStrictMode(LocalFrame*);
  static float CalculateDevicePixelRatio(LocalFrame*);
  static bool CalculateDeviceSupportsHDR(LocalFrame*);
  static int CalculateColorBitsPerComponent(LocalFrame*);
  static int CalculateMonochromeBitsPerComponent(LocalFrame*);
  static const String CalculateMediaType(LocalFrame*);
  static blink::mojom::DisplayMode CalculateDisplayMode(LocalFrame*);
  static bool CalculateThreeDEnabled(LocalFrame*);
  static bool CalculateInImmersiveMode(LocalFrame*);
  static mojom::blink::PointerType CalculatePrimaryPointerType(LocalFrame*);
  static int CalculateAvailablePointerTypes(LocalFrame*);
  static mojom::blink::HoverType CalculatePrimaryHoverType(LocalFrame*);
  static int CalculateAvailableHoverTypes(LocalFrame*);
  static ColorSpaceGamut CalculateColorGamut(LocalFrame*);
  static mojom::blink::PreferredColorScheme CalculatePreferredColorScheme(
      LocalFrame*);
  static mojom::blink::PreferredContrast CalculatePreferredContrast(
      LocalFrame*);
  static bool CalculatePrefersReducedMotion(LocalFrame*);
  static bool CalculatePrefersReducedData(LocalFrame*);
  static ForcedColors CalculateForcedColors(LocalFrame*);
  static NavigationControls CalculateNavigationControls(LocalFrame*);
  static int CalculateHorizontalViewportSegments(LocalFrame*);
  static int CalculateVerticalViewportSegments(LocalFrame*);
  static device::mojom::blink::DevicePostureType CalculateDevicePosture(
      LocalFrame*);

  bool ComputeLengthImpl(double value,
                         CSSPrimitiveValue::UnitType,
                         double& result) const;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_MEDIA_VALUES_H_
