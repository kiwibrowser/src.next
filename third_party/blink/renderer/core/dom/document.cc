/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2011, 2012 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2008, 2009, 2011, 2012 Google Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) Research In Motion Limited 2010-2011. All rights reserved.
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

#include "third_party/blink/renderer/core/dom/document.h"

#include <memory>
#include <utility>

#include "base/auto_reset.h"
#include "base/containers/adapters.h"
#include "base/containers/contains.h"
#include "base/debug/dump_without_crashing.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/time/time.h"
#include "cc/animation/animation_host.h"
#include "cc/animation/animation_timeline.h"
#include "cc/input/overscroll_behavior.h"
#include "cc/input/scroll_snap_data.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/mojom/base/text_direction.mojom-blink.h"
#include "services/metrics/public/cpp/metrics_utils.h"
#include "services/metrics/public/cpp/mojo_ukm_recorder.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "services/network/public/cpp/web_sandbox_flags.h"
#include "services/network/public/mojom/trust_tokens.mojom-blink.h"
#include "services/network/public/mojom/web_sandbox_flags.mojom-blink.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/permissions_policy/document_policy_features.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_sample_collector.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_study_settings.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/mojom/css/preferred_color_scheme.mojom-blink-forward.h"
#include "third_party/blink/public/mojom/frame/frame.mojom-blink.h"
#include "third_party/blink/public/mojom/input/focus_type.mojom-blink.h"
#include "third_party/blink/public/mojom/page_state/page_state.mojom-blink.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "third_party/blink/public/platform/web_theme_engine.h"
#include "third_party/blink/public/web/web_print_page_description.h"
#include "third_party/blink/renderer/bindings/core/v8/isolated_world_csp.h"
#include "third_party/blink/renderer/bindings/core/v8/script_controller.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/script_value.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_element_creation_options.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_element_registration_options.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_observable_array_css_style_sheet.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_throw_dom_exception.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_elementcreationoptions_string.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_htmlscriptelement_svgscriptelement.h"
#include "third_party/blink/renderer/bindings/core/v8/window_proxy.h"
#include "third_party/blink/renderer/core/accessibility/ax_context.h"
#include "third_party/blink/renderer/core/accessibility/ax_object_cache.h"
#include "third_party/blink/renderer/core/animation/document_animations.h"
#include "third_party/blink/renderer/core/animation/document_timeline.h"
#include "third_party/blink/renderer/core/animation/pending_animations.h"
#include "third_party/blink/renderer/core/animation/worklet_animation_controller.h"
#include "third_party/blink/renderer/core/annotation/annotation_agent_container_impl.h"
#include "third_party/blink/renderer/core/aom/computed_accessible_node.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_font_selector.h"
#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css/css_style_declaration.h"
#include "third_party/blink/renderer/core/css/css_style_sheet.h"
#include "third_party/blink/renderer/core/css/cssom/computed_style_property_map.h"
#include "third_party/blink/renderer/core/css/element_rule_collector.h"
#include "third_party/blink/renderer/core/css/font_face_set_document.h"
#include "third_party/blink/renderer/core/css/invalidation/style_invalidator.h"
#include "third_party/blink/renderer/core/css/layout_upgrade.h"
#include "third_party/blink/renderer/core/css/media_query_list.h"
#include "third_party/blink/renderer/core/css/media_query_matcher.h"
#include "third_party/blink/renderer/core/css/media_values.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/post_style_update_scope.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/css/property_registry.h"
#include "third_party/blink/renderer/core/css/resolver/font_builder.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_stats.h"
#include "third_party/blink/renderer/core/css/selector_query.h"
#include "third_party/blink/renderer/core/css/style_change_reason.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css/style_sheet_contents.h"
#include "third_party/blink/renderer/core/css/style_sheet_list.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_context.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_document_state.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_utilities.h"
#include "third_party/blink/renderer/core/document_transition/document_transition_supplement.h"
#include "third_party/blink/renderer/core/dom/attr.h"
#include "third_party/blink/renderer/core/dom/beforeunload_event_listener.h"
#include "third_party/blink/renderer/core/dom/cdata_section.h"
#include "third_party/blink/renderer/core/dom/comment.h"
#include "third_party/blink/renderer/core/dom/context_features.h"
#include "third_party/blink/renderer/core/dom/document_data.h"
#include "third_party/blink/renderer/core/dom/document_fragment.h"
#include "third_party/blink/renderer/core/dom/document_init.h"
#include "third_party/blink/renderer/core/dom/document_parser_timing.h"
#include "third_party/blink/renderer/core/dom/document_type.h"
#include "third_party/blink/renderer/core/dom/dom_implementation.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/element_data_cache.h"
#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/dom/events/event_dispatch_forbidden_scope.h"
#include "third_party/blink/renderer/core/dom/events/event_listener.h"
#include "third_party/blink/renderer/core/dom/events/native_event_listener.h"
#include "third_party/blink/renderer/core/dom/events/scoped_event_queue.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/focus_params.h"
#include "third_party/blink/renderer/core/dom/focused_element_change_observer.h"
#include "third_party/blink/renderer/core/dom/layout_tree_builder_traversal.h"
#include "third_party/blink/renderer/core/dom/live_node_list.h"
#include "third_party/blink/renderer/core/dom/mutation_observer.h"
#include "third_party/blink/renderer/core/dom/node_child_removal_tracker.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/node_iterator.h"
#include "third_party/blink/renderer/core/dom/node_lists_node_data.h"
#include "third_party/blink/renderer/core/dom/node_rare_data.h"
#include "third_party/blink/renderer/core/dom/node_traversal.h"
#include "third_party/blink/renderer/core/dom/node_with_index.h"
#include "third_party/blink/renderer/core/dom/processing_instruction.h"
#include "third_party/blink/renderer/core/dom/scripted_animation_controller.h"
#include "third_party/blink/renderer/core/dom/scripted_idle_task_controller.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/dom/slot_assignment.h"
#include "third_party/blink/renderer/core/dom/slot_assignment_engine.h"
#include "third_party/blink/renderer/core/dom/slot_assignment_recalc_forbidden_scope.h"
#include "third_party/blink/renderer/core/dom/static_node_list.h"
#include "third_party/blink/renderer/core/dom/transform_source.h"
#include "third_party/blink/renderer/core/dom/tree_walker.h"
#include "third_party/blink/renderer/core/dom/visited_link_state.h"
#include "third_party/blink/renderer/core/dom/whitespace_attacher.h"
#include "third_party/blink/renderer/core/dom/xml_document.h"
#include "third_party/blink/renderer/core/editing/editing_utilities.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/editing/markers/document_marker_controller.h"
#include "third_party/blink/renderer/core/editing/position_with_affinity.h"
#include "third_party/blink/renderer/core/editing/serializers/serialization.h"
#include "third_party/blink/renderer/core/events/before_unload_event.h"
#include "third_party/blink/renderer/core/events/event_factory.h"
#include "third_party/blink/renderer/core/events/hash_change_event.h"
#include "third_party/blink/renderer/core/events/overscroll_event.h"
#include "third_party/blink/renderer/core/events/page_transition_event.h"
#include "third_party/blink/renderer/core/events/visual_viewport_resize_event.h"
#include "third_party/blink/renderer/core/events/visual_viewport_scroll_event.h"
#include "third_party/blink/renderer/core/execution_context/window_agent.h"
#include "third_party/blink/renderer/core/fragment_directive/fragment_directive.h"
#include "third_party/blink/renderer/core/fragment_directive/text_fragment_handler.h"
#include "third_party/blink/renderer/core/frame/csp/content_security_policy.h"
#include "third_party/blink/renderer/core/frame/dom_timer.h"
#include "third_party/blink/renderer/core/frame/dom_visual_viewport.h"
#include "third_party/blink/renderer/core/frame/event_handler_registry.h"
#include "third_party/blink/renderer/core/frame/frame_console.h"
#include "third_party/blink/renderer/core/frame/history.h"
#include "third_party/blink/renderer/core/frame/intervention.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/local_frame_ukm_aggregator.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/page_dismissal_scope.h"
#include "third_party/blink/renderer/core/frame/performance_monitor.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/viewport_data.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_font_cache.h"
#include "third_party/blink/renderer/core/html/canvas/canvas_rendering_context.h"
#include "third_party/blink/renderer/core/html/canvas/html_canvas_element.h"
#include "third_party/blink/renderer/core/html/custom/custom_element.h"
#include "third_party/blink/renderer/core/html/custom/custom_element_definition.h"
#include "third_party/blink/renderer/core/html/custom/custom_element_descriptor.h"
#include "third_party/blink/renderer/core/html/custom/custom_element_registry.h"
#include "third_party/blink/renderer/core/html/document_all_name_collection.h"
#include "third_party/blink/renderer/core/html/document_name_collection.h"
#include "third_party/blink/renderer/core/html/forms/email_input_type.h"
#include "third_party/blink/renderer/core/html/forms/form_controller.h"
#include "third_party/blink/renderer/core/html/forms/html_form_element.h"
#include "third_party/blink/renderer/core/html/forms/html_input_element.h"
#include "third_party/blink/renderer/core/html/html_all_collection.h"
#include "third_party/blink/renderer/core/html/html_anchor_element.h"
#include "third_party/blink/renderer/core/html/html_base_element.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/html/html_collection.h"
#include "third_party/blink/renderer/core/html/html_dialog_element.h"
#include "third_party/blink/renderer/core/html/html_document.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/html_head_element.h"
#include "third_party/blink/renderer/core/html/html_html_element.h"
#include "third_party/blink/renderer/core/html/html_link_element.h"
#include "third_party/blink/renderer/core/html/html_meta_element.h"
#include "third_party/blink/renderer/core/html/html_object_element.h"
#include "third_party/blink/renderer/core/html/html_plugin_element.h"
#include "third_party/blink/renderer/core/html/html_script_element.h"
#include "third_party/blink/renderer/core/html/html_title_element.h"
#include "third_party/blink/renderer/core/html/html_unknown_element.h"
#include "third_party/blink/renderer/core/html/lazy_load_image_observer.h"
#include "third_party/blink/renderer/core/html/nesting_level_incrementer.h"
#include "third_party/blink/renderer/core/html/parser/html_document_parser.h"
#include "third_party/blink/renderer/core/html/parser/html_parser_idioms.h"
#include "third_party/blink/renderer/core/html/parser/text_resource_decoder.h"
#include "third_party/blink/renderer/core/html/parser/text_resource_decoder_builder.h"
#include "third_party/blink/renderer/core/html/plugin_document.h"
#include "third_party/blink/renderer/core/html/portal/document_portals.h"
#include "third_party/blink/renderer/core/html/portal/portal_contents.h"
#include "third_party/blink/renderer/core/html/window_name_collection.h"
#include "third_party/blink/renderer/core/html_element_factory.h"
#include "third_party/blink/renderer/core/html_element_type_helpers.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/input/touch_list.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/intersection_observer/element_intersection_observer_data.h"
#include "third_party/blink/renderer/core/intersection_observer/intersection_observer_controller.h"
#include "third_party/blink/renderer/core/intersection_observer/intersection_observer_entry.h"
#include "third_party/blink/renderer/core/layout/adjust_for_absolute_zoom.h"
#include "third_party/blink/renderer/core/layout/deferred_shaping_controller.h"
#include "third_party/blink/renderer/core/layout/hit_test_canvas_result.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_object_factory.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/text_autosizer.h"
#include "third_party/blink/renderer/core/loader/anchor_element_interaction_tracker.h"
#include "third_party/blink/renderer/core/loader/cookie_jar.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/loader/frame_fetch_context.h"
#include "third_party/blink/renderer/core/loader/frame_loader.h"
#include "third_party/blink/renderer/core/loader/http_refresh_scheduler.h"
#include "third_party/blink/renderer/core/loader/idleness_detector.h"
#include "third_party/blink/renderer/core/loader/interactive_detector.h"
#include "third_party/blink/renderer/core/loader/no_state_prefetch_client.h"
#include "third_party/blink/renderer/core/loader/pending_link_preload.h"
#include "third_party/blink/renderer/core/loader/progress_tracker.h"
#include "third_party/blink/renderer/core/loader/render_blocking_resource_manager.h"
#include "third_party/blink/renderer/core/mathml/mathml_element.h"
#include "third_party/blink/renderer/core/mathml/mathml_row_element.h"
#include "third_party/blink/renderer/core/mathml_element_factory.h"
#include "third_party/blink/renderer/core/mathml_names.h"
#include "third_party/blink/renderer/core/mobile_metrics/mobile_friendliness_checker.h"
#include "third_party/blink/renderer/core/origin_trials/origin_trial_context.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/event_with_hit_test_results.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/frame_tree.h"
#include "third_party/blink/renderer/core/page/named_pages_mapper.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/page/plugin_script_forbidden_scope.h"
#include "third_party/blink/renderer/core/page/pointer_lock_controller.h"
#include "third_party/blink/renderer/core/page/scrolling/overscroll_controller.h"
#include "third_party/blink/renderer/core/page/scrolling/root_scroller_controller.h"
#include "third_party/blink/renderer/core/page/scrolling/scroll_state_callback.h"
#include "third_party/blink/renderer/core/page/scrolling/scrolling_coordinator.h"
#include "third_party/blink/renderer/core/page/scrolling/snap_coordinator.h"
#include "third_party/blink/renderer/core/page/scrolling/top_document_root_scroller_controller.h"
#include "third_party/blink/renderer/core/page/spatial_navigation_controller.h"
#include "third_party/blink/renderer/core/page/validation_message_client.h"
#include "third_party/blink/renderer/core/paint/first_meaningful_paint_detector.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/paint/paint_timing.h"
#include "third_party/blink/renderer/core/permissions_policy/dom_feature_policy.h"
#include "third_party/blink/renderer/core/permissions_policy/permissions_policy_parser.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer_controller.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer_entry.h"
#include "third_party/blink/renderer/core/script/detect_javascript_frameworks.h"
#include "third_party/blink/renderer/core/script/script_runner.h"
#include "third_party/blink/renderer/core/scroll/scrollbar_theme.h"
#include "third_party/blink/renderer/core/svg/svg_document_extensions.h"
#include "third_party/blink/renderer/core/svg/svg_script_element.h"
#include "third_party/blink/renderer/core/svg/svg_title_element.h"
#include "third_party/blink/renderer/core/svg/svg_unknown_element.h"
#include "third_party/blink/renderer/core/svg/svg_use_element.h"
#include "third_party/blink/renderer/core/svg_element_factory.h"
#include "third_party/blink/renderer/core/svg_names.h"
#include "third_party/blink/renderer/core/timing/soft_navigation_heuristics.h"
#include "third_party/blink/renderer/core/trustedtypes/trusted_html.h"
#include "third_party/blink/renderer/core/xml/parser/xml_document_parser.h"
#include "third_party/blink/renderer/core/xml_names.h"
#include "third_party/blink/renderer/core/xmlns_names.h"
#include "third_party/blink/renderer/platform/bindings/dom_data_store.h"
#include "third_party/blink/renderer/platform/bindings/dom_wrapper_world.h"
#include "third_party/blink/renderer/platform/bindings/exception_messages.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/bindings/microtask.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/bindings/source_location.h"
#include "third_party/blink/renderer/platform/bindings/v8_dom_wrapper.h"
#include "third_party/blink/renderer/platform/bindings/v8_per_isolate_data.h"
#include "third_party/blink/renderer/platform/fonts/font_matching_metrics.h"
#include "third_party/blink/renderer/platform/fonts/font_performance.h"
#include "third_party/blink/renderer/platform/geometry/length_functions.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/thread_state.h"
#include "third_party/blink/renderer/platform/instrumentation/histogram.h"
#include "third_party/blink/renderer/platform/instrumentation/instance_counters.h"
#include "third_party/blink/renderer/platform/instrumentation/resource_coordinator/document_resource_coordinator.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/language.h"
#include "third_party/blink/renderer/platform/loader/fetch/null_resource_fetcher_properties.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/network/content_security_policy_parsers.h"
#include "third_party/blink/renderer/platform/network/http_parsers.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/scheduler/public/event_loop.h"
#include "third_party/blink/renderer/platform/scheduler/public/frame_or_worker_scheduler.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/text/platform_locale.h"
#include "third_party/blink/renderer/platform/theme/web_theme_engine_helper.h"
#include "third_party/blink/renderer/platform/web_test_support.h"
#include "third_party/blink/renderer/platform/weborigin/origin_access_entry.h"
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/widget/frame_widget.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/hash_functions.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/text/character_names.h"
#include "third_party/blink/renderer/platform/wtf/text/string_buffer.h"
#include "third_party/blink/renderer/platform/wtf/text/text_encoding_registry.h"

#ifndef NDEBUG
using WeakDocumentSet = blink::HeapHashSet<blink::WeakMember<blink::Document>>;
static WeakDocumentSet& LiveDocumentSet();
#endif

namespace blink {

namespace {

// This enum must match the numbering for RequestStorageResult in
// histograms/enums.xml. Do not reorder or remove items, only add new items
// at the end.
enum class RequestStorageResult {
  APPROVED_EXISTING_ACCESS = 0,
  APPROVED_NEW_GRANT = 1,
  REJECTED_NO_USER_GESTURE = 2,
  REJECTED_NO_ORIGIN = 3,
  REJECTED_OPAQUE_ORIGIN = 4,
  REJECTED_EXISTING_DENIAL = 5,
  REJECTED_SANDBOXED = 6,
  REJECTED_GRANT_DENIED = 7,
  kMaxValue = REJECTED_GRANT_DENIED,
};
void FireRequestStorageAccessHistogram(RequestStorageResult result) {
  base::UmaHistogramEnumeration("API.StorageAccess.RequestStorageAccess",
                                result);
}

class IntrinsicSizeResizeObserverDelegate : public ResizeObserver::Delegate {
 public:
  void OnResize(const HeapVector<Member<ResizeObserverEntry>>& entries) final;
};

// Returns true if any of <object> ancestors don't start loading or are loading
// plugins/frames/images. If there are no <object> ancestors, this function
// returns false.
bool IsInIndeterminateObjectAncestor(const Element* element) {
  if (!element->isConnected())
    return false;
  for (; element; element = element->ParentOrShadowHostElement()) {
    if (const auto* object = DynamicTo<HTMLObjectElement>(element)) {
      if (!object->DidFinishLoading())
        return true;
    }
  }
  return false;
}

// Helper function to notify both `first` and `second` that the priority scroll
// anchor status changed. This is used when, for example, a focused element
// changes from `first` to `second`.
void NotifyPriorityScrollAnchorStatusChanged(Node* first, Node* second) {
  if (first)
    first->NotifyPriorityScrollAnchorStatusChanged();
  if (second)
    second->NotifyPriorityScrollAnchorStatusChanged();
}

// Before fetching the default URL, make sure it won't be blocked by CSP. The
// webpage didn't requested "/favicon.ico", it is automatic. Developers
// shouldn't suffer from any errors provoked by Chrome.
// See https://crbug.com/820846
bool DefaultFaviconAllowedByCSP(const Document* document, const IconURL& icon) {
  ExecutionContext* context = document->GetExecutionContext();
  if (!context) {
    // LocalFrame::UpdateFaviconURL() is sometimes called after a LocalFrame
    // swap. When this happens, the document has lost its ExecutionContext and
    // the favicon won't be loaded anyway. The output of this function doesn't
    // matter anymore.
    return false;
  }

  return context->GetContentSecurityPolicy()->AllowImageFromSource(
      icon.icon_url_, icon.icon_url_, RedirectStatus::kNoRedirect,
      ReportingDisposition::kSuppressReporting,
      ContentSecurityPolicy::CheckHeaderType::kCheckAll);
}

}  // namespace

static const unsigned kCMaxWriteRecursionDepth = 21;

// This amount of time must have elapsed before we will even consider scheduling
// a layout without a delay.
// FIXME: For faster machines this value can really be lowered to 200.  250 is
// adequate, but a little high for dual G5s. :)
static const base::TimeDelta kCLayoutScheduleThreshold =
    base::Milliseconds(250);

// DOM Level 2 says (letters added):
//
// a) Name start characters must have one of the categories Ll, Lu, Lo, Lt, Nl.
// b) Name characters other than Name-start characters must have one of the
//    categories Mc, Me, Mn, Lm, or Nd.
// c) Characters in the compatibility area (i.e. with character code greater
//    than #xF900 and less than #xFFFE) are not allowed in XML names.
// d) Characters which have a font or compatibility decomposition (i.e. those
//    with a "compatibility formatting tag" in field 5 of the database -- marked
//    by field 5 beginning with a "<") are not allowed.
// e) The following characters are treated as name-start characters rather than
//    name characters, because the property file classifies them as Alphabetic:
//    [#x02BB-#x02C1], #x0559, #x06E5, #x06E6.
// f) Characters #x20DD-#x20E0 are excluded (in accordance with Unicode, section
//    5.14).
// g) Character #x00B7 is classified as an extender, because the property list
//    so identifies it.
// h) Character #x0387 is added as a name character, because #x00B7 is its
//    canonical equivalent.
// i) Characters ':' and '_' are allowed as name-start characters.
// j) Characters '-' and '.' are allowed as name characters.
//
// It also contains complete tables. If we decide it's better, we could include
// those instead of the following code.

static inline bool IsValidNameStart(UChar32 c) {
  // rule (e) above
  if ((c >= 0x02BB && c <= 0x02C1) || c == 0x559 || c == 0x6E5 || c == 0x6E6)
    return true;

  // rule (i) above
  if (c == ':' || c == '_')
    return true;

  // rules (a) and (f) above
  const uint32_t kNameStartMask =
      WTF::unicode::kLetter_Lowercase | WTF::unicode::kLetter_Uppercase |
      WTF::unicode::kLetter_Other | WTF::unicode::kLetter_Titlecase |
      WTF::unicode::kNumber_Letter;
  if (!(WTF::unicode::Category(c) & kNameStartMask))
    return false;

  // rule (c) above
  if (c >= 0xF900 && c < 0xFFFE)
    return false;

  // rule (d) above
  WTF::unicode::CharDecompositionType decomp_type =
      WTF::unicode::DecompositionType(c);
  if (decomp_type == WTF::unicode::kDecompositionFont ||
      decomp_type == WTF::unicode::kDecompositionCompat)
    return false;

  return true;
}

static inline bool IsValidNamePart(UChar32 c) {
  // rules (a), (e), and (i) above
  if (IsValidNameStart(c))
    return true;

  // rules (g) and (h) above
  if (c == 0x00B7 || c == 0x0387)
    return true;

  // rule (j) above
  if (c == '-' || c == '.')
    return true;

  // rules (b) and (f) above
  const uint32_t kOtherNamePartMask =
      WTF::unicode::kMark_NonSpacing | WTF::unicode::kMark_Enclosing |
      WTF::unicode::kMark_SpacingCombining | WTF::unicode::kLetter_Modifier |
      WTF::unicode::kNumber_DecimalDigit;
  if (!(WTF::unicode::Category(c) & kOtherNamePartMask))
    return false;

  // rule (c) above
  if (c >= 0xF900 && c < 0xFFFE)
    return false;

  // rule (d) above
  WTF::unicode::CharDecompositionType decomp_type =
      WTF::unicode::DecompositionType(c);
  if (decomp_type == WTF::unicode::kDecompositionFont ||
      decomp_type == WTF::unicode::kDecompositionCompat)
    return false;

  return true;
}

// Tests whether |name| is something the HTML parser would accept as a
// tag name.
template <typename CharType>
static inline bool IsValidElementNamePerHTMLParser(const CharType* characters,
                                                   unsigned length) {
  CharType c = characters[0] | 0x20;
  if (!('a' <= c && c <= 'z'))
    return false;

  for (unsigned i = 1; i < length; ++i) {
    c = characters[i];
    if (c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ' ||
        c == '/' || c == '>')
      return false;
  }

  return true;
}

static bool IsValidElementNamePerHTMLParser(const String& name) {
  unsigned length = name.length();
  if (!length)
    return false;

  if (name.Is8Bit()) {
    const LChar* characters = name.Characters8();
    return IsValidElementNamePerHTMLParser(characters, length);
  }
  const UChar* characters = name.Characters16();
  return IsValidElementNamePerHTMLParser(characters, length);
}

// Tests whether |name| is a valid name per DOM spec. Also checks
// whether the HTML parser would accept this element name and counts
// cases of mismatches.
static bool IsValidElementName(Document* document, const String& name) {
  bool is_valid_dom_name = Document::IsValidName(name);
  bool is_valid_html_name = IsValidElementNamePerHTMLParser(name);
  if (UNLIKELY(is_valid_html_name != is_valid_dom_name)) {
    // This is inaccurate because it will not report activity in
    // detached documents. However retrieving the frame from the
    // bindings is too slow.
    UseCounter::Count(document,
                      is_valid_dom_name
                          ? WebFeature::kElementNameDOMValidHTMLParserInvalid
                          : WebFeature::kElementNameDOMInvalidHTMLParserValid);
  }
  return is_valid_dom_name;
}

static bool AcceptsEditingFocus(const Element& element) {
  DCHECK(IsEditable(element));

  return element.GetDocument().GetFrame() && RootEditableElement(element);
}

uint64_t Document::global_tree_version_ = 0;

static bool g_force_synchronous_parsing_for_testing = false;

void IntrinsicSizeResizeObserverDelegate::OnResize(
    const HeapVector<Member<ResizeObserverEntry>>& entries) {
  for (const auto& entry : entries) {
    DCHECK_GT(entry->contentBoxSize().size(), 0u);
    entry->target()->SaveIntrinsicSize(entry->contentBoxSize().at(0));
  }
}

void Document::UnassociatedListedElementsList::MarkDirty() {
  dirty_ = true;
  list_.clear();
}

void Document::UnassociatedListedElementsList::Trace(Visitor* visitor) const {
  visitor->Trace(list_);
}

const ListedElement::List& Document::UnassociatedListedElementsList::Get(
    const Document& owner) {
  if (dirty_) {
    const Node& root = owner.GetTreeScope().RootNode();
    DCHECK(list_.IsEmpty());

    // TODO(crbug.com/1243730): We do not consider shadow trees for now.
    for (HTMLElement& element : Traversal<HTMLElement>::StartsAfter(root)) {
      ListedElement* listed_element = ListedElement::From(element);
      if (listed_element && !listed_element->Form()) {
        list_.push_back(listed_element);
      }
    }
    dirty_ = false;
  }
  return list_;
}

const ListedElement::List& Document::UnassociatedListedElements() const {
  return const_cast<Document*>(this)->unassociated_listed_elements_.Get(*this);
}

void Document::MarkUnassociatedListedElementsDirty() {
  unassociated_listed_elements_.MarkDirty();
}

ExplicitlySetAttrElementsMap* Document::GetExplicitlySetAttrElementsMap(
    Element* element) {
  DCHECK(element);
  DCHECK(element->GetDocument() == this);
  auto add_result =
      element_explicitly_set_attr_elements_map_.insert(element, nullptr);
  if (add_result.is_new_entry) {
    add_result.stored_value->value =
        MakeGarbageCollected<ExplicitlySetAttrElementsMap>();
  }
  return add_result.stored_value->value;
}

void Document::MoveElementExplicitlySetAttrElementsMapToNewDocument(
    Element* element,
    Document& new_document) {
  DCHECK(element);
  auto it = element_explicitly_set_attr_elements_map_.find(element);
  if (it != element_explicitly_set_attr_elements_map_.end()) {
    new_document.element_explicitly_set_attr_elements_map_.insert(element,
                                                                  it->value);
    element_explicitly_set_attr_elements_map_.erase(it);
  }
}

UnloadEventTimingInfo::UnloadEventTimingInfo(
    scoped_refptr<SecurityOrigin> new_document_origin)
    : new_document_origin(std::move(new_document_origin)) {}

Document* Document::Create(Document& document) {
  Document* new_document = MakeGarbageCollected<Document>(
      DocumentInit::Create()
          .WithExecutionContext(document.GetExecutionContext())
          .WithURL(BlankURL()));
  new_document->SetContextFeatures(document.GetContextFeatures());
  return new_document;
}

Document* Document::CreateForTest() {
  return MakeGarbageCollected<Document>(DocumentInit::Create().ForTest());
}

Document::Document(const DocumentInit& initializer,
                   DocumentClassFlags document_classes)
    : ContainerNode(nullptr, kCreateDocument),
      TreeScope(
          *this,
          static_cast<V8ObservableArrayCSSStyleSheet::SetAlgorithmCallback>(
              &Document::OnAdoptedStyleSheetSet),
          static_cast<V8ObservableArrayCSSStyleSheet::DeleteAlgorithmCallback>(
              &Document::OnAdoptedStyleSheetDelete)),
      is_initial_empty_document_(initializer.IsInitialEmptyDocument()),
      is_prerendering_(initializer.IsPrerendering()),
      evaluate_media_queries_on_style_recalc_(false),
      pending_sheet_layout_(kNoLayoutWithPendingSheets),
      dom_window_(initializer.GetWindow()),
      execution_context_(initializer.GetExecutionContext()),
      context_features_(ContextFeatures::DefaultSwitch()),
      http_refresh_scheduler_(MakeGarbageCollected<HttpRefreshScheduler>(this)),
      well_formed_(false),
      cookie_url_(dom_window_ ? initializer.GetCookieUrl()
                              : KURL(g_empty_string)),
      printing_(kNotPrinting),
      paint_preview_(kNotPaintingPreview),
      compatibility_mode_(kNoQuirksMode),
      compatibility_mode_locked_(false),
      last_focus_type_(mojom::blink::FocusType::kNone),
      had_keyboard_event_(false),
      clear_focused_element_timer_(
          GetTaskRunner(TaskType::kInternalUserInteraction),
          this,
          &Document::ClearFocusedElementTimerFired),
      dom_tree_version_(++global_tree_version_),
      style_version_(0),
      listener_types_(0),
      mutation_observer_types_(0),
      visited_link_state_(MakeGarbageCollected<VisitedLinkState>(*this)),
      visually_ordered_(false),
      // https://html.spec.whatwg.org/multipage/dom.html#current-document-readiness
      // says the ready state starts as 'loading' if there's an associated
      // parser and 'complete' otherwise. We don't know whether there's an
      // associated parser here (we create the parser in ImplicitOpen). But
      // waiting to set the ready state to 'loading' in ImplicitOpen fires a
      // readystatechange event, which can be observed in the case where we
      // reuse a window. If there's a window being reused, there must be an
      // associated parser, so setting based on dom_window_ here is sufficient
      // to ensure that the quirk of when we set the ready state is not
      // web-observable.
      ready_state_(dom_window_ ? kLoading : kComplete),
      parsing_state_(kFinishedParsing),
      contains_plugins_(false),
      ignore_destructive_write_count_(0),
      throw_on_dynamic_markup_insertion_count_(0),
      ignore_opens_during_unload_count_(0),
      markers_(MakeGarbageCollected<DocumentMarkerController>(*this)),
      css_target_(nullptr),
      was_discarded_(false),
      load_event_progress_(kLoadEventCompleted),
      is_freezing_in_progress_(false),
      script_runner_(MakeGarbageCollected<ScriptRunner>(this)),
      script_runner_delayer_(MakeGarbageCollected<ScriptRunnerDelayer>(
          script_runner_,
          ScriptRunner::DelayReason::kMilestone)),
      xml_version_("1.0"),
      xml_standalone_(kStandaloneUnspecified),
      has_xml_declaration_(0),
      viewport_unit_flags_(0),
      design_mode_(false),
      is_running_exec_command_(false),
      has_annotated_regions_(false),
      annotated_regions_dirty_(false),
      document_classes_(document_classes),
      is_view_source_(false),
      saw_elements_in_known_namespaces_(false),
      is_srcdoc_document_(initializer.IsSrcdocDocument()),
      is_mobile_document_(false),
      layout_view_(nullptr),
      load_event_delay_count_(0),
      // We already intentionally fire load event asynchronously and here we use
      // kDOMManipulation to ensure that we run onload() in order with other
      // callbacks (e.g. onloadstart()) per the spec.
      // See: https://html.spec.whatwg.org/#delay-the-load-event
      load_event_delay_timer_(GetTaskRunner(TaskType::kDOMManipulation),
                              this,
                              &Document::LoadEventDelayTimerFired),
      plugin_loading_timer_(GetTaskRunner(TaskType::kInternalLoading),
                            this,
                            &Document::PluginLoadingTimerFired),
      document_timing_(*this),
      write_recursion_is_too_deep_(false),
      write_recursion_depth_(0),
      scripted_animation_controller_(
          MakeGarbageCollected<ScriptedAnimationController>(domWindow())),
      element_data_cache_clear_timer_(
          GetTaskRunner(TaskType::kInternalUserInteraction),
          this,
          &Document::ElementDataCacheClearTimerFired),
      document_animations_(MakeGarbageCollected<DocumentAnimations>(this)),
      timeline_(MakeGarbageCollected<DocumentTimeline>(this)),
      pending_animations_(MakeGarbageCollected<PendingAnimations>(*this)),
      worklet_animation_controller_(
          MakeGarbageCollected<WorkletAnimationController>(this)),
      template_document_host_(nullptr),
      did_associate_form_controls_timer_(
          GetTaskRunner(TaskType::kInternalLoading),
          this,
          &Document::DidAssociateFormControlsTimerFired),
      parser_sync_policy_(kAllowDeferredParsing),
      node_count_(0),
      // Use the source id from the document initializer if it is available.
      // Otherwise, generate a new source id to cover any cases that don't
      // receive a valid source id, this for example includes but is not limited
      // to SVGImage which does not have an associated RenderFrameHost. No URLs
      // will be associated to this source id. No DocumentCreated events will be
      // created either.
      ukm_source_id_(initializer.UkmSourceId() == ukm::kInvalidSourceId
                         ? ukm::UkmRecorder::GetNewSourceID()
                         : initializer.UkmSourceId()),
      viewport_data_(MakeGarbageCollected<ViewportData>(*this)),
      is_for_external_handler_(initializer.IsForExternalHandler()),
      fragment_directive_(MakeGarbageCollected<FragmentDirective>(*this)),
      display_lock_document_state_(
          MakeGarbageCollected<DisplayLockDocumentState>(this)),
      render_blocking_resource_manager_(
          initializer.GetType() == DocumentInit::Type::kHTML
              ? MakeGarbageCollected<RenderBlockingResourceManager>(*this)
              : nullptr),
      data_(MakeGarbageCollected<DocumentData>(GetExecutionContext())) {
  if (base::FeatureList::IsEnabled(features::kDelayAsyncScriptExecution))
    script_runner_delayer_->Activate();

  if (GetFrame()) {
    DCHECK(GetFrame()->GetPage());
    ProvideContextFeaturesToDocumentFrom(*this, *GetFrame()->GetPage());
    fetcher_ = FrameFetchContext::CreateFetcherForCommittedDocument(
        *GetFrame()->Loader().GetDocumentLoader(), *this);
    cookie_jar_ = MakeGarbageCollected<CookieJar>(this);
  } else {
    // We disable fetches for frame-less Documents.
    // See https://crbug.com/961614 for details.
    auto& properties =
        *MakeGarbageCollected<DetachableResourceFetcherProperties>(
            *MakeGarbageCollected<NullResourceFetcherProperties>());
    fetcher_ = MakeGarbageCollected<ResourceFetcher>(
        ResourceFetcherInit(properties, &FetchContext::NullInstance(),
                            GetTaskRunner(TaskType::kNetworking),
                            GetTaskRunner(TaskType::kNetworkingUnfreezable),
                            nullptr /* loader_factory */, GetExecutionContext(),
                            nullptr /* back_forward_cache_loader_helper */));
  }
  DCHECK(fetcher_);

  root_scroller_controller_ =
      MakeGarbageCollected<RootScrollerController>(*this);

  // We depend on the url getting immediately set in subframes, but we
  // also depend on the url NOT getting immediately set in opened windows.
  // See fast/dom/early-frame-url.html
  // and fast/dom/location-new-window-no-crash.html, respectively.
  // FIXME: Can/should we unify this behavior?
  if (initializer.ShouldSetURL()) {
    SetURL(initializer.Url());
  } else {
    // Even if this document has no URL, we need to initialize base URL with
    // fallback base URL.
    UpdateBaseURL();
  }

  if (initializer.GetWebBundleClaimedUrl().IsValid()) {
    web_bundle_claimed_url_ = initializer.GetWebBundleClaimedUrl();
    SetBaseURLOverride(initializer.GetWebBundleClaimedUrl());
  }

  is_vertical_scroll_enforced_ =
      GetFrame() && !GetFrame()->IsOutermostMainFrame() &&
      RuntimeEnabledFeatures::ExperimentalPoliciesEnabled() &&
      !dom_window_->IsFeatureEnabled(
          mojom::blink::PermissionsPolicyFeature::kVerticalScroll);

  InitDNSPrefetch();

  InstanceCounters::IncrementCounter(InstanceCounters::kDocumentCounter);

  lifecycle_.AdvanceTo(DocumentLifecycle::kInactive);

  // Since CSSFontSelector requires Document::fetcher_ and StyleEngine owns
  // CSSFontSelector, need to initialize |style_engine_| after initializing
  // |fetcher_|.
  style_engine_ = MakeGarbageCollected<StyleEngine>(*this);

  UpdateThemeColorCache();

  // The parent's parser should be suspended together with all the other
  // objects, else this new Document would have a new ExecutionContext which
  // suspended state would not match the one from the parent, and could start
  // loading resources ignoring the defersLoading flag.
  DCHECK(!ParentDocument() ||
         !ParentDocument()->domWindow()->IsContextPaused());

#ifndef NDEBUG
  LiveDocumentSet().insert(this);
#endif
}

Document::~Document() {
  DCHECK(!GetLayoutView());
  DCHECK(!ParentTreeScope());
  // If a top document with a cache, verify that it was comprehensively
  // cleared during detach.
  DCHECK(!ax_object_cache_);

  InstanceCounters::DecrementCounter(InstanceCounters::kDocumentCounter);
}

Range* Document::CreateRangeAdjustedToTreeScope(const TreeScope& tree_scope,
                                                const Position& position) {
  DCHECK(position.IsNotNull());
  // Note: Since |Position::ComputeContainerNode()| returns |nullptr| if
  // |position| is |BeforeAnchor| or |AfterAnchor|.
  Node* const anchor_node = position.AnchorNode();
  if (anchor_node->GetTreeScope() == tree_scope) {
    return MakeGarbageCollected<Range>(tree_scope.GetDocument(), position,
                                       position);
  }
  Node* const shadow_host = tree_scope.AncestorInThisScope(anchor_node);
  return MakeGarbageCollected<Range>(tree_scope.GetDocument(),
                                     Position::BeforeNode(*shadow_host),
                                     Position::BeforeNode(*shadow_host));
}

SelectorQueryCache& Document::GetSelectorQueryCache() {
  if (!selector_query_cache_)
    selector_query_cache_ = std::make_unique<SelectorQueryCache>();
  return *selector_query_cache_;
}

MediaQueryMatcher& Document::GetMediaQueryMatcher() {
  if (!media_query_matcher_)
    media_query_matcher_ = MakeGarbageCollected<MediaQueryMatcher>(*this);
  return *media_query_matcher_;
}

void Document::MediaQueryAffectingValueChanged(MediaValueChange change) {
  GetStyleEngine().MediaQueryAffectingValueChanged(change);
  if (NeedsLayoutTreeUpdate())
    evaluate_media_queries_on_style_recalc_ = true;
  else
    EvaluateMediaQueryList();
  probe::MediaQueryResultChanged(this);
}

void Document::SetCompatibilityMode(CompatibilityMode mode) {
  if (compatibility_mode_locked_ || mode == compatibility_mode_)
    return;

  if (compatibility_mode_ == kQuirksMode)
    UseCounter::Count(*this, WebFeature::kQuirksModeDocument);
  else if (compatibility_mode_ == kLimitedQuirksMode)
    UseCounter::Count(*this, WebFeature::kLimitedQuirksModeDocument);

  compatibility_mode_ = mode;
  GetSelectorQueryCache().Invalidate();
}

String Document::compatMode() const {
  return InQuirksMode() ? "BackCompat" : "CSS1Compat";
}

void Document::SetDoctype(DocumentType* doc_type) {
  // This should never be called more than once.
  DCHECK(!doc_type_ || !doc_type);
  doc_type_ = doc_type;
  if (doc_type_) {
    AdoptIfNeeded(*doc_type_);
    if (doc_type_->publicId().StartsWithIgnoringASCIICase(
            "-//wapforum//dtd xhtml mobile 1.")) {
      is_mobile_document_ = true;
      style_engine_->ViewportRulesChanged();
    }
  }
}

DOMImplementation& Document::implementation() {
  if (!implementation_)
    implementation_ = MakeGarbageCollected<DOMImplementation>(*this);
  return *implementation_;
}

Location* Document::location() const {
  if (!GetFrame())
    return nullptr;

  return domWindow()->location();
}

bool Document::DocumentPolicyFeatureObserved(
    mojom::blink::DocumentPolicyFeature feature) {
  wtf_size_t feature_index = static_cast<wtf_size_t>(feature);
  if (parsed_document_policies_.size() == 0) {
    parsed_document_policies_.resize(
        static_cast<wtf_size_t>(
            mojom::blink::DocumentPolicyFeature::kMaxValue) +
        1);
  } else if (parsed_document_policies_[feature_index]) {
    return true;
  }
  parsed_document_policies_[feature_index] = true;
  return false;
}

void Document::ChildrenChanged(const ChildrenChange& change) {
  ContainerNode::ChildrenChanged(change);
  document_element_ = ElementTraversal::FirstWithin(*this);

  // For non-HTML documents the willInsertBody notification won't happen
  // so we resume as soon as we have a document element. Even for XHTML
  // documents there may never be a <body> (since the parser won't always
  // insert one), so we resume here too. That does mean XHTML documents make
  // frames when there's only a <head>, but such documents are pretty rare.
  if (document_element_ && !IsA<HTMLDocument>(this))
    BeginLifecycleUpdatesIfRenderingReady();
}

bool Document::IsInMainFrame() const {
  return GetFrame() && GetFrame()->IsMainFrame();
}

bool Document::IsInOutermostMainFrame() const {
  return GetFrame() && GetFrame()->IsOutermostMainFrame();
}

AtomicString Document::ConvertLocalName(const AtomicString& name) {
  return IsA<HTMLDocument>(this) ? name.LowerASCII() : name;
}

// Just creates an element with specified qualified name without any
// custom element processing.
// This is a common code for step 5.2 and 7.2 of "create an element"
// <https://dom.spec.whatwg.org/#concept-create-element>
// Functions other than this one should not use HTMLElementFactory and
// SVGElementFactory because they don't support prefixes correctly.
Element* Document::CreateRawElement(const QualifiedName& qname,
                                    CreateElementFlags flags) {
  Element* element = nullptr;
  if (qname.NamespaceURI() == html_names::xhtmlNamespaceURI) {
    // https://html.spec.whatwg.org/C/#elements-in-the-dom:element-interface
    element = HTMLElementFactory::Create(qname.LocalName(), *this, flags);
    if (!element) {
      // 6. If name is a valid custom element name, then return
      // HTMLElement.
      // 7. Return HTMLUnknownElement.
      if (CustomElement::IsValidName(qname.LocalName()))
        element = MakeGarbageCollected<HTMLElement>(qname, *this);
      else
        element = MakeGarbageCollected<HTMLUnknownElement>(qname, *this);
    }
    saw_elements_in_known_namespaces_ = true;
  } else if (qname.NamespaceURI() == svg_names::kNamespaceURI) {
    element = SVGElementFactory::Create(qname.LocalName(), *this, flags);
    if (!element)
      element = MakeGarbageCollected<SVGUnknownElement>(qname, *this);
    saw_elements_in_known_namespaces_ = true;
  } else if (qname.NamespaceURI() == mathml_names::kNamespaceURI) {
    if (RuntimeEnabledFeatures::MathMLCoreEnabled()) {
      element = MathMLElementFactory::Create(qname.LocalName(), *this, flags);
      // An unknown MathML element is treated like an <mrow> element.
      // TODO(crbug.com/1021837): Determine if we need to introduce a
      // MathMLUnknownElement IDL.
      if (!element)
        element = MakeGarbageCollected<MathMLRowElement>(qname, *this);
      saw_elements_in_known_namespaces_ = true;
    } else {
      element = MakeGarbageCollected<MathMLElement>(qname, *this);
    }
  } else {
    element = MakeGarbageCollected<Element>(qname, this);
  }

  if (element->prefix() != qname.Prefix())
    element->SetTagNameForCreateElementNS(qname);
  DCHECK(qname == element->TagQName());

  return element;
}

// https://dom.spec.whatwg.org/#dom-document-createelement
Element* Document::CreateElementForBinding(const AtomicString& name,
                                           ExceptionState& exception_state) {
  if (!IsValidElementName(this, name)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidCharacterError,
        "The tag name provided ('" + name + "') is not a valid name.");
    return nullptr;
  }

  if (IsXHTMLDocument() || IsA<HTMLDocument>(this)) {
    // 2. If the context object is an HTML document, let localName be
    // converted to ASCII lowercase.
    AtomicString local_name = ConvertLocalName(name);
    if (CustomElement::ShouldCreateCustomElement(local_name)) {
      return CustomElement::CreateCustomElement(
          *this,
          QualifiedName(g_null_atom, local_name, html_names::xhtmlNamespaceURI),
          CreateElementFlags::ByCreateElement());
    }
    if (auto* element = HTMLElementFactory::Create(
            local_name, *this, CreateElementFlags::ByCreateElement()))
      return element;
    QualifiedName q_name(g_null_atom, local_name,
                         html_names::xhtmlNamespaceURI);
    return MakeGarbageCollected<HTMLUnknownElement>(q_name, *this);
  }
  return MakeGarbageCollected<Element>(
      QualifiedName(g_null_atom, name, g_null_atom), this);
}

AtomicString GetTypeExtension(
    Document* document,
    const V8UnionElementCreationOptionsOrString* string_or_options) {
  DCHECK(string_or_options);

  switch (string_or_options->GetContentType()) {
    case V8UnionElementCreationOptionsOrString::ContentType::
        kElementCreationOptions: {
      const ElementCreationOptions* options =
          string_or_options->GetAsElementCreationOptions();
      if (options->hasIs())
        return AtomicString(options->is());
      return AtomicString();
    }
    case V8UnionElementCreationOptionsOrString::ContentType::kString:
      UseCounter::Count(document,
                        WebFeature::kDocumentCreateElement2ndArgStringHandling);
      return AtomicString(string_or_options->GetAsString());
  }
  NOTREACHED();
  return AtomicString();
}

// https://dom.spec.whatwg.org/#dom-document-createelement
Element* Document::CreateElementForBinding(
    const AtomicString& local_name,
    const V8UnionElementCreationOptionsOrString* string_or_options,
    ExceptionState& exception_state) {
  if (!string_or_options) {
    return CreateElementForBinding(local_name, exception_state);
  }

  // 1. If localName does not match Name production, throw InvalidCharacterError
  if (!IsValidElementName(this, local_name)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidCharacterError,
        "The tag name provided ('" + local_name + "') is not a valid name.");
    return nullptr;
  }

  // 2. localName converted to ASCII lowercase
  const AtomicString& converted_local_name = ConvertLocalName(local_name);
  QualifiedName q_name(g_null_atom, converted_local_name,
                       IsXHTMLDocument() || IsA<HTMLDocument>(this)
                           ? html_names::xhtmlNamespaceURI
                           : g_null_atom);

  // 3.
  const AtomicString& is = GetTypeExtension(this, string_or_options);

  // 5. Let element be the result of creating an element given ...
  Element* element =
      CreateElement(q_name, CreateElementFlags::ByCreateElement(), is);

  return element;
}

static inline QualifiedName CreateQualifiedName(
    const AtomicString& namespace_uri,
    const AtomicString& qualified_name,
    ExceptionState& exception_state) {
  AtomicString prefix, local_name;
  if (!Document::ParseQualifiedName(qualified_name, prefix, local_name,
                                    exception_state))
    return QualifiedName::Null();

  QualifiedName q_name(prefix, local_name, namespace_uri);
  if (!Document::HasValidNamespaceForElements(q_name)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNamespaceError,
        "The namespace URI provided ('" + namespace_uri +
            "') is not valid for the qualified name provided ('" +
            qualified_name + "').");
    return QualifiedName::Null();
  }

  return q_name;
}

Element* Document::createElementNS(const AtomicString& namespace_uri,
                                   const AtomicString& qualified_name,
                                   ExceptionState& exception_state) {
  QualifiedName q_name(
      CreateQualifiedName(namespace_uri, qualified_name, exception_state));
  if (q_name == QualifiedName::Null())
    return nullptr;

  CreateElementFlags flags = CreateElementFlags::ByCreateElement();
  if (CustomElement::ShouldCreateCustomElement(q_name))
    return CustomElement::CreateCustomElement(*this, q_name, flags);
  return CreateRawElement(q_name, flags);
}

// https://dom.spec.whatwg.org/#internal-createelementns-steps
Element* Document::createElementNS(
    const AtomicString& namespace_uri,
    const AtomicString& qualified_name,
    const V8UnionElementCreationOptionsOrString* string_or_options,
    ExceptionState& exception_state) {
  DCHECK(string_or_options);

  // 1. Validate and extract
  QualifiedName q_name(
      CreateQualifiedName(namespace_uri, qualified_name, exception_state));
  if (q_name == QualifiedName::Null())
    return nullptr;

  // 2.
  const AtomicString& is = GetTypeExtension(this, string_or_options);

  if (!IsValidElementName(this, qualified_name)) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidCharacterError,
                                      "The tag name provided ('" +
                                          qualified_name +
                                          "') is not a valid name.");
    return nullptr;
  }

  // 3. Let element be the result of creating an element
  Element* element =
      CreateElement(q_name, CreateElementFlags::ByCreateElement(), is);

  return element;
}

// Entry point of "create an element".
// https://dom.spec.whatwg.org/#concept-create-element
Element* Document::CreateElement(const QualifiedName& q_name,
                                 const CreateElementFlags flags,
                                 const AtomicString& is) {
  CustomElementDefinition* definition = nullptr;
  if (flags.IsCustomElements() &&
      q_name.NamespaceURI() == html_names::xhtmlNamespaceURI) {
    const CustomElementDescriptor desc(is.IsNull() ? q_name.LocalName() : is,
                                       q_name.LocalName());
    if (CustomElementRegistry* registry = CustomElement::Registry(*this))
      definition = registry->DefinitionFor(desc);
  }

  if (definition)
    return definition->CreateElement(*this, q_name, flags);

  return CustomElement::CreateUncustomizedOrUndefinedElement(*this, q_name,
                                                             flags, is);
}

DocumentFragment* Document::createDocumentFragment() {
  return DocumentFragment::Create(*this);
}

Text* Document::createTextNode(const String& data) {
  return Text::Create(*this, data);
}

Comment* Document::createComment(const String& data) {
  return Comment::Create(*this, data);
}

CDATASection* Document::createCDATASection(const String& data,
                                           ExceptionState& exception_state) {
  if (IsA<HTMLDocument>(this)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "This operation is not supported for HTML documents.");
    return nullptr;
  }
  if (data.Contains("]]>")) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidCharacterError,
                                      "String cannot contain ']]>' since that "
                                      "is the end delimiter of a CData "
                                      "section.");
    return nullptr;
  }
  return CDATASection::Create(*this, data);
}

ProcessingInstruction* Document::createProcessingInstruction(
    const String& target,
    const String& data,
    ExceptionState& exception_state) {
  if (!IsValidName(target)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidCharacterError,
        "The target provided ('" + target + "') is not a valid name.");
    return nullptr;
  }
  if (data.Contains("?>")) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidCharacterError,
        "The data provided ('" + data + "') contains '?>'.");
    return nullptr;
  }
  if (IsA<HTMLDocument>(this)) {
    UseCounter::Count(*this,
                      WebFeature::kHTMLDocumentCreateProcessingInstruction);
  }
  return MakeGarbageCollected<ProcessingInstruction>(*this, target, data);
}

Text* Document::CreateEditingTextNode(const String& text) {
  return Text::CreateEditingText(*this, text);
}

Node* Document::importNode(Node* imported_node,
                           bool deep,
                           ExceptionState& exception_state) {
  // https://dom.spec.whatwg.org/#dom-document-importnode

  // 1. If node is a document or shadow root, then throw a "NotSupportedError"
  // DOMException.
  if (imported_node->IsDocumentNode()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The node provided is a document, which may not be imported.");
    return nullptr;
  }
  if (imported_node->IsShadowRoot()) {
    // ShadowRoot nodes should not be explicitly importable.  Either they are
    // imported along with their host node, or created implicitly.
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The node provided is a shadow root, which may not be imported.");
    return nullptr;
  }

  // 2. Return a clone of node, with context object and the clone children flag
  // set if deep is true.
  return imported_node->Clone(
      *this, deep ? CloneChildrenFlag::kClone : CloneChildrenFlag::kSkip);
}

Node* Document::adoptNode(Node* source, ExceptionState& exception_state) {
  EventQueueScope scope;

  switch (source->getNodeType()) {
    case kDocumentNode:
      exception_state.ThrowDOMException(DOMExceptionCode::kNotSupportedError,
                                        "The node provided is of type '" +
                                            source->nodeName() +
                                            "', which may not be adopted.");
      return nullptr;
    case kAttributeNode: {
      auto* attr = To<Attr>(source);
      if (Element* owner_element = attr->ownerElement())
        owner_element->removeAttributeNode(attr, exception_state);
      break;
    }
    default:
      if (source->IsShadowRoot()) {
        // ShadowRoot cannot disconnect itself from the host node.
        exception_state.ThrowDOMException(
            DOMExceptionCode::kHierarchyRequestError,
            "The node provided is a shadow root, which may not be adopted.");
        return nullptr;
      }

      if (auto* frame_owner_element =
              DynamicTo<HTMLFrameOwnerElement>(source)) {
        if (GetFrame() && GetFrame()->Tree().IsDescendantOf(
                              frame_owner_element->ContentFrame())) {
          exception_state.ThrowDOMException(
              DOMExceptionCode::kHierarchyRequestError,
              "The node provided is a frame which contains this document.");
          return nullptr;
        }
      }
      if (source->parentNode()) {
        source->parentNode()->RemoveChild(source, exception_state);
        if (exception_state.HadException())
          return nullptr;
        // The above removeChild() can execute arbitrary JavaScript code.
        if (source->parentNode()) {
          AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
              mojom::ConsoleMessageSource::kJavaScript,
              mojom::ConsoleMessageLevel::kWarning,
              ExceptionMessages::FailedToExecute("adoptNode", "Document",
                                                 "Unable to remove the "
                                                 "specified node from the "
                                                 "original parent.")));
          return nullptr;
        }
      }
  }

  AdoptIfNeeded(*source);

  return source;
}

bool Document::HasValidNamespaceForElements(const QualifiedName& q_name) {
  // These checks are from DOM Core Level 2, createElementNS
  // http://www.w3.org/TR/DOM-Level-2-Core/core.html#ID-DocCrElNS
  // createElementNS(null, "html:div")
  if (!q_name.Prefix().IsEmpty() && q_name.NamespaceURI().IsNull())
    return false;
  // createElementNS("http://www.example.com", "xml:lang")
  if (q_name.Prefix() == g_xml_atom &&
      q_name.NamespaceURI() != xml_names::kNamespaceURI)
    return false;

  // Required by DOM Level 3 Core and unspecified by DOM Level 2 Core:
  // http://www.w3.org/TR/2004/REC-DOM-Level-3-Core-20040407/core.html#ID-DocCrElNS
  // createElementNS("http://www.w3.org/2000/xmlns/", "foo:bar"),
  // createElementNS(null, "xmlns:bar"), createElementNS(null, "xmlns")
  if (q_name.Prefix() == g_xmlns_atom ||
      (q_name.Prefix().IsEmpty() && q_name.LocalName() == g_xmlns_atom))
    return q_name.NamespaceURI() == xmlns_names::kNamespaceURI;
  return q_name.NamespaceURI() != xmlns_names::kNamespaceURI;
}

bool Document::HasValidNamespaceForAttributes(const QualifiedName& q_name) {
  return HasValidNamespaceForElements(q_name);
}

String Document::readyState() const {
  DEFINE_STATIC_LOCAL(const String, loading, ("loading"));
  DEFINE_STATIC_LOCAL(const String, interactive, ("interactive"));
  DEFINE_STATIC_LOCAL(const String, complete, ("complete"));

  switch (ready_state_) {
    case kLoading:
      return loading;
    case kInteractive:
      return interactive;
    case kComplete:
      return complete;
  }

  NOTREACHED();
  return String();
}

void Document::SetReadyState(DocumentReadyState ready_state) {
  if (ready_state == ready_state_)
    return;

  switch (ready_state) {
    case kLoading:
      if (document_timing_.DomLoading().is_null()) {
        document_timing_.MarkDomLoading();
      }
      break;
    case kInteractive:
      if (document_timing_.DomInteractive().is_null())
        document_timing_.MarkDomInteractive();
      break;
    case kComplete:
      if (document_timing_.DomComplete().is_null())
        document_timing_.MarkDomComplete();
      break;
  }

  ready_state_ = ready_state;
  DispatchEvent(*Event::Create(event_type_names::kReadystatechange));
}

bool Document::IsLoadCompleted() const {
  return ready_state_ == kComplete;
}

AtomicString Document::EncodingName() const {
  // TextEncoding::name() returns a char*, no need to allocate a new
  // String for it each time.
  // FIXME: We should fix TextEncoding to speak AtomicString anyway.
  return AtomicString(Encoding().GetName());
}

void Document::SetContentLanguage(const AtomicString& language) {
  if (content_language_ == language)
    return;
  content_language_ = language;

  // Document's style depends on the content language.
  GetStyleEngine().MarkViewportStyleDirty();
  GetStyleEngine().MarkAllElementsForStyleRecalc(
      StyleChangeReasonForTracing::Create(style_change_reason::kLanguage));
}

void Document::setXMLVersion(const String& version,
                             ExceptionState& exception_state) {
  if (!XMLDocumentParser::SupportsXMLVersion(version)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "This document does not support the XML version '" + version + "'.");
    return;
  }

  xml_version_ = version;
}

void Document::setXMLStandalone(bool standalone,
                                ExceptionState& exception_state) {
  xml_standalone_ = standalone ? kStandalone : kNotStandalone;
}

void Document::SetContent(const String& content) {
  // Only set the content of the document if it is ready to be set. This method
  // could be called at any time.
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser()) {
    if (parser->IsParsing() && parser->IsExecutingScript())
      return;
  }
  if (ignore_opens_during_unload_count_)
    return;

  open();
  parser_->Append(content);
  close();
}

String Document::SuggestedMIMEType() const {
  if (IsA<XMLDocument>(this)) {
    if (IsXHTMLDocument())
      return "application/xhtml+xml";
    if (IsSVGDocument())
      return "image/svg+xml";
    return "application/xml";
  }
  if (xmlStandalone())
    return "text/xml";
  if (IsA<HTMLDocument>(this))
    return "text/html";

  if (DocumentLoader* document_loader = Loader())
    return document_loader->MimeType();
  return String();
}

void Document::SetMimeType(const AtomicString& mime_type) {
  mime_type_ = mime_type;
}

AtomicString Document::contentType() const {
  if (!mime_type_.IsEmpty())
    return mime_type_;

  if (DocumentLoader* document_loader = Loader())
    return document_loader->MimeType();

  String mime_type = SuggestedMIMEType();
  if (!mime_type.IsEmpty())
    return AtomicString(mime_type);

  return AtomicString("application/xml");
}

Range* Document::caretRangeFromPoint(int x, int y) {
  if (!GetLayoutView())
    return nullptr;

  HitTestResult result = HitTestInDocument(this, x, y);
  PositionWithAffinity position_with_affinity = result.GetPosition();
  if (position_with_affinity.IsNull())
    return nullptr;

  Position range_compliant_position =
      position_with_affinity.GetPosition().ParentAnchoredEquivalent();
  return CreateRangeAdjustedToTreeScope(*this, range_compliant_position);
}

Element* Document::scrollingElement() {
  if (RuntimeEnabledFeatures::ScrollTopLeftInteropEnabled() && InQuirksMode())
    UpdateStyleAndLayoutTree();
  return ScrollingElementNoLayout();
}

Element* Document::ScrollingElementNoLayout() {
  if (RuntimeEnabledFeatures::ScrollTopLeftInteropEnabled()) {
    if (InQuirksMode()) {
      HTMLBodyElement* body = FirstBodyElement();
      if (body && body->GetLayoutObject() &&
          body->GetLayoutObject()->IsScrollContainer())
        return nullptr;

      return body;
    }

    return documentElement();
  }

  return body();
}

/*
 * Performs three operations:
 *  1. Convert control characters to spaces
 *  2. Trim leading and trailing spaces
 *  3. Collapse internal whitespace.
 */
template <typename CharacterType>
static inline String CanonicalizedTitle(Document* document,
                                        const String& title) {
  unsigned length = title.length();
  unsigned builder_index = 0;
  const CharacterType* characters = title.GetCharacters<CharacterType>();

  StringBuffer<CharacterType> buffer(length);

  // Replace control characters with spaces and collapse whitespace.
  bool pending_whitespace = false;
  for (unsigned i = 0; i < length; ++i) {
    UChar32 c = characters[i];
    if ((c <= WTF::unicode::kSpaceCharacter &&
         c != WTF::unicode::kLineTabulationCharacter) ||
        c == WTF::unicode::kDeleteCharacter) {
      if (builder_index != 0)
        pending_whitespace = true;
    } else {
      if (pending_whitespace) {
        buffer[builder_index++] = ' ';
        pending_whitespace = false;
      }
      buffer[builder_index++] = c;
    }
  }
  buffer.Shrink(builder_index);

  return String::Adopt(buffer);
}

void Document::UpdateTitle(const String& title) {
  if (raw_title_ == title)
    return;

  raw_title_ = title;

  String old_title = title_;
  if (raw_title_.IsEmpty())
    title_ = String();
  else if (raw_title_.Is8Bit())
    title_ = CanonicalizedTitle<LChar>(this, raw_title_);
  else
    title_ = CanonicalizedTitle<UChar>(this, raw_title_);

  if (!dom_window_ || old_title == title_)
    return;
  DispatchDidReceiveTitle();

  if (AXObjectCache* cache = ExistingAXObjectCache())
    cache->DocumentTitleChanged();
}

void Document::DispatchDidReceiveTitle() {
  if (IsInMainFrame()) {
    String shortened_title = title_.Substring(0, mojom::blink::kMaxTitleChars);
    GetFrame()->GetLocalFrameHostRemote().UpdateTitle(
        shortened_title, base::i18n::TextDirection::LEFT_TO_RIGHT);
    GetFrame()->GetPage()->GetPageScheduler()->OnTitleOrFaviconUpdated();
  }
  GetFrame()->Client()->DispatchDidReceiveTitle(title_);
}

void Document::setTitle(const String& title) {
  // Title set by JavaScript -- overrides any title elements.
  Element* element = documentElement();
  if (IsA<SVGSVGElement>(element)) {
    if (!title_element_) {
      title_element_ = MakeGarbageCollected<SVGTitleElement>(*this);
      element->InsertBefore(title_element_.Get(), element->firstChild());
    }
    if (auto* svg_title = DynamicTo<SVGTitleElement>(title_element_.Get()))
      svg_title->SetText(title);
  } else if (element && element->IsHTMLElement()) {
    if (!title_element_) {
      HTMLElement* head_element = head();
      if (!head_element)
        return;
      title_element_ = MakeGarbageCollected<HTMLTitleElement>(*this);
      head_element->AppendChild(title_element_.Get());
    }
    if (auto* html_title = DynamicTo<HTMLTitleElement>(title_element_.Get()))
      html_title->setText(title);
  }
}

void Document::SetTitleElement(Element* title_element) {
  // If the root element is an svg element in the SVG namespace, then let value
  // be the child text content of the first title element in the SVG namespace
  // that is a child of the root element.
  if (IsA<SVGSVGElement>(documentElement())) {
    title_element_ = Traversal<SVGTitleElement>::FirstChild(*documentElement());
  } else {
    if (title_element_ && title_element_ != title_element)
      title_element_ = Traversal<HTMLTitleElement>::FirstWithin(*this);
    else
      title_element_ = title_element;

    // If the root element isn't an svg element in the SVG namespace and the
    // title element is in the SVG namespace, it is ignored.
    if (IsA<SVGTitleElement>(*title_element_)) {
      title_element_ = nullptr;
      return;
    }
  }

  if (auto* html_title = DynamicTo<HTMLTitleElement>(title_element_.Get()))
    UpdateTitle(html_title->text());
  else if (auto* svg_title = DynamicTo<SVGTitleElement>(title_element_.Get()))
    UpdateTitle(svg_title->textContent());
}

void Document::RemoveTitle(Element* title_element) {
  if (title_element_ != title_element)
    return;

  title_element_ = nullptr;

  // Update title based on first title element in the document, if one exists.
  if (IsA<HTMLDocument>(this) || IsXHTMLDocument()) {
    if (HTMLTitleElement* title =
            Traversal<HTMLTitleElement>::FirstWithin(*this))
      SetTitleElement(title);
  } else if (IsSVGDocument()) {
    if (SVGTitleElement* title = Traversal<SVGTitleElement>::FirstWithin(*this))
      SetTitleElement(title);
  }

  if (!title_element_)
    UpdateTitle(String());
}

const AtomicString& Document::dir() {
  Element* root_element = documentElement();
  if (auto* html = DynamicTo<HTMLHtmlElement>(root_element))
    return html->dir();
  return g_null_atom;
}

void Document::setDir(const AtomicString& value) {
  Element* root_element = documentElement();
  if (auto* html = DynamicTo<HTMLHtmlElement>(root_element))
    html->setDir(value);
}

bool Document::IsPageVisible() const {
  // The visibility of the document is inherited from the visibility of the
  // page. If there is no page associated with the document, we will assume
  // that the page is hidden, as specified by the spec:
  // https://w3c.github.io/page-visibility/#hidden-attribute
  if (!GetFrame() || !GetFrame()->GetPage())
    return false;
  // While visibilitychange is being dispatched during unloading it is
  // expected that the visibility is hidden regardless of the page's
  // visibility.
  if (load_event_progress_ >= kUnloadVisibilityChangeInProgress)
    return false;
  return GetFrame()->GetPage()->IsPageVisible();
}

bool Document::IsPrefetchOnly() const {
  if (!GetFrame() || !GetFrame()->GetPage())
    return false;

  NoStatePrefetchClient* no_state_prefetch_client =
      NoStatePrefetchClient::From(GetFrame()->GetPage());
  return no_state_prefetch_client && no_state_prefetch_client->IsPrefetchOnly();
}

AtomicString Document::visibilityState() const {
  return PageHiddenStateString(hidden());
}

bool Document::prerendering() const {
  return IsPrerendering();
}
uint32_t Document::softNavigations() const {
  LocalDOMWindow* window = domWindow();
  if (!window) {
    return 0;
  }
  LocalFrame* frame = window->GetFrame();
  if (!frame || !frame->IsMainFrame()) {
    return 0;
  }
  SoftNavigationHeuristics* heuristics =
      SoftNavigationHeuristics::From(*window);
  DCHECK(heuristics);
  return heuristics->SoftNavigationCount();
}

bool Document::hidden() const {
  return !IsPageVisible();
}

bool Document::wasDiscarded() const {
  return was_discarded_;
}

void Document::SetWasDiscarded(bool was_discarded) {
  was_discarded_ = was_discarded;
}

void Document::DidChangeVisibilityState() {
  if (load_event_progress_ >= kUnloadVisibilityChangeInProgress) {
    // It's possible to get here even after we've started unloading the document
    // and dispatched the visibilitychange event, e.g. when we're closing a tab,
    // where we would first try to dispatch unload events, and then close the
    // tab and update the visibility state.
    return;
  }
  DispatchEvent(*Event::CreateBubble(event_type_names::kVisibilitychange));
  // Also send out the deprecated version until it can be removed.
  DispatchEvent(
      *Event::CreateBubble(event_type_names::kWebkitvisibilitychange));

  if (IsPageVisible())
    GetDocumentAnimations().MarkAnimationsCompositorPending();

  if (hidden() && canvas_font_cache_)
    canvas_font_cache_->PruneAll();

  InteractiveDetector* interactive_detector = InteractiveDetector::From(*this);
  if (interactive_detector) {
    interactive_detector->OnPageHiddenChanged(hidden());
  }

  // Don't create a |ukm_recorder_| and |ukm_source_id_| unless necessary.
  if (hidden() && IdentifiabilityStudySettings::Get()->IsActive()) {
    // Flush UKM data here in addition to Document::Shutdown(). We want to flush
    // the UKM data before this document becomes invisible (e.g. before entering
    // back/forward cache) because we want to send the UKM data before the
    // renderer process is killed.
    IdentifiabilitySampleCollector::Get()->FlushSource(UkmRecorder(),
                                                       UkmSourceID());
  }
}

String Document::nodeName() const {
  return "#document";
}

Node::NodeType Document::getNodeType() const {
  return kDocumentNode;
}

FormController& Document::GetFormController() {
  if (!form_controller_) {
    form_controller_ = MakeGarbageCollected<FormController>(*this);
    HistoryItem* history_item = Loader() ? Loader()->GetHistoryItem() : nullptr;
    if (history_item)
      history_item->SetDocumentState(form_controller_->ControlStates());
  }
  return *form_controller_;
}

DocumentState* Document::GetDocumentState() const {
  if (!form_controller_)
    return nullptr;
  return form_controller_->ControlStates();
}

void Document::SetStateForNewControls(const Vector<String>& state_vector) {
  if (!state_vector.size() && !form_controller_)
    return;
  GetFormController().SetStateForNewControls(state_vector);
}

LocalFrameView* Document::View() const {
  return GetFrame() ? GetFrame()->View() : nullptr;
}

LocalFrame* Document::GetFrame() const {
  return dom_window_ ? dom_window_->GetFrame() : nullptr;
}

Page* Document::GetPage() const {
  return GetFrame() ? GetFrame()->GetPage() : nullptr;
}

Settings* Document::GetSettings() const {
  return GetFrame() ? GetFrame()->GetSettings() : nullptr;
}

Range* Document::createRange() {
  return Range::Create(*this);
}

NodeIterator* Document::createNodeIterator(Node* root,
                                           unsigned what_to_show,
                                           V8NodeFilter* filter) {
  DCHECK(root);
  return MakeGarbageCollected<NodeIterator>(root, what_to_show, filter);
}

TreeWalker* Document::createTreeWalker(Node* root,
                                       unsigned what_to_show,
                                       V8NodeFilter* filter) {
  DCHECK(root);
  return MakeGarbageCollected<TreeWalker>(root, what_to_show, filter);
}

Document::StyleAndLayoutTreeUpdate Document::CalculateStyleAndLayoutTreeUpdate()
    const {
  Document::StyleAndLayoutTreeUpdate local =
      CalculateStyleAndLayoutTreeUpdateForThisDocument();
  if (local == StyleAndLayoutTreeUpdate::kFull)
    return local;
  Document::StyleAndLayoutTreeUpdate parent =
      CalculateStyleAndLayoutTreeUpdateForParentFrame();
  if (parent != StyleAndLayoutTreeUpdate::kNone)
    return StyleAndLayoutTreeUpdate::kFull;
  return local;
}

Document::StyleAndLayoutTreeUpdate
Document::CalculateStyleAndLayoutTreeUpdateForThisDocument() const {
  if (!IsActive() || !View())
    return StyleAndLayoutTreeUpdate::kNone;

  if (style_engine_->NeedsFullStyleUpdate())
    return StyleAndLayoutTreeUpdate::kFull;
  if (!use_elements_needing_update_.IsEmpty())
    return StyleAndLayoutTreeUpdate::kFull;
  // We have scheduled an invalidation set on the document node which means any
  // element may need a style recalc.
  if (NeedsStyleInvalidation())
    return StyleAndLayoutTreeUpdate::kFull;
  if (IsSlotAssignmentDirty())
    return StyleAndLayoutTreeUpdate::kFull;
  if (document_animations_->NeedsAnimationTimingUpdate())
    return StyleAndLayoutTreeUpdate::kFull;

  if (style_engine_->NeedsStyleRecalc())
    return StyleAndLayoutTreeUpdate::kAnalyzed;
  if (style_engine_->NeedsStyleInvalidation())
    return StyleAndLayoutTreeUpdate::kAnalyzed;
  if (style_engine_->NeedsLayoutTreeRebuild()) {
    // TODO(futhark): there a couple of places where call back into the top
    // frame while recursively doing a lifecycle update. One of them are for the
    // RootScrollerController. These should probably be post layout tasks and
    // make this test unnecessary since the layout tree rebuild dirtiness is
    // internal to StyleEngine::UpdateStyleAndLayoutTree().
    DCHECK(InStyleRecalc());
    return StyleAndLayoutTreeUpdate::kAnalyzed;
  }

  return StyleAndLayoutTreeUpdate::kNone;
}

Document::StyleAndLayoutTreeUpdate
Document::CalculateStyleAndLayoutTreeUpdateForParentFrame() const {
  if (HTMLFrameOwnerElement* owner = LocalOwner())
    return owner->GetDocument().CalculateStyleAndLayoutTreeUpdate();
  return StyleAndLayoutTreeUpdate::kNone;
}

bool Document::ShouldScheduleLayoutTreeUpdate() const {
  if (!IsActive())
    return false;
  if (InStyleRecalc())
    return false;
  if (lifecycle_.GetState() == DocumentLifecycle::kInPerformLayout)
    return false;
  if (!ShouldScheduleLayout())
    return false;
  return true;
}

void Document::ScheduleLayoutTreeUpdate() {
  DCHECK(!HasPendingVisualUpdate());
  DCHECK(ShouldScheduleLayoutTreeUpdate());
  DCHECK(NeedsLayoutTreeUpdate());

  if (!View()->CanThrottleRendering())
    GetPage()->Animator().ScheduleVisualUpdate(GetFrame());

  // FrameSelection caches visual selection information, which must be
  // invalidated on dirty layout tree.
  GetFrame()->Selection().MarkCacheDirty();

  lifecycle_.EnsureStateAtMost(DocumentLifecycle::kVisualUpdatePending);

  DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT_WITH_CATEGORIES(
      TRACE_DISABLED_BY_DEFAULT("devtools.timeline"),
      "ScheduleStyleRecalculation", inspector_recalculate_styles_event::Data,
      GetFrame());
  ++style_version_;
}

bool Document::HasPendingForcedStyleRecalc() const {
  return HasPendingVisualUpdate() && !InStyleRecalc() &&
         GetStyleChangeType() == kSubtreeStyleChange;
}

void Document::UpdateStyleInvalidationIfNeeded() {
  DCHECK(IsActive());
  ScriptForbiddenScope forbid_script;

  if (!GetStyleEngine().NeedsStyleInvalidation())
    return;
  TRACE_EVENT0("blink", "Document::updateStyleInvalidationIfNeeded");
  SCOPED_BLINK_UMA_HISTOGRAM_TIMER_HIGHRES("Style.InvalidationTime");
  GetStyleEngine().InvalidateStyle();
}

#if DCHECK_IS_ON()
static void AssertNodeClean(const Node& node) {
  DCHECK(!node.NeedsStyleRecalc());
  DCHECK(!node.ChildNeedsStyleRecalc());
  DCHECK(!node.NeedsReattachLayoutTree());
  DCHECK(!node.ChildNeedsReattachLayoutTree());
  DCHECK(!node.NeedsStyleInvalidation());
  DCHECK(!node.ChildNeedsStyleInvalidation());
  DCHECK(!node.GetForceReattachLayoutTree());
  DCHECK(!node.NeedsLayoutSubtreeUpdate());
}

static void AssertLayoutTreeUpdatedForPseudoElements(const Element& element) {
  WTF::Vector<PseudoId> pseudo_ids = {kPseudoIdFirstLetter, kPseudoIdBefore,
                                      kPseudoIdAfter, kPseudoIdMarker,
                                      kPseudoIdBackdrop};
  for (auto pseudo_id : pseudo_ids) {
    if (auto* pseudo_element = element.GetPseudoElement(pseudo_id))
      AssertNodeClean(*pseudo_element);
  }
}

static void AssertLayoutTreeUpdated(Node& root,
                                    bool allow_dirty_container_subtrees) {
  Node* node = &root;
  while (node) {
    if (auto* element = DynamicTo<Element>(node)) {
      if (element->ChildStyleRecalcBlockedByDisplayLock() ||
          (allow_dirty_container_subtrees && element->GetLayoutObject() &&
           element->GetLayoutObject()->StyleRef().CanMatchSizeContainerQueries(
               *element))) {
        node = FlatTreeTraversal::NextSkippingChildren(*node);
        continue;
      }
      // Check pseudo elements.
      AssertLayoutTreeUpdatedForPseudoElements(*element);
    }

    AssertNodeClean(*node);

    // Make sure there is no node which has a LayoutObject, but doesn't have a
    // parent in a flat tree. If there is such a node, we forgot to detach the
    // node. DocumentNode is only an exception.
    DCHECK((node->IsDocumentNode() || !node->GetLayoutObject() ||
            FlatTreeTraversal::Parent(*node)))
        << *node;

    node = FlatTreeTraversal::Next(*node);
  }
}

#endif

#if EXPENSIVE_DCHECKS_ARE_ON()
void Document::AssertLayoutTreeUpdatedAfterLayout() {
  AssertLayoutTreeUpdated(*this, false /* allow_dirty_container_subtrees */);
  DCHECK(!GetStyleEngine().SkippedContainerRecalc());
}
#endif

void Document::UpdateStyleAndLayoutTree() {
  DocumentLayoutUpgrade upgrade(*this);
  UpdateStyleAndLayoutTree(upgrade);
}

void Document::UpdateStyleAndLayoutTree(LayoutUpgrade& upgrade) {
  DCHECK(IsMainThread());
  DCHECK(ThreadState::Current()->IsAllocationAllowed());
  if (!IsActive() || !View() || View()->ShouldThrottleRendering() ||
      Lifecycle().LifecyclePostponed()) {
    return;
  }

  HTMLFrameOwnerElement::PluginDisposeSuspendScope suspend_plugin_dispose;
  ScriptForbiddenScope forbid_script;

  if (HTMLFrameOwnerElement* owner = LocalOwner()) {
    ParentLayoutUpgrade parent_upgrade(*this, *owner);
    owner->GetDocument().UpdateStyleAndLayoutTree(parent_upgrade);
  }

  PostStyleUpdateScope post_style_update_scope(*this);

  // This call has to happen even if UpdateStyleAndLayout below will be called.
  // This is because the subsequent call to ShouldUpgrade may depend on the
  // results produced by UpdateStyleAndLayoutTreeForThisDocument.
  UpdateStyleAndLayoutTreeForThisDocument();

  if (upgrade.ShouldUpgrade()) {
    GetDisplayLockDocumentState().EnsureMinimumForcedPhase(
        DisplayLockContext::ForcedPhase::kLayout);

    // TODO(crbug.com/1145970): Provide a better reason.
    UpdateStyleAndLayout(DocumentUpdateReason::kUnknown);
  }

  // If the above call to UpdateStyleAndLayoutTreeForThisDocument caused us to
  // skip style recalc for some node, we should have upgraded [1] and performed
  // layout to clear that flag again.
  //
  // [1] LayoutUpgrade::ShouldUpgrade
  DCHECK(!GetStyleEngine().SkippedContainerRecalc());
}

void Document::UpdateStyleAndLayoutTreeForThisDocument() {
  DCHECK(IsMainThread());
  DCHECK(ThreadState::Current()->IsAllocationAllowed());
  if (!IsActive() || !View() || View()->ShouldThrottleRendering() ||
      Lifecycle().LifecyclePostponed()) {
    return;
  }

#if EXPENSIVE_DCHECKS_ARE_ON()
  if (HTMLFrameOwnerElement* owner = LocalOwner()) {
    DCHECK(!owner->GetDocument()
                .GetSlotAssignmentEngine()
                .HasPendingSlotAssignmentRecalc());
    DCHECK(!owner->GetDocument().NeedsLayoutTreeUpdate());
    AssertLayoutTreeUpdated(owner->GetDocument(),
                            false /* allow_dirty_container_subtrees */);
  }
#endif  // EXPENSIVE_DCHECKS_ARE_ON()

  auto advance_to_style_clean = [this]() {
    if (Lifecycle().GetState() < DocumentLifecycle::kStyleClean) {
      // NeedsLayoutTreeUpdateForThisDocument may change to false without any
      // actual layout tree update.  For example, NeedsAnimationTimingUpdate
      // may change to false when time elapses.  Advance lifecycle to
      // StyleClean because style is actually clean now.
      Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
      Lifecycle().AdvanceTo(DocumentLifecycle::kStyleClean);
    }
    // If we insert <object> elements into display:none subtrees, we might not
    // need a layout tree update, but need to make sure they are not blocking
    // the load event.
    UnblockLoadEventAfterLayoutTreeUpdate();
  };

  bool needs_slot_assignment = IsSlotAssignmentDirty();
  bool needs_layout_tree_update = false;

  if (!needs_slot_assignment) {
    needs_layout_tree_update = NeedsLayoutTreeUpdateForThisDocument();
    if (!needs_layout_tree_update) {
      // Early out for no-op calls before the UMA/UKM measurement is set up to
      // avoid a large number of close-to-zero samples.
      advance_to_style_clean();
      return;
    }
  }

  SCOPED_UMA_AND_UKM_TIMER(View()->EnsureUkmAggregator(),
                           LocalFrameUkmAggregator::kStyle);
  FontPerformance::StyleScope font_performance_scope;
  ENTER_EMBEDDER_STATE(V8PerIsolateData::MainThreadIsolate(), GetFrame(),
                       BlinkState::STYLE);

  if (needs_slot_assignment) {
    // RecalcSlotAssignments should be done before checking
    // NeedsLayoutTreeUpdateForThisDocument().
    GetSlotAssignmentEngine().RecalcSlotAssignments();
    DCHECK(!needs_layout_tree_update) << "Should be postponed above";
    needs_layout_tree_update = NeedsLayoutTreeUpdateForThisDocument();
  }

  if (!needs_layout_tree_update) {
    advance_to_style_clean();
    return;
  }

  // We can call FlatTreeTraversal::AssertFlatTreeNodeDataUpdated just after
  // calling RecalcSlotAssignments(), however, it would be better to call it at
  // least after InStyleRecalc() check below in order to avoid superfluous
  // check, which would be the cause of web tests timeout when dcheck is on.

  SlotAssignmentRecalcForbiddenScope forbid_slot_recalc(*this);

  if (InStyleRecalc()) {
    NOTREACHED() << "We should not re-enter style recalc for the same document";
    return;
  }

#if DCHECK_IS_ON()
  int assigned_nodes_in_slot_count = 0;
  int nodes_which_have_assigned_slot_count = 0;
  FlatTreeTraversal::AssertFlatTreeNodeDataUpdated(
      *this, assigned_nodes_in_slot_count,
      nodes_which_have_assigned_slot_count);
  DCHECK_EQ(assigned_nodes_in_slot_count, nodes_which_have_assigned_slot_count);
#endif

  // Entering here from inside layout, paint etc. would be catastrophic since
  // recalcStyle can tear down the layout tree or (unfortunately) run
  // script. Kill the whole layoutObject if someone managed to get into here in
  // states not allowing tree mutations.
  CHECK(Lifecycle().StateAllowsTreeMutations());

  TRACE_EVENT_BEGIN1("blink,devtools.timeline", "UpdateLayoutTree", "beginData",
                     [&](perfetto::TracedValue context) {
                       inspector_recalculate_styles_event::Data(
                           std::move(context), GetFrame());
                     });

  unsigned start_element_count = GetStyleEngine().StyleForElementCount();

  probe::RecalculateStyle recalculate_style_scope(this);

  document_animations_->UpdateAnimationTimingIfNeeded();
  EvaluateMediaQueryListIfNeeded();
  UpdateUseShadowTreesIfNeeded();

  GetStyleEngine().UpdateActiveStyle();
  GetStyleEngine().UpdateCounterStyles();
  GetStyleEngine().InvalidateViewportUnitStylesIfNeeded();
  InvalidateStyleAndLayoutForFontUpdates();
  UpdateStyleInvalidationIfNeeded();
  UpdateStyle();
  if (GetStyleResolver().WasViewportResized()) {
    GetStyleResolver().ClearResizedForViewportUnits();
    View()->MarkOrthogonalWritingModeRootsForLayout();
  }

  GetLayoutView()->ClearHitTestCache();

  DCHECK(!document_animations_->NeedsAnimationTimingUpdate());

  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_element_count;

  // Make sure that document.fonts.ready fires, if appropriate.
  FontFaceSetDocument::DidLayout(*this);

  UnblockLoadEventAfterLayoutTreeUpdate();

  TRACE_EVENT_END1("blink,devtools.timeline", "UpdateLayoutTree",
                   "elementCount", element_count);

  ElementRuleCollector::DumpAndClearRulesPerfMap();

#if DCHECK_IS_ON()
  AssertLayoutTreeUpdated(*this, true /* allow_dirty_container_subtrees */);
#endif
}

void Document::InvalidateStyleAndLayoutForFontUpdates() {
  DCHECK(IsActive());
  DCHECK(IsMainThread());
  GetStyleEngine().InvalidateStyleAndLayoutForFontUpdates();
}

void Document::UpdateStyle() {
  DCHECK(!View()->ShouldThrottleRendering());
  TRACE_EVENT_BEGIN0("blink,blink_style", "Document::updateStyle");
  RUNTIME_CALL_TIMER_SCOPE(V8PerIsolateData::MainThreadIsolate(),
                           RuntimeCallStats::CounterId::kUpdateStyle);

  unsigned initial_element_count = GetStyleEngine().StyleForElementCount();

  lifecycle_.AdvanceTo(DocumentLifecycle::kInStyleRecalc);

  // SetNeedsStyleRecalc should only happen on Element and Text nodes.
  DCHECK(!NeedsStyleRecalc());

  bool should_record_stats;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("blink,blink_style", &should_record_stats);
  GetStyleEngine().SetStatsEnabled(should_record_stats);

  GetStyleEngine().UpdateStyleAndLayoutTree();

  GetLayoutView()->UpdateMarkersAndCountersAfterStyleChange();
  GetLayoutView()->RecalcLayoutOverflow();

#if DCHECK_IS_ON()
  AssertNodeClean(*this);
#endif
  DCHECK(InStyleRecalc());
  lifecycle_.AdvanceTo(DocumentLifecycle::kStyleClean);
  if (should_record_stats) {
    TRACE_EVENT_END2(
        "blink,blink_style", "Document::updateStyle", "resolverAccessCount",
        GetStyleEngine().StyleForElementCount() - initial_element_count,
        "counters", GetStyleEngine().Stats()->ToTracedValue());
  } else {
    TRACE_EVENT_END1(
        "blink,blink_style", "Document::updateStyle", "resolverAccessCount",
        GetStyleEngine().StyleForElementCount() - initial_element_count);
  }
}

bool Document::NeedsLayoutTreeUpdateForNode(const Node& node) const {
  // TODO(rakina): Switch some callers that may need to call
  // NeedsLayoutTreeUpdateForNodeIncludingDisplayLocked instead of this.
  if (DisplayLockUtilities::LockedAncestorPreventingStyle(node)) {
    // |node| is in a locked-subtree, so we don't need to update it.
    return false;
  }
  return NeedsLayoutTreeUpdateForNodeIncludingDisplayLocked(node);
}

bool Document::NeedsLayoutTreeUpdateForNodeIncludingDisplayLocked(
    const Node& node) const {
  if (!node.isConnected())
    return false;
  if (node.IsShadowRoot())
    return false;
  const StyleAndLayoutTreeUpdate update = CalculateStyleAndLayoutTreeUpdate();
  if (update == StyleAndLayoutTreeUpdate::kFull)
    return true;
  bool analyze = update == StyleAndLayoutTreeUpdate::kAnalyzed;

  // If DisplayLockUtilities::IsUnlockedQuickCheck returns 'false', then
  // we may or may not be unlocked: we have to traverse the ancestor chain
  // to know for sure.
  if (!analyze)
    analyze = !DisplayLockUtilities::IsUnlockedQuickCheck(node);

  bool maybe_affected_by_layout =
      GetStyleEngine().StyleMaybeAffectedByLayout(node);
  // Even if we don't need layout *now*, any dirty style may invalidate layout.
  bool maybe_needs_layout =
      (update != StyleAndLayoutTreeUpdate::kNone) || View()->NeedsLayout();
  bool needs_update_inside_interleaving_root =
      maybe_affected_by_layout && maybe_needs_layout;

  if (!analyze)
    analyze = needs_update_inside_interleaving_root;

  if (!analyze) {
    DCHECK_EQ(StyleAndLayoutTreeUpdate::kNone, update);
    return false;
  }

  switch (GetStyleEngine().AnalyzeAncestors(node)) {
    case StyleEngine::AncestorAnalysis::kNone:
      return false;
    case StyleEngine::AncestorAnalysis::kInterleavingRoot:
      return needs_update_inside_interleaving_root;
    case StyleEngine::AncestorAnalysis::kStyleRoot:
      return true;
  }
}

void Document::UpdateStyleAndLayoutTreeForNode(const Node* node) {
  DCHECK(node);
  if (!node->InActiveDocument()) {
    // If |node| is not in the active document, we can't update its style or
    // layout tree.
    DCHECK_EQ(node->ownerDocument(), this);
    return;
  }
  DCHECK(!InStyleRecalc())
      << "UpdateStyleAndLayoutTreeForNode called from within style recalc";
  if (!NeedsLayoutTreeUpdateForNodeIncludingDisplayLocked(*node))
    return;

  DisplayLockUtilities::ScopedForcedUpdate scoped_update_forced(
      node, DisplayLockContext::ForcedPhase::kStyleAndLayoutTree);
  NodeLayoutUpgrade upgrade(*node);
  UpdateStyleAndLayoutTree(upgrade);
}

void Document::UpdateStyleAndLayoutTreeForSubtree(const Node* node) {
  DCHECK(node);
  if (!node->InActiveDocument()) {
    DCHECK_EQ(node->ownerDocument(), this);
    return;
  }
  DCHECK(!InStyleRecalc())
      << "UpdateStyleAndLayoutTreeForSubtree called from within style recalc";

  if (NeedsLayoutTreeUpdateForNodeIncludingDisplayLocked(*node) ||
      node->ChildNeedsStyleRecalc() || node->ChildNeedsStyleInvalidation()) {
    DisplayLockUtilities::ScopedForcedUpdate scoped_update_forced(
        node, DisplayLockContext::ForcedPhase::kStyleAndLayoutTree);
    UpdateStyleAndLayoutTree();
  }
}

void Document::UpdateStyleAndLayoutForRange(const Range* range,
                                            DocumentUpdateReason reason) {
  DisplayLockUtilities::ScopedForcedUpdate scoped_update_forced(
      range, DisplayLockContext::ForcedPhase::kLayout);
  UpdateStyleAndLayout(reason);
}

void Document::UpdateStyleAndLayoutForNode(const Node* node,
                                           DocumentUpdateReason reason) {
  DCHECK(node);
  if (!node->InActiveDocument())
    return;

  DisplayLockUtilities::ScopedForcedUpdate scoped_update_forced(
      node, DisplayLockContext::ForcedPhase::kLayout);
  UpdateStyleAndLayout(reason);
}

void Document::AddToRecalcStyleForToggle(Element* element) {
  elements_needing_style_recalc_for_toggle_.insert(element);
}

bool Document::SetNeedsStyleRecalcForToggles() {
  // TODO(crbug.com/1250716): We currently call this from
  // LocalFrameView::RunCSSToggleSteps().  This is not ideal, but it produces
  // behavior that's basically what we want, except for making
  // getComputedStyle() produce correct results, which is hopefully fixable
  // with future changes to PostStyleUpdateScope).  The behavior is also not
  // yet well-defined; see https://github.com/tabatkins/css-toggle/issues/27
  // for making this better.

  if (elements_needing_style_recalc_for_toggle_.size() == 0)
    return false;

  HeapHashSet<Member<Element>> elements;
  std::swap(elements_needing_style_recalc_for_toggle_, elements);

  const auto& reason = StyleChangeReasonForTracing::CreateWithExtraData(
      style_change_reason::kPseudoClass, style_change_extra_data::g_toggle);

  for (auto element : elements) {
    element->SetNeedsStyleRecalc(StyleChangeType::kSubtreeStyleChange, reason);
  }

  return true;
}

void Document::ApplyScrollRestorationLogic() {
  DCHECK(View());
  // This function in not re-entrant. However, the places that invoke this are
  // re-entrant. Specifically, UpdateStyleAndLayout() calls this, which in turn
  // can do a find-in-page for the scroll-to-text feature, which can cause
  // UpdateStyleAndLayout to happen with content-visibility, which gets back
  // here and recurses indefinitely. As a result, we ensure to early out from
  // this function if are currently in process of restoring scroll.
  if (applying_scroll_restoration_logic_)
    return;
  base::AutoReset<bool> applying_scroll_restoration_logic_scope(
      &applying_scroll_restoration_logic_, true);

  // If we're restoring a scroll position from history, that takes precedence
  // over scrolling to the anchor in the URL.
  View()->InvokeFragmentAnchor();

  auto& frame_loader = GetFrame()->Loader();
  auto* document_loader = frame_loader.GetDocumentLoader();
  if (!document_loader)
    return;
  if (GetFrame()->IsLoading() &&
      !FrameLoader::NeedsHistoryItemRestore(document_loader->LoadType()))
    return;

  HistoryItem* history_item = document_loader->GetHistoryItem();

  if (!history_item || !history_item->GetViewState())
    return;

  if (!View()->GetScrollableArea()->HasPendingHistoryRestoreScrollOffset())
    return;

  bool should_restore_scroll = history_item->ScrollRestorationType() !=
                               mojom::blink::ScrollRestorationType::kManual;
  auto& scroll_offset = history_item->GetViewState()->scroll_offset_;

  // This tries to balance:
  // 1. restoring as soon as possible.
  // 2. not overriding user scroll (TODO(majidvp): also respect user scale).
  // 3. detecting clamping to avoid repeatedly popping the scroll position
  // down
  //    as the page height increases.
  // 4. ignoring clamp detection if scroll state is not being restored, if
  // load
  //    is complete, or if the navigation is same-document (as the new page
  //    may be smaller than the previous page).
  bool can_restore_without_clamping =
      View()->LayoutViewport()->ClampScrollOffset(scroll_offset) ==
      scroll_offset;

  bool can_restore_without_annoying_user =
      !document_loader->GetInitialScrollState().was_scrolled_by_user &&
      (can_restore_without_clamping || !GetFrame()->IsLoading() ||
       !should_restore_scroll);
  if (!can_restore_without_annoying_user)
    return;

  // Apply scroll restoration to the LayoutView's scroller. Note that we do
  // *not* apply it to the RootFrameViewport's LayoutViewport, because that
  // may be for child frame's implicit root scroller, which is not the right
  // one to apply to because scroll restoration does not affect implicit root
  // scrollers.
  auto* layout_scroller = View()->LayoutViewport();
  layout_scroller->ApplyPendingHistoryRestoreScrollOffset();

  // Also apply restoration to the visual viewport of the root frame, if needed.
  auto* root_frame_scroller = View()->GetScrollableArea();
  if (root_frame_scroller != layout_scroller)
    root_frame_scroller->ApplyPendingHistoryRestoreScrollOffset();

  document_loader->GetInitialScrollState().did_restore_from_history = true;
}

void Document::MarkHasFindInPageRequest() {
  // Only record the event once in a document.
  if (had_find_in_page_request_)
    return;

  auto* recorder = UkmRecorder();
  DCHECK(recorder);
  DCHECK(UkmSourceID() != ukm::kInvalidSourceId);
  ukm::builders::Blink_FindInPage(UkmSourceID())
      .SetDidSearch(true)
      .Record(recorder);
  had_find_in_page_request_ = true;
}

void Document::MarkHasFindInPageContentVisibilityActiveMatch() {
  // Only record the event once in a document.
  if (had_find_in_page_render_subtree_active_match_)
    return;

  auto* recorder = UkmRecorder();
  DCHECK(recorder);
  DCHECK(UkmSourceID() != ukm::kInvalidSourceId);
  // TODO(vmpstr): Rename UKM values if possible.
  ukm::builders::Blink_FindInPage(UkmSourceID())
      .SetDidHaveRenderSubtreeMatch(true)
      .Record(recorder);
  had_find_in_page_render_subtree_active_match_ = true;
}

void Document::MarkHasFindInPageBeforematchExpandedHiddenMatchable() {
  // Only record the event once in a document.
  if (had_find_in_page_beforematch_expanded_hidden_matchable_)
    return;

  auto* recorder = UkmRecorder();
  DCHECK(recorder);
  DCHECK(UkmSourceID() != ukm::kInvalidSourceId);
  ukm::builders::Blink_FindInPage(UkmSourceID())
      .SetBeforematchExpandedHiddenMatchable(true)
      .Record(recorder);
  had_find_in_page_beforematch_expanded_hidden_matchable_ = true;
}

void Document::UpdateStyleAndLayout(DocumentUpdateReason reason) {
  DCHECK(IsMainThread());
  LocalFrameView* frame_view = View();

  if (reason != DocumentUpdateReason::kBeginMainFrame && frame_view)
    frame_view->WillStartForcedLayout();

  HTMLFrameOwnerElement::PluginDisposeSuspendScope suspend_plugin_dispose;
  ScriptForbiddenScope forbid_script;

  DCHECK(!frame_view || !frame_view->IsInPerformLayout())
      << "View layout should not be re-entrant";

  if (HTMLFrameOwnerElement* owner = LocalOwner()) {
    owner->GetDocument().UpdateStyleAndLayout(reason);
  }

  if (!IsActive()) {
    if (reason != DocumentUpdateReason::kBeginMainFrame && frame_view)
      frame_view->DidFinishForcedLayout(reason);
    return;
  }

  if (frame_view)
    frame_view->UpdateStyleAndLayout();

  if (Lifecycle().GetState() < DocumentLifecycle::kLayoutClean)
    Lifecycle().AdvanceTo(DocumentLifecycle::kLayoutClean);

  if (frame_view)
    ApplyScrollRestorationLogic();

  if (LocalFrameView* frame_view_anchored = View())
    frame_view_anchored->PerformScrollAnchoringAdjustments();
  PerformScrollSnappingTasks();

  if (reason != DocumentUpdateReason::kBeginMainFrame && frame_view)
    frame_view->DidFinishForcedLayout(reason);

  if (should_update_selection_after_layout_)
    UpdateSelectionAfterLayout();
}

void Document::LayoutUpdated() {
  DCHECK(GetFrame());
  DCHECK(View());

  // Plugins can run script inside layout which can detach the page.
  // TODO(dcheng): Does it make sense to do any of this work if detached?
  if (auto* frame = GetFrame()) {
    if (frame->IsMainFrame())
      frame->GetPage()->GetChromeClient().MainFrameLayoutUpdated();

    // We do attach here, during lifecycle update, because until then we
    // don't have a good place that has access to its local root's FrameWidget.
    // TODO(dcheng): If we create FrameWidget before Frame then we could move
    // this to Document::Initialize().
    AttachCompositorTimeline(Timeline().CompositorTimeline());

    frame->Client()->DidObserveLayoutNg(
        layout_blocks_counter_, layout_blocks_counter_ng_,
        layout_calls_counter_, layout_calls_counter_ng_);
    layout_blocks_counter_ = layout_blocks_counter_ng_ = layout_calls_counter_ =
        layout_calls_counter_ng_ = 0;
  }

  Markers().InvalidateRectsForAllTextMatchMarkers();
}

void Document::AttachCompositorTimeline(cc::AnimationTimeline* timeline) const {
  if (!Platform::Current()->IsThreadedAnimationEnabled() ||
      !GetSettings()->GetAcceleratedCompositingEnabled())
    return;

  if (timeline->IsScrollTimeline() && timeline->animation_host())
    return;

  if (cc::AnimationHost* host =
          GetPage()->GetChromeClient().GetCompositorAnimationHost(
              *GetFrame())) {
    host->AddAnimationTimeline(timeline);
  }
}

void Document::ClearFocusedElementIfNeeded() {
  if (!clear_focused_element_timer_.IsActive() && focused_element_ &&
      !focused_element_->IsFocusable()) {
    clear_focused_element_timer_.StartOneShot(base::TimeDelta(), FROM_HERE);
  }
}

void Document::ClearFocusedElementTimerFired(TimerBase*) {
  UpdateStyleAndLayoutTree();

  if (focused_element_ && !focused_element_->IsFocusable())
    focused_element_->blur();
}

scoped_refptr<const ComputedStyle> Document::StyleForPage(uint32_t page_index) {
  AtomicString page_name;
  if (const LayoutView* layout_view = GetLayoutView())
    page_name = layout_view->NamedPageAtIndex(page_index);
  GetStyleEngine().UpdateActiveStyle();
  return GetStyleEngine().GetStyleResolver().StyleForPage(page_index,
                                                          page_name);
}

void Document::EnsurePaintLocationDataValidForNode(
    const Node* node,
    DocumentUpdateReason reason) {
  DCHECK(node);
  if (!node->InActiveDocument())
    return;

  if (reason == DocumentUpdateReason::kJavaScript) {
    DeferredShapingController::From(*this)->ReshapeDeferred(
        ReshapeReason::kGeometryApi, *node);
  }

  DisplayLockUtilities::ScopedForcedUpdate scoped_update_forced(
      node, DisplayLockContext::ForcedPhase::kLayout);

  // For all nodes we must have up-to-date style and have performed layout to do
  // any location-based calculation.
  UpdateStyleAndLayout(reason);
}

void Document::EnsurePaintLocationDataValidForNode(const Node* node,
                                                   DocumentUpdateReason reason,
                                                   CSSPropertyID property_id) {
  DCHECK(node);
  if (!node->InActiveDocument())
    return;

  if (RuntimeEnabledFeatures::DeferredShapingEnabled()) {
    auto* ds_controller = DeferredShapingController::From(*this);
    if (property_id == CSSPropertyID::kWidth)
      ds_controller->ReshapeDeferredForWidth(*node->GetLayoutObject());
    else
      ds_controller->ReshapeDeferredForHeight(*node->GetLayoutObject());
  }

  DisplayLockUtilities::ScopedForcedUpdate scoped_update_forced(
      node, DisplayLockContext::ForcedPhase::kLayout);

  // For all nodes we must have up-to-date style and have performed layout to do
  // any location-based calculation.
  UpdateStyleAndLayout(reason);
}

bool Document::IsPageBoxVisible(uint32_t page_index) {
  return StyleForPage(page_index)->Visibility() !=
         EVisibility::kHidden;  // display property doesn't apply to @page.
}

void Document::GetPageDescription(uint32_t page_index,
                                  WebPrintPageDescription* description) {
  scoped_refptr<const ComputedStyle> style = StyleForPage(page_index);

  switch (style->GetPageSizeType()) {
    case PageSizeType::kAuto:
      break;
    case PageSizeType::kLandscape:
      if (description->size.width() < description->size.height())
        description->size.Transpose();
      break;
    case PageSizeType::kPortrait:
      if (description->size.width() > description->size.height())
        description->size.Transpose();
      break;
    case PageSizeType::kFixed:
      description->size = style->PageSize();
      break;
    default:
      NOTREACHED();
  }

  // The percentage is calculated with respect to the width even for margin top
  // and bottom.
  // http://www.w3.org/TR/CSS2/box.html#margin-properties
  float width = description->size.width();
  if (!style->MarginTop().IsAuto())
    description->margin_top = IntValueForLength(style->MarginTop(), width);
  if (!style->MarginRight().IsAuto())
    description->margin_right = IntValueForLength(style->MarginRight(), width);
  if (!style->MarginBottom().IsAuto()) {
    description->margin_bottom =
        IntValueForLength(style->MarginBottom(), width);
  }
  if (!style->MarginLeft().IsAuto())
    description->margin_left = IntValueForLength(style->MarginLeft(), width);

  description->orientation = style->GetPageOrientation();
}

void Document::SetIsXrOverlay(bool val, Element* overlay_element) {
  if (!documentElement())
    return;

  if (val == is_xr_overlay_)
    return;

  is_xr_overlay_ = val;

  // On navigation, the layout view may be invalid, skip style changes.
  if (!GetLayoutView())
    return;

  if (val) {
    // The UA style sheet for the :xr-overlay pseudoclass uses lazy loading.
    // If we get here, we need to ensure that it's present.
    GetStyleEngine().EnsureUAStyleForXrOverlay();
  }

  if (overlay_element) {
    // Now that the custom style sheet is loaded, update the pseudostyle for
    // the overlay element.
    overlay_element->PseudoStateChanged(CSSSelector::kPseudoXrOverlay);
  }

  // The DOM overlay may change the effective root element. Need to update
  // compositing inputs to avoid a mismatch in CompositingRequirementsUpdater.
  GetLayoutView()->Layer()->SetNeedsCompositingInputsUpdate();
}

void Document::ScheduleUseShadowTreeUpdate(SVGUseElement& element) {
  use_elements_needing_update_.insert(&element);
  ScheduleLayoutTreeUpdateIfNeeded();
}

void Document::UnscheduleUseShadowTreeUpdate(SVGUseElement& element) {
  use_elements_needing_update_.erase(&element);
}

void Document::UpdateUseShadowTreesIfNeeded() {
  ScriptForbiddenScope forbid_script;

  // Breadth-first search since nested use elements add to the queue.
  while (!use_elements_needing_update_.IsEmpty()) {
    HeapHashSet<Member<SVGUseElement>> elements;
    use_elements_needing_update_.swap(elements);
    for (SVGUseElement* element : elements)
      element->BuildPendingResource();
  }
}

StyleResolver& Document::GetStyleResolver() const {
  return style_engine_->GetStyleResolver();
}

void Document::Initialize() {
  DCHECK_EQ(lifecycle_.GetState(), DocumentLifecycle::kInactive);
  DCHECK(!ax_object_cache_ || this != &AXObjectCacheOwner());

  UpdateForcedColors();
  scoped_refptr<ComputedStyle> style = GetStyleResolver().StyleForViewport();
  layout_view_ = LayoutObjectFactory::CreateView(*this, *style);
  SetLayoutObject(layout_view_);

  layout_view_->SetStyle(style);

  AttachContext context;
  AttachLayoutTree(context);

  // The TextAutosizer can't update layout view info while the Document is
  // detached, so update now in case anything changed.
  if (TextAutosizer* autosizer = GetTextAutosizer())
    autosizer->UpdatePageInfo();

  GetFrame()->DidAttachDocument();
  lifecycle_.AdvanceTo(DocumentLifecycle::kStyleClean);

  if (View())
    View()->DidAttachDocument();
}

void Document::Shutdown() {
  TRACE_EVENT0("blink", "Document::shutdown");
  CHECK((!GetFrame() || GetFrame()->Tree().ChildCount() == 0) &&
        ConnectedSubframeCount() == 0);
  if (!IsActive())
    return;

  // An active Document must have an associated window.
  CHECK(dom_window_);

  // Frame navigation can cause a new Document to be attached. Don't allow that,
  // since that will cause a situation where LocalFrame still has a Document
  // attached after this finishes!  Normally, it shouldn't actually be possible
  // to trigger navigation here.  However, plugins (see below) can cause lots of
  // crazy things to happen, since plugin detach involves nested run loops.
  FrameNavigationDisabler navigation_disabler(*GetFrame());
  // Defer plugin dispose to avoid plugins trying to run script inside
  // ScriptForbiddenScope, which will crash the renderer after
  // https://crrev.com/200984
  // TODO(dcheng): This is a temporary workaround, Document::Shutdown() should
  // not be running script at all.
  HTMLFrameOwnerElement::PluginDisposeSuspendScope suspend_plugin_dispose;
  // Don't allow script to run in the middle of DetachLayoutTree() because a
  // detaching Document is not in a consistent state.
  ScriptForbiddenScope forbid_script;

  lifecycle_.AdvanceTo(DocumentLifecycle::kStopping);

  // Do not add code before this without a documented reason. A postcondition of
  // Shutdown() is that |dom_window_| must not have an attached Document.
  // Allowing script execution when the Document is shutting down can make it
  // easy to accidentally violate this condition, and the ordering of the
  // scopers above is subtle due to legacy interactions with plugins.

  if (num_canvases_ > 0)
    UMA_HISTOGRAM_COUNTS_100("Blink.Canvas.NumCanvasesPerPage", num_canvases_);

  if (!data_->already_sent_automatic_lazy_load_frame_ukm_) {
    data_->already_sent_automatic_lazy_load_frame_ukm_ = true;
    if (data_->lazy_ads_frame_count_ > 0 ||
        data_->lazy_embeds_frame_count_ > 0) {
      ukm::builders::Blink_AutomaticLazyLoadFrame(UkmSourceID())
          .SetLazyAdsFrameCount(ukm::GetExponentialBucketMinForCounts1000(
              data_->lazy_ads_frame_count_))
          .SetLazyEmbedsFrameCount(ukm::GetExponentialBucketMinForCounts1000(
              data_->lazy_embeds_frame_count_))
          .Record(UkmRecorder());
    }
  }

  GetFontMatchingMetrics()->PublishAllMetrics();

  GetViewportData().Shutdown();

  View()->Dispose();
  DCHECK(!View()->IsAttached());

  // If the EmbeddedContentView of the document's frame owner doesn't match
  // view() then LocalFrameView::Dispose() didn't clear the owner's
  // EmbeddedContentView. If we don't clear it here, it may be clobbered later
  // in LocalFrame::CreateView(). See also https://crbug.com/673170 and the
  // comment in LocalFrameView::Dispose().
  HTMLFrameOwnerElement* owner_element = GetFrame()->DeprecatedLocalOwner();

  // In the case of a provisional frame, skip clearing the EmbeddedContentView.
  // A provisional frame is not fully attached to the DOM yet and clearing the
  // EmbeddedContentView here could clear a not-yet-swapped-out frame
  // (https://crbug.com/807772).
  if (owner_element && !GetFrame()->IsProvisional())
    owner_element->SetEmbeddedContentView(nullptr);

  markers_->PrepareForDestruction();

  if (TextFragmentHandler* handler = GetFrame()->GetTextFragmentHandler())
    handler->DidDetachDocumentOrFrame();

  GetPage()->DocumentDetached(this);

  probe::DocumentDetached(this);

  scripted_idle_task_controller_.Clear();

  if (SvgExtensions())
    AccessSVGExtensions().PauseAnimations();

  CancelPendingJavaScriptUrls();
  http_refresh_scheduler_->Cancel();

  GetDocumentAnimations().DetachCompositorTimelines();

  if (GetFrame()->IsLocalRoot())
    GetPage()->GetChromeClient().AttachRootLayer(nullptr, GetFrame());

  MutationObserver::CleanSlotChangeList(*this);

  hover_element_ = nullptr;
  active_element_ = nullptr;
  autofocus_candidates_.clear();

  if (focused_element_.Get()) {
    Element* old_focused_element = focused_element_;
    focused_element_ = nullptr;
    NotifyFocusedElementChanged(old_focused_element, nullptr,
                                mojom::blink::FocusType::kNone);
  }
  sequential_focus_navigation_starting_point_ = nullptr;
  focused_element_change_observers_.clear();

  if (this == &AXObjectCacheOwner()) {
    ax_contexts_.clear();
    ClearAXObjectCache();
  }
  computed_node_mapping_.clear();

  DetachLayoutTree();
  layout_view_ = nullptr;
  DCHECK(!View()->IsAttached());

  if (this != &AXObjectCacheOwner()) {
    if (AXObjectCache* cache = ExistingAXObjectCache()) {
      // Documents that are not a root document use the AXObjectCache in
      // their root document. Node::removedFrom is called after the
      // document has been detached so it can't find the root document.
      // We do the removals here instead.
      for (Node& node : NodeTraversal::DescendantsOf(*this)) {
        cache->Remove(&node);
      }
    }
  }

  GetStyleEngine().DidDetach();

  GetFrame()->GetEventHandlerRegistry().DocumentDetached(*this);

  // Signal destruction to mutation observers.
  synchronous_mutation_observer_set_.ForEachObserver(
      [](SynchronousMutationObserver* observer) {
        observer->ContextDestroyed();
        observer->ObserverSetWillBeCleared();
      });
  synchronous_mutation_observer_set_.Clear();

  cookie_jar_ = nullptr;  // Not accessible after navigated away.
  fetcher_->ClearContext();

  if (media_query_matcher_)
    media_query_matcher_->DocumentDetached();

  lifecycle_.AdvanceTo(DocumentLifecycle::kStopped);
  DCHECK(!View()->IsAttached());

  // Don't create a |ukm_recorder_| and |ukm_source_id_| unless necessary.
  if (IdentifiabilityStudySettings::Get()->IsActive()) {
    IdentifiabilitySampleCollector::Get()->FlushSource(UkmRecorder(),
                                                       UkmSourceID());
  }

  mime_handler_view_before_unload_event_listener_ = nullptr;

  resource_coordinator_.reset();

  // This is required, as our LocalFrame might delete itself as soon as it
  // detaches us. However, this violates Node::detachLayoutTree() semantics, as
  // it's never possible to re-attach. Eventually Document::detachLayoutTree()
  // should be renamed, or this setting of the frame to 0 could be made
  // explicit in each of the callers of Document::detachLayoutTree().
  dom_window_ = nullptr;
  execution_context_ = nullptr;
}

void Document::RemovedEventListener(
    const AtomicString& event_type,
    const RegisteredEventListener& registered_listener) {
  ContainerNode::RemovedEventListener(event_type, registered_listener);

  // We need to track the existence of the visibilitychange event listeners to
  // enable/disable sudden terminations.
  if (event_type == event_type_names::kVisibilitychange) {
    if (auto* frame = GetFrame())
      frame->RemovedSuddenTerminationDisablerListener(*this, event_type);
  }
}

void Document::RemoveAllEventListeners() {
  int previous_visibility_change_handlers_count =
      NumberOfEventListeners(event_type_names::kVisibilitychange);

  ContainerNode::RemoveAllEventListeners();

  if (LocalDOMWindow* dom_window = domWindow())
    dom_window->RemoveAllEventListeners();

  // Update sudden termination disabler state if we previously have listeners
  // for visibilitychange.
  if (previous_visibility_change_handlers_count) {
    if (auto* frame = GetFrame()) {
      frame->RemovedSuddenTerminationDisablerListener(
          *this, event_type_names::kVisibilitychange);
    }
  }
}

Document& Document::AXObjectCacheOwner() const {
  // Every document has its own axObjectCache if accessibility is enabled,
  // except for page popups, which share the axObjectCache of their owner.
  Document* doc = const_cast<Document*>(this);
  auto* frame = doc->GetFrame();
  if (frame && frame->HasPagePopupOwner()) {
    DCHECK(!doc->ax_object_cache_);
    return frame->PagePopupOwner()->GetDocument().AXObjectCacheOwner();
  }
  return *doc;
}

static ui::AXMode ComputeAXModeFromAXContexts(Vector<AXContext*> ax_contexts) {
  ui::AXMode ax_mode = 0;
  for (AXContext* context : ax_contexts)
    ax_mode |= context->GetAXMode();

  if (!ax_contexts.IsEmpty()) {
    DCHECK(!ax_mode.is_mode_off())
        << "The computed AX mode was empty but there were > 0 AXContext "
           "objects. A caller should have called RemoveAXContext().";
  }

  return ax_mode;
}

void Document::AddAXContext(AXContext* context) {
  // The only case when |&cache_owner| is not |this| is when this is a
  // pop-up. We want pop-ups to share the AXObjectCache of their parent
  // document. However, there's no valid reason to explicitly create an
  // AXContext for a pop-up document, so check to make sure we're not
  // trying to do that here.
  DCHECK_EQ(&AXObjectCacheOwner(), this);

  // If the document has already been detached, do not make a new AXObjectCache.
  if (!GetLayoutView())
    return;

  ax_contexts_.push_back(context);
  if (ax_contexts_.size() != 1) {
    DCHECK(ax_object_cache_);
    ax_object_cache_->SetAXMode(ComputeAXModeFromAXContexts(ax_contexts_));
    return;
  }

  if (!ax_object_cache_) {
    ax_object_cache_ =
        AXObjectCache::Create(*this, ComputeAXModeFromAXContexts(ax_contexts_));
  }
}

void Document::AXContextModeChanged() {
  DCHECK_GT(ax_contexts_.size(), 0u);
  DCHECK(ax_object_cache_);
  ax_object_cache_->SetAXMode(ComputeAXModeFromAXContexts(ax_contexts_));
}

void Document::RemoveAXContext(AXContext* context) {
  auto** iter =
      std::find_if(ax_contexts_.begin(), ax_contexts_.end(),
                   [&context](const auto& item) { return item == context; });
  if (iter != ax_contexts_.end())
    ax_contexts_.erase(iter);
  if (ax_contexts_.size() == 0) {
    ClearAXObjectCache();
  } else {
    DCHECK(ax_object_cache_);
    ax_object_cache_->SetAXMode(ComputeAXModeFromAXContexts(ax_contexts_));
  }
}

void Document::ClearAXObjectCache() {
  DCHECK_EQ(&AXObjectCacheOwner(), this);

  // Clear the cache member variable before calling delete because attempts
  // are made to access it during destruction.
  if (ax_object_cache_)
    ax_object_cache_->Dispose();
  ax_object_cache_.Clear();

  // If there's at least one AXContext in scope and there's still a LayoutView
  // around, recreate an empty AXObjectCache.
  //
  // TODO(dmazzoni): right now ClearAXObjectCache() is being used as a way
  // to invalidate / reset the AXObjectCache while keeping it around. We
  // should rewrite that as a method on AXObjectCache rather than destroying
  // and recreating it here.
  if (ax_contexts_.size() > 0 && GetLayoutView()) {
    ax_object_cache_ =
        AXObjectCache::Create(*this, ComputeAXModeFromAXContexts(ax_contexts_));
  }
}

AXObjectCache* Document::ExistingAXObjectCache() const {
  auto& cache_owner = AXObjectCacheOwner();

  // If the LayoutView is gone then we are in the process of destruction.
  if (!cache_owner.GetLayoutView())
    return nullptr;

  return cache_owner.ax_object_cache_.Get();
}

bool Document::HasAXObjectCache() const {
  auto& cache_owner = AXObjectCacheOwner();

  // If the LayoutView is gone then we are in the process of destruction.
  if (!cache_owner.layout_view_)
    return false;

  return cache_owner.ax_object_cache_;
}

CanvasFontCache* Document::GetCanvasFontCache() {
  if (!canvas_font_cache_)
    canvas_font_cache_ = MakeGarbageCollected<CanvasFontCache>(*this);

  return canvas_font_cache_.Get();
}

DocumentParser* Document::CreateParser() {
  if (auto* html_document = DynamicTo<HTMLDocument>(this)) {
    return MakeGarbageCollected<HTMLDocumentParser>(*html_document,
                                                    parser_sync_policy_);
  }
  // FIXME: this should probably pass the frame instead
  return MakeGarbageCollected<XMLDocumentParser>(*this, View());
}

bool Document::IsFrameSet() const {
  if (!IsA<HTMLDocument>(this))
    return false;
  return IsA<HTMLFrameSetElement>(body());
}

ScriptableDocumentParser* Document::GetScriptableDocumentParser() const {
  return Parser() ? Parser()->AsScriptableDocumentParser() : nullptr;
}

void Document::DisplayNoneChangedForFrame() {
  if (!documentElement())
    return;
  // LayoutView()::CanHaveChildren(), hence the existence of style and
  // layout tree, depends on the owner being display:none or not. Trigger
  // detaching or attaching the style/layout-tree as a result of that
  // changing.
  documentElement()->SetNeedsStyleRecalc(
      kLocalStyleChange,
      StyleChangeReasonForTracing::Create(style_change_reason::kFrame));
}

bool Document::WillPrintSoon() {
  loading_for_print_ =
      EnsureLazyLoadImageObserver().LoadAllImagesAndBlockLoadEvent();

  if (auto* view = View()) {
    loading_for_print_ = loading_for_print_ || view->LoadAllLazyLoadedIframes();
  }

  return loading_for_print_;
}

void Document::SetPrinting(PrintingState state) {
  bool was_printing = Printing();
  printing_ = state;
  bool is_printing = Printing();

  if (was_printing != is_printing) {
    GetDisplayLockDocumentState().NotifyPrintingOrPreviewChanged();
    if (auto* ds_controller = DeferredShapingController::From(*this))
      ds_controller->ReshapeAllDeferred(ReshapeReason::kPrinting);

    // We force the color-scheme to light for printing.
    ColorSchemeChanged();
    // StyleResolver::InitialStyleForElement uses different zoom for printing.
    GetStyleEngine().MarkViewportStyleDirty();
    // Separate UA sheet for printing.
    GetStyleEngine().MarkAllElementsForStyleRecalc(
        StyleChangeReasonForTracing::Create(
            style_change_reason::kStyleSheetChange));

    if (documentElement() && GetFrame() && !GetFrame()->IsMainFrame() &&
        GetFrame()->Owner() && GetFrame()->Owner()->IsDisplayNone()) {
      // In non-printing mode we do not generate style or layout objects for
      // display:none iframes, yet we do when printing (see
      // LayoutView::CanHaveChildren). Trigger a style recalc on the root
      // element to create a layout tree for printing.
      DisplayNoneChangedForFrame();
    }
  }
}

// https://html.spec.whatwg.org/C/dynamic-markup-insertion.html#document-open-steps
void Document::open(LocalDOMWindow* entered_window,
                    ExceptionState& exception_state) {
  // If |document| is an XML document, then throw an "InvalidStateError"
  // DOMException exception.
  if (!IsA<HTMLDocument>(this)) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "Only HTML documents support open().");
    return;
  }

  // If |document|'s throw-on-dynamic-markup-insertion counter is greater than
  // 0, then throw an "InvalidStateError" DOMException.
  if (throw_on_dynamic_markup_insertion_count_) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Custom Element constructor should not use open().");
    return;
  }

  if (!AllowedToUseDynamicMarkUpInsertion("open", exception_state))
    return;

  if (entered_window && !entered_window->GetFrame())
    return;

  // If |document|'s origin is not same origin to the origin of the responsible
  // document specified by the entry settings object, then throw a
  // "SecurityError" DOMException.
  if (entered_window && GetExecutionContext() &&
      !GetExecutionContext()->GetSecurityOrigin()->IsSameOriginWith(
          entered_window->GetSecurityOrigin())) {
    exception_state.ThrowSecurityError(
        "Can only call open() on same-origin documents.");
    return;
  }

  // If |document| has an active parser whose script nesting level is greater
  // than 0, then return |document|.
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser()) {
    if (parser->IsParsing() && parser->IsExecutingScript())
      return;
  }

  // Similarly, if |document|'s ignore-opens-during-unload counter is greater
  // than 0, then return |document|.
  if (ignore_opens_during_unload_count_)
    return;

  // If |document|'s active parser was aborted is true, then return |document|.
  if (ignore_opens_and_writes_for_abort_)
    return;

  // If this document is fully active, then update the URL
  // for this document with the entered window's url.
  if (dom_window_ && entered_window) {
    KURL new_url = entered_window->Url();
    // Clear the hash fragment from the inherited URL to prevent a
    // scroll-into-view for any document.open()'d frame.
    if (dom_window_ != entered_window)
      new_url.SetFragmentIdentifier(String());
    SetURL(new_url);
    if (Loader())
      Loader()->DidOpenDocumentInputStream(new_url);

    if (dom_window_ != entered_window) {
      // We inherit the sandbox flags of the entered document, so mask on
      // the ones contained in the CSP. The operator| is a bitwise operation on
      // the sandbox flags bits. It makes the sandbox policy stricter (or as
      // strict) as both policy.
      //
      // TODO(arthursonzogni): Why merging sandbox flags?
      // This doesn't look great at many levels:
      // - The browser process won't be notified of the update.
      // - The origin won't be made opaque, despite the new flags.
      // - The sandbox flags of the document can't be considered to be an
      //   immutable property anymore.
      //
      // Ideally:
      // - javascript-url document.
      // - XSLT document.
      // - document.open.
      // should not mutate the security properties of the current document. From
      // the browser process point of view, all of those operations are not
      // considered to produce new documents. No IPCs are sent, it is as if it
      // was a no-op.
      dom_window_->GetSecurityContext().SetSandboxFlags(
          dom_window_->GetSecurityContext().GetSandboxFlags() |
          entered_window->GetSandboxFlags());

      dom_window_->GetSecurityContext().SetSecurityOrigin(
          entered_window->GetMutableSecurityOrigin());
      cookie_url_ = entered_window->document()->CookieURL();
    }
  }

  open();
}

// https://html.spec.whatwg.org/C/dynamic-markup-insertion.html#document-open-steps
void Document::open() {
  DCHECK(!ignore_opens_during_unload_count_);
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser())
    DCHECK(!parser->IsParsing() || !parser->IsExecutingScript());

  // If |document| has a browsing context and there is an existing attempt to
  // navigate |document|'s browsing context, then stop document loading given
  // |document|.
  //
  // As noted in the spec and https://github.com/whatwg/html/issues/3975, we
  // want to treat ongoing navigation and queued navigation the same way.
  // However, we don't want to consider navigations scheduled too much into the
  // future through Refresh headers or a <meta> refresh pragma to be a current
  // navigation. Thus, we cut it off with
  // IsHttpRefreshScheduledWithin(base::TimeDelta()).
  //
  // This also prevents window.open(url) -- eg window.open("about:blank") --
  // from blowing away results from a subsequent window.document.open /
  // window.document.write call.
  if (GetFrame() && (GetFrame()->Loader().HasProvisionalNavigation() ||
                     IsHttpRefreshScheduledWithin(base::TimeDelta()))) {
    GetFrame()->Loader().StopAllLoaders(/*abort_client=*/true);
  }
  CancelPendingJavaScriptUrls();

  // TODO(crbug.com/1085514): Consider making HasProvisionalNavigation() return
  // true when form submission task is active, in which case we can delete this
  // redundant attempt to cancel it.
  if (GetFrame())
    GetFrame()->CancelFormSubmission();

  // For each shadow-including inclusive descendant |node| of |document|, erase
  // all event listeners and handlers given |node|.
  //
  // Erase all event listeners and handlers given |window|.
  //
  // NB: Document::RemoveAllEventListeners() (called by
  // RemoveAllEventListenersRecursively()) erases event listeners from the
  // Window object as well.
  RemoveAllEventListenersRecursively();

  ResetTreeScope();
  if (GetFrame())
    GetFrame()->Selection().Clear();

  // Create a new HTML parser and associate it with |document|.
  //
  // Set the current document readiness of |document| to "loading".
  ImplicitOpen(kForceSynchronousParsing);

  // This is a script-created parser.
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser())
    parser->SetWasCreatedByScript(true);

  // Calling document.open counts as committing the first real document load.
  is_initial_empty_document_ = false;
  if (GetFrame())
    GetFrame()->Loader().DidExplicitOpen();
}

void Document::DetachParser() {
  if (!parser_)
    return;
  parser_->Detach();
  parser_.Clear();
  DocumentParserTiming::From(*this).MarkParserDetached();
}

void Document::CancelParsing() {
  // There appears to be an unspecced assumption that a document.open()
  // or document.write() immediately after a navigation start won't cancel
  // the navigation. Firefox avoids cancelling the navigation by ignoring an
  // open() or write() after an active parser is aborted. See
  // https://github.com/whatwg/html/issues/4723 for discussion about
  // standardizing this behavior.
  if (parser_ && parser_->IsParsing())
    ignore_opens_and_writes_for_abort_ = true;
  DetachParser();
  SetParsingState(kFinishedParsing);
  SetReadyState(kComplete);
  if (!LoadEventFinished())
    load_event_progress_ = kLoadEventCompleted;
  CancelPendingJavaScriptUrls();
  http_refresh_scheduler_->Cancel();
}

DocumentParser* Document::OpenForNavigation(
    ParserSynchronizationPolicy parser_sync_policy,
    const AtomicString& mime_type,
    const AtomicString& encoding) {
  DocumentParser* parser = ImplicitOpen(parser_sync_policy);
  if (parser->NeedsDecoder())
    parser->SetDecoder(BuildTextResourceDecoderFor(this, mime_type, encoding));
  if (AnchorElementInteractionTracker::IsFeatureEnabled() &&
      !GetFrame()->IsProvisional()) {
    anchor_element_interaction_tracker_ =
        MakeGarbageCollected<AnchorElementInteractionTracker>(*this);
  }
  return parser;
}

DocumentParser* Document::ImplicitOpen(
    ParserSynchronizationPolicy parser_sync_policy) {
  RemoveChildren();
  DCHECK(!focused_element_);

  SetCompatibilityMode(kNoQuirksMode);

  bool force_sync_policy = false;
  // Give inspector a chance to force sync parsing when virtual time is on.
  probe::WillCreateDocumentParser(this, force_sync_policy);
  // Prefetch must be synchronous.
  force_sync_policy |= ForceSynchronousParsingForTesting() || IsPrefetchOnly();
  if (force_sync_policy)
    parser_sync_policy = kForceSynchronousParsing;
  DetachParser();
  parser_sync_policy_ = parser_sync_policy;
  parser_ = CreateParser();
  DocumentParserTiming::From(*this).MarkParserStart();
  SetParsingState(kParsing);
  SetReadyState(kLoading);
  if (load_event_progress_ != kLoadEventInProgress &&
      PageDismissalEventBeingDispatched() == kNoDismissal) {
    load_event_progress_ = kLoadEventNotRun;
  }
  DispatchHandleLoadStart();
  return parser_;
}

void Document::DispatchHandleLoadStart() {
  if (AXObjectCache* cache = ExistingAXObjectCache()) {
    // Don't fire load start for popup document.
    if (this == &AXObjectCacheOwner())
      cache->HandleLoadStart(this);
  }
}

void Document::DispatchHandleLoadOrLayoutComplete() {
  if (AXObjectCache* cache = ExistingAXObjectCache()) {
    if (this == &AXObjectCacheOwner())
      cache->HandleLoadComplete(this);
    else
      cache->HandleLayoutComplete(this);
  }
}

HTMLElement* Document::body() const {
  if (!IsA<HTMLHtmlElement>(documentElement()))
    return nullptr;

  for (HTMLElement* child =
           Traversal<HTMLElement>::FirstChild(*documentElement());
       child; child = Traversal<HTMLElement>::NextSibling(*child)) {
    if (IsA<HTMLFrameSetElement>(*child) || IsA<HTMLBodyElement>(*child))
      return child;
  }

  return nullptr;
}

HTMLBodyElement* Document::FirstBodyElement() const {
  if (!IsA<HTMLHtmlElement>(documentElement()))
    return nullptr;

  for (HTMLElement* child =
           Traversal<HTMLElement>::FirstChild(*documentElement());
       child; child = Traversal<HTMLElement>::NextSibling(*child)) {
    if (auto* body = DynamicTo<HTMLBodyElement>(*child))
      return body;
  }

  return nullptr;
}

void Document::setBody(HTMLElement* prp_new_body,
                       ExceptionState& exception_state) {
  HTMLElement* new_body = prp_new_body;

  if (!new_body) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kHierarchyRequestError,
        ExceptionMessages::ArgumentNullOrIncorrectType(1, "HTMLElement"));
    return;
  }
  if (!documentElement()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kHierarchyRequestError,
                                      "No document element exists.");
    return;
  }

  if (!IsA<HTMLBodyElement>(*new_body) &&
      !IsA<HTMLFrameSetElement>(*new_body)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kHierarchyRequestError,
        "The new body element is of type '" + new_body->tagName() +
            "'. It must be either a 'BODY' or 'FRAMESET' element.");
    return;
  }

  HTMLElement* old_body = body();
  if (old_body == new_body)
    return;

  if (old_body)
    documentElement()->ReplaceChild(new_body, old_body, exception_state);
  else
    documentElement()->AppendChild(new_body, exception_state);
}

void Document::WillInsertBody() {
  if (Loader())
    fetcher_->LoosenLoadThrottlingPolicy();

  // If we get to the <body> try to resume commits since we should have content
  // to paint now.
  // TODO(esprehn): Is this really optimal? We might start producing frames
  // for very little content, should we wait for some heuristic like
  // isVisuallyNonEmpty() ?
  BeginLifecycleUpdatesIfRenderingReady();
}

HTMLHeadElement* Document::head() const {
  Node* de = documentElement();
  if (!de)
    return nullptr;

  return Traversal<HTMLHeadElement>::FirstChild(*de);
}

Element* Document::ViewportDefiningElement() const {
  // If a BODY element sets non-visible overflow, it is to be propagated to the
  // viewport, as long as the following conditions are all met:
  // (1) The root element is HTML.
  // (2) It is the primary BODY element.
  // (3) The root element has visible overflow.
  // (4) The root or BODY elements do not apply any containment.
  // Otherwise it's the root element's properties that are to be propagated.

  // This method is called in the middle of a lifecycle update, for instance
  // from a LayoutObject which is created but not yet inserted into the box
  // tree, which is why we have to do the decision based on the ComputedStyle
  // and not the LayoutObject style and the containment checks below also.

  Element* root_element = documentElement();
  if (!root_element)
    return nullptr;
  const ComputedStyle* root_style = root_element->GetComputedStyle();
  if (!root_style || root_style->IsEnsuredInDisplayNone())
    return nullptr;
  if (!root_style->IsOverflowVisibleAlongBothAxes())
    return root_element;
  HTMLBodyElement* body_element = FirstBodyElement();
  if (!body_element)
    return root_element;
  const ComputedStyle* body_style = body_element->GetComputedStyle();
  if (!body_style || body_style->IsEnsuredInDisplayNone())
    return root_element;
  if (root_style->ShouldApplyAnyContainment(*root_element) ||
      body_style->ShouldApplyAnyContainment(*body_element)) {
    return root_element;
  }
  return body_element;
}

Document* Document::open(v8::Isolate* isolate,
                         const AtomicString& type,
                         const AtomicString& replace,
                         ExceptionState& exception_state) {
  if (replace == "replace") {
    CountUse(WebFeature::kDocumentOpenTwoArgsWithReplace);
  }
  open(EnteredDOMWindow(isolate), exception_state);
  return this;
}

DOMWindow* Document::open(v8::Isolate* isolate,
                          const String& url_string,
                          const AtomicString& name,
                          const AtomicString& features,
                          ExceptionState& exception_state) {
  if (!domWindow()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidAccessError,
                                      "The document has no window associated.");
    return nullptr;
  }

  return domWindow()->open(isolate, url_string, name, features,
                           exception_state);
}

// https://html.spec.whatwg.org/C/dynamic-markup-insertion.html#dom-document-close
void Document::close(ExceptionState& exception_state) {
  // If the Document object is an XML document, then throw an
  // "InvalidStateError" DOMException.
  if (!IsA<HTMLDocument>(this)) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "Only HTML documents support close().");
    return;
  }

  // If the Document object's throw-on-dynamic-markup-insertion counter is
  // greater than zero, then throw an "InvalidStateError" DOMException.
  if (throw_on_dynamic_markup_insertion_count_) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Custom Element constructor should not use close().");
    return;
  }

  if (!AllowedToUseDynamicMarkUpInsertion("close", exception_state))
    return;

  close();
}

// https://html.spec.whatwg.org/C/dynamic-markup-insertion.html#dom-document-close
void Document::close() {
  // If there is no script-created parser associated with the document, then
  // return.
  if (!GetScriptableDocumentParser() ||
      !GetScriptableDocumentParser()->WasCreatedByScript() ||
      !GetScriptableDocumentParser()->IsParsing())
    return;

  // Insert an explicit "EOF" character at the end of the parser's input
  // stream.
  parser_->Finish();

  // TODO(timothygu): We should follow the specification more closely.
  if (!parser_ || !parser_->IsParsing())
    SetReadyState(kComplete);
  CheckCompleted();
}

void Document::ImplicitClose() {
  DCHECK(!InStyleRecalc());

  load_event_progress_ = kLoadEventInProgress;

  // We have to clear the parser, in case someone document.write()s from the
  // onLoad event handler, as in Radar 3206524.
  DetachParser();

  // JS running below could remove the frame or destroy the LayoutView so we
  // call those two functions repeatedly and don't save them on the stack.

  // To align the HTML load event and the SVGLoad event for the outermost <svg>
  // element, fire it from here, instead of doing it from
  // SVGElement::finishedParsingChildren.
  if (SvgExtensions())
    AccessSVGExtensions().DispatchSVGLoadEventToOutermostSVGElements();

  if (domWindow())
    domWindow()->DocumentWasClosed();

  if (GetFrame() && GetFrame()->IsMainFrame())
    GetFrame()->GetLocalFrameHostRemote().DocumentOnLoadCompleted();

  if (GetFrame()) {
    GetFrame()->Client()->DispatchDidHandleOnloadEvents();
  }

  if (!GetFrame()) {
    load_event_progress_ = kLoadEventCompleted;
    return;
  }

  if (GetFrame()->Loader().HasProvisionalNavigation() &&
      start_time_.Elapsed() < kCLayoutScheduleThreshold) {
    // Just bail out. Before or during the onload we were shifted to another
    // page.  The old i-Bench suite does this. When this happens don't bother
    // painting or laying out.
    load_event_progress_ = kLoadEventCompleted;
    return;
  }

  if (HaveRenderBlockingStylesheetsLoaded())
    UpdateStyleAndLayout(DocumentUpdateReason::kUnknown);

  load_event_progress_ = kLoadEventCompleted;

  if (GetFrame() && GetLayoutView()) {
    DispatchHandleLoadOrLayoutComplete();
    FontFaceSetDocument::DidLayout(*this);
  }

  if (SvgExtensions())
    AccessSVGExtensions().StartAnimations();
}

static bool AllDescendantsAreComplete(Document* document) {
  Frame* frame = document->GetFrame();
  if (!frame)
    return true;

  // TODO(crbug.com/1262022): Remove this Fenced FrameTreeBoundary when Fenced
  // Frames transition to MPArch completely.
  for (Frame* child = frame->Tree().FirstChild(FrameTreeBoundary::kFenced);
       child;
       child = child->Tree().TraverseNext(frame, FrameTreeBoundary::kFenced)) {
    if (child->IsLoading())
      return false;
  }

  if (auto* portals = DocumentPortals::Get(*document)) {
    for (PortalContents* portal : portals->GetPortals()) {
      auto* portal_frame = portal->GetFrame();
      if (portal_frame && portal_frame->IsLoading())
        return false;
    }
  }
  return true;
}

bool Document::ShouldComplete() {
  return parsing_state_ == kFinishedParsing &&
         !fetcher_->BlockingRequestCount() && !IsDelayingLoadEvent() &&
         !javascript_url_task_handle_.IsActive() &&
         load_event_progress_ != kLoadEventInProgress &&
         AllDescendantsAreComplete(this) && !Fetcher()->IsInRequestResource();
}

void Document::Abort() {
  CancelParsing();
  CheckCompletedInternal();
}

void Document::CheckCompleted() {
  if (CheckCompletedInternal()) {
    GetFrame()->Loader().DidFinishNavigation(
        FrameLoader::NavigationFinishState::kSuccess);
  }
}

bool Document::CheckCompletedInternal() {
  if (!ShouldComplete())
    return false;

  if (GetFrame() && !UnloadStarted()) {
    GetFrame()->Client()->RunScriptsAtDocumentIdle();

    // Injected scripts may have disconnected this frame.
    if (!GetFrame())
      return false;

    // Check again, because runScriptsAtDocumentIdle() may have delayed the load
    // event.
    if (!ShouldComplete())
      return false;
  }

  // OK, completed. Fire load completion events as needed.
  SetReadyState(kComplete);
  if (LoadEventStillNeeded())
    ImplicitClose();

  DCHECK(fetcher_);
  fetcher_->ScheduleWarnUnusedPreloads();

  // The readystatechanged or load event may have disconnected this frame.
  if (!GetFrame() || !GetFrame()->IsAttached())
    return false;
  http_refresh_scheduler_->MaybeStartTimer();
  View()->HandleLoadCompleted();
  // The document itself is complete, but if a child frame was restarted due to
  // an event, this document is still considered to be in progress.
  if (!AllDescendantsAreComplete(this))
    return false;

  // No need to repeat if we've already notified this load as finished.
  if (!Loader()->SentDidFinishLoad()) {
    if (GetFrame()->IsOutermostMainFrame()) {
      GetViewportData().GetViewportDescription().ReportMobilePageStats(
          GetFrame());
    }
    Loader()->SetSentDidFinishLoad();
    GetFrame()->Client()->DispatchDidFinishLoad();
    // RenderFrameObservers may execute script, which could detach this frame.
    if (!GetFrame())
      return false;
    GetFrame()->GetLocalFrameHostRemote().DidFinishLoad(Loader()->Url());

    GetFrame()->GetFrameScheduler()->RegisterStickyFeature(
        SchedulingPolicy::Feature::kDocumentLoaded,
        {SchedulingPolicy::DisableBackForwardCache()});
    GetFrame()->GetFrameScheduler()->OnLoad();

    DetectJavascriptFrameworksOnLoad(*this);
  } else if (loading_for_print_) {
    loading_for_print_ = false;
    GetFrame()->Client()->DispatchDidFinishLoadForPrinting();
  }

  if (auto* view = View()) {
    if (view->GetFragmentAnchor()) {
      // Schedule an animation frame to process fragment anchors. The frame
      // can't be scheduled when the fragment anchor is set because, per spec,
      // we must wait for the document to be loaded before invoking fragment
      // anchors.
      View()->ScheduleAnimation();
    }
  }

  return true;
}

namespace {

enum class BeforeUnloadUse {
  kNoDialogNoText,
  kNoDialogNoUserGesture,
  kNoDialogMultipleConfirmationForNavigation,
  kShowDialog,
  kNoDialogAutoCancelTrue,
  kMaxValue = kNoDialogAutoCancelTrue,
};

void RecordBeforeUnloadUse(BeforeUnloadUse metric) {
  base::UmaHistogramEnumeration("Document.BeforeUnloadDialog", metric);
}

}  // namespace

bool Document::DispatchBeforeUnloadEvent(ChromeClient* chrome_client,
                                         bool is_reload,
                                         bool& did_allow_navigation) {
  if (!dom_window_)
    return true;

  if (!body())
    return true;

  if (ProcessingBeforeUnload())
    return false;

  // Since we do not allow registering the beforeunload event handlers in
  // fenced frames, it should not be fired by fencedframes.
  DCHECK(!GetFrame() || !GetFrame()->IsInFencedFrameTree() ||
         !GetEventTargetData() ||
         !GetEventTargetData()->event_listener_map.Contains(
             event_type_names::kBeforeunload));

  PageDismissalScope in_page_dismissal;
  auto& before_unload_event = *MakeGarbageCollected<BeforeUnloadEvent>();
  before_unload_event.initEvent(event_type_names::kBeforeunload, false, true);

  {
    // We want to avoid progressing to kBeforeUnloadEventHandled if the page
    // cancels the unload. Because a subframe may cancel unload on our behalf,
    // only the caller, which makes this call over the frame subtree, can know
    // whether or not  we'll unload so the caller is responsible for advancing
    // to kBeforeUnloadEventHandled. Here, we'll reset back to our prior value
    // once the handler has run.
    base::AutoReset<LoadEventProgress> set_in_progress(
        &load_event_progress_, kBeforeUnloadEventInProgress);
    dom_window_->DispatchEvent(before_unload_event, this);
  }

  if (!before_unload_event.defaultPrevented())
    DefaultEventHandler(before_unload_event);

  if (before_unload_event.returnValue().IsNull()) {
    RecordBeforeUnloadUse(BeforeUnloadUse::kNoDialogNoText);
  }
  if (!GetFrame() || before_unload_event.returnValue().IsNull())
    return true;

  if (!GetFrame()->HasStickyUserActivation()) {
    RecordBeforeUnloadUse(BeforeUnloadUse::kNoDialogNoUserGesture);
    String message =
        "Blocked attempt to show a 'beforeunload' confirmation panel for a "
        "frame that never had a user gesture since its load. "
        "https://www.chromestatus.com/feature/5082396709879808";
    Intervention::GenerateReport(GetFrame(), "BeforeUnloadNoGesture", message);
    return true;
  }

  if (did_allow_navigation) {
    RecordBeforeUnloadUse(
        BeforeUnloadUse::kNoDialogMultipleConfirmationForNavigation);
    String message =
        "Blocked attempt to show multiple 'beforeunload' confirmation panels "
        "for a single navigation.";
    Intervention::GenerateReport(GetFrame(), "BeforeUnloadMultiple", message);
    return true;
  }

  // If |chrome_client| is null simply indicate that the navigation should
  // not proceed.
  if (!chrome_client) {
    RecordBeforeUnloadUse(BeforeUnloadUse::kNoDialogAutoCancelTrue);
    did_allow_navigation = false;
    return false;
  }

  String text = before_unload_event.returnValue();
  RecordBeforeUnloadUse(BeforeUnloadUse::kShowDialog);
  const base::TimeTicks beforeunload_confirmpanel_start =
      base::TimeTicks::Now();
  did_allow_navigation =
      chrome_client->OpenBeforeUnloadConfirmPanel(text, GetFrame(), is_reload);
  const base::TimeTicks beforeunload_confirmpanel_end = base::TimeTicks::Now();
  if (did_allow_navigation) {
    // Only record when a navigation occurs, since we want to understand
    // the impact of the before unload dialog on overall input to navigation.
    UMA_HISTOGRAM_MEDIUM_TIMES(
        "DocumentEventTiming.BeforeUnloadDialogDuration.ByNavigation",
        beforeunload_confirmpanel_end - beforeunload_confirmpanel_start);
    return true;
  }

  return false;
}

void Document::DispatchUnloadEvents(UnloadEventTimingInfo* unload_timing_info) {
  PluginScriptForbiddenScope forbid_plugin_destructor_scripting;
  PageDismissalScope in_page_dismissal;
  if (parser_)
    parser_->StopParsing();

  if (load_event_progress_ == kLoadEventNotRun ||
      // TODO(dcheng): We should consider if we can make this conditional check
      // stronger with a DCHECK() that this isn't called if the unload event is
      // already complete.
      load_event_progress_ > kUnloadEventInProgress) {
    return;
  }

  Element* current_focused_element = FocusedElement();
  if (auto* input = DynamicTo<HTMLInputElement>(current_focused_element))
    input->EndEditing();

  // Since we do not allow registering the unload event handlers in
  // fenced frames, it should not be fired by fencedframes.
  DCHECK(!GetFrame() || !GetFrame()->IsInFencedFrameTree() ||
         !GetEventTargetData() ||
         !GetEventTargetData()->event_listener_map.Contains(
             event_type_names::kUnload));

  // If we've dispatched the pagehide event with 'persisted' set to true, it
  // means we've dispatched the visibilitychange event before too. Also, we
  // shouldn't dispatch the unload event because that event should only be
  // fired when the pagehide event's 'persisted' bit is set to false.
  bool dispatched_pagehide_persisted =
      GetPage() && GetPage()->DispatchedPagehidePersistedAndStillHidden();

  if (load_event_progress_ >= kPageHideInProgress ||
      dispatched_pagehide_persisted) {
    load_event_progress_ = kUnloadEventHandled;
    return;
  }

  load_event_progress_ = kPageHideInProgress;
  LocalDOMWindow* window = domWindow();
  // We check for DispatchedPagehideAndStillHidden() here because it's possible
  // to dispath pagehide with 'persisted' set to false before this and pass the
  // |dispatched_pagehide_persisted| above, if we enable same-site
  // ProactivelySwapBrowsingInstance but not BackForwardCache.
  if (window && !GetPage()->DispatchedPagehideAndStillHidden()) {
    const base::TimeTicks pagehide_event_start = base::TimeTicks::Now();
    window->DispatchEvent(
        *PageTransitionEvent::Create(event_type_names::kPagehide, false), this);
    const base::TimeTicks pagehide_event_end = base::TimeTicks::Now();
    DEFINE_STATIC_LOCAL(
        CustomCountHistogram, pagehide_histogram,
        ("DocumentEventTiming.PageHideDuration", 0, 10000000, 50));
    pagehide_histogram.CountMicroseconds(pagehide_event_end -
                                         pagehide_event_start);
  }
  if (!dom_window_)
    return;

  // This must be queried before |load_event_progress_| is changed to
  // kUnloadVisibilityChangeInProgress because that would change the result.
  bool page_visible = IsPageVisible();
  load_event_progress_ = kUnloadVisibilityChangeInProgress;
  if (page_visible) {
    // Dispatch visibilitychange event, but don't bother doing
    // other notifications as we're about to be unloaded.
    const base::TimeTicks pagevisibility_hidden_event_start =
        base::TimeTicks::Now();
    DispatchEvent(*Event::CreateBubble(event_type_names::kVisibilitychange));
    const base::TimeTicks pagevisibility_hidden_event_end =
        base::TimeTicks::Now();
    DEFINE_STATIC_LOCAL(
        CustomCountHistogram, pagevisibility_histogram,
        ("DocumentEventTiming.PageVibilityHiddenDuration", 0, 10000000, 50));
    pagevisibility_histogram.CountMicroseconds(
        pagevisibility_hidden_event_end - pagevisibility_hidden_event_start);
    DispatchEvent(
        *Event::CreateBubble(event_type_names::kWebkitvisibilitychange));
  }
  if (!dom_window_)
    return;

  GetFrame()->Loader().SaveScrollAnchor();

  load_event_progress_ = kUnloadEventInProgress;
  Event& unload_event = *Event::Create(event_type_names::kUnload);
  const base::TimeTicks unload_event_start = base::TimeTicks::Now();
  dom_window_->DispatchEvent(unload_event, this);
  const base::TimeTicks unload_event_end = base::TimeTicks::Now();

  if (unload_timing_info) {
    // Record unload event timing when navigating cross-document.
    DEFINE_STATIC_LOCAL(
        CustomCountHistogram, unload_histogram,
        ("DocumentEventTiming.UnloadDuration", 0, 10000000, 50));
    unload_histogram.CountMicroseconds(unload_event_end - unload_event_start);

    auto& timing = unload_timing_info->unload_timing.emplace();
    timing.can_request =
        unload_timing_info->new_document_origin->CanRequest(Url());
    timing.unload_event_start = unload_event_start;
    timing.unload_event_end = unload_event_end;
  }
  load_event_progress_ = kUnloadEventHandled;
}

void Document::DispatchFreezeEvent() {
  const base::TimeTicks freeze_event_start = base::TimeTicks::Now();
  SetFreezingInProgress(true);
  DispatchEvent(*Event::Create(event_type_names::kFreeze));
  SetFreezingInProgress(false);
  const base::TimeTicks freeze_event_end = base::TimeTicks::Now();
  DEFINE_STATIC_LOCAL(CustomCountHistogram, freeze_histogram,
                      ("DocumentEventTiming.FreezeDuration", 0, 10000000, 50));
  freeze_histogram.CountMicroseconds(freeze_event_end - freeze_event_start);
  UseCounter::Count(*this, WebFeature::kPageLifeCycleFreeze);
}

Document::PageDismissalType Document::PageDismissalEventBeingDispatched()
    const {
  switch (load_event_progress_) {
    case kBeforeUnloadEventInProgress:
      return kBeforeUnloadDismissal;
    case kPageHideInProgress:
      return kPageHideDismissal;
    case kUnloadVisibilityChangeInProgress:
      return kUnloadVisibilityChangeDismissal;
    case kUnloadEventInProgress:
      return kUnloadDismissal;

    case kLoadEventNotRun:
    case kLoadEventInProgress:
    case kLoadEventCompleted:
    case kBeforeUnloadEventHandled:
    case kUnloadEventHandled:
      return kNoDismissal;
  }
  NOTREACHED();
  return kNoDismissal;
}

void Document::SetParsingState(ParsingState parsing_state) {
  ParsingState previous_state = parsing_state_;
  parsing_state_ = parsing_state;

  if (Parsing() && !element_data_cache_)
    element_data_cache_ = MakeGarbageCollected<ElementDataCache>();
  if (previous_state != kFinishedParsing &&
      parsing_state_ == kFinishedParsing) {
    if (form_controller_ && form_controller_->HasControlStates())
      form_controller_->ScheduleRestore();
    if (auto* ds_controller = DeferredShapingController::From(*this)) {
      PaintTiming& timing = PaintTiming::From(*this);
      if (!timing.FirstContentfulPaint().is_null())
        ds_controller->ReshapeAllDeferred(ReshapeReason::kDomContentLoaded);
    }
  }
}

bool Document::ShouldScheduleLayout() const {
  // This function will only be called when LocalFrameView thinks a layout is
  // needed. This enforces a couple extra rules.
  //
  //    (a) Only schedule a layout once the stylesheets are loaded.
  //    (b) Only schedule layout once we have a body element.
  if (!IsActive())
    return false;

  if (HaveRenderBlockingResourcesLoaded() && body())
    return true;

  if (documentElement() && !IsA<HTMLHtmlElement>(documentElement()))
    return true;

  return false;
}

void Document::write(const String& text,
                     LocalDOMWindow* entered_window,
                     ExceptionState& exception_state) {
  if (!IsA<HTMLDocument>(this)) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "Only HTML documents support write().");
    return;
  }

  if (throw_on_dynamic_markup_insertion_count_) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Custom Element constructor should not use write().");
    return;
  }

  if (entered_window && !entered_window->GetFrame())
    return;

  if (entered_window && GetExecutionContext() &&
      !GetExecutionContext()->GetSecurityOrigin()->IsSameOriginWith(
          entered_window->GetSecurityOrigin())) {
    exception_state.ThrowSecurityError(
        "Can only call write() on same-origin documents.");
    return;
  }

  if (ignore_opens_and_writes_for_abort_)
    return;

  NestingLevelIncrementer nesting_level_incrementer(write_recursion_depth_);

  write_recursion_is_too_deep_ =
      (write_recursion_depth_ > 1) && write_recursion_is_too_deep_;
  write_recursion_is_too_deep_ =
      (write_recursion_depth_ > kCMaxWriteRecursionDepth) ||
      write_recursion_is_too_deep_;

  if (write_recursion_is_too_deep_)
    return;

  bool has_insertion_point = parser_ && parser_->HasInsertionPoint();

  if (!has_insertion_point) {
    if (ignore_destructive_write_count_) {
      AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
          mojom::ConsoleMessageSource::kJavaScript,
          mojom::ConsoleMessageLevel::kWarning,
          ExceptionMessages::FailedToExecute(
              "write", "Document",
              "It isn't possible to write into a document "
              "from an asynchronously-loaded external "
              "script unless it is explicitly opened.")));
      return;
    }
    if (ignore_opens_during_unload_count_)
      return;

    open(entered_window, ASSERT_NO_EXCEPTION);
  }

  DCHECK(parser_);
  PerformanceMonitor::ReportGenericViolation(
      domWindow(), PerformanceMonitor::kDiscouragedAPIUse,
      "Avoid using document.write(). "
      "https://developers.google.com/web/updates/2016/08/"
      "removing-document-write",
      base::TimeDelta(), nullptr);
  probe::BreakableLocation(domWindow(), "Document.write");
  parser_->insert(text);
}

void Document::writeln(const String& text,
                       LocalDOMWindow* entered_window,
                       ExceptionState& exception_state) {
  write(text, entered_window, exception_state);
  if (exception_state.HadException())
    return;
  write("\n", entered_window);
}

void Document::write(v8::Isolate* isolate,
                     const Vector<String>& text,
                     ExceptionState& exception_state) {
  if (!AllowedToUseDynamicMarkUpInsertion("write", exception_state))
    return;

  StringBuilder builder;
  for (const String& string : text)
    builder.Append(string);
  String string = TrustedTypesCheckForHTML(
      builder.ReleaseString(), GetExecutionContext(), exception_state);
  if (exception_state.HadException())
    return;

  write(string, EnteredDOMWindow(isolate), exception_state);
}

void Document::writeln(v8::Isolate* isolate,
                       const Vector<String>& text,
                       ExceptionState& exception_state) {
  if (!AllowedToUseDynamicMarkUpInsertion("writeln", exception_state))
    return;

  StringBuilder builder;
  for (const String& string : text)
    builder.Append(string);
  String string = TrustedTypesCheckForHTML(
      builder.ReleaseString(), GetExecutionContext(), exception_state);
  if (exception_state.HadException())
    return;

  writeln(string, EnteredDOMWindow(isolate), exception_state);
}

void Document::write(v8::Isolate* isolate,
                     TrustedHTML* text,
                     ExceptionState& exception_state) {
  write(text->toString(), EnteredDOMWindow(isolate), exception_state);
}

void Document::writeln(v8::Isolate* isolate,
                       TrustedHTML* text,
                       ExceptionState& exception_state) {
  writeln(text->toString(), EnteredDOMWindow(isolate), exception_state);
}

KURL Document::urlForBinding() const {
  if (WebBundleClaimedUrl().IsValid()) {
    return WebBundleClaimedUrl();
  }
  if (!Url().IsNull()) {
    return Url();
  }
  return BlankURL();
}

void Document::SetURL(const KURL& url) {
  KURL new_url = url.IsEmpty() ? BlankURL() : url;
  if (new_url == url_)
    return;

  TRACE_EVENT1("navigation", "Document::SetURL", "url",
               new_url.GetString().Utf8());

  // Strip the fragment directive from the URL fragment. E.g. "#id:~:text=a"
  // --> "#id". See https://github.com/WICG/scroll-to-text-fragment.
  new_url = fragment_directive_->ConsumeFragmentDirective(new_url);

  url_ = new_url;
  UpdateBaseURL();
  GetContextFeatures().UrlDidChange(this);

  if (GetFrame()) {
    if (FrameScheduler* frame_scheduler = GetFrame()->GetFrameScheduler())
      frame_scheduler->TraceUrlChange(url_.GetString());
  }
}

KURL Document::ValidBaseElementURL() const {
  if (base_element_url_.IsValid())
    return base_element_url_;

  return KURL();
}

void Document::UpdateBaseURL() {
  KURL old_base_url = base_url_;
  // DOM 3 Core: When the Document supports the feature "HTML" [DOM Level 2
  // HTML], the base URI is computed using first the value of the href attribute
  // of the HTML BASE element if any, and the value of the documentURI attribute
  // from the Document interface otherwise (which we store, preparsed, in
  // |url_|).
  if (!base_element_url_.IsEmpty())
    base_url_ = base_element_url_;
  else if (!base_url_override_.IsEmpty())
    base_url_ = base_url_override_;
  else
    base_url_ = FallbackBaseURL();

  GetSelectorQueryCache().Invalidate();

  if (!base_url_.IsValid())
    base_url_ = KURL();

  if (elem_sheet_) {
    // Element sheet is silly. It never contains anything.
    DCHECK(!elem_sheet_->Contents()->RuleCount());
    elem_sheet_ = CSSStyleSheet::CreateInline(*this, base_url_);
  }

  if (!EqualIgnoringFragmentIdentifier(old_base_url, base_url_)) {
    // Base URL change changes any relative visited links.
    // FIXME: There are other URLs in the tree that would need to be
    // re-evaluated on dynamic base URL change. Style should be invalidated too.
    for (HTMLAnchorElement& anchor :
         Traversal<HTMLAnchorElement>::StartsAfter(*this))
      anchor.InvalidateCachedVisitedLinkHash();
  }
}

// [spec] https://html.spec.whatwg.org/C/#fallback-base-url
KURL Document::FallbackBaseURL() const {
  const bool is_parent_cross_origin =
      GetFrame() && GetFrame()->IsCrossOriginToParentOrOuterDocument();
  // TODO(https://crbug.com/751329, https://crbug.com/1336904): Referring to
  // ParentDocument() is not correct.
  // We avoid using it when it is cross-origin, to avoid leaking cross-origin.
  const Document* same_origin_parent =
      is_parent_cross_origin ? nullptr : ParentDocument();

  // [spec] 1. If document is an iframe srcdoc document, then return the
  //           document base URL of document's browsing context's container
  //           document.
  if (IsSrcdocDocument()) {
    // TODO(https://crbug.com/751329, https://crbug.com/1336904): Referring to
    // ParentDocument() is not correct.
    if (Document* parent = ParentDocument()) {
      return parent->BaseURL();
    }
    // TODO(https://crbug.com/1339824) Sandboxed about:srcdoc document can be
    // hosted in a different process. As a result, their `parent` may be null,
    // and we might return something wrong, in a different way here.
  }

  // [spec] 2. If document's URL is about:blank, and document's browsing
  //           context's creator base URL is non-null, then return that creator
  //           base URL.
  if (urlForBinding().IsAboutBlankURL()) {
    if (!dom_window_ && execution_context_) {
      return execution_context_->BaseURL();
    }

    if (same_origin_parent) {
      return same_origin_parent->BaseURL();
    }
  }

  // [spec] 3. Return document's URL.
  return urlForBinding();
}

const KURL& Document::BaseURL() const {
  if (!base_url_.IsNull())
    return base_url_;
  return BlankURL();
}

void Document::SetBaseURLOverride(const KURL& url) {
  base_url_override_ = url;
  UpdateBaseURL();
}

void Document::ProcessBaseElement() {
  UseCounter::Count(*this, WebFeature::kBaseElement);

  // Find the first href attribute in a base element and the first target
  // attribute in a base element.
  const AtomicString* href = nullptr;
  const AtomicString* target = nullptr;
  for (HTMLBaseElement* base = Traversal<HTMLBaseElement>::FirstWithin(*this);
       base && (!href || !target);
       base = Traversal<HTMLBaseElement>::Next(*base)) {
    if (!href) {
      const AtomicString& value = base->FastGetAttribute(html_names::kHrefAttr);
      if (!value.IsNull())
        href = &value;
    }
    if (!target) {
      const AtomicString& value =
          base->FastGetAttribute(html_names::kTargetAttr);
      if (!value.IsNull())
        target = &value;
    }
    if (GetExecutionContext() &&
        GetExecutionContext()->GetContentSecurityPolicy()->IsActive()) {
      UseCounter::Count(*this,
                        WebFeature::kContentSecurityPolicyWithBaseElement);
    }
  }

  // FIXME: Since this doesn't share code with completeURL it may not handle
  // encodings correctly.
  KURL base_element_url;
  if (href) {
    String stripped_href = StripLeadingAndTrailingHTMLSpaces(*href);
    if (!stripped_href.IsEmpty())
      base_element_url = KURL(FallbackBaseURL(), stripped_href);
  }

  if (!base_element_url.IsEmpty()) {
    if (base_element_url.ProtocolIsData() ||
        base_element_url.ProtocolIsJavaScript()) {
      UseCounter::Count(*this, WebFeature::kBaseWithDataHref);
      AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
          mojom::ConsoleMessageSource::kSecurity,
          mojom::ConsoleMessageLevel::kError,
          "'" + base_element_url.Protocol() +
              "' URLs may not be used as base URLs for a document."));
    }
    if (GetExecutionContext() &&
        !GetExecutionContext()->GetSecurityOrigin()->CanRequest(
            base_element_url)) {
      UseCounter::Count(*this, WebFeature::kBaseWithCrossOriginHref);
    }
  }

  if (base_element_url != base_element_url_ &&
      !base_element_url.ProtocolIsData() &&
      !base_element_url.ProtocolIsJavaScript() && GetExecutionContext() &&
      GetExecutionContext()->GetContentSecurityPolicy()->AllowBaseURI(
          base_element_url)) {
    base_element_url_ = base_element_url;
    UpdateBaseURL();
  }

  if (target) {
    if (target->Contains('\n') || target->Contains('\r'))
      UseCounter::Count(*this, WebFeature::kBaseWithNewlinesInTarget);
    if (target->Contains('<'))
      UseCounter::Count(*this, WebFeature::kBaseWithOpenBracketInTarget);
    base_target_ = *target;
  } else {
    base_target_ = g_null_atom;
  }
}

void Document::DidAddPendingParserBlockingStylesheet() {
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser())
    parser->DidAddPendingParserBlockingStylesheet();
}

void Document::DidRemoveAllPendingStylesheets() {
  DidLoadAllScriptBlockingResources();
}

void Document::DidLoadAllPendingParserBlockingStylesheets() {
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser())
    parser->DidLoadAllPendingParserBlockingStylesheets();
}

void Document::DidLoadAllScriptBlockingResources() {
  // Use wrapWeakPersistent because the task should not keep this Document alive
  // just for executing scripts.
  execute_scripts_waiting_for_resources_task_handle_ = PostCancellableTask(
      *GetTaskRunner(TaskType::kNetworking), FROM_HERE,
      WTF::Bind(&Document::ExecuteScriptsWaitingForResources,
                WrapWeakPersistent(this)));

  if (IsA<HTMLDocument>(this) && body()) {
    // For HTML if we have no more stylesheets to load and we're past the body
    // tag, we should have something to paint so resume.
    BeginLifecycleUpdatesIfRenderingReady();
  } else if (!IsA<HTMLDocument>(this) && documentElement()) {
    // For non-HTML there is no body so resume as soon as the sheets are loaded.
    BeginLifecycleUpdatesIfRenderingReady();
  }
}

void Document::ExecuteScriptsWaitingForResources() {
  if (!IsScriptExecutionReady())
    return;
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser())
    parser->ExecuteScriptsWaitingForResources();
}

CSSStyleSheet& Document::ElementSheet() {
  if (!elem_sheet_)
    elem_sheet_ = CSSStyleSheet::CreateInline(*this, base_url_);
  return *elem_sheet_;
}

bool Document::InPostLifecycleSteps() const {
  return View() && View()->InPostLifecycleSteps();
}

void Document::MaybeHandleHttpRefresh(const String& content,
                                      HttpRefreshType http_refresh_type) {
  if (is_view_source_ || !dom_window_)
    return;

  base::TimeDelta delay;
  String refresh_url_string;
  if (!ParseHTTPRefresh(content,
                        http_refresh_type == kHttpRefreshFromMetaTag
                            ? IsHTMLSpace<UChar>
                            : nullptr,
                        delay, refresh_url_string))
    return;
  KURL refresh_url =
      refresh_url_string.IsEmpty() ? Url() : CompleteURL(refresh_url_string);

  if (refresh_url.ProtocolIsJavaScript()) {
    String message =
        "Refused to refresh " + url_.ElidedString() + " to a javascript: URL";
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::ConsoleMessageSource::kSecurity,
        mojom::ConsoleMessageLevel::kError, message));
    return;
  }

  if (http_refresh_type == kHttpRefreshFromMetaTag &&
      dom_window_->IsSandboxed(
          network::mojom::blink::WebSandboxFlags::kAutomaticFeatures)) {
    String message =
        "Refused to execute the redirect specified via '<meta "
        "http-equiv='refresh' content='...'>'. The document is sandboxed, and "
        "the 'allow-scripts' keyword is not set.";
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::ConsoleMessageSource::kSecurity,
        mojom::ConsoleMessageLevel::kError, message));
    return;
  }

  // Monitor blocking refresh usage when scripting is disabled.
  // See https://crbug.com/63107
  if (!dom_window_->CanExecuteScripts(kNotAboutToExecuteScript))
    UseCounter::Count(this, WebFeature::kHttpRefreshWhenScriptingDisabled);

  if (http_refresh_type == kHttpRefreshFromHeader) {
    UseCounter::Count(this, WebFeature::kRefreshHeader);
  }
  http_refresh_scheduler_->Schedule(delay, refresh_url, http_refresh_type);
}

bool Document::IsHttpRefreshScheduledWithin(base::TimeDelta interval) {
  return http_refresh_scheduler_->IsScheduledWithin(interval);
}

network::mojom::ReferrerPolicy Document::GetReferrerPolicy() const {
  return GetExecutionContext() ? GetExecutionContext()->GetReferrerPolicy()
                               : network::mojom::ReferrerPolicy::kDefault;
}

MouseEventWithHitTestResults Document::PerformMouseEventHitTest(
    const HitTestRequest& request,
    const PhysicalOffset& document_point,
    const WebMouseEvent& event) {
  DCHECK(!GetLayoutView() || IsA<LayoutView>(GetLayoutView()));

  // LayoutView::hitTest causes a layout, and we don't want to hit that until
  // the first layout because until then, there is nothing shown on the screen -
  // the user can't have intentionally clicked on something belonging to this
  // page.  Furthermore, mousemove events before the first layout should not
  // lead to a premature layout() happening, which could show a flash of white.
  // See also the similar code in EventHandler::hitTestResultAtPoint.
  if (!GetLayoutView() || !View() || !View()->DidFirstLayout()) {
    HitTestLocation location((PhysicalOffset()));
    return MouseEventWithHitTestResults(event, location,
                                        HitTestResult(request, location));
  }

  HitTestLocation location(document_point);
  HitTestResult result(request, location);
  GetLayoutView()->HitTest(location, result);

  if (!request.ReadOnly()) {
    UpdateHoverActiveState(request.Active(), !request.Move(),
                           result.InnerElement());
  }

  return MouseEventWithHitTestResults(event, location, result);
}

// DOM Section 1.1.1
bool Document::ChildTypeAllowed(NodeType type) const {
  switch (type) {
    case kAttributeNode:
    case kCdataSectionNode:
    case kDocumentFragmentNode:
    case kDocumentNode:
    case kTextNode:
      return false;
    case kCommentNode:
    case kProcessingInstructionNode:
      return true;
    case kDocumentTypeNode:
    case kElementNode:
      // Documents may contain no more than one of each of these.
      // (One Element and one DocumentType.)
      for (Node& c : NodeTraversal::ChildrenOf(*this)) {
        if (c.getNodeType() == type)
          return false;
      }
      return true;
  }
  return false;
}

// This is an implementation of step 6 of
// https://dom.spec.whatwg.org/#concept-node-ensure-pre-insertion-validity
// and https://dom.spec.whatwg.org/#concept-node-replace .
//
// 6. If parent is a document, and any of the statements below, switched on
// node, are true, throw a HierarchyRequestError.
//  -> DocumentFragment node
//     If node has more than one element child or has a Text node child.
//     Otherwise, if node has one element child and either parent has an element
//     child, child is a doctype, or child is not null and a doctype is
//     following child.
//  -> element
//     parent has an element child, child is a doctype, or child is not null and
//     a doctype is following child.
//  -> doctype
//     parent has a doctype child, child is non-null and an element is preceding
//     child, or child is null and parent has an element child.
//
// 6. If parent is a document, and any of the statements below, switched on
// node, are true, throw a HierarchyRequestError.
//  -> DocumentFragment node
//     If node has more than one element child or has a Text node child.
//     Otherwise, if node has one element child and either parent has an element
//     child that is not child or a doctype is following child.
//  -> element
//     parent has an element child that is not child or a doctype is following
//     child.
//  -> doctype
//     parent has a doctype child that is not child, or an element is preceding
//     child.
bool Document::CanAcceptChild(const Node& new_child,
                              const Node* next,
                              const Node* old_child,
                              ExceptionState& exception_state) const {
  DCHECK(!(next && old_child));
  if (old_child && old_child->getNodeType() == new_child.getNodeType())
    return true;

  int num_doctypes = 0;
  int num_elements = 0;
  bool has_doctype_after_reference_node = false;
  bool has_element_after_reference_node = false;

  // First, check how many doctypes and elements we have, not counting
  // the child we're about to remove.
  bool saw_reference_node = false;
  for (Node& child : NodeTraversal::ChildrenOf(*this)) {
    if (old_child && *old_child == child) {
      saw_reference_node = true;
      continue;
    }
    if (&child == next)
      saw_reference_node = true;

    switch (child.getNodeType()) {
      case kDocumentTypeNode:
        num_doctypes++;
        has_doctype_after_reference_node = saw_reference_node;
        break;
      case kElementNode:
        num_elements++;
        has_element_after_reference_node = saw_reference_node;
        break;
      default:
        break;
    }
  }

  // Then, see how many doctypes and elements might be added by the new child.
  if (auto* new_child_fragment = DynamicTo<DocumentFragment>(new_child)) {
    for (Node& child : NodeTraversal::ChildrenOf(*new_child_fragment)) {
      switch (child.getNodeType()) {
        case kAttributeNode:
        case kCdataSectionNode:
        case kDocumentFragmentNode:
        case kDocumentNode:
        case kTextNode:
          exception_state.ThrowDOMException(
              DOMExceptionCode::kHierarchyRequestError,
              "Nodes of type '" + new_child.nodeName() +
                  "' may not be inserted inside nodes of type '#document'.");
          return false;
        case kCommentNode:
        case kProcessingInstructionNode:
          break;
        case kDocumentTypeNode:
          num_doctypes++;
          break;
        case kElementNode:
          num_elements++;
          if (has_doctype_after_reference_node) {
            exception_state.ThrowDOMException(
                DOMExceptionCode::kHierarchyRequestError,
                "Can't insert an element before a doctype.");
            return false;
          }
          break;
      }
    }
  } else {
    switch (new_child.getNodeType()) {
      case kAttributeNode:
      case kCdataSectionNode:
      case kDocumentFragmentNode:
      case kDocumentNode:
      case kTextNode:
        exception_state.ThrowDOMException(
            DOMExceptionCode::kHierarchyRequestError,
            "Nodes of type '" + new_child.nodeName() +
                "' may not be inserted inside nodes of type '#document'.");
        return false;
      case kCommentNode:
      case kProcessingInstructionNode:
        return true;
      case kDocumentTypeNode:
        num_doctypes++;
        if (num_elements > 0 && !has_element_after_reference_node) {
          exception_state.ThrowDOMException(
              DOMExceptionCode::kHierarchyRequestError,
              "Can't insert a doctype before the root element.");
          return false;
        }
        break;
      case kElementNode:
        num_elements++;
        if (has_doctype_after_reference_node) {
          exception_state.ThrowDOMException(
              DOMExceptionCode::kHierarchyRequestError,
              "Can't insert an element before a doctype.");
          return false;
        }
        break;
    }
  }

  if (num_elements > 1 || num_doctypes > 1) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kHierarchyRequestError,
        String::Format("Only one %s on document allowed.",
                       num_elements > 1 ? "element" : "doctype"));
    return false;
  }

  return true;
}

Node* Document::Clone(Document& factory, CloneChildrenFlag flag) const {
  DCHECK_EQ(this, &factory)
      << "Document::Clone() doesn't support importNode mode.";

  if (!execution_context_)
    return nullptr;
  Document* clone = CloneDocumentWithoutChildren();
  clone->CloneDataFromDocument(*this);
  if (flag != CloneChildrenFlag::kSkip)
    clone->CloneChildNodesFrom(*this, flag);
  return clone;
}

ResizeObserver& Document::EnsureResizeObserver() {
  if (!intrinsic_size_observer_) {
    intrinsic_size_observer_ = ResizeObserver::Create(
        domWindow(),
        MakeGarbageCollected<IntrinsicSizeResizeObserverDelegate>());
  }
  return *intrinsic_size_observer_;
}

void Document::ObserveForIntrinsicSize(Element* element) {
  // Defaults to content-box, which is what we want.
  EnsureResizeObserver().observe(element);
}

void Document::UnobserveForIntrinsicSize(Element* element) {
  if (intrinsic_size_observer_)
    intrinsic_size_observer_->unobserve(element);
}

Document* Document::CloneDocumentWithoutChildren() const {
  DocumentInit init = DocumentInit::Create()
                          .WithExecutionContext(execution_context_.Get())
                          .WithURL(Url());
  if (IsA<XMLDocument>(this)) {
    if (IsXHTMLDocument())
      return XMLDocument::CreateXHTML(init);
    return MakeGarbageCollected<XMLDocument>(init);
  }
  return MakeGarbageCollected<Document>(init);
}

void Document::CloneDataFromDocument(const Document& other) {
  SetCompatibilityMode(other.GetCompatibilityMode());
  SetEncodingData(other.encoding_data_);
  SetContextFeatures(other.GetContextFeatures());
  SetMimeType(other.contentType());
}

StyleSheetList& Document::StyleSheets() {
  if (!style_sheet_list_)
    style_sheet_list_ = MakeGarbageCollected<StyleSheetList>(this);
  return *style_sheet_list_;
}

void Document::EvaluateMediaQueryListIfNeeded() {
  if (!evaluate_media_queries_on_style_recalc_)
    return;
  EvaluateMediaQueryList();
  evaluate_media_queries_on_style_recalc_ = false;
}

void Document::EvaluateMediaQueryList() {
  if (media_query_matcher_)
    media_query_matcher_->MediaFeaturesChanged();
}

void Document::LayoutViewportWasResized() {
  MediaQueryAffectingValueChanged(MediaValueChange::kSize);
  if (media_query_matcher_)
    media_query_matcher_->ViewportChanged();
  // We need to be careful not to trigger a resize event when setting the
  // initial layout size. It might seem like the correct check should be
  // (load_event_progress_ >= kLoadEventInProgress), but that doesn't actually
  // work because the initial value of load_event_progress_ is
  // kLoadEventCompleted. DidFirstLayout() is a reliable indicator that the load
  // event *actually* completed; but we also need to fire a resize event if the
  // window size changes during load event dispatch.
  if (View()->DidFirstLayout() ||
      load_event_progress_ == kLoadEventInProgress) {
    EnqueueResizeEvent();
    EnqueueVisualViewportResizeEvent();
    if (GetFrame()->IsMainFrame() && !Printing())
      probe::DidResizeMainFrame(GetFrame());
  }
  if (!HasStaticViewportUnits())
    return;
  GetStyleResolver().SetResizedForViewportUnits();
  GetStyleEngine().MarkViewportUnitDirty(ViewportUnitFlag::kStatic);
  GetStyleEngine().MarkViewportUnitDirty(ViewportUnitFlag::kDynamic);
}

void Document::DynamicViewportUnitsChanged() {
  if (!RuntimeEnabledFeatures::CSSViewportUnits4Enabled())
    return;
  MediaQueryAffectingValueChanged(MediaValueChange::kDynamicViewport);
  if (media_query_matcher_)
    media_query_matcher_->DynamicViewportChanged();
  if (!HasDynamicViewportUnits())
    return;
  GetStyleResolver().SetResizedForViewportUnits();
  GetStyleEngine().MarkViewportUnitDirty(ViewportUnitFlag::kDynamic);
}

void Document::SetHoverElement(Element* new_hover_element) {
  hover_element_ = new_hover_element;
}

void Document::SetActiveElement(Element* new_active_element) {
  if (!new_active_element) {
    active_element_.Clear();
    return;
  }

  active_element_ = new_active_element;
}

void Document::RemoveFocusedElementOfSubtree(Node& node,
                                             bool among_children_only) {
  if (!focused_element_)
    return;

  // We can't be focused if we're not in the document.
  if (!node.isConnected())
    return;
  bool contains = node.IsShadowIncludingInclusiveAncestorOf(*focused_element_);
  if (contains && (focused_element_ != &node || !among_children_only))
    ClearFocusedElement();
}

static Element* SkipDisplayNoneAncestors(Element* element) {
  for (; element; element = FlatTreeTraversal::ParentElement(*element)) {
    if (element->GetLayoutObject() || element->HasDisplayContentsStyle())
      return element;
  }
  return nullptr;
}

static Element* SkipDisplayNoneAncestorsOrReturnNullIfFlatTreeIsDirty(
    Element& element) {
  if (element.GetDocument().IsSlotAssignmentDirty()) {
    // We shouldn't use FlatTreeTraversal during detach if slot assignment is
    // dirty because it might trigger assignment recalc. The hover and active
    // elements are then set to null. The hover element is updated on the next
    // lifecycle update instead.
    //
    // TODO(crbug.com/939769): The active element is not updated on the next
    // lifecycle update, and is generally not correctly updated on re-slotting.
    return nullptr;
  }
  return SkipDisplayNoneAncestors(&element);
}

void Document::HoveredElementDetached(Element& element) {
  if (!hover_element_)
    return;
  if (element != hover_element_)
    return;
  hover_element_ =
      SkipDisplayNoneAncestorsOrReturnNullIfFlatTreeIsDirty(element);

  // If the mouse cursor is not visible, do not clear existing
  // hover effects on the ancestors of |element| and do not invoke
  // new hover effects on any other element.
  if (!GetPage()->IsCursorVisible())
    return;

  if (GetFrame())
    GetFrame()->GetEventHandler().ScheduleHoverStateUpdate();
}

void Document::ActiveChainNodeDetached(Element& element) {
  if (active_element_ && element == active_element_) {
    active_element_ =
        SkipDisplayNoneAncestorsOrReturnNullIfFlatTreeIsDirty(element);
  }
}

const Vector<AnnotatedRegionValue>& Document::AnnotatedRegions() const {
  return annotated_regions_;
}

void Document::SetAnnotatedRegions(
    const Vector<AnnotatedRegionValue>& regions) {
  annotated_regions_ = regions;
  SetAnnotatedRegionsDirty(false);
}

void Document::SetLastFocusType(mojom::blink::FocusType last_focus_type) {
  last_focus_type_ = last_focus_type;
}

bool Document::SetFocusedElement(Element* new_focused_element,
                                 const FocusParams& params) {
  DCHECK(!lifecycle_.InDetach());

  clear_focused_element_timer_.Stop();

  // Make sure new_focused_element is actually in this document.
  if (new_focused_element) {
    if (new_focused_element->GetDocument() != this)
      return true;

    if (NodeChildRemovalTracker::IsBeingRemoved(*new_focused_element))
      return true;
  }

  if (focused_element_ == new_focused_element)
    return true;

  bool focus_change_blocked = false;
  Element* old_focused_element = focused_element_;
  focused_element_ = nullptr;

  Node* ancestor = (old_focused_element && old_focused_element->isConnected() &&
                    new_focused_element)
                       ? FlatTreeTraversal::CommonAncestor(*old_focused_element,
                                                           *new_focused_element)
                       : nullptr;

  // Remove focus from the existing focus node (if any)
  if (old_focused_element) {
    old_focused_element->SetFocused(false, params.type);
    old_focused_element->SetHasFocusWithinUpToAncestor(false, ancestor);

    DisplayLockUtilities::ElementLostFocus(old_focused_element);

    // Dispatch the blur event and let the node do any other blur related
    // activities (important for text fields)
    // If page lost focus, blur event will have already been dispatched
    if (!params.omit_blur_events && GetPage() &&
        (GetPage()->GetFocusController().IsFocused())) {
      old_focused_element->DispatchBlurEvent(new_focused_element, params.type,
                                             params.source_capabilities);
      if (focused_element_) {
        // handler shifted focus
        focus_change_blocked = true;
        new_focused_element = nullptr;
      }

      // 'focusout' is a DOM level 3 name for the bubbling blur event.
      old_focused_element->DispatchFocusOutEvent(event_type_names::kFocusout,
                                                 new_focused_element,
                                                 params.source_capabilities);
      // 'DOMFocusOut' is a DOM level 2 name for compatibility.
      // FIXME: We should remove firing DOMFocusOutEvent event when we are sure
      // no content depends on it, probably when <rdar://problem/8503958> is
      // resolved.
      old_focused_element->DispatchFocusOutEvent(event_type_names::kDOMFocusOut,
                                                 new_focused_element,
                                                 params.source_capabilities);

      if (focused_element_) {
        // handler shifted focus
        focus_change_blocked = true;
        new_focused_element = nullptr;
      }
    }
  }

  // Blur/focusout handlers could have moved the new element out of this
  // document. See crbug.com/1204223.
  if (new_focused_element && new_focused_element->GetDocument() != this)
    return true;

  if (new_focused_element)
    UpdateStyleAndLayoutTreeForNode(new_focused_element);
  if (new_focused_element && new_focused_element->IsFocusable()) {
    if (IsRootEditableElement(*new_focused_element) &&
        !AcceptsEditingFocus(*new_focused_element)) {
      // delegate blocks focus change
      UpdateStyleAndLayoutTree();
      if (LocalFrame* frame = GetFrame())
        frame->Selection().DidChangeFocus();
      return false;
    }
    // Set focus on the new node
    focused_element_ = new_focused_element;
    SetSequentialFocusNavigationStartingPoint(focused_element_.Get());

    // Keep track of last focus from user interaction, ignoring focus from code
    // and other non-user internal interventions.
    if (params.type != mojom::blink::FocusType::kNone &&
        params.type != mojom::blink::FocusType::kScript)
      SetLastFocusType(params.type);

    for (auto& observer : focused_element_change_observers_)
      observer->DidChangeFocus();

    focused_element_->SetFocused(true, params.type);
    // Setting focus can cause the element to become detached (e.g. if an
    // ancestor element's onblur removes it), so return early here if that's
    // happened.
    if (focused_element_ == nullptr) {
      return false;
    }
    focused_element_->SetHasFocusWithinUpToAncestor(true, ancestor);
    DisplayLockUtilities::ElementGainedFocus(focused_element_.Get());

    // Element::setFocused for frames can dispatch events.
    if (focused_element_ != new_focused_element) {
      UpdateStyleAndLayoutTree();
      if (LocalFrame* frame = GetFrame())
        frame->Selection().DidChangeFocus();
      return false;
    }
    SetShouldUpdateSelectionAfterLayout(false);
    EnsurePaintLocationDataValidForNode(focused_element_,
                                        DocumentUpdateReason::kFocus);
    focused_element_->UpdateSelectionOnFocus(params.selection_behavior,
                                             params.options);

    // Dispatch the focus event and let the node do any other focus related
    // activities (important for text fields)
    // If page lost focus, event will be dispatched on page focus, don't
    // duplicate
    if (GetPage() && (GetPage()->GetFocusController().IsFocused())) {
      focused_element_->DispatchFocusEvent(old_focused_element, params.type,
                                           params.source_capabilities);

      if (focused_element_ != new_focused_element) {
        // handler shifted focus
        UpdateStyleAndLayoutTree();
        if (LocalFrame* frame = GetFrame())
          frame->Selection().DidChangeFocus();
        return false;
      }
      // DOM level 3 bubbling focus event.
      focused_element_->DispatchFocusInEvent(event_type_names::kFocusin,
                                             old_focused_element, params.type,
                                             params.source_capabilities);

      if (focused_element_ != new_focused_element) {
        // handler shifted focus
        UpdateStyleAndLayoutTree();
        if (LocalFrame* frame = GetFrame())
          frame->Selection().DidChangeFocus();
        return false;
      }

      // For DOM level 2 compatibility.
      // FIXME: We should remove firing DOMFocusInEvent event when we are sure
      // no content depends on it, probably when <rdar://problem/8503958> is m.
      focused_element_->DispatchFocusInEvent(event_type_names::kDOMFocusIn,
                                             old_focused_element, params.type,
                                             params.source_capabilities);

      if (focused_element_ != new_focused_element) {
        // handler shifted focus
        UpdateStyleAndLayoutTree();
        if (LocalFrame* frame = GetFrame())
          frame->Selection().DidChangeFocus();
        return false;
      }
    }
  }

  if (!focus_change_blocked) {
    NotifyFocusedElementChanged(old_focused_element, focused_element_.Get(),
                                params.type);
  }

  UpdateStyleAndLayoutTree();
  if (LocalFrame* frame = GetFrame())
    frame->Selection().DidChangeFocus();
  return !focus_change_blocked;
}

void Document::ClearFocusedElement() {
  SetFocusedElement(nullptr,
                    FocusParams(SelectionBehaviorOnFocus::kNone,
                                mojom::blink::FocusType::kNone, nullptr));
}

void Document::SendFocusNotification(Element* new_focused_element,
                                     mojom::blink::FocusType focus_type) {
  if (!GetPage())
    return;

  bool is_editable = false;
  gfx::Rect element_bounds_in_dips;
  if (new_focused_element) {
    auto* text_control = ToTextControlOrNull(new_focused_element);
    is_editable =
        IsEditable(*new_focused_element) ||
        (text_control && !text_control->IsDisabledOrReadOnly()) ||
        EqualIgnoringASCIICase(
            new_focused_element->FastGetAttribute(html_names::kRoleAttr),
            "textbox");
    gfx::Rect bounds_in_viewport;

    if (new_focused_element->IsSVGElement()) {
      // Convert to window coordinate system (this will be in DIPs).
      bounds_in_viewport = new_focused_element->BoundsInWidget();
    } else {
      Vector<gfx::Rect> outline_rects =
          new_focused_element->OutlineRectsInWidget(
              DocumentUpdateReason::kFocus);
      for (auto& outline_rect : outline_rects)
        bounds_in_viewport.Union(outline_rect);
    }

    if (GetFrame()->GetWidgetForLocalRoot()) {
      element_bounds_in_dips =
          GetFrame()->GetWidgetForLocalRoot()->BlinkSpaceToEnclosedDIPs(
              bounds_in_viewport);
    } else {
      element_bounds_in_dips = bounds_in_viewport;
    }
  }

  GetFrame()->GetLocalFrameHostRemote().FocusedElementChanged(
      is_editable, element_bounds_in_dips, focus_type);
}

void Document::NotifyFocusedElementChanged(Element* old_focused_element,
                                           Element* new_focused_element,
                                           mojom::blink::FocusType focus_type) {
  // |old_focused_element| may not belong to this document by invoking
  // adoptNode in event handlers during moving the focus to the new element.
  DCHECK(!new_focused_element || new_focused_element->GetDocument() == this);

  if (AXObjectCache* cache = ExistingAXObjectCache()) {
    cache->HandleFocusedUIElementChanged(old_focused_element,
                                         new_focused_element);
  }

  if (GetPage()) {
    GetPage()->GetValidationMessageClient().DidChangeFocusTo(
        new_focused_element);

    SendFocusNotification(new_focused_element, focus_type);

    Document* old_document =
        old_focused_element ? &old_focused_element->GetDocument() : nullptr;
    if (old_document && old_document != this && old_document->GetFrame())
      old_document->GetFrame()->Client()->FocusedElementChanged(nullptr);

    // Ensures that further text input state can be sent even when previously
    // focused input and the newly focused input share the exact same state.
    if (GetFrame()->GetWidgetForLocalRoot())
      GetFrame()->GetWidgetForLocalRoot()->ClearTextInputState();
    GetFrame()->Client()->FocusedElementChanged(new_focused_element);

    GetPage()->GetChromeClient().SetKeyboardFocusURL(new_focused_element);

    if (GetSettings()->GetSpatialNavigationEnabled())
      GetPage()->GetSpatialNavigationController().FocusedNodeChanged(this);
  }

  blink::NotifyPriorityScrollAnchorStatusChanged(old_focused_element,
                                                 new_focused_element);
}

// This forwards to the TreeScope implementation.
void Document::OnAdoptedStyleSheetSet(
    ScriptState* script_state,
    V8ObservableArrayCSSStyleSheet& observable_array,
    uint32_t index,
    Member<CSSStyleSheet>& sheet,
    ExceptionState& exception_state) {
  TreeScope::OnAdoptedStyleSheetSet(script_state, observable_array, index,
                                    sheet, exception_state);
}

// This forwards to the TreeScope implementation.
void Document::OnAdoptedStyleSheetDelete(
    ScriptState* script_state,
    V8ObservableArrayCSSStyleSheet& observable_array,
    uint32_t index,
    ExceptionState& exception_state) {
  TreeScope::OnAdoptedStyleSheetDelete(script_state, observable_array, index,
                                       exception_state);
}

void Document::SetSequentialFocusNavigationStartingPoint(Node* node) {
  if (!dom_window_)
    return;
  if (!node || node->GetDocument() != this) {
    sequential_focus_navigation_starting_point_ = nullptr;
    return;
  }
  if (!sequential_focus_navigation_starting_point_)
    sequential_focus_navigation_starting_point_ = Range::Create(*this);
  sequential_focus_navigation_starting_point_->selectNodeContents(
      node, ASSERT_NO_EXCEPTION);
}

Element* Document::SequentialFocusNavigationStartingPoint(
    mojom::blink::FocusType type) const {
  if (focused_element_)
    return focused_element_.Get();
  if (!sequential_focus_navigation_starting_point_)
    return nullptr;
  DCHECK(sequential_focus_navigation_starting_point_->IsConnected());
  if (!sequential_focus_navigation_starting_point_->collapsed()) {
    Node* node = sequential_focus_navigation_starting_point_->startContainer();
    DCHECK_EQ(node,
              sequential_focus_navigation_starting_point_->endContainer());
    if (auto* element = DynamicTo<Element>(node))
      return element;
    if (Element* neighbor_element = type == mojom::blink::FocusType::kForward
                                        ? ElementTraversal::Previous(*node)
                                        : ElementTraversal::Next(*node))
      return neighbor_element;
    return node->ParentOrShadowHostElement();
  }

  // Range::selectNodeContents didn't select contents because the element had
  // no children.
  auto* element = DynamicTo<Element>(
      sequential_focus_navigation_starting_point_->startContainer());
  if (element && !element->hasChildren() &&
      sequential_focus_navigation_starting_point_->startOffset() == 0)
    return element;

  // A node selected by Range::selectNodeContents was removed from the
  // document tree.
  if (Node* next_node =
          sequential_focus_navigation_starting_point_->FirstNode()) {
    if (next_node->IsShadowRoot())
      return next_node->OwnerShadowHost();
    // TODO(tkent): Using FlatTreeTraversal is inconsistent with
    // FocusController. Ideally we should find backward/forward focusable
    // elements before the starting point is disconnected. crbug.com/606582
    if (type == mojom::blink::FocusType::kForward) {
      Node* previous = FlatTreeTraversal::Previous(*next_node);
      for (; previous; previous = FlatTreeTraversal::Previous(*previous)) {
        if (auto* previous_element = DynamicTo<Element>(previous))
          return previous_element;
      }
    }
    for (Node* next = next_node; next; next = FlatTreeTraversal::Next(*next)) {
      if (auto* next_element = DynamicTo<Element>(next))
        return next_element;
    }
  }
  return nullptr;
}

void Document::SetSelectorFragmentAnchorCSSTarget(Element* new_target) {
  SetCSSTarget(new_target);
  if (css_target_) {
    css_target_is_selector_fragment_ = true;
    css_target_->PseudoStateChanged(CSSSelector::kPseudoSelectorFragmentAnchor);
  }
}

void Document::SetCSSTarget(Element* new_target) {
  if (css_target_) {
    css_target_->PseudoStateChanged(CSSSelector::kPseudoTarget);
    if (css_target_is_selector_fragment_) {
      css_target_->PseudoStateChanged(
          CSSSelector::kPseudoSelectorFragmentAnchor);
    }
  }
  css_target_ = new_target;
  css_target_is_selector_fragment_ = false;
  if (css_target_)
    css_target_->PseudoStateChanged(CSSSelector::kPseudoTarget);
}

void Document::RegisterNodeList(const LiveNodeListBase* list) {
  node_lists_.Add(list, list->InvalidationType());
  if (list->IsRootedAtTreeScope())
    lists_invalidated_at_document_.insert(list);
}

void Document::UnregisterNodeList(const LiveNodeListBase* list) {
  node_lists_.Remove(list, list->InvalidationType());
  if (list->IsRootedAtTreeScope()) {
    DCHECK(lists_invalidated_at_document_.Contains(list));
    lists_invalidated_at_document_.erase(list);
  }
}

void Document::RegisterNodeListWithIdNameCache(const LiveNodeListBase* list) {
  node_lists_.Add(list, kInvalidateOnIdNameAttrChange);
}

void Document::UnregisterNodeListWithIdNameCache(const LiveNodeListBase* list) {
  node_lists_.Remove(list, kInvalidateOnIdNameAttrChange);
}

void Document::AttachNodeIterator(NodeIterator* ni) {
  node_iterators_.insert(ni);
}

void Document::DetachNodeIterator(NodeIterator* ni) {
  // The node iterator can be detached without having been attached if its root
  // node didn't have a document when the iterator was created, but has it now.
  node_iterators_.erase(ni);
}

void Document::MoveNodeIteratorsToNewDocument(Node& node,
                                              Document& new_document) {
  HeapHashSet<WeakMember<NodeIterator>> node_iterators_list = node_iterators_;
  for (NodeIterator* ni : node_iterators_list) {
    if (ni->root() == node) {
      DetachNodeIterator(ni);
      new_document.AttachNodeIterator(ni);
    }
  }
}

void Document::DidMoveTreeToNewDocument(const Node& root) {
  DCHECK_NE(root.GetDocument(), this);
  if (!ranges_.IsEmpty()) {
    AttachedRangeSet ranges = ranges_;
    for (Range* range : ranges)
      range->UpdateOwnerDocumentIfNeeded();
  }
  synchronous_mutation_observer_set_.ForEachObserver(
      [&](SynchronousMutationObserver* observer) {
        observer->DidMoveTreeToNewDocument(root);
      });
}

void Document::NodeChildrenWillBeRemoved(ContainerNode& container) {
  EventDispatchForbiddenScope assert_no_event_dispatch;
  for (Range* range : ranges_) {
    range->NodeChildrenWillBeRemoved(container);
    if (range == sequential_focus_navigation_starting_point_)
      range->FixupRemovedChildrenAcrossShadowBoundary(container);
  }

  for (NodeIterator* ni : node_iterators_) {
    for (Node& n : NodeTraversal::ChildrenOf(container))
      ni->NodeWillBeRemoved(n);
  }

  synchronous_mutation_observer_set_.ForEachObserver(
      [&](SynchronousMutationObserver* observer) {
        observer->NodeChildrenWillBeRemoved(container);
      });

  if (MayContainShadowRoots()) {
    for (Node& n : NodeTraversal::ChildrenOf(container))
      n.CheckSlotChangeBeforeRemoved();
  }
}

void Document::NodeWillBeRemoved(Node& n) {
  for (NodeIterator* ni : node_iterators_)
    ni->NodeWillBeRemoved(n);

  for (Range* range : ranges_) {
    range->NodeWillBeRemoved(n);
    if (range == sequential_focus_navigation_starting_point_)
      range->FixupRemovedNodeAcrossShadowBoundary(n);
  }

  synchronous_mutation_observer_set_.ForEachObserver(
      [&](SynchronousMutationObserver* observer) {
        observer->NodeWillBeRemoved(n);
      });

  if (MayContainShadowRoots())
    n.CheckSlotChangeBeforeRemoved();

  if (n.InActiveDocument())
    GetStyleEngine().NodeWillBeRemoved(n);
}

void Document::NotifyUpdateCharacterData(CharacterData* character_data,
                                         unsigned offset,
                                         unsigned old_length,
                                         unsigned new_length) {
  synchronous_mutation_observer_set_.ForEachObserver(
      [&](SynchronousMutationObserver* observer) {
        observer->DidUpdateCharacterData(character_data, offset, old_length,
                                         new_length);
      });
}

void Document::NotifyChangeChildren(
    const ContainerNode& container,
    const ContainerNode::ChildrenChange& change) {
  synchronous_mutation_observer_set_.ForEachObserver(
      [&](SynchronousMutationObserver* observer) {
        observer->DidChangeChildren(container, change);
      });
}

void Document::NotifyAttributeChanged(const Element& element,
                                      const QualifiedName& name,
                                      const AtomicString& old_value,
                                      const AtomicString& new_value) {
  synchronous_mutation_observer_set_.ForEachObserver(
      [&](SynchronousMutationObserver* observer) {
        observer->AttributeChanged(element, name, old_value, new_value);
      });
}

void Document::DidInsertText(const CharacterData& text,
                             unsigned offset,
                             unsigned length) {
  for (Range* range : ranges_)
    range->DidInsertText(text, offset, length);
}

void Document::DidRemoveText(const CharacterData& text,
                             unsigned offset,
                             unsigned length) {
  for (Range* range : ranges_)
    range->DidRemoveText(text, offset, length);
}

void Document::DidMergeTextNodes(const Text& merged_node,
                                 const Text& node_to_be_removed,
                                 unsigned old_length) {
  NodeWithIndex node_to_be_removed_with_index(
      const_cast<Text&>(node_to_be_removed));
  if (!ranges_.IsEmpty()) {
    for (Range* range : ranges_)
      range->DidMergeTextNodes(node_to_be_removed_with_index, old_length);
  }

  synchronous_mutation_observer_set_.ForEachObserver(
      [&](SynchronousMutationObserver* observer) {
        observer->DidMergeTextNodes(merged_node, node_to_be_removed_with_index,
                                    old_length);
      });

  // FIXME: This should update markers for spelling and grammar checking.
}

void Document::DidSplitTextNode(const Text& old_node) {
  for (Range* range : ranges_)
    range->DidSplitTextNode(old_node);

  synchronous_mutation_observer_set_.ForEachObserver(
      [&](SynchronousMutationObserver* observer) {
        observer->DidSplitTextNode(old_node);
      });

  // FIXME: This should update markers for spelling and grammar checking.
}

void Document::SetWindowAttributeEventListener(const AtomicString& event_type,
                                               EventListener* listener) {
  LocalDOMWindow* dom_window = domWindow();
  if (!dom_window)
    return;
  dom_window->SetAttributeEventListener(event_type, listener);
}

EventListener* Document::GetWindowAttributeEventListener(
    const AtomicString& event_type) {
  LocalDOMWindow* dom_window = domWindow();
  if (!dom_window)
    return nullptr;
  return dom_window->GetAttributeEventListener(event_type);
}

void Document::EnqueueDisplayLockActivationTask(base::OnceClosure task) {
  scripted_animation_controller_->EnqueueTask(std::move(task));
}

void Document::EnqueueAnimationFrameTask(base::OnceClosure task) {
  scripted_animation_controller_->EnqueueTask(std::move(task));
}

void Document::EnqueueAnimationFrameEvent(Event* event) {
  scripted_animation_controller_->EnqueueEvent(event);
}

void Document::EnqueueUniqueAnimationFrameEvent(Event* event) {
  scripted_animation_controller_->EnqueuePerFrameEvent(event);
}

void Document::EnqueueScrollEventForNode(Node* target) {
  // Per the W3C CSSOM View Module only scroll events fired at the document
  // should bubble.
  overscroll_accumulated_delta_x_ = overscroll_accumulated_delta_y_ = 0;
  Event* scroll_event = target->IsDocumentNode()
                            ? Event::CreateBubble(event_type_names::kScroll)
                            : Event::Create(event_type_names::kScroll);
  scroll_event->SetTarget(target);
  scripted_animation_controller_->EnqueuePerFrameEvent(scroll_event);
}

void Document::EnqueueScrollEndEventForNode(Node* target) {
  // Mimic bubbling behavior of scroll event for consistency.
  overscroll_accumulated_delta_x_ = overscroll_accumulated_delta_y_ = 0;
  Event* scroll_end_event =
      target->IsDocumentNode()
          ? Event::CreateBubble(event_type_names::kScrollend)
          : Event::Create(event_type_names::kScrollend);
  scroll_end_event->SetTarget(target);
  scripted_animation_controller_->EnqueuePerFrameEvent(scroll_end_event);
}

void Document::EnqueueOverscrollEventForNode(Node* target,
                                             double delta_x,
                                             double delta_y) {
  // Mimic bubbling behavior of scroll event for consistency.
  overscroll_accumulated_delta_x_ += delta_x;
  overscroll_accumulated_delta_y_ += delta_y;
  bool bubbles = target->IsDocumentNode();
  Event* overscroll_event = OverscrollEvent::Create(
      event_type_names::kOverscroll, bubbles, overscroll_accumulated_delta_x_,
      overscroll_accumulated_delta_y_);
  overscroll_event->SetTarget(target);
  scripted_animation_controller_->EnqueuePerFrameEvent(overscroll_event);
}

void Document::EnqueueResizeEvent() {
  Event* event = Event::Create(event_type_names::kResize);
  event->SetTarget(domWindow());
  scripted_animation_controller_->EnqueuePerFrameEvent(event);
}

void Document::EnqueueMediaQueryChangeListeners(
    HeapVector<Member<MediaQueryListListener>>& listeners) {
  scripted_animation_controller_->EnqueueMediaQueryChangeListeners(listeners);
}

void Document::EnqueueVisualViewportScrollEvent() {
  VisualViewportScrollEvent* event =
      MakeGarbageCollected<VisualViewportScrollEvent>();
  event->SetTarget(domWindow()->visualViewport());
  scripted_animation_controller_->EnqueuePerFrameEvent(event);
}

void Document::EnqueueVisualViewportResizeEvent() {
  VisualViewportResizeEvent* event =
      MakeGarbageCollected<VisualViewportResizeEvent>();
  event->SetTarget(domWindow()->visualViewport());
  scripted_animation_controller_->EnqueuePerFrameEvent(event);
}

void Document::DispatchEventsForPrinting() {
  scripted_animation_controller_->DispatchEventsAndCallbacksForPrinting();
}

Document::EventFactorySet& Document::EventFactories() {
  DEFINE_STATIC_LOCAL(EventFactorySet, event_factory, ());
  return event_factory;
}

void Document::RegisterEventFactory(
    std::unique_ptr<EventFactoryBase> event_factory) {
  DCHECK(!EventFactories().Contains(event_factory.get()));
  EventFactories().insert(std::move(event_factory));
}

Event* Document::createEvent(ScriptState* script_state,
                             const String& event_type,
                             ExceptionState& exception_state) {
  Event* event = nullptr;
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  for (const auto& factory : EventFactories()) {
    event = factory->Create(execution_context, event_type);
    if (event) {
      // createEvent for TouchEvent should throw DOM exception if touch event
      // feature detection is not enabled. See crbug.com/392584#c22
      if (EqualIgnoringASCIICase(event_type, "TouchEvent") &&
          !RuntimeEnabledFeatures::TouchEventFeatureDetectionEnabled(
              execution_context))
        break;
      return event;
    }
  }
  exception_state.ThrowDOMException(
      DOMExceptionCode::kNotSupportedError,
      "The provided event type ('" + event_type + "') is invalid.");
  return nullptr;
}

void Document::AddMutationEventListenerTypeIfEnabled(
    ListenerType listener_type) {
  if (ContextFeatures::MutationEventsEnabled(this))
    AddListenerType(listener_type);
}

void Document::AddListenerTypeIfNeeded(const AtomicString& event_type,
                                       EventTarget& event_target) {
  if (event_type == event_type_names::kDOMSubtreeModified) {
    UseCounter::Count(*this, WebFeature::kDOMSubtreeModifiedEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMSubtreeModifiedListener);
  } else if (event_type == event_type_names::kDOMNodeInserted) {
    UseCounter::Count(*this, WebFeature::kDOMNodeInsertedEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMNodeInsertedListener);
  } else if (event_type == event_type_names::kDOMNodeRemoved) {
    UseCounter::Count(*this, WebFeature::kDOMNodeRemovedEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMNodeRemovedListener);
  } else if (event_type == event_type_names::kDOMNodeRemovedFromDocument) {
    UseCounter::Count(*this, WebFeature::kDOMNodeRemovedFromDocumentEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMNodeRemovedFromDocumentListener);
  } else if (event_type == event_type_names::kDOMNodeInsertedIntoDocument) {
    UseCounter::Count(*this, WebFeature::kDOMNodeInsertedIntoDocumentEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMNodeInsertedIntoDocumentListener);
  } else if (event_type == event_type_names::kDOMCharacterDataModified) {
    UseCounter::Count(*this, WebFeature::kDOMCharacterDataModifiedEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMCharacterDataModifiedListener);
  } else if (event_type == event_type_names::kWebkitAnimationStart ||
             event_type == event_type_names::kAnimationstart) {
    AddListenerType(kAnimationStartListener);
  } else if (event_type == event_type_names::kWebkitAnimationEnd ||
             event_type == event_type_names::kAnimationend) {
    AddListenerType(kAnimationEndListener);
  } else if (event_type == event_type_names::kWebkitAnimationIteration ||
             event_type == event_type_names::kAnimationiteration) {
    AddListenerType(kAnimationIterationListener);
    if (View()) {
      // Need to re-evaluate time-to-effect-change for any running animations.
      View()->ScheduleAnimation();
    }
  } else if (event_type == event_type_names::kAnimationcancel) {
    AddListenerType(kAnimationCancelListener);
  } else if (event_type == event_type_names::kTransitioncancel) {
    AddListenerType(kTransitionCancelListener);
  } else if (event_type == event_type_names::kTransitionrun) {
    AddListenerType(kTransitionRunListener);
  } else if (event_type == event_type_names::kTransitionstart) {
    AddListenerType(kTransitionStartListener);
  } else if (event_type == event_type_names::kWebkitTransitionEnd ||
             event_type == event_type_names::kTransitionend) {
    AddListenerType(kTransitionEndListener);
  } else if (event_type == event_type_names::kScroll) {
    AddListenerType(kScrollListener);
  } else if (event_type == event_type_names::kLoad) {
    if (Node* node = event_target.ToNode()) {
      if (IsA<HTMLStyleElement>(*node)) {
        AddListenerType(kLoadListenerAtCapturePhaseOrAtStyleElement);
        return;
      }
    }
    if (event_target.HasCapturingEventListeners(event_type))
      AddListenerType(kLoadListenerAtCapturePhaseOrAtStyleElement);
  }
}

HTMLFrameOwnerElement* Document::LocalOwner() const {
  if (!GetFrame())
    return nullptr;
  // FIXME: This probably breaks the attempts to layout after a load is finished
  // in implicitClose(), and probably tons of other things...
  return GetFrame()->DeprecatedLocalOwner();
}

void Document::WillChangeFrameOwnerProperties(
    int margin_width,
    int margin_height,
    mojom::blink::ScrollbarMode scrollbar_mode,
    bool is_display_none,
    mojom::blink::ColorScheme color_scheme) {
  DCHECK(GetFrame() && GetFrame()->Owner());
  FrameOwner* owner = GetFrame()->Owner();

  if (is_display_none != owner->IsDisplayNone())
    DisplayNoneChangedForFrame();
  // body() may become null as a result of modification event listeners, so we
  // check before each call.
  if (margin_width != owner->MarginWidth()) {
    if (auto* body_element = body()) {
      body_element->SetIntegralAttribute(html_names::kMarginwidthAttr,
                                         margin_width);
    }
  }
  if (margin_height != owner->MarginHeight()) {
    if (auto* body_element = body()) {
      body_element->SetIntegralAttribute(html_names::kMarginheightAttr,
                                         margin_height);
    }
  }
  if (scrollbar_mode != owner->ScrollbarMode() && View()) {
    View()->SetCanHaveScrollbars(scrollbar_mode !=
                                 mojom::blink::ScrollbarMode::kAlwaysOff);
    View()->SetNeedsLayout();
  }
  GetStyleEngine().SetOwnerColorScheme(color_scheme);
}

String Document::cookie(ExceptionState& exception_state) const {
  if (!dom_window_ || !GetSettings()->GetCookieEnabled())
    return String();

  CountUse(WebFeature::kCookieGet);

  if (!dom_window_->GetSecurityOrigin()->CanAccessCookies()) {
    if (dom_window_->IsSandboxed(
            network::mojom::blink::WebSandboxFlags::kOrigin)) {
      exception_state.ThrowSecurityError(
          "The document is sandboxed and lacks the 'allow-same-origin' flag.");
    } else if (Url().ProtocolIsData()) {
      exception_state.ThrowSecurityError(
          "Cookies are disabled inside 'data:' URLs.");
    } else {
      exception_state.ThrowSecurityError("Access is denied for this document.");
      // Count cookie accesses in opaque-origin documents from WebBundles.
      if (Url().ProtocolIs("uuid-in-package")) {
        CountUse(WebFeature::kUrnDocumentAccessedCookies);
      }
    }
    return String();
  } else if (dom_window_->GetSecurityOrigin()->IsLocal()) {
    CountUse(WebFeature::kFileAccessedCookies);
  }

  return cookie_jar_->Cookies();
}

void Document::setCookie(const String& value, ExceptionState& exception_state) {
  if (!dom_window_ || !GetSettings()->GetCookieEnabled())
    return;

  UseCounter::Count(*this, WebFeature::kCookieSet);

  if (!dom_window_->GetSecurityOrigin()->CanAccessCookies()) {
    if (dom_window_->IsSandboxed(
            network::mojom::blink::WebSandboxFlags::kOrigin)) {
      exception_state.ThrowSecurityError(
          "The document is sandboxed and lacks the 'allow-same-origin' flag.");
    } else if (Url().ProtocolIsData()) {
      exception_state.ThrowSecurityError(
          "Cookies are disabled inside 'data:' URLs.");
    } else {
      exception_state.ThrowSecurityError("Access is denied for this document.");
      // Count cookie accesses in opaque-origin documents from WebBundles.
      if (Url().ProtocolIs("uuid-in-package")) {
        CountUse(WebFeature::kUrnDocumentAccessedCookies);
      }
    }
    return;
  } else if (dom_window_->GetSecurityOrigin()->IsLocal()) {
    UseCounter::Count(*this, WebFeature::kFileAccessedCookies);
  }

  cookie_jar_->SetCookie(value);
}

bool Document::CookiesEnabled() const {
  if (!dom_window_)
    return false;
  // Compatible behavior in contexts that don't have cookie access.
  if (!dom_window_->GetSecurityOrigin()->CanAccessCookies())
    return true;
  return cookie_jar_->CookiesEnabled();
}

void Document::SetCookieManager(
    mojo::PendingRemote<network::mojom::blink::RestrictedCookieManager>
        cookie_manager) {
  cookie_jar_->SetCookieManager(std::move(cookie_manager));
}

const AtomicString& Document::referrer() const {
  if (Loader())
    return Loader()->GetReferrer();
  return g_null_atom;
}

String Document::domain() const {
  return GetExecutionContext()
             ? GetExecutionContext()->GetSecurityOrigin()->Domain()
             : String();
}

void Document::setDomain(const String& raw_domain,
                         ExceptionState& exception_state) {
  UseCounter::Count(*this, WebFeature::kDocumentSetDomain);

  if (!dom_window_) {
    exception_state.ThrowSecurityError(
        "A browsing context is required to set a domain.");
    return;
  }

  if (dom_window_->IsSandboxed(
          network::mojom::blink::WebSandboxFlags::kDocumentDomain)) {
    exception_state.ThrowSecurityError(
        dom_window_->GetFrame()->IsInFencedFrameTree()
            ? "Assignment is forbidden in a fenced frame tree."
            : "Assignment is forbidden for sandboxed iframes.");
    return;
  }

  const String permissions_policy_error =
      "Setting `document.domain` is disabled by permissions policy.";
  if (!dom_window_->IsFeatureEnabled(
          mojom::blink::PermissionsPolicyFeature::kDocumentDomain,
          ReportOptions::kReportOnFailure, permissions_policy_error)) {
    exception_state.ThrowSecurityError(permissions_policy_error);
    return;
  }

  const String document_policy_error =
      "Setting `document.domain` is disabled by document policy.";
  if (!dom_window_->IsFeatureEnabled(
          mojom::blink::DocumentPolicyFeature::kDocumentDomain,
          ReportOptions::kReportOnFailure, document_policy_error)) {
    return;
  }

  if (SchemeRegistry::IsDomainRelaxationForbiddenForURLScheme(
          dom_window_->GetSecurityOrigin()->Protocol())) {
    exception_state.ThrowSecurityError(
        "Assignment is forbidden for the '" +
        dom_window_->GetSecurityOrigin()->Protocol() + "' scheme.");
    return;
  }

  bool success = false;
  String new_domain = SecurityOrigin::CanonicalizeHost(raw_domain, &success);
  if (!success) {
    exception_state.ThrowSecurityError("'" + raw_domain +
                                       "' could not be parsed properly.");
    return;
  }

  if (new_domain.IsEmpty()) {
    exception_state.ThrowSecurityError("'" + new_domain +
                                       "' is an empty domain.");
    return;
  }

  scoped_refptr<SecurityOrigin> new_origin =
      dom_window_->GetSecurityOrigin()->IsolatedCopy();
  new_origin->SetDomainFromDOM(new_domain);
  OriginAccessEntry access_entry(
      *new_origin, network::mojom::CorsDomainMatchMode::kAllowSubdomains);
  network::cors::OriginAccessEntry::MatchResult result =
      access_entry.MatchesOrigin(*dom_window_->GetSecurityOrigin());
  if (result == network::cors::OriginAccessEntry::kDoesNotMatchOrigin) {
    exception_state.ThrowSecurityError(
        "'" + new_domain + "' is not a suffix of '" + domain() + "'.");
    return;
  }

  if (result ==
      network::cors::OriginAccessEntry::kMatchesOriginButIsPublicSuffix) {
    exception_state.ThrowSecurityError("'" + new_domain +
                                       "' is a top-level domain.");
    return;
  }

  // We technically only need to IsOriginKeyed(), as IsCrossOriginIsolated()
  // implies IsOriginKeyed(). (The spec only checks "is origin-keyed".) But,
  // we'll check both, in order to give warning messages that are more specific
  // about the cause. Note: this means the order of the checks is important.

  if (Agent::IsCrossOriginIsolated()) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kWarning,
        "document.domain mutation is ignored because the surrounding agent "
        "cluster is cross-origin isolated."));
    return;
  }

  if (RuntimeEnabledFeatures::OriginIsolationHeaderEnabled(dom_window_) &&
      dom_window_->GetAgent()->IsOriginKeyed()) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kWarning,
        "document.domain mutation is ignored because the surrounding agent "
        "cluster is origin-keyed."));
    return;
  }

  // TODO(crbug.com/1259920): Remove this check once the Origin-Agent-Cluster
  // default behaviour change has been default-enabled.
  if (base::FeatureList::IsEnabled(
          blink::features::kOriginAgentClusterDefaultWarning) &&
      Loader()) {
    const AtomicString& origin_agent_cluster_header =
        Loader()->GetResponse().HttpHeaderField(
            http_names::kOriginAgentCluster);
    if (origin_agent_cluster_header != "?0" &&
        origin_agent_cluster_header != "?1") {
      DCHECK(!dom_window_->GetAgent()->IsOriginKeyed());
      Deprecation::CountDeprecation(
          GetExecutionContext(),
          WebFeature::kDocumentDomainSettingWithoutOriginAgentClusterHeader);
      // No return; warning only.
    }
  }

  if (GetFrame()) {
    // This code should never fire for fenced frames because it should be
    // blocked by permission policy.
    DCHECK(!GetFrame()->IsInFencedFrameTree());
    UseCounter::Count(*this,
                      dom_window_->GetSecurityOrigin()->Port() == 0
                          ? WebFeature::kDocumentDomainSetWithDefaultPort
                          : WebFeature::kDocumentDomainSetWithNonDefaultPort);
    bool was_cross_origin_to_nearest_main_frame =
        GetFrame()->IsCrossOriginToNearestMainFrame();
    bool was_cross_origin_to_parent_frame =
        GetFrame()->IsCrossOriginToParentOrOuterDocument();
    dom_window_->GetMutableSecurityOrigin()->SetDomainFromDOM(new_domain);
    bool is_cross_origin_to_nearest_main_frame =
        GetFrame()->IsCrossOriginToNearestMainFrame();
    if (FrameScheduler* frame_scheduler = GetFrame()->GetFrameScheduler()) {
      frame_scheduler->SetCrossOriginToNearestMainFrame(
          is_cross_origin_to_nearest_main_frame);
    }
    if (View() && (was_cross_origin_to_nearest_main_frame !=
                   is_cross_origin_to_nearest_main_frame)) {
      View()->CrossOriginToNearestMainFrameChanged();
    }
    if (GetFrame()->IsMainFrame()) {
      // Notify descendants if their cross-origin-to-main-frame status changed.
      // TODO(pdr): This will notify even if
      // |Frame::IsCrossOriginToNearestMainFrame| is the same. Track whether
      // each child was cross-origin to main before and after changing the
      // domain, and only notify the changed ones.
      for (Frame* child = GetFrame()->Tree().FirstChild(); child;
           child = child->Tree().TraverseNext(GetFrame())) {
        auto* child_local_frame = DynamicTo<LocalFrame>(child);
        if (child_local_frame && child_local_frame->View())
          child_local_frame->View()->CrossOriginToNearestMainFrameChanged();
      }
    }

    if (View() && was_cross_origin_to_parent_frame !=
                      GetFrame()->IsCrossOriginToParentOrOuterDocument()) {
      View()->CrossOriginToParentFrameChanged();
    }
    // Notify all child frames if their cross-origin-to-parent status changed.
    // TODO(pdr): This will notify even if
    // |Frame::IsCrossOriginToParentOrOuterDocument| is the same. Track whether
    // each child was cross-origin-to-parent before and after changing the
    // domain, and only notify the changed ones.
    for (Frame* child = GetFrame()->Tree().FirstChild(); child;
         child = child->Tree().NextSibling()) {
      auto* child_local_frame = DynamicTo<LocalFrame>(child);
      if (child_local_frame && child_local_frame->View())
        child_local_frame->View()->CrossOriginToParentFrameChanged();
    }

    dom_window_->GetScriptController().UpdateSecurityOrigin(
        dom_window_->GetSecurityOrigin());
  }
}

absl::optional<base::Time> Document::lastModifiedTime() const {
  AtomicString http_last_modified = override_last_modified_;
  if (http_last_modified.IsEmpty()) {
    if (DocumentLoader* document_loader = Loader()) {
      http_last_modified = document_loader->GetResponse().HttpHeaderField(
          http_names::kLastModified);
    }
  }
  if (!http_last_modified.IsEmpty()) {
    return ParseDate(http_last_modified);
  }
  return absl::nullopt;
}

// https://html.spec.whatwg.org/C#dom-document-lastmodified
String Document::lastModified() const {
  const base::Time time = lastModifiedTime().value_or(base::Time::Now());
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  return String::Format("%02d/%02d/%04d %02d:%02d:%02d", exploded.month,
                        exploded.day_of_month, exploded.year, exploded.hour,
                        exploded.minute, exploded.second);
}

scoped_refptr<const SecurityOrigin> Document::TopFrameOrigin() const {
  if (!GetFrame())
    return scoped_refptr<const SecurityOrigin>();

  return GetFrame()->Tree().Top().GetSecurityContext()->GetSecurityOrigin();
}

net::SiteForCookies Document::SiteForCookies() const {
  if (!GetFrame())
    return net::SiteForCookies();

  Frame& top = GetFrame()->Tree().Top();
  const SecurityOrigin* origin = top.GetSecurityContext()->GetSecurityOrigin();
  // TODO(yhirano): Ideally |origin| should not be null here.
  if (!origin)
    return net::SiteForCookies();

  net::SiteForCookies candidate =
      net::SiteForCookies::FromOrigin(origin->ToUrlOrigin());

  if (SchemeRegistry::ShouldTreatURLSchemeAsFirstPartyWhenTopLevel(
          origin->Protocol())) {
    return candidate;
  }

  const Frame* current_frame = GetFrame();
  if (SchemeRegistry::
          ShouldTreatURLSchemeAsFirstPartyWhenTopLevelEmbeddingSecure(
              origin->Protocol(), current_frame->GetSecurityContext()
                                      ->GetSecurityOrigin()
                                      ->Protocol())) {
    return candidate;
  }

  while (current_frame) {
    const url::Origin cur_security_origin =
        current_frame->GetSecurityContext()->GetSecurityOrigin()->ToUrlOrigin();
    if (!candidate.CompareWithFrameTreeOriginAndRevise(cur_security_origin))
      return candidate;
    current_frame = current_frame->Tree().Parent();
  }

  return candidate;
}

mojom::blink::PermissionService* Document::GetPermissionService(
    ExecutionContext* execution_context) {
  if (!data_->permission_service_.is_bound()) {
    execution_context->GetBrowserInterfaceBroker().GetInterface(
        data_->permission_service_.BindNewPipeAndPassReceiver(
            execution_context->GetTaskRunner(TaskType::kPermission)));
    data_->permission_service_.set_disconnect_handler(WTF::Bind(
        &Document::PermissionServiceConnectionError, WrapWeakPersistent(this)));
  }
  return data_->permission_service_.get();
}

void Document::PermissionServiceConnectionError() {
  data_->permission_service_.reset();
}

ScriptPromise Document::hasStorageAccess(ScriptState* script_state) {
  const bool has_access =
      TopFrameOrigin() && GetExecutionContext() &&
      !GetExecutionContext()->GetSecurityOrigin()->IsOpaque() &&
      CookiesEnabled();
  ScriptPromiseResolver* resolver =
      MakeGarbageCollected<ScriptPromiseResolver>(script_state);

  ScriptPromise promise = resolver->Promise();
  resolver->Resolve(has_access);
  return promise;
}

ScriptPromise Document::requestStorageAccessForSite(ScriptState* script_state,
                                                    const AtomicString& site) {
  if (!GetFrame()) {
    // Note that in detached frames, resolvers are not able to return a promise.
    return ScriptPromise::RejectWithDOMException(
        script_state, MakeGarbageCollected<DOMException>(
                          DOMExceptionCode::kSecurityError,
                          "requestStorageAccessForSite: Cannot be used unless "
                          "the document is fully active."));
  }

  ScriptPromiseResolver* resolver =
      MakeGarbageCollected<ScriptPromiseResolver>(script_state);

  // Access the promise first to ensure it is created so that the proper state
  // can be changed when it is resolved or rejected.
  ScriptPromise promise = resolver->Promise();

  const bool has_user_gesture =
      LocalFrame::HasTransientUserActivation(GetFrame());
  if (!has_user_gesture) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        "requestStorageAccessForSite: Must be handling a user gesture to "
        "use."));

    resolver->Reject();
    return promise;
  }

  if (!IsInOutermostMainFrame()) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        "requestStorageAccessForSite: Only supported in primary top-level "
        "browsing contexts."));
    resolver->Reject();
    return promise;
  }

  if (dom_window_->GetSecurityOrigin()->IsOpaque()) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        "requestStorageAccessForSite: Cannot be used by opaque origins."));

    resolver->Reject();
    return promise;
  }

  KURL site_as_kurl{site};
  if (!site_as_kurl.IsValid()) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        "requestStorageAccessForSite: Invalid site parameter."));
    resolver->Reject();
    return promise;
  }

  scoped_refptr<SecurityOrigin> supplied_origin =
      SecurityOrigin::Create(site_as_kurl);
  if (supplied_origin->IsOpaque()) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        "requestStorageAccessForSite: Invalid site parameter."));
    resolver->Reject();
    return promise;
  }

  if (dom_window_->GetSecurityOrigin()->IsSameSiteWith(supplied_origin.get())) {
    // Access is not actually disabled, so accept the request.
    resolver->Resolve();
    return promise;
  }

  auto descriptor = mojom::blink::PermissionDescriptor::New();
  descriptor->name = mojom::blink::PermissionName::STORAGE_ACCESS;
  auto storage_access_extension =
      mojom::blink::StorageAccessPermissionDescriptor::New();
  storage_access_extension->siteOverride = supplied_origin;
  descriptor->extension =
      mojom::blink::PermissionDescriptorExtension::NewStorageAccess(
          std::move(storage_access_extension));

  GetPermissionService(ExecutionContext::From(script_state))
      ->RequestPermission(
          std::move(descriptor), has_user_gesture,
          WTF::Bind(
              [](ScriptPromiseResolver* resolver, Document* document,
                 mojom::blink::PermissionStatus status) {
                DCHECK(resolver);
                DCHECK(document);

                switch (status) {
                  case mojom::blink::PermissionStatus::GRANTED:
                    document->expressly_denied_storage_access_ = false;
                    resolver->Resolve();
                    break;
                  case mojom::blink::PermissionStatus::DENIED:
                    document->expressly_denied_storage_access_ = true;
                    [[fallthrough]];
                  case mojom::blink::PermissionStatus::ASK:
                  default:
                    resolver->Reject();
                }
              },
              WrapPersistent(resolver), WrapPersistent(this)));

  return promise;
}

ScriptPromise Document::requestStorageAccess(ScriptState* script_state) {
  if (!GetFrame()) {
    FireRequestStorageAccessHistogram(RequestStorageResult::REJECTED_NO_ORIGIN);

    // Note that in detached frames, resolvers are not able to return a promise.
    return ScriptPromise::RejectWithDOMException(
        script_state, MakeGarbageCollected<DOMException>(
                          DOMExceptionCode::kSecurityError,
                          "requestStorageAccess: Cannot be used unless the "
                          "document is fully active."));
  }

  ScriptPromiseResolver* resolver =
      MakeGarbageCollected<ScriptPromiseResolver>(script_state);

  // Access the promise first to ensure it is created so that the proper state
  // can be changed when it is resolved or rejected.
  ScriptPromise promise = resolver->Promise();

  const bool has_user_gesture =
      LocalFrame::HasTransientUserActivation(GetFrame());
  if (!has_user_gesture) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        "requestStorageAccess: Must be handling a user gesture to use."));
    FireRequestStorageAccessHistogram(
        RequestStorageResult::REJECTED_NO_USER_GESTURE);

    resolver->Reject();
    return promise;
  }

  if (dom_window_->GetSecurityOrigin()->IsOpaque()) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        "requestStorageAccess: Cannot be used by opaque origins."));
    FireRequestStorageAccessHistogram(
        RequestStorageResult::REJECTED_OPAQUE_ORIGIN);

    resolver->Reject();
    return promise;
  }

  if (dom_window_->IsSandboxed(network::mojom::blink::WebSandboxFlags::
                                   kStorageAccessByUserActivation)) {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kSecurity,
        mojom::blink::ConsoleMessageLevel::kError,
        dom_window_->GetFrame()->IsInFencedFrameTree()
            ? "requestStorageAccess: Refused to execute request. The document "
              "is in a fenced frame tree."
            : "requestStorageAccess: Refused to execute request. The document "
              "is sandboxed, and the 'allow-storage-access-by-user-activation' "
              "keyword is not set."));
    if (!dom_window_->GetFrame()->IsInFencedFrameTree()) {
      FireRequestStorageAccessHistogram(
          RequestStorageResult::REJECTED_SANDBOXED);
    }

    resolver->Reject();
    return promise;
  }

  if (CookiesEnabled()) {
    FireRequestStorageAccessHistogram(
        RequestStorageResult::APPROVED_EXISTING_ACCESS);

    // If there is current access to storage we no longer need to make a request
    // and can resolve the promise.
    resolver->Resolve();
    return promise;
  }

  if (expressly_denied_storage_access_) {
    FireRequestStorageAccessHistogram(
        RequestStorageResult::REJECTED_EXISTING_DENIAL);

    // If a previous rejection has been received the promise can be immediately
    // rejected without further action.
    resolver->Reject();
    return promise;
  }

  auto descriptor = mojom::blink::PermissionDescriptor::New();
  descriptor->name = mojom::blink::PermissionName::STORAGE_ACCESS;
  GetPermissionService(ExecutionContext::From(script_state))
      ->RequestPermission(
          std::move(descriptor), has_user_gesture,
          WTF::Bind(
              [](ScriptPromiseResolver* resolver, Document* document,
                 mojom::blink::PermissionStatus status) {
                DCHECK(resolver);
                DCHECK(document);

                switch (status) {
                  case mojom::blink::PermissionStatus::GRANTED:
                    document->expressly_denied_storage_access_ = false;
                    FireRequestStorageAccessHistogram(
                        RequestStorageResult::APPROVED_NEW_GRANT);
                    resolver->Resolve();
                    break;
                  case mojom::blink::PermissionStatus::DENIED:
                    LocalFrame::ConsumeTransientUserActivation(
                        document->GetFrame());
                    document->expressly_denied_storage_access_ = true;
                    [[fallthrough]];
                  case mojom::blink::PermissionStatus::ASK:
                  default:
                    FireRequestStorageAccessHistogram(
                        RequestStorageResult::REJECTED_GRANT_DENIED);
                    resolver->Reject();
                }
              },
              WrapPersistent(resolver), WrapPersistent(this)));

  return promise;
}

FragmentDirective& Document::fragmentDirective() const {
  return *fragment_directive_;
}

ScriptPromise Document::hasTrustToken(ScriptState* script_state,
                                      const String& issuer,
                                      ExceptionState& exception_state) {
  ScriptPromiseResolver* resolver =
      MakeGarbageCollected<ScriptPromiseResolver>(script_state);

  ScriptPromise promise = resolver->Promise();

  // Trust Tokens state is keyed by issuer and top-frame origins that
  // are both (1) HTTP or HTTPS and (2) potentially trustworthy. Consequently,
  // we can return early if either the issuer or the top-frame origin fails to
  // satisfy either of these requirements.
  KURL issuer_url = KURL(issuer);
  auto issuer_origin = SecurityOrigin::Create(issuer_url);
  if (!issuer_url.ProtocolIsInHTTPFamily() ||
      !issuer_origin->IsPotentiallyTrustworthy()) {
    exception_state.ThrowTypeError(
        "hasTrustToken: Trust token issuer origins must be both HTTP(S) and "
        "secure (\"potentially trustworthy\").");
    resolver->Reject(exception_state);
    return promise;
  }

  scoped_refptr<const SecurityOrigin> top_frame_origin = TopFrameOrigin();
  if (!top_frame_origin) {
    // Note: One case where there might be no top frame origin is if this
    // document is destroyed. In this case, this function will return
    // `undefined`. Still bother adding the exception and rejecting, just in
    // case there are other situations in which the top frame origin might be
    // absent.
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "hasTrustToken: Cannot execute in "
                                      "documents lacking top-frame origins.");
    resolver->Reject(exception_state);
    return promise;
  }

  DCHECK(top_frame_origin->IsPotentiallyTrustworthy());
  if (top_frame_origin->Protocol() != url::kHttpsScheme &&
      top_frame_origin->Protocol() != url::kHttpScheme) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotAllowedError,
        "hasTrustToken: Cannot execute in "
        "documents without secure, HTTP(S), top-frame origins.");
    resolver->Reject(exception_state);
    return promise;
  }

  if (!data_->trust_token_query_answerer_.is_bound()) {
    GetFrame()->GetBrowserInterfaceBroker().GetInterface(
        data_->trust_token_query_answerer_.BindNewPipeAndPassReceiver(
            GetExecutionContext()->GetTaskRunner(TaskType::kInternalDefault)));
    data_->trust_token_query_answerer_.set_disconnect_handler(
        WTF::Bind(&Document::TrustTokenQueryAnswererConnectionError,
                  WrapWeakPersistent(this)));
  }

  data_->pending_trust_token_query_resolvers_.insert(resolver);

  data_->trust_token_query_answerer_->HasTrustTokens(
      issuer_origin,
      WTF::Bind(
          [](WeakPersistent<ScriptPromiseResolver> resolver,
             WeakPersistent<Document> document,
             network::mojom::blink::HasTrustTokensResultPtr result) {
            // If there was a Mojo connection error, the promise was already
            // resolved and deleted.
            if (!base::Contains(
                    document->data_->pending_trust_token_query_resolvers_,
                    resolver)) {
              return;
            }

            if (result->status ==
                network::mojom::blink::TrustTokenOperationStatus::kOk) {
              resolver->Resolve(result->has_trust_tokens);
            } else {
              ScriptState* state = resolver->GetScriptState();
              ScriptState::Scope scope(state);
              resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
                  state->GetIsolate(), DOMExceptionCode::kOperationError,
                  "Failed to retrieve hasTrustToken response. (Would "
                  "associating the given issuer with this top-level origin "
                  "have exceeded its number-of-issuers limit?)"));
            }

            document->data_->pending_trust_token_query_resolvers_.erase(
                resolver);
          },
          WrapWeakPersistent(resolver), WrapWeakPersistent(this)));

  return promise;
}

ScriptPromise Document::hasRedemptionRecord(ScriptState* script_state,
                                            const String& issuer,
                                            ExceptionState& exception_state) {
  ScriptPromiseResolver* resolver =
      MakeGarbageCollected<ScriptPromiseResolver>(script_state);

  ScriptPromise promise = resolver->Promise();

  // Trust Tokens state is keyed by issuer and top-frame origins that
  // are both (1) HTTP or HTTPS and (2) potentially trustworthy. Consequently,
  // we can return early if either the issuer or the top-frame origin fails to
  // satisfy either of these requirements.
  KURL issuer_url = KURL(issuer);
  auto issuer_origin = SecurityOrigin::Create(issuer_url);
  if (!issuer_url.ProtocolIsInHTTPFamily() ||
      !issuer_origin->IsPotentiallyTrustworthy()) {
    exception_state.ThrowTypeError(
        "hasRedemptionRecord: Trust token issuer origins must be both HTTP(S) "
        "and "
        "secure (\"potentially trustworthy\").");
    resolver->Reject(exception_state);
    return promise;
  }

  scoped_refptr<const SecurityOrigin> top_frame_origin = TopFrameOrigin();
  if (!top_frame_origin) {
    // Note: One case where there might be no top frame origin is if this
    // document is destroyed. In this case, this function will return
    // `undefined`. Still bother adding the exception and rejecting, just in
    // case there are other situations in which the top frame origin might be
    // absent.
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "hasRedemptionRecord: Cannot execute in "
                                      "documents lacking top-frame origins.");
    resolver->Reject(exception_state);
    return promise;
  }

  DCHECK(top_frame_origin->IsPotentiallyTrustworthy());
  if (top_frame_origin->Protocol() != url::kHttpsScheme &&
      top_frame_origin->Protocol() != url::kHttpScheme) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotAllowedError,
        "hasRedemptionRecord: Cannot execute in "
        "documents without secure, HTTP(S), top-frame origins.");
    resolver->Reject(exception_state);
    return promise;
  }

  if (!data_->trust_token_query_answerer_.is_bound()) {
    GetFrame()->GetBrowserInterfaceBroker().GetInterface(
        data_->trust_token_query_answerer_.BindNewPipeAndPassReceiver(
            GetExecutionContext()->GetTaskRunner(TaskType::kInternalDefault)));
    data_->trust_token_query_answerer_.set_disconnect_handler(
        WTF::Bind(&Document::TrustTokenQueryAnswererConnectionError,
                  WrapWeakPersistent(this)));
  }

  data_->pending_trust_token_query_resolvers_.insert(resolver);

  data_->trust_token_query_answerer_->HasRedemptionRecord(
      issuer_origin,
      WTF::Bind(
          [](WeakPersistent<ScriptPromiseResolver> resolver,
             WeakPersistent<Document> document,
             network::mojom::blink::HasRedemptionRecordResultPtr result) {
            // If there was a Mojo connection error, the promise was already
            // resolved and deleted.
            if (!base::Contains(
                    document->data_->pending_trust_token_query_resolvers_,
                    resolver)) {
              return;
            }

            if (result->status ==
                network::mojom::blink::TrustTokenOperationStatus::kOk) {
              resolver->Resolve(result->has_redemption_record);
            } else {
              ScriptState* state = resolver->GetScriptState();
              ScriptState::Scope scope(state);
              resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
                  state->GetIsolate(), DOMExceptionCode::kOperationError,
                  "Failed to retrieve hasRedemptionRecord response. (Would "
                  "associating the given issuer with this top-level origin "
                  "have exceeded its number-of-issuers limit?)"));
            }

            document->data_->pending_trust_token_query_resolvers_.erase(
                resolver);
          },
          WrapWeakPersistent(resolver), WrapWeakPersistent(this)));

  return promise;
}

void Document::TrustTokenQueryAnswererConnectionError() {
  data_->trust_token_query_answerer_.reset();
  for (const auto& resolver : data_->pending_trust_token_query_resolvers_) {
    ScriptState* state = resolver->GetScriptState();
    ScriptState::Scope scope(state);
    resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
        state->GetIsolate(), DOMExceptionCode::kOperationError,
        "Internal error retrieving trust token response."));
  }
  data_->pending_trust_token_query_resolvers_.clear();
}

static bool IsValidNameNonASCII(const LChar* characters, unsigned length) {
  if (!IsValidNameStart(characters[0]))
    return false;

  for (unsigned i = 1; i < length; ++i) {
    if (!IsValidNamePart(characters[i]))
      return false;
  }

  return true;
}

static bool IsValidNameNonASCII(const UChar* characters, unsigned length) {
  for (unsigned i = 0; i < length;) {
    bool first = i == 0;
    UChar32 c;
    U16_NEXT(characters, i, length, c);  // Increments i.
    if (first ? !IsValidNameStart(c) : !IsValidNamePart(c))
      return false;
  }

  return true;
}

template <typename CharType>
static inline bool IsValidNameASCII(const CharType* characters,
                                    unsigned length) {
  CharType c = characters[0];
  if (!(IsASCIIAlpha(c) || c == ':' || c == '_'))
    return false;

  for (unsigned i = 1; i < length; ++i) {
    c = characters[i];
    if (!(IsASCIIAlphanumeric(c) || c == ':' || c == '_' || c == '-' ||
          c == '.'))
      return false;
  }

  return true;
}

bool Document::IsValidName(const StringView& name) {
  unsigned length = name.length();
  if (!length)
    return false;

  if (name.Is8Bit()) {
    const LChar* characters = name.Characters8();

    if (IsValidNameASCII(characters, length))
      return true;

    return IsValidNameNonASCII(characters, length);
  }

  const UChar* characters = name.Characters16();

  if (IsValidNameASCII(characters, length))
    return true;

  return IsValidNameNonASCII(characters, length);
}

enum QualifiedNameStatus {
  kQNValid,
  kQNMultipleColons,
  kQNInvalidStartChar,
  kQNInvalidChar,
  kQNEmptyPrefix,
  kQNEmptyLocalName
};

struct ParseQualifiedNameResult {
  QualifiedNameStatus status;
  UChar32 character;
  ParseQualifiedNameResult() = default;
  explicit ParseQualifiedNameResult(QualifiedNameStatus status)
      : status(status) {}
  ParseQualifiedNameResult(QualifiedNameStatus status, UChar32 character)
      : status(status), character(character) {}
};

template <typename CharType>
static ParseQualifiedNameResult ParseQualifiedNameInternal(
    const AtomicString& qualified_name,
    const CharType* characters,
    unsigned length,
    AtomicString& prefix,
    AtomicString& local_name) {
  bool name_start = true;
  bool saw_colon = false;
  unsigned colon_pos = 0;

  for (unsigned i = 0; i < length;) {
    UChar32 c;
    U16_NEXT(characters, i, length, c);
    if (c == ':') {
      if (saw_colon)
        return ParseQualifiedNameResult(kQNMultipleColons);
      name_start = true;
      saw_colon = true;
      colon_pos = i - 1;
    } else if (name_start) {
      if (!IsValidNameStart(c))
        return ParseQualifiedNameResult(kQNInvalidStartChar, c);
      name_start = false;
    } else {
      if (!IsValidNamePart(c))
        return ParseQualifiedNameResult(kQNInvalidChar, c);
    }
  }

  if (!saw_colon) {
    prefix = g_null_atom;
    local_name = qualified_name;
  } else {
    prefix = AtomicString(characters, colon_pos);
    if (prefix.IsEmpty())
      return ParseQualifiedNameResult(kQNEmptyPrefix);
    int prefix_start = colon_pos + 1;
    local_name = AtomicString(characters + prefix_start, length - prefix_start);
  }

  if (local_name.IsEmpty())
    return ParseQualifiedNameResult(kQNEmptyLocalName);

  return ParseQualifiedNameResult(kQNValid);
}

bool Document::ParseQualifiedName(const AtomicString& qualified_name,
                                  AtomicString& prefix,
                                  AtomicString& local_name,
                                  ExceptionState& exception_state) {
  unsigned length = qualified_name.length();

  if (!length) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidCharacterError,
                                      "The qualified name provided is empty.");
    return false;
  }

  ParseQualifiedNameResult return_value;
  if (qualified_name.Is8Bit())
    return_value =
        ParseQualifiedNameInternal(qualified_name, qualified_name.Characters8(),
                                   length, prefix, local_name);
  else
    return_value = ParseQualifiedNameInternal(qualified_name,
                                              qualified_name.Characters16(),
                                              length, prefix, local_name);
  if (return_value.status == kQNValid)
    return true;

  StringBuilder message;
  message.Append("The qualified name provided ('");
  message.Append(qualified_name);
  message.Append("') ");

  if (return_value.status == kQNMultipleColons) {
    message.Append("contains multiple colons.");
  } else if (return_value.status == kQNInvalidStartChar) {
    message.Append("contains the invalid name-start character '");
    message.Append(return_value.character);
    message.Append("'.");
  } else if (return_value.status == kQNInvalidChar) {
    message.Append("contains the invalid character '");
    message.Append(return_value.character);
    message.Append("'.");
  } else if (return_value.status == kQNEmptyPrefix) {
    message.Append("has an empty namespace prefix.");
  } else {
    DCHECK_EQ(return_value.status, kQNEmptyLocalName);
    message.Append("has an empty local name.");
  }

  exception_state.ThrowDOMException(DOMExceptionCode::kInvalidCharacterError,
                                    message.ReleaseString());
  return false;
}

void Document::SetEncodingData(const DocumentEncodingData& new_data) {
  // It's possible for the encoding of the document to change while we're
  // decoding data. That can only occur while we're processing the <head>
  // portion of the document. There isn't much user-visible content in the
  // <head>, but there is the <title> element. This function detects that
  // situation and re-decodes the document's title so that the user doesn't see
  // an incorrectly decoded title in the title bar.
  if (title_element_ && Encoding() != new_data.Encoding() &&
      !ElementTraversal::FirstWithin(*title_element_) &&
      Encoding() == Latin1Encoding() &&
      title_element_->textContent().ContainsOnlyLatin1OrEmpty()) {
    std::string original_bytes = title_element_->textContent().Latin1();
    std::unique_ptr<TextCodec> codec = NewTextCodec(new_data.Encoding());
    String correctly_decoded_title =
        codec->Decode(original_bytes.c_str(),
                      static_cast<wtf_size_t>(original_bytes.length()),
                      WTF::FlushBehavior::kDataEOF);
    title_element_->setTextContent(correctly_decoded_title);
  }

  DCHECK(new_data.Encoding().IsValid());
  encoding_data_ = new_data;

  // FIXME: Should be removed as part of
  // https://code.google.com/p/chromium/issues/detail?id=319643
  bool should_use_visual_ordering =
      encoding_data_.Encoding().UsesVisualOrdering();
  if (should_use_visual_ordering != visually_ordered_) {
    visually_ordered_ = should_use_visual_ordering;
    GetStyleEngine().MarkViewportStyleDirty();
    GetStyleEngine().MarkAllElementsForStyleRecalc(
        StyleChangeReasonForTracing::Create(
            style_change_reason::kVisuallyOrdered));
  }
}

KURL Document::CompleteURL(const String& url) const {
  return CompleteURLWithOverride(url, base_url_);
}

KURL Document::CompleteURLWithOverride(const String& url,
                                       const KURL& base_url_override) const {
  DCHECK(base_url_override.IsEmpty() || base_url_override.IsValid());

  // Always return a null URL when passed a null string.
  // FIXME: Should we change the KURL constructor to have this behavior?
  // See also [CSS]StyleSheet::completeURL(const String&)
  if (url.IsNull())
    return KURL();
  if (!Encoding().IsValid())
    return KURL(base_url_override, url);
  return KURL(base_url_override, url, Encoding());
}

// static
bool Document::ShouldInheritSecurityOriginFromOwner(const KURL& url) {
  // https://html.spec.whatwg.org/C/#origin
  //
  // If a Document is the initial "about:blank" document, the origin and
  // effective script origin of the Document are those it was assigned when its
  // browsing context was created.
  //
  // Note: We generalize this to all "blank" URLs and invalid URLs because we
  // treat all of these URLs as about:blank.  This is okay to do for
  // "about:mumble" because the Browser process will translate such URLs into
  // "about:blank#blocked".  This is necessary, because of practices pointed out
  // in https://crbug.com/1220186.
  return url.IsEmpty() || url.ProtocolIsAbout();
}

KURL Document::OpenSearchDescriptionURL() {
  static const char kOpenSearchMIMEType[] =
      "application/opensearchdescription+xml";
  static const char kOpenSearchRelation[] = "search";

  // FIXME: Why do only top-level frames have openSearchDescriptionURLs?
  if (!GetFrame() || GetFrame()->Tree().Parent())
    return KURL();

  // FIXME: Why do we need to wait for load completion?
  if (!LoadEventFinished())
    return KURL();

  if (!head())
    return KURL();

  for (HTMLLinkElement* link_element =
           Traversal<HTMLLinkElement>::FirstChild(*head());
       link_element;
       link_element = Traversal<HTMLLinkElement>::NextSibling(*link_element)) {
    if (!EqualIgnoringASCIICase(link_element->GetType(), kOpenSearchMIMEType) ||
        !EqualIgnoringASCIICase(link_element->Rel(), kOpenSearchRelation))
      continue;
    if (link_element->Href().IsEmpty())
      continue;

    // Count usage; perhaps we can lock this to secure contexts.
    WebFeature osd_disposition;
    scoped_refptr<const SecurityOrigin> target =
        SecurityOrigin::Create(link_element->Href());
    if (execution_context_->IsSecureContext()) {
      osd_disposition = target->IsPotentiallyTrustworthy()
                            ? WebFeature::kOpenSearchSecureOriginSecureTarget
                            : WebFeature::kOpenSearchSecureOriginInsecureTarget;
    } else {
      osd_disposition =
          target->IsPotentiallyTrustworthy()
              ? WebFeature::kOpenSearchInsecureOriginSecureTarget
              : WebFeature::kOpenSearchInsecureOriginInsecureTarget;
    }
    UseCounter::Count(*this, osd_disposition);

    return link_element->Href();
  }

  return KURL();
}

V8HTMLOrSVGScriptElement* Document::currentScriptForBinding() const {
  if (current_script_stack_.IsEmpty())
    return nullptr;
  ScriptElementBase* script_element_base = current_script_stack_.back();
  if (!script_element_base)
    return nullptr;
  return script_element_base->AsV8HTMLOrSVGScriptElement();
}

void Document::PushCurrentScript(ScriptElementBase* new_current_script) {
  current_script_stack_.push_back(new_current_script);
}

void Document::PopCurrentScript(ScriptElementBase* script) {
  DCHECK(!current_script_stack_.IsEmpty());
  DCHECK_EQ(current_script_stack_.back(), script);
  current_script_stack_.pop_back();
}

void Document::SetTransformSource(std::unique_ptr<TransformSource> source) {
  transform_source_ = std::move(source);
}

String Document::designMode() const {
  return InDesignMode() ? "on" : "off";
}

void Document::setDesignMode(const String& value) {
  bool new_value = design_mode_;
  if (EqualIgnoringASCIICase(value, "on")) {
    new_value = true;
    UseCounter::Count(*this, WebFeature::kDocumentDesignModeEnabeld);
  } else if (EqualIgnoringASCIICase(value, "off")) {
    new_value = false;
  }
  if (new_value == design_mode_)
    return;
  design_mode_ = new_value;
  GetStyleEngine().MarkViewportStyleDirty();
  GetStyleEngine().MarkAllElementsForStyleRecalc(
      StyleChangeReasonForTracing::Create(style_change_reason::kDesignMode));
}

Document* Document::ParentDocument() const {
  if (!GetFrame())
    return nullptr;
  auto* parent_local_frame = DynamicTo<LocalFrame>(GetFrame()->Tree().Parent());
  if (!parent_local_frame)
    return nullptr;
  return parent_local_frame->GetDocument();
}

Document& Document::TopDocument() const {
  // FIXME: Not clear what topDocument() should do in the OOPI case--should it
  // return the topmost available Document, or something else?
  Document* doc = const_cast<Document*>(this);
  for (HTMLFrameOwnerElement* element = doc->LocalOwner(); element;
       element = doc->LocalOwner())
    doc = &element->GetDocument();

  DCHECK(doc);
  return *doc;
}

ExecutionContext* Document::GetExecutionContext() const {
  return execution_context_.Get();
}

Attr* Document::createAttribute(const AtomicString& name,
                                ExceptionState& exception_state) {
  if (!IsValidName(name)) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidCharacterError,
                                      "The localName provided ('" + name +
                                          "') contains an invalid character.");
    return nullptr;
  }
  return MakeGarbageCollected<Attr>(
      *this, QualifiedName(g_null_atom, ConvertLocalName(name), g_null_atom),
      g_empty_atom);
}

Attr* Document::createAttributeNS(const AtomicString& namespace_uri,
                                  const AtomicString& qualified_name,
                                  ExceptionState& exception_state) {
  AtomicString prefix, local_name;
  if (!ParseQualifiedName(qualified_name, prefix, local_name, exception_state))
    return nullptr;

  QualifiedName q_name(prefix, local_name, namespace_uri);

  if (!HasValidNamespaceForAttributes(q_name)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNamespaceError,
        "The namespace URI provided ('" + namespace_uri +
            "') is not valid for the qualified name provided ('" +
            qualified_name + "').");
    return nullptr;
  }

  return MakeGarbageCollected<Attr>(*this, q_name, g_empty_atom);
}

const SVGDocumentExtensions* Document::SvgExtensions() const {
  return svg_extensions_.Get();
}

SVGDocumentExtensions& Document::AccessSVGExtensions() {
  if (!svg_extensions_)
    svg_extensions_ = MakeGarbageCollected<SVGDocumentExtensions>(this);
  return *svg_extensions_;
}

bool Document::HasSVGRootNode() const {
  return IsA<SVGSVGElement>(documentElement());
}

HTMLCollection* Document::images() {
  return EnsureCachedCollection<HTMLCollection>(kDocImages);
}

HTMLCollection* Document::applets() {
  return EnsureCachedCollection<HTMLCollection>(kDocApplets);
}

HTMLCollection* Document::embeds() {
  return EnsureCachedCollection<HTMLCollection>(kDocEmbeds);
}

HTMLCollection* Document::scripts() {
  return EnsureCachedCollection<HTMLCollection>(kDocScripts);
}

HTMLCollection* Document::links() {
  return EnsureCachedCollection<HTMLCollection>(kDocLinks);
}

HTMLCollection* Document::forms() {
  return EnsureCachedCollection<HTMLCollection>(kDocForms);
}

HTMLCollection* Document::anchors() {
  return EnsureCachedCollection<HTMLCollection>(kDocAnchors);
}

HTMLAllCollection* Document::all() {
  return EnsureCachedCollection<HTMLAllCollection>(kDocAll);
}

HTMLCollection* Document::WindowNamedItems(const AtomicString& name) {
  return EnsureCachedCollection<WindowNameCollection>(kWindowNamedItems, name);
}

DocumentNameCollection* Document::DocumentNamedItems(const AtomicString& name) {
  return EnsureCachedCollection<DocumentNameCollection>(kDocumentNamedItems,
                                                        name);
}

HTMLCollection* Document::DocumentAllNamedItems(const AtomicString& name) {
  return EnsureCachedCollection<DocumentAllNameCollection>(
      kDocumentAllNamedItems, name);
}

void Document::IncrementLazyAdsFrameCount() {
  data_->lazy_ads_frame_count_++;
}

void Document::IncrementLazyEmbedsFrameCount() {
  data_->lazy_embeds_frame_count_++;
}

void Document::IncrementImmediateChildFrameCreationCount() {
  data_->immediate_child_frame_creation_count_++;
}

int Document::GetImmediateChildFrameCreationCount() const {
  return data_->immediate_child_frame_creation_count_;
}

DOMWindow* Document::defaultView() const {
  return dom_window_;
}

using AllowState = blink::Document::DeclarativeShadowRootAllowState;
AllowState Document::GetDeclarativeShadowRootAllowState() const {
  return declarative_shadow_root_allow_state_;
}

void Document::setAllowDeclarativeShadowRoots(bool val) {
  declarative_shadow_root_allow_state_ =
      val ? AllowState::kAllow : AllowState::kDeny;
}

void Document::MaybeExecuteDelayedAsyncScripts(
    MilestoneForDelayedAsyncScript milestone) {
  // This is called on each paint when DelayAsyncScriptDelayType is kEachPaint,
  // which causes regression. Cache the feature status to avoid frequent
  // calculation.
  static const bool delay_async_script_execution_is_enabled =
      base::FeatureList::IsEnabled(features::kDelayAsyncScriptExecution);
  if (!delay_async_script_execution_is_enabled)
    return;

  // Cache for performance reason.
  static const features::DelayAsyncScriptDelayType
      delay_async_script_delay_type =
          features::kDelayAsyncScriptExecutionDelayParam.Get();
  switch (delay_async_script_delay_type) {
    case features::DelayAsyncScriptDelayType::kFirstPaintOrFinishedParsing:
      // Notify the ScriptRunner if the first paint has been recorded and
      // we're delaying async scripts until first paint or finished parsing
      // (whichever comes first).
      if (milestone == MilestoneForDelayedAsyncScript::kFirstPaint ||
          milestone == MilestoneForDelayedAsyncScript::kFinishedParsing) {
        script_runner_delayer_->Deactivate();
      }
      break;
    case features::DelayAsyncScriptDelayType::kFinishedParsing:
      // Notify the ScriptRunner if we're finished parsing and we're delaying
      // async scripts until finished parsing occurs.
      if (milestone == MilestoneForDelayedAsyncScript::kFinishedParsing)
        script_runner_delayer_->Deactivate();
      break;
    case features::DelayAsyncScriptDelayType::kEachLcpCandidate:
      // Notify the ScriptRunner if a LCP candidate is reported.
      if (milestone == MilestoneForDelayedAsyncScript::kLcpCandidate) {
        // Flush all async scripts that are already prepared but forced to be
        // delayed.
        script_runner_delayer_->Deactivate();
        // Delay async scripts until next LCP candidate occurs or reaches the
        // time limit.
        script_runner_delayer_->Activate();
      }
      break;
    case features::DelayAsyncScriptDelayType::kEachPaint:
      // Notify the ScriptRunner if paint happened.
      if (milestone == MilestoneForDelayedAsyncScript::kPaint) {
        // Flush all async scripts that are already prepared but forced to be
        // delayed.
        script_runner_delayer_->Deactivate();
        // Delay async scripts until next paint or reaches the time limit.
        script_runner_delayer_->Activate();
      }
      break;
  }
}

void Document::MarkFirstPaint() {
  MaybeExecuteDelayedAsyncScripts(MilestoneForDelayedAsyncScript::kFirstPaint);
}

void Document::OnPaintFinished() {
  MaybeExecuteDelayedAsyncScripts(MilestoneForDelayedAsyncScript::kPaint);
}

void Document::OnLargestContentfulPaintUpdated() {
  MaybeExecuteDelayedAsyncScripts(
      MilestoneForDelayedAsyncScript::kLcpCandidate);
}

void Document::OnPrepareToStopParsing() {
  MaybeExecuteDelayedAsyncScripts(
      MilestoneForDelayedAsyncScript::kFinishedParsing);
}

void Document::FinishedParsing() {
  DCHECK(!GetScriptableDocumentParser() || !parser_->IsParsing());
  DCHECK(!GetScriptableDocumentParser() || ready_state_ != kLoading);
  SetParsingState(kInDOMContentLoaded);
  DocumentParserTiming::From(*this).MarkParserStop();

  // FIXME: DOMContentLoaded is dispatched synchronously, but this should be
  // dispatched in a queued task, see https://crbug.com/961428
  if (document_timing_.DomContentLoadedEventStart().is_null())
    document_timing_.MarkDomContentLoadedEventStart();
  if (!ScriptForbiddenScope::IsScriptForbidden())
    DispatchEvent(*Event::CreateBubble(event_type_names::kDOMContentLoaded));
  if (document_timing_.DomContentLoadedEventEnd().is_null())
    document_timing_.MarkDomContentLoadedEventEnd();
  SetParsingState(kFinishedParsing);

  // Ensure Custom Element callbacks are drained before DOMContentLoaded.
  // FIXME: Remove this ad-hoc checkpoint when DOMContentLoaded is dispatched in
  // a queued task, which will do a checkpoint anyway. https://crbug.com/425790
  if (!ScriptForbiddenScope::IsScriptForbidden())
    Microtask::PerformCheckpoint(V8PerIsolateData::MainThreadIsolate());

  ScriptableDocumentParser* parser = GetScriptableDocumentParser();
  well_formed_ = parser && parser->WellFormed();

  if (LocalFrame* frame = GetFrame()) {
    // Guarantee at least one call to the client specifying a title. (If
    // |title_| is not empty, then the title has already been dispatched.)
    if (title_.IsEmpty())
      DispatchDidReceiveTitle();

    // Don't update the layout tree if we haven't requested the main resource
    // yet to avoid adding extra latency. Note that the first layout tree update
    // can be expensive since it triggers the parsing of the default stylesheets
    // which are compiled-in.
    // FrameLoader::finishedParsing() might end up calling
    // Document::implicitClose() if all resource loads are
    // complete. HTMLObjectElements can start loading their resources from post
    // attach callbacks triggered by recalcStyle().  This means if we parse out
    // an <object> tag and then reach the end of the document without updating
    // styles, we might not have yet started the resource load and might fire
    // the window load event too early.  To avoid this we force the styles to be
    // up to date before calling FrameLoader::finishedParsing().  See
    // https://bugs.webkit.org/show_bug.cgi?id=36864 starting around comment 35.
    if (!is_initial_empty_document_ && HaveRenderBlockingStylesheetsLoaded()) {
      UpdateStyleAndLayoutTree();
    }

    BeginLifecycleUpdatesIfRenderingReady();

    frame->Loader().FinishedParsing();

    if (parser) {
      if (SourceKeyedCachedMetadataHandler* metadata_handler =
              parser->GetInlineScriptCacheHandler()) {
        metadata_handler->LogUsageMetrics();
      }
    }

    if (ShouldMarkFontPerformance())
      FontPerformance::MarkDomContentLoaded();

    if (frame->IsAttached()) {
      DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT(
          "MarkDOMContent", inspector_mark_load_event::Data, frame);
      probe::DomContentLoadedEventFired(frame);
    }
    frame->GetIdlenessDetector()->DomContentLoadedEventFired();
  }

  // Schedule dropping of the ElementDataCache. We keep it alive for a while
  // after parsing finishes so that dynamically inserted content can also
  // benefit from sharing optimizations.  Note that we don't refresh the timer
  // on cache access since that could lead to huge caches being kept alive
  // indefinitely by something innocuous like JS setting .innerHTML repeatedly
  // on a timer.
  element_data_cache_clear_timer_.StartOneShot(base::Seconds(10), FROM_HERE);

  // Parser should have picked up all preloads by now
  fetcher_->ClearPreloads(ResourceFetcher::kClearSpeculativeMarkupPreloads);
}

void Document::ElementDataCacheClearTimerFired(TimerBase*) {
  element_data_cache_.Clear();
}

void Document::BeginLifecycleUpdatesIfRenderingReady() {
  if (!IsActive())
    return;
  if (!HaveRenderBlockingResourcesLoaded())
    return;
  rendering_has_begun_ = true;
  // TODO(japhet): If IsActive() is true, View() should always be non-null.
  // Speculative fix for https://crbug.com/1171891
  if (auto* view = View()) {
    view->BeginLifecycleUpdates();
  } else {
    NOTREACHED();
    base::debug::DumpWithoutCrashing();
  }
}

Vector<IconURL> Document::IconURLs(int icon_types_mask) {
  IconURL first_favicon;
  IconURL first_touch_icon;
  IconURL first_touch_precomposed_icon;
  Vector<IconURL> secondary_icons;

  using TraversalFunction = HTMLLinkElement* (*)(const Node&);
  TraversalFunction find_next_candidate =
      &Traversal<HTMLLinkElement>::NextSibling;

  HTMLLinkElement* first_element = nullptr;
  if (head()) {
    first_element = Traversal<HTMLLinkElement>::FirstChild(*head());
  } else if (IsSVGDocument() && IsA<SVGSVGElement>(documentElement())) {
    first_element = Traversal<HTMLLinkElement>::FirstWithin(*documentElement());
    find_next_candidate = &Traversal<HTMLLinkElement>::Next;
  }

  // Start from the first child node so that icons seen later take precedence as
  // required by the spec.
  for (HTMLLinkElement* link_element = first_element; link_element;
       link_element = find_next_candidate(*link_element)) {
    if (!((1 << static_cast<int>(link_element->GetIconType())) &
          icon_types_mask)) {
      continue;
    }
    if (link_element->Href().IsEmpty())
      continue;

    if (!link_element->Media().IsEmpty()) {
      auto* media_query =
          GetMediaQueryMatcher().MatchMedia(link_element->Media());
      if (!media_query->matches())
        continue;
    }

    IconURL new_url(link_element->Href(), link_element->IconSizes(),
                    link_element->GetType(), link_element->GetIconType());
    if (link_element->GetIconType() ==
        mojom::blink::FaviconIconType::kFavicon) {
      if (first_favicon.icon_type_ != mojom::blink::FaviconIconType::kInvalid)
        secondary_icons.push_back(first_favicon);
      first_favicon = new_url;
    } else if (link_element->GetIconType() ==
               mojom::blink::FaviconIconType::kTouchIcon) {
      if (first_touch_icon.icon_type_ !=
          mojom::blink::FaviconIconType::kInvalid)
        secondary_icons.push_back(first_touch_icon);
      first_touch_icon = new_url;
    } else if (link_element->GetIconType() ==
               mojom::blink::FaviconIconType::kTouchPrecomposedIcon) {
      if (first_touch_precomposed_icon.icon_type_ !=
          mojom::blink::FaviconIconType::kInvalid)
        secondary_icons.push_back(first_touch_precomposed_icon);
      first_touch_precomposed_icon = new_url;
    } else {
      NOTREACHED();
    }
  }

  Vector<IconURL> icon_urls;
  if (first_favicon.icon_type_ != mojom::blink::FaviconIconType::kInvalid) {
    icon_urls.push_back(first_favicon);
  } else if (url_.ProtocolIsInHTTPFamily() &&
             icon_types_mask & 1 << static_cast<int>(
                                   mojom::blink::FaviconIconType::kFavicon)) {
    IconURL default_favicon = IconURL::DefaultFavicon(url_);
    if (DefaultFaviconAllowedByCSP(this, default_favicon))
      icon_urls.push_back(std::move(default_favicon));
  }

  if (first_touch_icon.icon_type_ != mojom::blink::FaviconIconType::kInvalid)
    icon_urls.push_back(first_touch_icon);
  if (first_touch_precomposed_icon.icon_type_ !=
      mojom::blink::FaviconIconType::kInvalid)
    icon_urls.push_back(first_touch_precomposed_icon);
  for (int i = secondary_icons.size() - 1; i >= 0; --i)
    icon_urls.push_back(secondary_icons[i]);
  return icon_urls;
}

void Document::UpdateThemeColorCache() {
  meta_theme_color_elements_.clear();
  auto* root_element = documentElement();
  if (!root_element)
    return;

  for (HTMLMetaElement& meta_element :
       Traversal<HTMLMetaElement>::DescendantsOf(*root_element)) {
    if (EqualIgnoringASCIICase(meta_element.GetName(), "theme-color"))
      meta_theme_color_elements_.push_back(meta_element);
  }
}

absl::optional<Color> Document::ThemeColor() {
  // Returns the color of the first meta[name=theme-color] element in
  // tree order that matches and is valid.
  // https://html.spec.whatwg.org/multipage/semantics.html#meta-theme-color
  for (auto& element : meta_theme_color_elements_) {
    if (!element->Media().IsEmpty()) {
      auto* media_query = GetMediaQueryMatcher().MatchMedia(
          element->Media().GetString().StripWhiteSpace());
      if (!media_query->matches())
        continue;
    }
    Color color;
    if (CSSParser::ParseColor(
            color, element->Content().GetString().StripWhiteSpace(), true)) {
      return color;
    }
  }
  return absl::nullopt;
}

void Document::ColorSchemeMetaChanged() {
  const CSSValue* color_scheme = nullptr;
  if (auto* root_element = documentElement()) {
    for (HTMLMetaElement& meta_element :
         Traversal<HTMLMetaElement>::DescendantsOf(*root_element)) {
      if (EqualIgnoringASCIICase(meta_element.GetName(), "color-scheme")) {
        if ((color_scheme = CSSParser::ParseSingleValue(
                 CSSPropertyID::kColorScheme,
                 meta_element.Content().GetString().StripWhiteSpace(),
                 ElementSheet().Contents()->ParserContext()))) {
          break;
        }
      }
    }
  }
  GetStyleEngine().SetPageColorSchemes(color_scheme);
}

void Document::SupportsReducedMotionMetaChanged() {
  auto* root_element = documentElement();
  if (!root_element)
    return;

  bool supports_reduced_motion = false;
  for (HTMLMetaElement& meta_element :
       Traversal<HTMLMetaElement>::DescendantsOf(*root_element)) {
    if (EqualIgnoringASCIICase(meta_element.GetName(),
                               "supports-reduced-motion")) {
      SpaceSplitString split_content(
          AtomicString(meta_element.Content().GetString().LowerASCII()));
      if (split_content.Contains("reduce"))
        supports_reduced_motion = true;
      break;
    }
  }
  // TODO(crbug.com/1287263): Recreate existing interpolations.
  supports_reduced_motion_ = supports_reduced_motion;
}

bool Document::ShouldForceReduceMotion() const {
  if (!RuntimeEnabledFeatures::ForceReduceMotionEnabled(GetExecutionContext()))
    return false;

  return GetFrame()->GetSettings()->GetPrefersReducedMotion() &&
         !supports_reduced_motion_;
}

static HTMLLinkElement* GetLinkElement(const Document* doc,
                                       bool (*match_fn)(HTMLLinkElement&)) {
  HTMLHeadElement* head = doc->head();
  if (!head)
    return nullptr;

  // The first matching link element is used. Others are ignored.
  for (HTMLLinkElement& link_element :
       Traversal<HTMLLinkElement>::ChildrenOf(*head)) {
    if (match_fn(link_element))
      return &link_element;
  }
  return nullptr;
}

HTMLLinkElement* Document::LinkManifest() const {
  return GetLinkElement(this, [](HTMLLinkElement& link_element) {
    return link_element.RelAttribute().IsManifest();
  });
}

HTMLLinkElement* Document::LinkCanonical() const {
  return GetLinkElement(this, [](HTMLLinkElement& link_element) {
    return link_element.RelAttribute().IsCanonical();
  });
}

bool Document::AllowedToUseDynamicMarkUpInsertion(
    const char* api_name,
    ExceptionState& exception_state) {
  if (!RuntimeEnabledFeatures::ExperimentalPoliciesEnabled()) {
    return true;
  }
  if (!GetFrame() || GetExecutionContext()->IsFeatureEnabled(
                         mojom::blink::DocumentPolicyFeature::kDocumentWrite,
                         ReportOptions::kReportOnFailure)) {
    return true;
  }

  // TODO(ekaramad): Throwing an exception seems an ideal resolution to mishaps
  // in using the API against the policy. But this cannot be applied to cross-
  // origin as there are security risks involved. We should perhaps unload the
  // whole frame instead of throwing.
  exception_state.ThrowDOMException(
      DOMExceptionCode::kNotAllowedError,
      String::Format(
          "The use of method '%s' has been blocked by permissions policy. The "
          "feature "
          "'document-write' is disabled in this document.",
          api_name));
  return false;
}

ukm::UkmRecorder* Document::UkmRecorder() {
  if (ukm_recorder_)
    return ukm_recorder_.get();

  mojo::PendingRemote<ukm::mojom::UkmRecorderInterface> recorder;
  Platform::Current()->GetBrowserInterfaceBroker()->GetInterface(
      recorder.InitWithNewPipeAndPassReceiver());
  ukm_recorder_ = std::make_unique<ukm::MojoUkmRecorder>(std::move(recorder));

  return ukm_recorder_.get();
}

ukm::SourceId Document::UkmSourceID() const {
  return ukm_source_id_;
}

FontMatchingMetrics* Document::GetFontMatchingMetrics() {
  if (font_matching_metrics_)
    return font_matching_metrics_.get();
  font_matching_metrics_ = std::make_unique<FontMatchingMetrics>(
      IsInOutermostMainFrame(), UkmRecorder(), UkmSourceID(),
      GetTaskRunner(TaskType::kInternalDefault));
  return font_matching_metrics_.get();
}

bool Document::AllowInlineEventHandler(Node* node,
                                       EventListener* listener,
                                       const String& context_url,
                                       const WTF::OrdinalNumber& context_line) {
  auto* element = DynamicTo<Element>(node);
  // HTML says that inline script needs browsing context to create its execution
  // environment.
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/webappapis.html#event-handler-attributes
  // Also, if the listening node came from other document, which happens on
  // context-less event dispatching, we also need to ask the owner document of
  // the node.
  LocalDOMWindow* window = domWindow();
  if (!window)
    return false;

  // https://html.spec.whatwg.org/multipage/webappapis.html#event-handler-content-attributes
  // Step 5.1. If the Should element's inline behavior be blocked by Content
  // Security Policy? algorithm returns "Blocked" when executed upon element,
  // "script attribute", and value, then return. [CSP] [spec text]
  if (!window->GetContentSecurityPolicyForCurrentWorld()->AllowInline(
          ContentSecurityPolicy::InlineType::kScriptAttribute, element,
          listener->ScriptBody(), String() /* nonce */, context_url,
          context_line))
    return false;

  if (!window->CanExecuteScripts(kNotAboutToExecuteScript))
    return false;
  if (node && node->GetDocument() != this &&
      !node->GetDocument().AllowInlineEventHandler(node, listener, context_url,
                                                   context_line))
    return false;

  return true;
}

void Document::UpdateSelectionAfterLayout() {
  should_update_selection_after_layout_ = false;
  Element* element = FocusedElement();
  if (!element)
    return;
  if (element->IsFocusable())
    element->UpdateSelectionOnFocus(SelectionBehaviorOnFocus::kRestore);
}

void Document::AttachRange(Range* range) {
  DCHECK(!ranges_.Contains(range));
  ranges_.insert(range);
}

void Document::DetachRange(Range* range) {
  // We don't DCHECK ranges_.contains(range) to allow us to call this
  // unconditionally to fix: https://bugs.webkit.org/show_bug.cgi?id=26044
  ranges_.erase(range);
}

void Document::InitDNSPrefetch() {
  Settings* settings = GetSettings();

  have_explicitly_disabled_dns_prefetch_ = false;
  is_dns_prefetch_enabled_ =
      settings && settings->GetDNSPrefetchingEnabled() &&
      dom_window_->GetSecurityContext().GetSecurityOrigin()->Protocol() ==
          "http";

  // Inherit DNS prefetch opt-out from parent frame
  if (Document* parent = ParentDocument()) {
    if (!parent->IsDNSPrefetchEnabled())
      is_dns_prefetch_enabled_ = false;
  }
}

void Document::ParseDNSPrefetchControlHeader(
    const String& dns_prefetch_control) {
  if (EqualIgnoringASCIICase(dns_prefetch_control, "on") &&
      !have_explicitly_disabled_dns_prefetch_) {
    is_dns_prefetch_enabled_ = true;
    return;
  }

  is_dns_prefetch_enabled_ = false;
  have_explicitly_disabled_dns_prefetch_ = true;
}

IntersectionObserverController* Document::GetIntersectionObserverController() {
  return intersection_observer_controller_;
}

IntersectionObserverController&
Document::EnsureIntersectionObserverController() {
  if (!intersection_observer_controller_) {
    intersection_observer_controller_ =
        MakeGarbageCollected<IntersectionObserverController>(
            GetExecutionContext());
  }
  return *intersection_observer_controller_;
}

ElementIntersectionObserverData*
Document::DocumentExplicitRootIntersectionObserverData() const {
  return document_explicit_root_intersection_observer_data_.Get();
}

ElementIntersectionObserverData&
Document::EnsureDocumentExplicitRootIntersectionObserverData() {
  if (!document_explicit_root_intersection_observer_data_) {
    document_explicit_root_intersection_observer_data_ =
        MakeGarbageCollected<ElementIntersectionObserverData>();
  }
  return *document_explicit_root_intersection_observer_data_;
}

const ScriptRegexp& Document::EnsureEmailRegexp() const {
  if (!data_->email_regexp_)
    data_->email_regexp_ = EmailInputType::CreateEmailRegexp();
  return *data_->email_regexp_;
}

void Document::SetMediaFeatureEvaluated(int feature) {
  evaluated_media_features_ |= (1 << feature);
}

bool Document::WasMediaFeatureEvaluated(int feature) {
  return (evaluated_media_features_ >> feature) & 1;
}

void Document::AddConsoleMessage(ConsoleMessage* message,
                                 bool discard_duplicates) const {
  // Don't let non-attached Documents spam the console.
  if (domWindow())
    domWindow()->AddConsoleMessage(message, discard_duplicates);
}

void Document::AddToTopLayer(Element* element, const Element* before) {
  if (element->IsInTopLayer())
    return;

  DCHECK(!top_layer_elements_.Contains(element));
  DCHECK(!before || top_layer_elements_.Contains(before));
  if (before) {
    DCHECK(element->IsBackdropPseudoElement())
        << "If this invariant changes, we might need to revisit Container "
           "Queries for top layer elements.";
    wtf_size_t before_position = top_layer_elements_.Find(before);
    top_layer_elements_.insert(before_position, element);
  } else {
    top_layer_elements_.push_back(element);
  }
  element->SetIsInTopLayer(true);
  display_lock_document_state_->ElementAddedToTopLayer(element);

  probe::TopLayerElementsChanged(this);
}

void Document::RemoveFromTopLayer(Element* element) {
  if (!element->IsInTopLayer())
    return;
  wtf_size_t position = top_layer_elements_.Find(element);
  DCHECK_NE(position, kNotFound);
  top_layer_elements_.EraseAt(position);
  element->SetIsInTopLayer(false);
  display_lock_document_state_->ElementRemovedFromTopLayer(element);

  probe::TopLayerElementsChanged(this);
}

HTMLDialogElement* Document::ActiveModalDialog() const {
  for (const auto& element : base::Reversed(top_layer_elements_)) {
    if (auto* dialog = DynamicTo<HTMLDialogElement>(*element))
      return dialog;
  }

  return nullptr;
}

Element* Document::TopmostPopupAutoOrHint() const {
  if (PopupHintShowing())
    return PopupHintShowing();
  if (PopupStack().IsEmpty())
    return nullptr;
  return PopupStack().back();
}

void Document::SetPopUpMousedownTarget(const Element* pop_up) {
  DCHECK(
      RuntimeEnabledFeatures::HTMLPopupAttributeEnabled(GetExecutionContext()));
  DCHECK(!pop_up || pop_up->HasPopupAttribute());
  pop_up_mousedown_target_ = pop_up;
}

void Document::exitPointerLock() {
  if (!GetPage())
    return;
  if (Element* target = GetPage()->GetPointerLockController().GetElement()) {
    if (target->GetDocument() != this)
      return;
    GetPage()->GetPointerLockController().ExitPointerLock();
  }
}

Element* Document::PointerLockElement() const {
  if (!GetPage() || GetPage()->GetPointerLockController().LockPending())
    return nullptr;
  if (Element* element = GetPage()->GetPointerLockController().GetElement()) {
    if (element->GetDocument() == this)
      return element;
  }
  return nullptr;
}

void Document::DecrementLoadEventDelayCount() {
  DCHECK(load_event_delay_count_);
  --load_event_delay_count_;

  if (!load_event_delay_count_)
    CheckLoadEventSoon();
}

void Document::DecrementLoadEventDelayCountAndCheckLoadEvent() {
  DCHECK(load_event_delay_count_);
  --load_event_delay_count_;

  if (!load_event_delay_count_)
    CheckCompleted();
}

void Document::CheckLoadEventSoon() {
  if (GetFrame() && !load_event_delay_timer_.IsActive())
    load_event_delay_timer_.StartOneShot(base::TimeDelta(), FROM_HERE);
}

bool Document::IsDelayingLoadEvent() {
  return load_event_delay_count_;
}

void Document::LoadEventDelayTimerFired(TimerBase*) {
  CheckCompleted();
}

void Document::LoadPluginsSoon() {
  // FIXME: Remove this timer once we don't need to compute layout to load
  // plugins.
  if (!plugin_loading_timer_.IsActive())
    plugin_loading_timer_.StartOneShot(base::TimeDelta(), FROM_HERE);
}

void Document::PluginLoadingTimerFired(TimerBase*) {
  UpdateStyleAndLayout(DocumentUpdateReason::kPlugin);
}

ScriptedAnimationController& Document::GetScriptedAnimationController() {
  return *scripted_animation_controller_;
}

int Document::RequestAnimationFrame(FrameCallback* callback) {
  return scripted_animation_controller_->RegisterFrameCallback(callback);
}

void Document::CancelAnimationFrame(int id) {
  scripted_animation_controller_->CancelFrameCallback(id);
}

void Document::ServiceScriptedAnimations(
    base::TimeTicks monotonic_animation_start_time,
    bool can_throttle) {
  base::TimeTicks start_time =
      can_throttle ? base::TimeTicks() : base::TimeTicks::Now();
  scripted_animation_controller_->ServiceScriptedAnimations(
      monotonic_animation_start_time, can_throttle);
  if (!can_throttle && GetFrame()) {
    GetFrame()->GetFrameScheduler()->AddTaskTime(base::TimeTicks::Now() -
                                                 start_time);
  }
}

ScriptedIdleTaskController& Document::EnsureScriptedIdleTaskController() {
  if (!scripted_idle_task_controller_) {
    scripted_idle_task_controller_ =
        ScriptedIdleTaskController::Create(domWindow());
    // We need to make sure that we don't start up if we're detached.
    if (!domWindow() || domWindow()->IsContextDestroyed()) {
      scripted_idle_task_controller_->ContextLifecycleStateChanged(
          mojom::FrameLifecycleState::kFrozen);
    }
  }
  return *scripted_idle_task_controller_;
}

int Document::RequestIdleCallback(IdleTask* idle_task,
                                  const IdleRequestOptions* options) {
  return EnsureScriptedIdleTaskController().RegisterCallback(idle_task,
                                                             options);
}

void Document::CancelIdleCallback(int id) {
  if (!scripted_idle_task_controller_)
    return;
  scripted_idle_task_controller_->CancelCallback(id);
}

DocumentLoader* Document::Loader() const {
  return GetFrame() ? GetFrame()->Loader().GetDocumentLoader() : nullptr;
}

Node* EventTargetNodeForDocument(Document* doc) {
  if (!doc)
    return nullptr;
  Node* node = doc->FocusedElement();
  auto* plugin_document = DynamicTo<PluginDocument>(doc);
  if (plugin_document && !node) {
    node = plugin_document->PluginNode();
  }
  if (!node && IsA<HTMLDocument>(doc))
    node = doc->body();
  if (!node)
    node = doc->documentElement();
  return node;
}

void Document::AdjustQuadsForScrollAndAbsoluteZoom(
    Vector<gfx::QuadF>& quads,
    const LayoutObject& layout_object) const {
  if (!View())
    return;

  for (auto& quad : quads)
    AdjustForAbsoluteZoom::AdjustQuad(quad, layout_object);
}

void Document::AdjustRectForScrollAndAbsoluteZoom(
    gfx::RectF& rect,
    const LayoutObject& layout_object) const {
  if (!View())
    return;

  AdjustForAbsoluteZoom::AdjustRectF(rect, layout_object);
}

void Document::SetForceSynchronousParsingForTesting(bool enabled) {
  g_force_synchronous_parsing_for_testing = enabled;
}

bool Document::ForceSynchronousParsingForTesting() {
  return g_force_synchronous_parsing_for_testing;
}

SnapCoordinator& Document::GetSnapCoordinator() {
  if (!snap_coordinator_)
    snap_coordinator_ = MakeGarbageCollected<SnapCoordinator>();

  return *snap_coordinator_;
}

void Document::PerformScrollSnappingTasks() {
  SnapCoordinator& snap_coordinator = GetSnapCoordinator();
  if (!snap_coordinator.AnySnapContainerDataNeedsUpdate())
    return;
  snap_coordinator.UpdateAllSnapContainerDataIfNeeded();
  snap_coordinator.ResnapAllContainersIfNeeded();
}

void Document::SetContextFeatures(ContextFeatures& features) {
  context_features_ = &features;
}

void Document::UpdateHoverActiveState(bool is_active,
                                      bool update_active_chain,
                                      Element* inner_element) {
  if (is_active && GetFrame())
    GetFrame()->GetEventHandler().NotifyElementActivated();

  Element* inner_element_in_document = inner_element;

  while (inner_element_in_document &&
         inner_element_in_document->GetDocument() != this) {
    inner_element_in_document->GetDocument().UpdateHoverActiveState(
        is_active, update_active_chain, inner_element_in_document);
    inner_element_in_document =
        inner_element_in_document->GetDocument().LocalOwner();
  }

  UpdateActiveState(is_active, update_active_chain, inner_element_in_document);
  UpdateHoverState(inner_element_in_document);
}

void Document::UpdateActiveState(bool is_active,
                                 bool update_active_chain,
                                 Element* new_active_element) {
  Element* old_active_element = GetActiveElement();
  if (old_active_element && !is_active) {
    // The oldActiveElement layoutObject is null, dropped on :active by setting
    // display: none, for instance. We still need to clear the ActiveChain as
    // the mouse is released.
    for (Element* element = old_active_element; element;
         element = FlatTreeTraversal::ParentElement(*element)) {
      element->SetActive(false);
      user_action_elements_.SetInActiveChain(element, false);
    }
    SetActiveElement(nullptr);
  } else {
    if (!old_active_element && new_active_element && is_active) {
      // We are setting the :active chain and freezing it. If future moves
      // happen, they will need to reference this chain.
      for (Element* element = new_active_element; element;
           element = FlatTreeTraversal::ParentElement(*element)) {
        user_action_elements_.SetInActiveChain(element, true);
      }
      SetActiveElement(new_active_element);
    }
  }

  // If the mouse has just been pressed, set :active on the chain. Those (and
  // only those) nodes should remain :active until the mouse is released.
  bool allow_active_changes = !old_active_element && GetActiveElement();
  if (!allow_active_changes)
    return;

  DCHECK(is_active);

  Element* new_element = SkipDisplayNoneAncestors(new_active_element);

  // Now set the active state for our new object up to the root.  If the mouse
  // is down and if this is a mouse move event, we want to restrict changes in
  // :active to only apply to elements that are in the :active chain that we
  // froze at the time the mouse went down.
  for (Element* curr = new_element; curr;
       curr = FlatTreeTraversal::ParentElement(*curr)) {
    if (update_active_chain || curr->InActiveChain())
      curr->SetActive(true);
  }
}

void Document::UpdateHoverState(Element* inner_element_in_document) {
  Element* old_hover_element = HoverElement();

  // The passed in innerElement may not be a result of a hit test for the
  // current up-to-date flat/layout tree. That means the element may be
  // display:none at this point. Skip up the ancestor chain until we reach an
  // element with a layoutObject or a display:contents element.
  Element* new_hover_element =
      SkipDisplayNoneAncestors(inner_element_in_document);

  if (old_hover_element == new_hover_element)
    return;

  // Update our current hover element.
  SetHoverElement(new_hover_element);

  Node* ancestor_element = nullptr;
  if (old_hover_element && old_hover_element->isConnected() &&
      new_hover_element) {
    Node* ancestor = FlatTreeTraversal::CommonAncestor(*old_hover_element,
                                                       *new_hover_element);
    if (auto* element = DynamicTo<Element>(ancestor))
      ancestor_element = element;
  }

  HeapVector<Member<Element>, 32> elements_to_remove_from_chain;
  HeapVector<Member<Element>, 32> elements_to_add_to_hover_chain;

  // The old hover path only needs to be cleared up to (and not including) the
  // common ancestor;
  //
  // TODO(emilio): old_hover_element may be disconnected from the tree already.
  if (old_hover_element && old_hover_element->isConnected()) {
    for (Element* curr = old_hover_element; curr && curr != ancestor_element;
         curr = FlatTreeTraversal::ParentElement(*curr)) {
      elements_to_remove_from_chain.push_back(curr);
    }
  }

  // Now set the hover state for our new object up to the root.
  for (Element* curr = new_hover_element; curr;
       curr = FlatTreeTraversal::ParentElement(*curr)) {
    elements_to_add_to_hover_chain.push_back(curr);
  }

  for (Element* element : elements_to_remove_from_chain)
    element->SetHovered(false);

  bool saw_common_ancestor = false;
  for (Element* element : elements_to_add_to_hover_chain) {
    if (element == ancestor_element)
      saw_common_ancestor = true;
    if (!saw_common_ancestor || element == hover_element_)
      element->SetHovered(true);
  }
}

bool Document::HaveScriptBlockingStylesheetsLoaded() const {
  return style_engine_->HaveScriptBlockingStylesheetsLoaded();
}

bool Document::HaveRenderBlockingStylesheetsLoaded() const {
  return !render_blocking_resource_manager_ ||
         !render_blocking_resource_manager_->HasPendingStylesheets();
}

bool Document::HaveRenderBlockingResourcesLoaded() const {
  return !render_blocking_resource_manager_ ||
         !render_blocking_resource_manager_->HasRenderBlockingResources();
}

Locale& Document::GetCachedLocale(const AtomicString& locale) {
  AtomicString locale_key = locale;
  if (locale.IsEmpty() ||
      !RuntimeEnabledFeatures::LangAttributeAwareFormControlUIEnabled())
    return Locale::DefaultLocale();
  LocaleIdentifierToLocaleMap::AddResult result =
      locale_cache_.insert(locale_key, nullptr);
  if (result.is_new_entry)
    result.stored_value->value = Locale::Create(locale_key);
  return *(result.stored_value->value);
}

AnimationClock& Document::GetAnimationClock() {
  DCHECK(GetPage());
  return GetPage()->Animator().Clock();
}

const AnimationClock& Document::GetAnimationClock() const {
  DCHECK(GetPage());
  return GetPage()->Animator().Clock();
}

Document& Document::EnsureTemplateDocument() {
  if (IsTemplateDocument())
    return *this;

  if (template_document_)
    return *template_document_;

  if (IsA<HTMLDocument>(this)) {
    template_document_ = MakeGarbageCollected<HTMLDocument>(
        DocumentInit::Create()
            .WithExecutionContext(execution_context_.Get())
            .WithURL(BlankURL()));
  } else {
    template_document_ = MakeGarbageCollected<Document>(
        DocumentInit::Create()
            .WithExecutionContext(execution_context_.Get())
            .WithURL(BlankURL()));
  }

  template_document_->template_document_host_ = this;  // balanced in dtor.

  return *template_document_.Get();
}

void Document::DidAssociateFormControl(Element* element) {
  if (!GetFrame() || !GetFrame()->GetPage() || !HasFinishedParsing())
    return;

  // We add a slight delay because this could be called rapidly.
  if (!did_associate_form_controls_timer_.IsActive()) {
    did_associate_form_controls_timer_.StartOneShot(base::Milliseconds(300),
                                                    FROM_HERE);
  }
}

void Document::DidAssociateFormControlsTimerFired(TimerBase* timer) {
  DCHECK_EQ(timer, &did_associate_form_controls_timer_);
  if (!GetFrame() || !GetFrame()->GetPage())
    return;

  GetFrame()->GetPage()->GetChromeClient().DidAssociateFormControlsAfterLoad(
      GetFrame());
}

float Document::DevicePixelRatio() const {
  return GetFrame() ? GetFrame()->DevicePixelRatio() : 1.0;
}

TextAutosizer* Document::GetTextAutosizer() {
  if (!text_autosizer_)
    text_autosizer_ = MakeGarbageCollected<TextAutosizer>(this);
  return text_autosizer_.Get();
}

bool Document::SetPseudoStateForTesting(Element& element,
                                        const String& pseudo,
                                        bool matches) {
  DCHECK(WebTestSupport::IsRunningWebTest());
  auto& set = UserActionElements();
  if (pseudo == ":focus") {
    set.SetFocused(&element, matches);
    element.PseudoStateChangedForTesting(CSSSelector::kPseudoFocus);
  } else if (pseudo == ":focus-within") {
    set.SetHasFocusWithin(&element, matches);
    element.PseudoStateChangedForTesting(CSSSelector::kPseudoFocusWithin);
  } else if (pseudo == ":active") {
    set.SetActive(&element, matches);
    element.PseudoStateChangedForTesting(CSSSelector::kPseudoActive);
  } else if (pseudo == ":hover") {
    set.SetHovered(&element, matches);
    element.PseudoStateChangedForTesting(CSSSelector::kPseudoHover);
  } else {
    return false;
  }
  return true;
}

void Document::EnqueueAutofocusCandidate(Element& element) {
  // https://html.spec.whatwg.org/C#the-autofocus-attribute
  // 7. If topDocument's autofocus processed flag is false, then remove the
  // element from topDocument's autofocus candidates, and append the element
  // to topDocument's autofocus candidates.
  if (autofocus_processed_flag_)
    return;
  wtf_size_t index = autofocus_candidates_.Find(&element);
  if (index != WTF::kNotFound)
    autofocus_candidates_.EraseAt(index);
  autofocus_candidates_.push_back(element);
}

bool Document::HasAutofocusCandidates() const {
  return autofocus_candidates_.size() > 0;
}

// https://html.spec.whatwg.org/C/#flush-autofocus-candidates
void Document::FlushAutofocusCandidates() {
  // 1. If topDocument's autofocus processed flag is true, then return.
  if (autofocus_processed_flag_)
    return;

  // 3. If candidates is empty, then return.
  if (autofocus_candidates_.IsEmpty())
    return;

  // 4. If topDocument's focused area is not topDocument itself, or
  //    topDocument's URL's fragment is not empty, then:
  //  1. Empty candidates.
  //  2. Set topDocument's autofocus processed flag to true.
  //  3. Return.
  if (AdjustedFocusedElement()) {
    autofocus_candidates_.clear();
    autofocus_processed_flag_ = true;
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::ConsoleMessageSource::kRendering,
        mojom::ConsoleMessageLevel::kInfo,
        "Autofocus processing was blocked because a "
        "document already has a focused element."));
    return;
  }
  if (CssTarget()) {
    autofocus_candidates_.clear();
    autofocus_processed_flag_ = true;
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::ConsoleMessageSource::kRendering,
        mojom::ConsoleMessageLevel::kInfo,
        "Autofocus processing was blocked because a "
        "document's URL has a fragment '#" +
            Url().FragmentIdentifier() + "'."));
    return;
  }

  // 5. While candidates is not empty:
  while (!autofocus_candidates_.IsEmpty()) {
    // 5.1. Let element be candidates[0].
    Element& element = *autofocus_candidates_[0];

    // 5.2. Let doc be element's node document.
    Document* doc = &element.GetDocument();

    // 5.3. If doc is not fully active, then remove element from candidates,
    // and continue.
    // 5.4. If doc's browsing context's top-level browsing context is not same
    // as topDocument's browsing context, then remove element from candidates,
    // and continue.
    if (&doc->TopDocument() != this) {
      autofocus_candidates_.EraseAt(0);
      continue;
    }

    // The element is in the fallback content of an OBJECT of which
    // fallback state is not fixed yet.
    // TODO(tkent): Standardize this behavior.
    if (IsInIndeterminateObjectAncestor(&element)) {
      return;
    }

    // 5.5. If doc's script-blocking style sheet counter is greater than 0,
    // then return.
    // TODO(tkent): Is this necessary? WPT spin-by-blocking-style-sheet.html
    // doesn't hit this condition, and FlushAutofocusCandidates() is not called
    // until the stylesheet is loaded.
    if (GetStyleEngine().HasPendingScriptBlockingSheets() ||
        !HaveRenderBlockingStylesheetsLoaded()) {
      return;
    }

    // 5.6. Remove element from candidates.
    autofocus_candidates_.EraseAt(0);

    // 5.7. Let inclusiveAncestorDocuments be a list consisting of doc, plus
    // the active documents of each of doc's browsing context's ancestor
    // browsing contexts.
    // 5.8. If URL's fragment of any Document in inclusiveAncestorDocuments
    // is not empty, then continue.
    if (doc != this) {
      for (HTMLFrameOwnerElement* frameOwner = doc->LocalOwner();
           !doc->CssTarget() && frameOwner; frameOwner = doc->LocalOwner()) {
        doc = &frameOwner->GetDocument();
      }
      if (doc->CssTarget()) {
        AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
            mojom::ConsoleMessageSource::kRendering,
            mojom::ConsoleMessageLevel::kInfo,
            "Autofocus processing was blocked because a "
            "document's URL has a fragment '#" +
                doc->Url().FragmentIdentifier() + "'."));
        continue;
      }
      DCHECK_EQ(doc, this);
    }

    // 9. Let target be element.
    Element* target = &element;

    // 10. If target is not a focusable area, then set target to the result of
    // getting the focusable area for target.
    element.GetDocument().UpdateStyleAndLayoutTree();
    if (!target->IsFocusable())
      target = target->GetFocusableArea();

    // 11. If target is not null, then:
    if (target) {
      // 11.1. Empty candidates.
      // 11.2. Set topDocument's autofocus processed flag to true.
      FinalizeAutofocus();
      // 11.3. Run the focusing steps for element.
      element.Focus();
    } else {
      // TODO(tkent): Show a console message, and fix LocalNTP*Test.*
      // in browser_tests.
    }
  }
}

void Document::FinalizeAutofocus() {
  autofocus_candidates_.clear();
  autofocus_processed_flag_ = true;
}

// https://html.spec.whatwg.org/C/#autofocus-delegate, although most uses are
// of Element::GetAutofocusDelegate().
Element* Document::GetAutofocusDelegate() const {
  if (HTMLElement* body_element = body())
    return body_element->GetAutofocusDelegate();

  return nullptr;
}

Element* Document::ActiveElement() const {
  if (Element* element = AdjustedFocusedElement())
    return element;
  return body();
}

bool Document::hasFocus() const {
  return GetPage() && GetPage()->GetFocusController().IsDocumentFocused(*this);
}

const AtomicString& Document::BodyAttributeValue(
    const QualifiedName& name) const {
  if (auto* bodyElement = body())
    return bodyElement->FastGetAttribute(name);
  return g_null_atom;
}

void Document::SetBodyAttribute(const QualifiedName& name,
                                const AtomicString& value) {
  if (auto* bodyElement = body()) {
    // FIXME: This check is apparently for benchmarks that set the same value
    // repeatedly.  It's not clear what benchmarks though, it's also not clear
    // why we don't avoid causing a style recalc when setting the same value to
    // a presentational attribute in the common case.
    if (bodyElement->FastGetAttribute(name) != value)
      bodyElement->setAttribute(name, value);
  }
}

const AtomicString& Document::bgColor() const {
  return BodyAttributeValue(html_names::kBgcolorAttr);
}

void Document::setBgColor(const AtomicString& value) {
  if (!IsFrameSet())
    SetBodyAttribute(html_names::kBgcolorAttr, value);
}

const AtomicString& Document::fgColor() const {
  return BodyAttributeValue(html_names::kTextAttr);
}

void Document::setFgColor(const AtomicString& value) {
  if (!IsFrameSet())
    SetBodyAttribute(html_names::kTextAttr, value);
}

const AtomicString& Document::alinkColor() const {
  return BodyAttributeValue(html_names::kAlinkAttr);
}

void Document::setAlinkColor(const AtomicString& value) {
  if (!IsFrameSet())
    SetBodyAttribute(html_names::kAlinkAttr, value);
}

const AtomicString& Document::linkColor() const {
  return BodyAttributeValue(html_names::kLinkAttr);
}

void Document::setLinkColor(const AtomicString& value) {
  if (!IsFrameSet())
    SetBodyAttribute(html_names::kLinkAttr, value);
}

const AtomicString& Document::vlinkColor() const {
  return BodyAttributeValue(html_names::kVlinkAttr);
}

void Document::setVlinkColor(const AtomicString& value) {
  if (!IsFrameSet())
    SetBodyAttribute(html_names::kVlinkAttr, value);
}

template <unsigned type>
bool ShouldInvalidateNodeListCachesForAttr(
    const LiveNodeListRegistry& node_lists,
    const QualifiedName& attr_name) {
  auto invalidation_type = static_cast<NodeListInvalidationType>(type);
  if (node_lists.ContainsInvalidationType(invalidation_type) &&
      LiveNodeListBase::ShouldInvalidateTypeOnAttributeChange(invalidation_type,
                                                              attr_name))
    return true;
  return ShouldInvalidateNodeListCachesForAttr<type + 1>(node_lists, attr_name);
}

template <>
bool ShouldInvalidateNodeListCachesForAttr<kNumNodeListInvalidationTypes>(
    const LiveNodeListRegistry&,
    const QualifiedName&) {
  return false;
}

bool Document::ShouldInvalidateNodeListCaches(
    const QualifiedName* attr_name) const {
  if (attr_name) {
    return ShouldInvalidateNodeListCachesForAttr<
        kDoNotInvalidateOnAttributeChanges + 1>(node_lists_, *attr_name);
  }

  // If the invalidation is not for an attribute, invalidation is needed if
  // there is any node list present (with any invalidation type).
  return !node_lists_.IsEmpty();
}

void Document::InvalidateNodeListCaches(const QualifiedName* attr_name) {
  for (const LiveNodeListBase* list : lists_invalidated_at_document_)
    list->InvalidateCacheForAttribute(attr_name);
}

void Document::PlatformColorsChanged() {
  if (!IsActive())
    return;

  GetStyleEngine().PlatformColorsChanged();
}

PropertyRegistry& Document::EnsurePropertyRegistry() {
  if (!property_registry_)
    property_registry_ = MakeGarbageCollected<PropertyRegistry>();
  return *property_registry_;
}

DocumentResourceCoordinator* Document::GetResourceCoordinator() {
  if (!resource_coordinator_ && GetFrame()) {
    resource_coordinator_ = DocumentResourceCoordinator::MaybeCreate(
        GetFrame()->GetBrowserInterfaceBroker());
  }
  return resource_coordinator_.get();
}

scoped_refptr<base::SingleThreadTaskRunner> Document::GetTaskRunner(
    TaskType type) {
  DCHECK(IsMainThread());
  if (GetExecutionContext())
    return GetExecutionContext()->GetTaskRunner(type);
  // GetExecutionContext() can be nullptr in unit tests and after Shutdown().
  // Fallback to the default task runner for this thread if all else fails.
  return Thread::Current()->GetDeprecatedTaskRunner();
}

DOMFeaturePolicy* Document::featurePolicy() {
  if (!policy_ && GetExecutionContext())
    policy_ = MakeGarbageCollected<DOMFeaturePolicy>(GetExecutionContext());
  return policy_.Get();
}

StylePropertyMapReadOnly* Document::ComputedStyleMap(Element* element) {
  ElementComputedStyleMap::AddResult add_result =
      element_computed_style_map_.insert(element, nullptr);
  if (add_result.is_new_entry) {
    add_result.stored_value->value =
        MakeGarbageCollected<ComputedStylePropertyMap>(element);
  }
  return add_result.stored_value->value;
}

void Document::AddComputedStyleMapItem(
    Element* element,
    StylePropertyMapReadOnly* computed_style) {
  element_computed_style_map_.insert(element, computed_style);
}

StylePropertyMapReadOnly* Document::RemoveComputedStyleMapItem(
    Element* element) {
  return element_computed_style_map_.Take(element);
}

void Document::Trace(Visitor* visitor) const {
  visitor->Trace(doc_type_);
  visitor->Trace(implementation_);
  visitor->Trace(autofocus_candidates_);
  visitor->Trace(focused_element_);
  visitor->Trace(sequential_focus_navigation_starting_point_);
  visitor->Trace(hover_element_);
  visitor->Trace(active_element_);
  visitor->Trace(document_element_);
  visitor->Trace(root_scroller_controller_);
  visitor->Trace(title_element_);
  visitor->Trace(ax_object_cache_);
  visitor->Trace(markers_);
  visitor->Trace(css_target_);
  visitor->Trace(current_script_stack_);
  visitor->Trace(script_runner_);
  visitor->Trace(script_runner_delayer_);
  visitor->Trace(lists_invalidated_at_document_);
  visitor->Trace(node_lists_);
  visitor->Trace(top_layer_elements_);
  visitor->Trace(popup_hint_showing_);
  visitor->Trace(popup_stack_);
  visitor->Trace(pop_up_mousedown_target_);
  visitor->Trace(popups_waiting_to_hide_);
  visitor->Trace(elements_needing_style_recalc_for_toggle_);
  visitor->Trace(load_event_delay_timer_);
  visitor->Trace(plugin_loading_timer_);
  visitor->Trace(elem_sheet_);
  visitor->Trace(clear_focused_element_timer_);
  visitor->Trace(node_iterators_);
  visitor->Trace(ranges_);
  visitor->Trace(document_explicit_root_intersection_observer_data_);
  visitor->Trace(style_engine_);
  visitor->Trace(form_controller_);
  visitor->Trace(visited_link_state_);
  visitor->Trace(element_computed_style_map_);
  visitor->Trace(dom_window_);
  visitor->Trace(fetcher_);
  visitor->Trace(parser_);
  visitor->Trace(context_features_);
  visitor->Trace(http_refresh_scheduler_);
  visitor->Trace(style_sheet_list_);
  visitor->Trace(document_timing_);
  visitor->Trace(media_query_matcher_);
  visitor->Trace(scripted_animation_controller_);
  visitor->Trace(scripted_idle_task_controller_);
  visitor->Trace(text_autosizer_);
  visitor->Trace(element_data_cache_clear_timer_);
  visitor->Trace(element_data_cache_);
  visitor->Trace(use_elements_needing_update_);
  visitor->Trace(template_document_);
  visitor->Trace(template_document_host_);
  visitor->Trace(did_associate_form_controls_timer_);
  visitor->Trace(user_action_elements_);
  visitor->Trace(svg_extensions_);
  visitor->Trace(layout_view_);
  visitor->Trace(document_animations_);
  visitor->Trace(timeline_);
  visitor->Trace(pending_animations_);
  visitor->Trace(worklet_animation_controller_);
  visitor->Trace(execution_context_);
  visitor->Trace(canvas_font_cache_);
  visitor->Trace(intersection_observer_controller_);
  visitor->Trace(snap_coordinator_);
  visitor->Trace(property_registry_);
  visitor->Trace(policy_);
  visitor->Trace(slot_assignment_engine_);
  visitor->Trace(viewport_data_);
  visitor->Trace(lazy_load_image_observer_);
  visitor->Trace(computed_node_mapping_);
  visitor->Trace(mime_handler_view_before_unload_event_listener_);
  visitor->Trace(cookie_jar_);
  visitor->Trace(synchronous_mutation_observer_set_);
  visitor->Trace(fragment_directive_);
  visitor->Trace(element_explicitly_set_attr_elements_map_);
  visitor->Trace(display_lock_document_state_);
  visitor->Trace(render_blocking_resource_manager_);
  visitor->Trace(find_in_page_active_match_node_);
  visitor->Trace(data_);
  visitor->Trace(meta_theme_color_elements_);
  visitor->Trace(unassociated_listed_elements_);
  visitor->Trace(intrinsic_size_observer_);
  visitor->Trace(anchor_element_interaction_tracker_);
  visitor->Trace(focused_element_change_observers_);
  visitor->Trace(pending_link_header_preloads_);
  Supplementable<Document>::Trace(visitor);
  TreeScope::Trace(visitor);
  ContainerNode::Trace(visitor);
}

SlotAssignmentEngine& Document::GetSlotAssignmentEngine() {
  if (!slot_assignment_engine_)
    slot_assignment_engine_ = MakeGarbageCollected<SlotAssignmentEngine>();
  return *slot_assignment_engine_;
}

bool Document::IsSlotAssignmentDirty() const {
  return slot_assignment_engine_ &&
         slot_assignment_engine_->HasPendingSlotAssignmentRecalc();
}

bool Document::IsFocusAllowed() const {
  if (!GetFrame() || GetFrame()->IsMainFrame() ||
      LocalFrame::HasTransientUserActivation(GetFrame())) {
    // 'autofocus' runs Element::focus asynchronously at which point the
    // document might not have a frame (see https://crbug.com/960224).
    return true;
  }

  WebFeature uma_type;
  bool sandboxed = dom_window_->IsSandboxed(
      network::mojom::blink::WebSandboxFlags::kNavigation);
  bool ad = GetFrame()->IsAdFrame();
  if (sandboxed) {
    uma_type = ad ? WebFeature::kFocusWithoutUserActivationSandboxedAdFrame
                  : WebFeature::kFocusWithoutUserActivationSandboxedNotAdFrame;
  } else {
    uma_type =
        ad ? WebFeature::kFocusWithoutUserActivationNotSandboxedAdFrame
           : WebFeature::kFocusWithoutUserActivationNotSandboxedNotAdFrame;
  }
  CountUse(uma_type);
  if (!RuntimeEnabledFeatures::BlockingFocusWithoutUserActivationEnabled())
    return true;
  return GetExecutionContext()->IsFeatureEnabled(
      mojom::blink::PermissionsPolicyFeature::kFocusWithoutUserActivation);
}

LazyLoadImageObserver& Document::EnsureLazyLoadImageObserver() {
  if (!lazy_load_image_observer_) {
    lazy_load_image_observer_ =
        MakeGarbageCollected<LazyLoadImageObserver>(*this);
  }
  return *lazy_load_image_observer_;
}

void Document::IncrementNumberOfCanvases() {
  num_canvases_++;
}

void Document::ExecuteJavaScriptUrls() {
  DCHECK(dom_window_);
  Vector<PendingJavascriptUrl> urls_to_execute;
  urls_to_execute.swap(pending_javascript_urls_);

  for (auto& url_to_execute : urls_to_execute) {
    dom_window_->GetScriptController().ExecuteJavaScriptURL(
        url_to_execute.url, network::mojom::CSPDisposition::CHECK,
        url_to_execute.world.get());
    if (!GetFrame())
      break;
  }
  CheckCompleted();
}

void Document::ProcessJavaScriptUrl(
    const KURL& url,
    scoped_refptr<const DOMWrapperWorld> world) {
  DCHECK(url.ProtocolIsJavaScript());
  if (is_initial_empty_document_)
    load_event_progress_ = kLoadEventNotRun;
  GetFrame()->Loader().Progress().ProgressStarted();
  pending_javascript_urls_.push_back(PendingJavascriptUrl(url, world));
  if (!javascript_url_task_handle_.IsActive()) {
    javascript_url_task_handle_ = PostCancellableTask(
        *GetTaskRunner(TaskType::kNetworking), FROM_HERE,
        WTF::Bind(&Document::ExecuteJavaScriptUrls, WrapWeakPersistent(this)));
  }
}

DisplayLockDocumentState& Document::GetDisplayLockDocumentState() const {
  return *display_lock_document_state_;
}

void Document::CancelPendingJavaScriptUrls() {
  if (javascript_url_task_handle_.IsActive())
    javascript_url_task_handle_.Cancel();
  pending_javascript_urls_.clear();
}

bool Document::IsInWebAppScope() const {
  if (!GetSettings())
    return false;

  const String& web_app_scope = GetSettings()->GetWebAppScope();
  if (web_app_scope.IsNull() || web_app_scope.IsEmpty())
    return false;

  DCHECK_EQ(KURL(web_app_scope).GetString(), web_app_scope);
  return Url().GetString().StartsWith(web_app_scope);
}

bool Document::ChildrenCanHaveStyle() const {
  if (LayoutObject* view = GetLayoutView())
    return view->CanHaveChildren();
  return false;
}

ComputedAccessibleNode* Document::GetOrCreateComputedAccessibleNode(
    AXID ax_id) {
  DCHECK(ax_id) << "Invalid ax_id";
  if (computed_node_mapping_.find(ax_id) == computed_node_mapping_.end()) {
    auto* node = MakeGarbageCollected<ComputedAccessibleNode>(ax_id, this);
    computed_node_mapping_.insert(ax_id, node);
  }
  return computed_node_mapping_.at(ax_id);
}

void Document::SetShowBeforeUnloadDialog(bool show_dialog) {
  if (!mime_handler_view_before_unload_event_listener_) {
    if (!show_dialog)
      return;

    mime_handler_view_before_unload_event_listener_ =
        MakeGarbageCollected<BeforeUnloadEventListener>(this);
    domWindow()->addEventListener(
        event_type_names::kBeforeunload,
        mime_handler_view_before_unload_event_listener_, false);
  }
  mime_handler_view_before_unload_event_listener_->SetShowBeforeUnloadDialog(
      show_dialog);
}

mojom::blink::PreferredColorScheme Document::GetPreferredColorScheme() const {
  return style_engine_->GetPreferredColorScheme();
}

void Document::ColorSchemeChanged() {
  UpdateForcedColors();
  GetStyleEngine().ColorSchemeChanged();
  MediaQueryAffectingValueChanged(MediaValueChange::kOther);
}

void Document::VisionDeficiencyChanged() {
  GetStyleEngine().VisionDeficiencyChanged();
}

void Document::UpdateForcedColors() {
  ForcedColors forced_colors =
      RuntimeEnabledFeatures::ForcedColorsEnabled()
          ? WebThemeEngineHelper::GetNativeThemeEngine()->GetForcedColors()
          : ForcedColors::kNone;
  in_forced_colors_mode_ = forced_colors != ForcedColors::kNone;
  if (in_forced_colors_mode_)
    GetStyleEngine().EnsureUAStyleForForcedColors();
}

bool Document::InForcedColorsMode() const {
  return in_forced_colors_mode_ && !Printing();
}

bool Document::InDarkMode() {
  return !InForcedColorsMode() && !Printing() &&
         GetStyleEngine().GetPreferredColorScheme() ==
             mojom::blink::PreferredColorScheme::kDark;
}

void Document::CountUse(mojom::WebFeature feature) const {
  if (execution_context_)
    execution_context_->CountUse(feature);
}

void Document::CountUse(mojom::WebFeature feature) {
  if (execution_context_)
    execution_context_->CountUse(feature);
}

void Document::CountDeprecation(mojom::WebFeature feature) {
  if (execution_context_)
    execution_context_->CountDeprecation(feature);
}

void Document::CountProperty(CSSPropertyID property) const {
  if (DocumentLoader* loader = Loader()) {
    loader->GetUseCounter().Count(
        property, UseCounterImpl::CSSPropertyType::kDefault, GetFrame());
  }
}

void Document::CountAnimatedProperty(CSSPropertyID property) const {
  if (DocumentLoader* loader = Loader()) {
    loader->GetUseCounter().Count(
        property, UseCounterImpl::CSSPropertyType::kAnimation, GetFrame());
  }
}

bool Document::IsUseCounted(mojom::WebFeature feature) const {
  if (DocumentLoader* loader = Loader()) {
    return loader->GetUseCounter().IsCounted(feature);
  }
  return false;
}

bool Document::IsPropertyCounted(CSSPropertyID property) const {
  if (DocumentLoader* loader = Loader()) {
    return loader->GetUseCounter().IsCounted(
        property, UseCounterImpl::CSSPropertyType::kDefault);
  }
  return false;
}

bool Document::IsAnimatedPropertyCounted(CSSPropertyID property) const {
  if (DocumentLoader* loader = Loader()) {
    return loader->GetUseCounter().IsCounted(
        property, UseCounterImpl::CSSPropertyType::kAnimation);
  }
  return false;
}

void Document::ClearUseCounterForTesting(mojom::WebFeature feature) {
  if (DocumentLoader* loader = Loader())
    loader->GetUseCounter().ClearMeasurementForTesting(feature);
}

void Document::RenderBlockingResourceUnblocked() {
  // Only HTML documents can ever be render-blocked by external resources.
  // https://html.spec.whatwg.org/#allows-adding-render-blocking-elements
  DCHECK(IsA<HTMLDocument>(this));
  if (body())
    BeginLifecycleUpdatesIfRenderingReady();
}

void Document::SetFindInPageActiveMatchNode(Node* node) {
  blink::NotifyPriorityScrollAnchorStatusChanged(
      find_in_page_active_match_node_, node);
  find_in_page_active_match_node_ = node;
}

const Node* Document::GetFindInPageActiveMatchNode() const {
  return find_in_page_active_match_node_;
}

void Document::ActivateForPrerendering(
    const mojom::blink::PrerenderPageActivationParams& params) {
  DCHECK(features::IsPrerender2Enabled());

  // TODO(bokan): Portals will change this assumption since they mean an active
  // document can be "adopted" into a portal.
  DCHECK(is_prerendering_);
  is_prerendering_ = false;

  if (DocumentLoader* loader = Loader()) {
    loader->NotifyPrerenderingDocumentActivated(params);
  }

  Vector<base::OnceClosure> callbacks;
  callbacks.swap(will_dispatch_prerenderingchange_callbacks_);
  for (auto& callback : callbacks) {
    std::move(callback).Run();
  }

  // https://wicg.github.io/nav-speculation/prerendering.html#prerendering-browsing-context-activate
  // Step 8.3.4 "Fire an event named prerenderingchange at doc."
  if (RuntimeEnabledFeatures::Prerender2RelatedFeaturesEnabled(
          GetExecutionContext())) {
    DispatchEvent(*Event::Create(event_type_names::kPrerenderingchange));
  } else {
    AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kJavaScript,
        mojom::blink::ConsoleMessageLevel::kWarning,
        "Failed to dispatch 'prerenderingchange' event: Prerender2 feature is "
        "not enabled on the document."));
  }

  // Step 8.3.5 "For each steps in doc’s post-prerendering activation steps
  // list:"
  RunPostPrerenderingActivationSteps();
}

void Document::AddWillDispatchPrerenderingchangeCallback(
    base::OnceClosure closure) {
  DCHECK(is_prerendering_);
  will_dispatch_prerenderingchange_callbacks_.push_back(std::move(closure));
}

void Document::AddPostPrerenderingActivationStep(base::OnceClosure callback) {
  DCHECK(is_prerendering_);
  post_prerendering_activation_callbacks_.push_back(std::move(callback));
}

void Document::RunPostPrerenderingActivationSteps() {
  DCHECK(!is_prerendering_);
  for (auto& callback : post_prerendering_activation_callbacks_)
    std::move(callback).Run();
  post_prerendering_activation_callbacks_.clear();
}

bool Document::InStyleRecalc() const {
  return lifecycle_.GetState() == DocumentLifecycle::kInStyleRecalc ||
         style_engine_->InContainerQueryStyleRecalc() ||
         style_engine_->InEnsureComputedStyle();
}

void Document::DelayLoadEventUntilLayoutTreeUpdate() {
  if (delay_load_event_until_layout_tree_update_)
    return;
  delay_load_event_until_layout_tree_update_ = true;
  IncrementLoadEventDelayCount();
}

void Document::UnblockLoadEventAfterLayoutTreeUpdate() {
  if (delay_load_event_until_layout_tree_update_) {
    delay_load_event_until_layout_tree_update_ = false;
    DecrementLoadEventDelayCount();
  }
}

void Document::AddPendingLinkHeaderPreload(const PendingLinkPreload& preload) {
  pending_link_header_preloads_.insert(&preload);
}

void Document::RemovePendingLinkHeaderPreloadIfNeeded(
    const PendingLinkPreload& preload) {
  pending_link_header_preloads_.erase(&preload);
}

void Document::AddFocusedElementChangeObserver(
    FocusedElementChangeObserver* observer) {
  DCHECK(observer);
  focused_element_change_observers_.insert(observer);
}

void Document::RemoveFocusedElementChangeObserver(
    FocusedElementChangeObserver* observer) {
  DCHECK(focused_element_change_observers_.Contains(observer));
  focused_element_change_observers_.erase(observer);
}

void Document::WriteIntoTrace(perfetto::TracedValue ctx) const {
  perfetto::TracedDictionary dict = std::move(ctx).WriteDictionary();
  dict.Add("url", Url());
}

bool Document::DeferredCompositorCommitIsAllowed() const {
  // Don't defer commits if a transition is in progress. It requires commits to
  // send directives to the compositor and uses a separate mechanism to pause
  // all rendering when needed.
  auto* supplement = DocumentTransitionSupplement::FromIfExists(*this);
  if (supplement && !supplement->GetTransition()->IsIdle())
    return false;

  return deferred_compositor_commit_is_allowed_;
}

Document::PaintPreviewScope::PaintPreviewScope(Document& document,
                                               PaintPreviewState state)
    : document_(document) {
  document_.paint_preview_ = state;
  document_.GetDisplayLockDocumentState().NotifyPrintingOrPreviewChanged();
  if (auto* ds_controller = DeferredShapingController::From(document_))
    ds_controller->ReshapeAllDeferred(ReshapeReason::kPrinting);
}

Document::PaintPreviewScope::~PaintPreviewScope() {
  document_.paint_preview_ = kNotPaintingPreview;
  document_.GetDisplayLockDocumentState().NotifyPrintingOrPreviewChanged();
}

Document::PendingJavascriptUrl::PendingJavascriptUrl(
    const KURL& input_url,
    scoped_refptr<const DOMWrapperWorld> world)
    : url(input_url), world(std::move(world)) {}

Document::PendingJavascriptUrl::~PendingJavascriptUrl() = default;

void Document::CheckPartitionedCookiesOriginTrial(
    const ResourceResponse& response) {
  cookie_jar_->CheckPartitionedCookiesOriginTrial(response);
}

template class CORE_TEMPLATE_EXPORT Supplement<Document>;

}  // namespace blink
#ifndef NDEBUG
static WeakDocumentSet& LiveDocumentSet() {
  DEFINE_STATIC_LOCAL(blink::Persistent<WeakDocumentSet>, set,
                      (blink::MakeGarbageCollected<WeakDocumentSet>()));
  return *set;
}

void ShowLiveDocumentInstances() {
  WeakDocumentSet& set = LiveDocumentSet();
  fprintf(stderr, "There are %u documents currently alive:\n", set.size());
  for (blink::Document* document : set) {
    fprintf(stderr, "- Document %p URL: %s\n", document,
            document->Url().GetString().Utf8().c_str());
  }
}
#endif
