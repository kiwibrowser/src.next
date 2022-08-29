// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/page/spatial_navigation_controller.h"

#include "third_party/blink/public/mojom/input/focus_type.mojom-blink.h"
#include "third_party/blink/public/mojom/scroll/scroll_into_view_params.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/dom/events/simulated_click_options.h"
#include "third_party/blink/renderer/core/dom/focus_params.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/editing/editing_utilities.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/events/keyboard_event.h"
#include "third_party/blink/renderer/core/events/web_input_event_conversion.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/html/forms/html_form_control_element.h"
#include "third_party/blink/renderer/core/html/forms/html_form_element.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/media/html_video_element.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/layout/hit_test_location.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/spatial_navigation.h"
#include "third_party/blink/renderer/core/scroll/scroll_into_view_util.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"

#include "third_party/blink/renderer/core/css/style_change_reason.h"

namespace blink {

namespace {

SpatialNavigationDirection FocusDirectionForKey(KeyboardEvent* event) {
  if (event->ctrlKey() || event->metaKey() || event->shiftKey())
    return SpatialNavigationDirection::kNone;

  SpatialNavigationDirection ret_val = SpatialNavigationDirection::kNone;
  if (event->key() == "ArrowDown")
    ret_val = SpatialNavigationDirection::kDown;
  else if (event->key() == "ArrowUp")
    ret_val = SpatialNavigationDirection::kUp;
  else if (event->key() == "ArrowLeft")
    ret_val = SpatialNavigationDirection::kLeft;
  else if (event->key() == "ArrowRight")
    ret_val = SpatialNavigationDirection::kRight;

  // TODO(bokan): We should probably assert that we don't get anything else but
  // currently KeyboardEventManager sends non-arrow keys here.

  return ret_val;
}

void ClearFocusInExitedFrames(LocalFrame* old_frame,
                              const LocalFrame* const new_frame) {
  while (old_frame && new_frame != old_frame) {
    // Focus is going away from this document, so clear the focused element.
    old_frame->GetDocument()->ClearFocusedElement();
    old_frame->GetDocument()->SetSequentialFocusNavigationStartingPoint(
        nullptr);
    Frame* parent = old_frame->Tree().Parent();
    old_frame = DynamicTo<LocalFrame>(parent);
  }
}

bool IsSkippableCandidate(const Element* element) {
  // SpatNav tries to ignore certain, inconvenient focus candidates.
  // If an element is recognized as focusable by
  // SupportsSpatialNavigationFocus() but has one or several focusable
  // descendant(s), then we might ignore it in favor for its focusable
  // descendant(s).

  if (element->GetIntegralAttribute(html_names::kTabindexAttr, -1) >= 0) {
    // non-negative tabindex was set explicitly.
    return false;
  }

  if (IsRootEditableElement(*element))
    return false;

  return true;
}

// Determines whether the given candidate is closer to the current interested
// node (in the given direction) than the current best. If so, it'll replace
// the current best.
static void ConsiderForBestCandidate(SpatialNavigationDirection direction,
                                     const FocusCandidate& current_interest,
                                     const FocusCandidate& candidate,
                                     FocusCandidate* best_candidate,
                                     double& best_distance,
                                     FocusCandidate* previous_best_candidate,
                                     double& previous_best_distance) {
  DCHECK(candidate.visible_node->IsElementNode());
  DCHECK(candidate.visible_node->GetLayoutObject());

  // Ignore iframes that don't have a src attribute
  if (FrameOwnerElement(candidate) &&
      (!FrameOwnerElement(candidate)->ContentFrame() ||
       candidate.rect_in_root_frame.IsEmpty()))
    return;

  // Ignore off-screen focusables, if there's nothing in the direction we'll
  // scroll until they come on-screen.
  if (candidate.is_offscreen)
    return;

  double distance =
      ComputeDistanceDataForNode(direction, current_interest, candidate);
  if (distance == kMaxDistance)
    return;

  Element* candidate_element = To<Element>(candidate.visible_node);
  Element* best_candidate_element = To<Element>(best_candidate->visible_node);

  if (candidate_element->IsDescendantOf(best_candidate_element) &&
      IsSkippableCandidate(best_candidate_element) &&
      best_candidate->rect_in_root_frame.Contains(
          candidate.rect_in_root_frame)) {
    // Revert to previous best_candidate because current best_candidate is
    // a skippable candidate.
    *best_candidate = *previous_best_candidate;
    best_distance = previous_best_distance;

    previous_best_distance = kMaxDistance;
  }

  if (distance < best_distance && IsUnobscured(candidate)) {
    *previous_best_candidate = *best_candidate;
    previous_best_distance = best_distance;
    *best_candidate = candidate;
    best_distance = distance;
  }
}

bool IsInAccessibilityMode(Page* page) {
  Frame* frame = page->GetFocusController().FocusedOrMainFrame();
  auto* local_frame = DynamicTo<LocalFrame>(frame);
  if (!local_frame)
    return false;

  Document* document = local_frame->GetDocument();
  if (!document)
    return false;

  // We do not support focusless spatial navigation in accessibility mode.
  return document->ExistingAXObjectCache();
}

}  // namespace

SpatialNavigationController::SpatialNavigationController(Page& page)
    : page_(&page) {
  DCHECK(page_->GetSettings().GetSpatialNavigationEnabled());
}

bool SpatialNavigationController::HandleArrowKeyboardEvent(
    KeyboardEvent* event) {
  DCHECK(page_->GetSettings().GetSpatialNavigationEnabled());

  // TODO(bokan): KeyboardEventManager sends non-arrow keys here. KEM should
  // filter out the non-arrow keys for us.
  SpatialNavigationDirection direction = FocusDirectionForKey(event);
  if (direction == SpatialNavigationDirection::kNone)
    return false;

  // If the focus has already moved by a previous handler, return false.
  const Element* focused = GetFocusedElement();
  if (focused && focused != event->target()) {
    // SpatNav does not need to handle this arrow key because
    // the webpage had a key-handler that already moved focus.
    return false;
  }

  // In focusless mode, the user must explicitly move focus in and out of an
  // editable so we can avoid advancing interest and we should swallow the
  // event. This prevents double-handling actions for things like search box
  // suggestions.
  if (RuntimeEnabledFeatures::FocuslessSpatialNavigationEnabled()) {
    if (focused) {
      if (IsEditable(*focused) || focused->IsTextControl())
        return true;
    }
  }

  return Advance(direction);
}

bool SpatialNavigationController::HandleEnterKeyboardEvent(
    KeyboardEvent* event) {
  DCHECK(page_->GetSettings().GetSpatialNavigationEnabled());

  Element* interest_element = GetInterestedElement();

  if (!interest_element)
    return false;

  if (event->type() == event_type_names::kKeydown) {
    enter_key_down_seen_ = true;
    interest_element->SetActive(true);
  } else if (event->type() == event_type_names::kKeypress) {
    enter_key_press_seen_ = true;
  } else if (event->type() == event_type_names::kKeyup) {
    interest_element->SetActive(false);

    // Ensure that the enter key has not already been handled by something else,
    // or we can end up clicking elements multiple times. Some elements already
    // convert the Enter key into click on down and press (and up) events.
    if (RuntimeEnabledFeatures::FocuslessSpatialNavigationEnabled() &&
        enter_key_down_seen_ && enter_key_press_seen_) {
      interest_element->Focus(
          FocusParams(SelectionBehaviorOnFocus::kReset,
                      mojom::blink::FocusType::kSpatialNavigation, nullptr));
      // We need enter to activate links, etc. The click should be after the
      // focus in case the site transfers focus upon clicking.
      interest_element->DispatchSimulatedClick(
          event, SimulatedClickCreationScope::kFromAccessibility);
    }
  }

  return true;
}

void SpatialNavigationController::ResetEnterKeyState() {
  enter_key_down_seen_ = false;
  enter_key_press_seen_ = false;
}

bool SpatialNavigationController::HandleImeSubmitKeyboardEvent(
    KeyboardEvent* event) {
  DCHECK(page_->GetSettings().GetSpatialNavigationEnabled());

  auto* element = DynamicTo<HTMLFormControlElement>(GetFocusedElement());
  if (!element)
    return false;

  if (!element->formOwner())
    return false;

  element->formOwner()->SubmitImplicitly(*event, true);
  return true;
}

bool SpatialNavigationController::HandleEscapeKeyboardEvent(
    KeyboardEvent* event) {
  DCHECK(page_->GetSettings().GetSpatialNavigationEnabled());

  if (!interest_element_)
    return false;

  if (Element* focused = GetFocusedElement())
    focused->blur();
  else
    MoveInterestTo(nullptr);

  return true;
}

Element* SpatialNavigationController::GetInterestedElement() const {
  if (RuntimeEnabledFeatures::FocuslessSpatialNavigationEnabled())
    return interest_element_;

  Frame* frame = page_->GetFocusController().FocusedOrMainFrame();
  auto* local_frame = DynamicTo<LocalFrame>(frame);
  if (!local_frame)
    return nullptr;

  Document* document = local_frame->GetDocument();
  if (!document)
    return nullptr;

  return document->ActiveElement();
}

void SpatialNavigationController::DidDetachFrameView(
    const LocalFrameView& view) {
  // If the interested element's view was lost (frame detached, navigated,
  // etc.) then reset navigation.
  if (interest_element_ && !interest_element_->GetDocument().View())
    interest_element_ = nullptr;
}

void SpatialNavigationController::Trace(Visitor* visitor) const {
  visitor->Trace(interest_element_);
  visitor->Trace(page_);
}

bool SpatialNavigationController::Advance(
    SpatialNavigationDirection direction) {
  Node* interest_node = StartingNode();
  if (!interest_node)
    return false;

  interest_node->GetDocument().View()->UpdateAllLifecyclePhasesExceptPaint(
      DocumentUpdateReason::kSpatialNavigation);

  Node* container = ScrollableAreaOrDocumentOf(interest_node);

  const PhysicalRect visible_rect =
      PhysicalRect::EnclosingRect(page_->GetVisualViewport().VisibleRect());
  const PhysicalRect start_box =
      SearchOrigin(visible_rect, interest_node, direction);

  if (IsScrollableAreaOrDocument(interest_node) &&
      !IsOffscreen(interest_node)) {
    // A visible scroller has interest. Search inside of it from one of its
    // edges.
    PhysicalRect edge = OppositeEdge(direction, start_box);
    if (AdvanceWithinContainer(*interest_node, edge, direction, nullptr))
      return true;
  }

  // The interested scroller had nothing. Let's search outside of it.
  Node* skipped_tree = interest_node;
  while (container) {
    if (AdvanceWithinContainer(*container, start_box, direction, skipped_tree))
      return true;

    // Containers are not focused “on the way out”. This prevents containers
    // from acting as “focus traps”. Take <c> <a> </c> <b>. Focus can move from
    // <a> to <b> but not from <a> to the scroll container <c>. If we'd allow
    // focus to move from <a> to <c>, the user would never be able to exit <c>.
    // When the scroll container <c> is focused, we move focus back to <a>...
    skipped_tree = container;
    // Nothing found in |container| so search the parent container.
    container = ScrollableAreaOrDocumentOf(container);

    // TODO(bokan): This needs to update the parent document when the _current_
    // container is a document since we're crossing the document boundary.
    // Currently this will fail if we're going from an inner document to a
    // sub-scroller in a parent document.
    if (auto* document = DynamicTo<Document>(container))
      document->UpdateStyleAndLayout(DocumentUpdateReason::kSpatialNavigation);
  }

  return false;
}

FocusCandidate SpatialNavigationController::FindNextCandidateInContainer(
    Node& container,
    const PhysicalRect& starting_rect_in_root_frame,
    SpatialNavigationDirection direction,
    Node* interest_child_in_container) {
  Element* element = ElementTraversal::FirstWithin(container);

  FocusCandidate current_interest;
  current_interest.rect_in_root_frame = starting_rect_in_root_frame;
  current_interest.focusable_node = interest_child_in_container;
  current_interest.visible_node = interest_child_in_container;

  FocusCandidate best_candidate, previous_best_candidate;
  double previous_best_distance = kMaxDistance;
  double best_distance = kMaxDistance;
  for (; element;
       element =
           IsScrollableAreaOrDocument(element)
               ? ElementTraversal::NextSkippingChildren(*element, &container)
               : ElementTraversal::Next(*element, &container)) {
    if (element == interest_child_in_container)
      continue;

    if (HasRemoteFrame(element))
      continue;

    if (!IsValidCandidate(element))
      continue;

    FocusCandidate candidate = FocusCandidate(element, direction);
    if (candidate.IsNull())
      continue;

    ConsiderForBestCandidate(direction, current_interest, candidate,
                             &best_candidate, best_distance,
                             &previous_best_candidate, previous_best_distance);
  }

  return best_candidate;
}

bool SpatialNavigationController::AdvanceWithinContainer(
    Node& container,
    const PhysicalRect& starting_rect_in_root_frame,
    SpatialNavigationDirection direction,
    Node* interest_child_in_container) {
  DCHECK(IsScrollableAreaOrDocument(&container));

  FocusCandidate candidate =
      FindNextCandidateInContainer(container, starting_rect_in_root_frame,
                                   direction, interest_child_in_container);

  if (candidate.IsNull()) {
    // Nothing to focus in this container, scroll if possible.
    // NOTE: If no scrolling is performed (i.e. ScrollInDirection returns
    // false), the spatial navigation algorithm will skip this container.
    return ScrollInDirection(&container, direction);
  }

  auto* element = To<Element>(candidate.focusable_node);
  DCHECK(element);
  MoveInterestTo(element);
  return true;
}

Node* SpatialNavigationController::StartingNode() {
  if (RuntimeEnabledFeatures::FocuslessSpatialNavigationEnabled()) {
    if (interest_element_ && IsValidCandidate(interest_element_)) {
      // If an iframe is interested, start the search from its document node.
      // This matches the behavior in the focus case below where focusing a
      // frame means the focused document doesn't have a focused element and so
      // we return the document itself.
      if (auto* frame_owner =
              DynamicTo<HTMLFrameOwnerElement>(interest_element_.Get()))
        return frame_owner->contentDocument();

      return interest_element_;
    }

    if (auto* main_local_frame = DynamicTo<LocalFrame>(page_->MainFrame()))
      return main_local_frame->GetDocument();

    return nullptr;
  }

  // FIXME: Directional focus changes don't yet work with RemoteFrames.
  const auto* current_frame =
      DynamicTo<LocalFrame>(page_->GetFocusController().FocusedOrMainFrame());
  if (!current_frame)
    return nullptr;

  Document* focused_document = current_frame->GetDocument();
  if (!focused_document)
    return nullptr;

  Node* focused_element = focused_document->FocusedElement();
  if (!focused_element)  // An iframe's document is focused.
    focused_element = focused_document;

  return focused_element;
}

void SpatialNavigationController::MoveInterestTo(Node* next_node) {
  DCHECK(!next_node || next_node->IsElementNode());
  auto* element = To<Element>(next_node);

  if (RuntimeEnabledFeatures::FocuslessSpatialNavigationEnabled()) {
    if (interest_element_) {
      interest_element_->blur();
      interest_element_->SetNeedsStyleRecalc(
          kLocalStyleChange, StyleChangeReasonForTracing::Create(
                                 style_change_reason::kPseudoClass));
    }

    interest_element_ = element;

    if (interest_element_) {
      interest_element_->SetNeedsStyleRecalc(
          kLocalStyleChange, StyleChangeReasonForTracing::Create(
                                 style_change_reason::kPseudoClass));

      LayoutObject* layout_object = interest_element_->GetLayoutObject();
      DCHECK(layout_object);

      scroll_into_view_util::ScrollRectToVisible(
          *layout_object, element->BoundingBoxForScrollIntoView(),
          ScrollAlignment::CreateScrollIntoViewParams());
    }

    // Despite the name, we actually do move focus in "focusless" mode if we're
    // also in accessibility mode since much of the existing machinery is tied
    // to the concept of focus.
    if (!IsInAccessibilityMode(page_)) {
      DispatchMouseMoveAt(interest_element_);
      return;
    }

    // Update |element| in order to use the non-focusless code to apply focus in
    // accessibility mode.
    element = interest_element_;
  }

  if (!element) {
    DispatchMouseMoveAt(nullptr);
    return;
  }

  // Before focusing the new element, check if we're leaving an iframe (= moving
  // focus out of an iframe). In this case, we want the exited [nested] iframes
  // to lose focus. This is tested in snav-iframe-nested.html.
  LocalFrame* old_frame = page_->GetFocusController().FocusedFrame();
  ClearFocusInExitedFrames(old_frame, next_node->GetDocument().GetFrame());

  element->Focus(FocusParams(SelectionBehaviorOnFocus::kReset,
                             mojom::blink::FocusType::kSpatialNavigation,
                             nullptr));
  // The focused element could be changed due to elm.focus() on focus handlers.
  // So we need to update the current focused element before DispatchMouseMove.
  // This is tested in snav-applies-hover-with-focused.html.
  Element* current_interest = GetInterestedElement();
  DispatchMouseMoveAt(current_interest);
}

void SpatialNavigationController::DispatchMouseMoveAt(Element* element) {
  gfx::PointF event_position(-1, -1);
  if (element) {
    event_position = RectInViewport(*element).origin();
    event_position.Offset(1, 1);
  }

  // TODO(bokan): Can we get better screen coordinates?
  gfx::PointF event_position_screen = event_position;
  int click_count = 0;
  WebMouseEvent fake_mouse_move_event(
      WebInputEvent::Type::kMouseMove, event_position, event_position_screen,
      WebPointerProperties::Button::kNoButton, click_count,
      WebInputEvent::kRelativeMotionEvent, base::TimeTicks::Now());
  Vector<WebMouseEvent> coalesced_events, predicted_events;

  DCHECK(IsA<LocalFrame>(page_->MainFrame()));
  LocalFrame* frame = DynamicTo<LocalFrame>(page_->MainFrame());

  DCHECK(frame);
  frame->GetEventHandler().HandleMouseMoveEvent(
      TransformWebMouseEvent(frame->View(), fake_mouse_move_event),
      coalesced_events, predicted_events);
}

bool SpatialNavigationController::IsValidCandidate(
    const Element* element) const {
  if (!element || !element->isConnected() || !element->GetLayoutObject())
    return false;

  LocalFrame* frame = element->GetDocument().GetFrame();
  if (!frame)
    return false;

  // If the author installed a click handler on the main document or body, we
  // almost certainly don't want to actually interest it. Doing so leads to
  // issues since the document/body will likely contain most of the other
  // content on the page.
  if (frame->IsOutermostMainFrame()) {
    if (IsA<HTMLHtmlElement>(element) || IsA<HTMLBodyElement>(element))
      return false;
  }

  return element->IsKeyboardFocusable();
}

Element* SpatialNavigationController::GetFocusedElement() const {
  LocalFrame* frame = page_->GetFocusController().FocusedFrame();
  if (!frame || !frame->GetDocument())
    return nullptr;

  return frame->GetDocument()->FocusedElement();
}

void SpatialNavigationController::OnSpatialNavigationSettingChanged() {
  if (!RuntimeEnabledFeatures::FocuslessSpatialNavigationEnabled())
    return;
  if (!page_->GetSettings().GetSpatialNavigationEnabled()) {
    MoveInterestTo(nullptr);
    return;
  }
  // FocusedController::FocusedOrMainFrame will crash if called before the
  // MainFrame is set.
  if (!page_->MainFrame())
    return;
  LocalFrame* frame =
      DynamicTo<LocalFrame>(page_->GetFocusController().FocusedOrMainFrame());
  if (frame && frame->GetDocument() &&
      IsValidCandidate(frame->GetDocument()->FocusedElement())) {
    MoveInterestTo(frame->GetDocument()->FocusedElement());
  }
}

void SpatialNavigationController::FocusedNodeChanged(Document* document) {
  if (!RuntimeEnabledFeatures::FocuslessSpatialNavigationEnabled())
    return;
  if (page_->GetFocusController().FocusedOrMainFrame() != document->GetFrame())
    return;

  if (document->FocusedElement() &&
      interest_element_ != document->FocusedElement()) {
    MoveInterestTo(document->FocusedElement());
  }
}

void SpatialNavigationController::FullscreenStateChanged(Element* element) {
  if (!RuntimeEnabledFeatures::FocuslessSpatialNavigationEnabled())
    return;
  if (IsA<HTMLMediaElement>(element)) {
    element->Focus(FocusParams(SelectionBehaviorOnFocus::kReset,
                               mojom::blink::FocusType::kSpatialNavigation,
                               nullptr));
  }
}

}  // namespace blink
