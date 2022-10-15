// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SITE_INSTANCE_GROUP_H_
#define CONTENT_BROWSER_SITE_INSTANCE_GROUP_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/safe_ref.h"
#include "base/observer_list.h"
#include "base/types/id_type.h"
#include "content/browser/renderer_host/agent_scheduling_group_host.h"
#include "content/common/content_export.h"
#include "content/public/browser/browsing_instance_id.h"
#include "content/public/browser/render_process_host_observer.h"
#include "third_party/perfetto/include/perfetto/tracing/traced_proto.h"

namespace perfetto::protos::pbzero {
class SiteInstanceGroup;
}  // namespace perfetto::protos::pbzero

namespace content {

class RenderProcessHost;
class SiteInstance;
class SiteInstanceImpl;
struct ChildProcessTerminationInfo;

using SiteInstanceGroupId = base::IdType32<class SiteInstanceGroupIdTag>;

// A SiteInstanceGroup represents one view of a browsing context group's frame
// trees within a renderer process. It provides a tuning knob, allowing the
// number of groups to vary (for process allocation and
// painting/input/scheduling decisions) without affecting the number of security
// principals that are tracked with SiteInstances.
//
// Similar to layers composing an image from many colors, a set of
// SiteInstanceGroups compose a web page from many renderer processes. Each
// group represents one renderer process' view of a browsing context group,
// containing both local frames (organized into widgets of contiguous frames)
// and proxies for frames in other groups or processes.
//
// The documents in the local frames of a group are organized into
// SiteInstances, representing an atomic group of similar origin documents that
// can access each other directly. A group contains all the documents of one or
// more SiteInstances, all belonging to the same browsing context group (aka
// BrowsingInstance). Each browsing context group has its own set of
// SiteInstanceGroups.
//
// A SiteInstanceGroup is used for generating painted surfaces, directing input
// events, and facilitating communication between frames in different groups.
// The browser process coordinates activities across groups to produce a full
// web page.
//
// A SiteInstanceGroup always has a RenderProcessHost. If the RenderProcessHost
// itself (and not just the renderer process) goes away, then all
// RenderFrameHosts, RenderFrameProxyHosts, and workers using it are gone, and
// the SiteInstanceGroup itself goes away as well. SiteInstances in the group
// may outlive this (e.g., when kept alive by NavigationEntry), in which case
// they will get a new SiteInstanceGroup the next time one is needed.
// SiteInstanceGroups are refcounted by the SiteInstances using them, allowing
// for flexible policies. Currently, each SiteInstanceGroup has exactly one
// SiteInstance. See crbug.com/1195535.
class CONTENT_EXPORT SiteInstanceGroup
    : public base::RefCounted<SiteInstanceGroup>,
      public RenderProcessHostObserver {
 public:
  class CONTENT_EXPORT Observer {
   public:
    // Called when this SiteInstanceGroup transitions to having no active
    // frames, as measured by active_frame_count().
    virtual void ActiveFrameCountIsZero(
        SiteInstanceGroup* site_instance_group) {}

    // Called when the renderer process of this SiteInstanceGroup has exited.
    // Note that GetProcess() still returns the same RenderProcessHost instance.
    // You can reinitialize it by a call to SiteInstance::GetProcess()->Init().
    virtual void RenderProcessGone(SiteInstanceGroup* site_instance_group,
                                   const ChildProcessTerminationInfo& info) {}
  };

  SiteInstanceGroup(BrowsingInstanceId browsing_instance_id,
                    RenderProcessHost* process);

  SiteInstanceGroup(const SiteInstanceGroup&) = delete;
  SiteInstanceGroup& operator=(const SiteInstanceGroup&) = delete;

  SiteInstanceGroupId GetId() const;

  base::SafeRef<SiteInstanceGroup> GetSafeRef();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Used to keep track of the SiteInstances that belong in this group, so they
  // can be notified to clear their references to `this` when it gets
  // destructed.
  void AddSiteInstance(SiteInstanceImpl* site_instance);
  void RemoveSiteInstance(SiteInstanceImpl* site_instance);

  // Increase the number of active frames in this SiteInstanceGroup. This is
  // increased when a frame is created.
  void IncrementActiveFrameCount();

  // Decrease the number of active frames in this SiteInstanceGroup. This is
  // decreased when a frame is destroyed. Decrementing this to zero will notify
  // observers, and may trigger deletion of proxies.
  void DecrementActiveFrameCount();

  // Get the number of active frames which belong to this SiteInstanceGroup. If
  // there are no active frames left, all frames in this SiteInstanceGroup can
  // be safely discarded.
  size_t active_frame_count() const { return active_frame_count_; }

  RenderProcessHost* process() const { return &*process_; }

  BrowsingInstanceId browsing_instance_id() const {
    return browsing_instance_id_;
  }

  AgentSchedulingGroupHost& agent_scheduling_group() {
    DCHECK_EQ(agent_scheduling_group_->GetProcess(), &*process_);
    return *agent_scheduling_group_;
  }

  using TraceProto = perfetto::protos::pbzero::SiteInstanceGroup;
  // Write a representation of this object into a trace.
  void WriteIntoTrace(perfetto::TracedProto<TraceProto> proto) const;

 private:
  friend class RefCounted<SiteInstanceGroup>;
  ~SiteInstanceGroup() override;

  // RenderProcessHostObserver implementation.
  void RenderProcessHostDestroyed(RenderProcessHost* host) override;
  void RenderProcessExited(RenderProcessHost* host,
                           const ChildProcessTerminationInfo& info) override;

  // A unique ID for this SiteInstanceGroup.
  SiteInstanceGroupId id_;

  // ID of the BrowsingInstance this SiteInstanceGroup belongs to.
  const BrowsingInstanceId browsing_instance_id_;

  // The number of active frames in this SiteInstanceGroup.
  size_t active_frame_count_ = 0;

  // Current RenderProcessHost that is rendering pages for this
  // SiteInstanceGroup, and AgentSchedulingGroupHost (within the process) this
  // SiteInstanceGroup belongs to.
  // If the RenderProcessHost gets destroyed, `this` will also be destructed.
  // Any SiteInstances in the group will get a new process and group the next
  // time they need a process. If the process crashes, `this` will not be
  // destructed as long as the RenderProcessHost is still alive.
  const base::SafeRef<RenderProcessHost> process_;
  const base::SafeRef<AgentSchedulingGroupHost> agent_scheduling_group_;

  // List of SiteInstanceImpls that belong in this group. When any SiteInstance
  // in the set goes away, it must also be removed from `site_instances_` to
  // prevent UaF.
  base::flat_set<SiteInstanceImpl*> site_instances_;

  base::ObserverList<Observer, true>::Unchecked observers_;

  base::WeakPtrFactory<SiteInstanceGroup> weak_ptr_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_SITE_INSTANCE_GROUP_H_
