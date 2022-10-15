// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SITE_INSTANCE_IMPL_H_
#define CONTENT_BROWSER_SITE_INSTANCE_IMPL_H_

#include <stddef.h>
#include <stdint.h>

#include "base/check.h"
#include "base/memory/scoped_refptr.h"
#include "content/browser/isolation_context.h"
#include "content/browser/site_info.h"
#include "content/browser/web_exposed_isolation_info.h"
#include "content/common/content_export.h"
#include "content/public/browser/browsing_instance_id.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/site_instance_process_assignment.h"
#include "third_party/perfetto/include/perfetto/tracing/traced_proto.h"
#include "third_party/perfetto/include/perfetto/tracing/traced_value_forward.h"
#include "url/gurl.h"

namespace url {
class Origin;
}

namespace content {

class AgentSchedulingGroupHost;
class BrowserContext;
class BrowsingInstance;
class SiteInstanceGroup;
class StoragePartitionConfig;
class StoragePartitionImpl;
struct UrlInfo;

class CONTENT_EXPORT SiteInstanceImpl final : public SiteInstance {
 public:
  SiteInstanceImpl(const SiteInstanceImpl&) = delete;
  SiteInstanceImpl& operator=(const SiteInstanceImpl&) = delete;

  // Methods for creating a new SiteInstance in a new BrowsingInstance. The
  // documentation for these methods are on the SiteInstance::Create* methods
  // with the same name.
  static scoped_refptr<SiteInstanceImpl> Create(
      BrowserContext* browser_context);
  static scoped_refptr<SiteInstanceImpl> CreateForGuest(
      BrowserContext* browser_context,
      const StoragePartitionConfig& partition_config);
  static scoped_refptr<SiteInstanceImpl> CreateForFencedFrame(
      SiteInstanceImpl* embedder_site_instance);

  // Similar to above, but creates an appropriate SiteInstance in a new
  // BrowsingInstance for a particular `url_info`. This is a more generic
  // version of SiteInstance::CreateForURL(). `url_info` contains the GURL for
  // which we want to create a SiteInstance, along with other state relevant to
  // making process allocation decisions. `is_guest` specifies whether the
  // newly SiteInstance and BrowsingInstance is for a <webview> guest. This is
  // used in site-isolated guests to support cross-BrowsingInstance navigations
  // within a guest; when true, the guest's StoragePartition information must
  // also be provided in `url_info`. `is_fenced` specifies if the
  // BrowsingInstance is for a fenced frame, and is used to isolate them from
  // non-fenced BrowsingInstances.
  static scoped_refptr<SiteInstanceImpl> CreateForUrlInfo(
      BrowserContext* browser_context,
      const UrlInfo& url_info,
      bool is_guest,
      bool is_fenced);

  // Creates a SiteInstance that will be use for a service worker.
  // `url_info` - The UrlInfo for the service worker. It contains the URL and
  //              other information necessary to take process model decisions.
  //
  //              Note: if `is_guest` is false, the URL is the main script URL.
  //              If `is_guest` is true, it is the <webview> guest site URL.
  //
  //              Note: `url_info`'s web_exposed_isolation_info indicates the
  //              web-exposed isolation state of the main script (note that
  //              ServiceWorker "cross-origin isolation" does not require
  //              Cross-Origin-Opener-Policy to be set).
  //
  // `can_reuse_process` - Set to true if the new SiteInstance can use the
  //                       same process as the renderer for `url_info`.
  // `is_guest` - Set to true if the new SiteInstance is for a <webview>
  // guest.
  // `is_fenced` - Set to true if the new SiteInstance is for a service worker
  // initialized by a fenced frame.
  static scoped_refptr<SiteInstanceImpl> CreateForServiceWorker(
      BrowserContext* browser_context,
      const UrlInfo& url_info,
      bool can_reuse_process = false,
      bool is_guest = false,
      bool is_fenced = false);

  // Creates a SiteInstance for |url| like CreateForUrlInfo() would except the
  // instance that is returned has its process_reuse_policy set to
  // REUSE_PENDING_OR_COMMITTED_SITE and the default SiteInstance will never
  // be returned.
  static scoped_refptr<SiteInstanceImpl> CreateReusableInstanceForTesting(
      BrowserContext* browser_context,
      const GURL& url);

  // Creates a SiteInstance for |url| in a new BrowsingInstance for testing
  // purposes. This works similarly to CreateForUrlInfo() but with default
  // parameters that are suitable for most tests.
  static scoped_refptr<SiteInstanceImpl> CreateForTesting(
      BrowserContext* browser_context,
      const GURL& url);

  static bool ShouldAssignSiteForURL(const GURL& url);

  // Returns the SiteInstanceGroup |this| belongs to.
  // Currently, each SiteInstanceGroup has exactly one SiteInstance, but that
  // will change as the migration continues. See crbug.com/1195535.
  SiteInstanceGroup* group() { return site_instance_group_.get(); }

  // Use this to get a related SiteInstance during navigations, where UrlInfo
  // may be requesting opt-in isolation. Outside of navigations, callers just
  // looking up an existing SiteInstance based on a GURL can use
  // GetRelatedSiteInstance (overridden from SiteInstance).
  scoped_refptr<SiteInstanceImpl> GetRelatedSiteInstanceImpl(
      const UrlInfo& url_info);
  bool IsSameSiteWithURLInfo(const UrlInfo& url_info);

  // Returns an AgentSchedulingGroupHost, or creates one if
  // `site_instance_group_` doesn't have one.
  AgentSchedulingGroupHost& GetOrCreateAgentSchedulingGroup();

  // Resets the `site_instance_group_` refptr, and must be called when its
  // RenderProcessHost goes away. `site_instance_group_` can be reassigned later
  // as needed.
  void ResetSiteInstanceGroup();

  // SiteInstance implementation.
  SiteInstanceId GetId() override;
  BrowsingInstanceId GetBrowsingInstanceId() override;
  bool HasProcess() override;
  RenderProcessHost* GetProcess() override;
  BrowserContext* GetBrowserContext() override;
  const GURL& GetSiteURL() override;
  const StoragePartitionConfig& GetStoragePartitionConfig() override;
  scoped_refptr<SiteInstance> GetRelatedSiteInstance(const GURL& url) override;
  bool IsRelatedSiteInstance(const SiteInstance* instance) override;
  size_t GetRelatedActiveContentsCount() override;
  bool RequiresDedicatedProcess() override;
  bool RequiresOriginKeyedProcess() override;
  bool IsSameSiteWithURL(const GURL& url) override;
  bool IsGuest() override;
  SiteInstanceProcessAssignment GetLastProcessAssignmentOutcome() override;
  void WriteIntoTrace(perfetto::TracedProto<TraceProto> context) override;
  int EstimateOriginAgentClusterOverheadForMetrics() override;

  // This is called every time a renderer process is assigned to a SiteInstance
  // and is used by the content embedder for collecting metrics.
  void set_process_assignment(SiteInstanceProcessAssignment assignment) {
    process_assignment_ = assignment;
  }

  // The policy to apply when selecting a RenderProcessHost for the
  // SiteInstance. If no suitable RenderProcessHost for the SiteInstance exists
  // according to the policy, and there are processes with unmatched service
  // workers for the site, the newest process with an unmatched service worker
  // is reused. If still no RenderProcessHost exists a new RenderProcessHost
  // will be created unless the process limit has been reached. When the limit
  // has been reached, the RenderProcessHost reused will be chosen randomly and
  // not based on the site.
  enum class ProcessReusePolicy {
    // In this mode, all instances of the site will be hosted in the same
    // RenderProcessHost.
    PROCESS_PER_SITE,

    // In this mode, the site will be rendered in a RenderProcessHost that is
    // already in use for the site, either for a pending navigation or a
    // committed navigation. If multiple such processes exist, ones that have
    // foreground frames are given priority, and otherwise one is selected
    // randomly.
    REUSE_PENDING_OR_COMMITTED_SITE,

    // In this mode, SiteInstances don't proactively reuse processes. An
    // existing process with an unmatched service worker for the site is reused
    // only for navigations, not for service workers. When the process limit has
    // been reached, a randomly chosen RenderProcessHost is reused as in the
    // other policies.
    DEFAULT,
  };

  void set_process_reuse_policy(ProcessReusePolicy policy) {
    CHECK(!IsDefaultSiteInstance());
    process_reuse_policy_ = policy;
  }
  ProcessReusePolicy process_reuse_policy() const {
    return process_reuse_policy_;
  }

  // Returns true if |has_site_| is true and |site_info_| indicates that the
  // process-per-site model should be used.
  bool ShouldUseProcessPerSite() const;

  // Checks if |current_process| can be reused for this SiteInstance, and
  // sets |process_| to |current_process| if so.
  void ReuseCurrentProcessIfPossible(RenderProcessHost* current_process);

  // Whether the SiteInstance is created for a service worker. If this flag
  // is true, when a new process is created for this SiteInstance or a randomly
  // chosen existing process is reused because of the process limit, the process
  // will be tracked as having an unmatched service worker until reused by
  // another SiteInstance from the same site.
  bool is_for_service_worker() const { return is_for_service_worker_; }

  // Returns the URL which was used to set the |site_info_| for this
  // SiteInstance. May be empty if this SiteInstance does not have a
  // |site_info_|.
  const GURL& original_url() {
    DCHECK(!IsDefaultSiteInstance());
    return original_url_;
  }

  // This is primarily a helper for RenderFrameHostImpl::IsNavigationSameSite();
  // most callers should use that API.
  //
  // Returns true if navigating a frame with (|last_successful_url| and
  // |last_committed_origin|) to |dest_url_info| should stay in the same
  // SiteInstance to preserve scripting relationships. |dest_url_info| carries
  // additional state, e.g. if the destination url requests origin isolation.
  //
  // |for_outermost_main_frame| is set to true if the caller is interested in an
  // answer for a outermost main frame. This is set to false for subframe or
  // embedded main frame (eg fenced frame) navigations.  Note: In some
  // circumstances, like hosted apps, different answers can be returned if we
  // are navigating an outermost main frame instead of an embedded frame.
  bool IsNavigationSameSite(const GURL& last_successful_url,
                            const url::Origin& last_committed_origin,
                            bool for_outermost_main_frame,
                            const UrlInfo& dest_url_info);

  // Returns true if a navigation to |dest_url| should be allowed to stay in
  // the current process due to effective URLs being involved in the
  // navigation, even if the navigation would normally result in a new process.
  //
  // This is needed to avoid BrowsingInstance swaps in cases where same-site
  // navigations transition from a hosted app to a non-hosted app URL and must
  // be kept in the same process due to scripting requirements.
  bool IsNavigationAllowedToStayInSameProcessDueToEffectiveURLs(
      BrowserContext* browser_context,
      bool for_main_frame,
      const GURL& dest_url);

  // SiteInfo related functions.

  // Returns the SiteInfo principal identifying all documents and workers within
  // this SiteInstance.
  // TODO(wjmaclean): eventually this function will replace const GURL&
  // GetSiteURL().
  const SiteInfo& GetSiteInfo();

  // Derives a new SiteInfo based on this SiteInstance's current state, and
  // the information provided in `url_info`. This function is slightly different
  // than SiteInfo::Create() because it takes into account information
  // specific to this SiteInstance, like whether it is a guest or not, and
  // changes its behavior accordingly. `is_related` - Controls the SiteInfo
  // returned for non-guest SiteInstances.
  //  Set to true if the caller wants the SiteInfo for an existing related
  //  SiteInstance associated with `url_info`. This is identical to what you
  //  would get from GetRelatedSiteInstanceImpl(url_info)->GetSiteInfo(). This
  //  may return the SiteInfo for the default SiteInstance so callers must be
  //  prepared to deal with that. If set to false, a SiteInfo created with
  //  SiteInfo::Create() is returned.
  //
  // For guest SiteInstances, `site_info_` is returned because guests are not
  // allowed to derive new guest SiteInfos. All guest navigations must stay in
  // the same SiteInstance with the same SiteInfo.
  //
  // Note: Since we're deriving the state of the SiteInfo based on both UrlInfo
  // and SiteInstance, we verify internally that their WebExposedIsolationInfos
  // are compatible.
  SiteInfo DeriveSiteInfo(const UrlInfo& url_info, bool is_related = false);

  // Helper function that returns the storage partition domain for this
  // object.
  // This is a temporary helper function used to verify that
  // the partition domain computed using this SiteInstance's site URL matches
  // the partition domain returned by storage_partition->GetPartitionDomain().
  // If there is a mismatch, we call DumpWithoutCrashing() and return the value
  // computed from the site URL since that is the legacy behavior.
  //
  // TODO(acolwell) : Remove this function and update callers to directly call
  // storage_partition->GetPartitionDomain() once we've verified that this is
  // safe.
  std::string GetPartitionDomain(StoragePartitionImpl* storage_partition);

  // Returns true if this SiteInstance is for a site that has JIT disabled.
  bool IsJitDisabled();

  // Returns true if this SiteInstance is for a site that contains PDF contents.
  bool IsPdf();

  // Set the web site that this SiteInstance is rendering pages for.
  // This includes the scheme and registered domain, but not the port.  If the
  // URL does not have a valid registered domain, then the full hostname is
  // stored. This method does not convert this instance into a default
  // SiteInstance, but the BrowsingInstance will call this method with
  // |url_info| set to GetDefaultSiteURL(), when it is creating its default
  // SiteInstance.
  void SetSite(const UrlInfo& url_info);

  // Same as above, but for SiteInfo. The above version should be used in most
  // cases, unless the UrlInfo is unavailable, such as for sandboxed srcdoc
  // frames.
  void SetSite(const SiteInfo& site_info);

  // Similar to SetSite(), but first attempts to convert this object to a
  // default SiteInstance if |url_info| can be placed inside a default
  // SiteInstance. If conversion is not possible, then the normal SetSite()
  // logic is run.
  void ConvertToDefaultOrSetSite(const UrlInfo& url_info);

  // Returns whether SetSite() has been called.
  bool HasSite() const;

  // Returns whether there is currently a related SiteInstance (registered with
  // BrowsingInstance) for the given SiteInfo.  If so, we should try to avoid
  // dedicating an unused SiteInstance to it (e.g., in a new tab).
  bool HasRelatedSiteInstance(const SiteInfo& site_info);

  // Returns whether this SiteInstance is compatible with and can host the given
  // |url_info|. If not, the browser should force a SiteInstance swap when
  // navigating to the URL in |url_info|.
  bool IsSuitableForUrlInfo(const UrlInfo& url_info);

  // Increase the number of active WebContentses using this SiteInstance. Note
  // that, unlike active_frame_count, this does not count pending RFHs.
  void IncrementRelatedActiveContentsCount();

  // Decrease the number of active WebContentses using this SiteInstance. Note
  // that, unlike active_frame_count, this does not count pending RFHs.
  void DecrementRelatedActiveContentsCount();

  // Whether GetProcess() method (when it needs to find a new process to
  // associate with the current SiteInstanceImpl) can return a spare process.
  bool CanAssociateWithSpareProcess();

  // Has no effect if the SiteInstanceImpl already has a |process_|.
  // Otherwise, prevents GetProcess() from associating this SiteInstanceImpl
  // with the spare RenderProcessHost - instead GetProcess will either need to
  // create a new, not-yet-initialized/spawned RenderProcessHost or will need to
  // reuse one of existing RenderProcessHosts.
  //
  // See also:
  // - https://crbug.com/840409.
  // - WebContents::CreateParams::desired_renderer_state
  // - SiteInstanceImpl::CanAssociateWithSpareProcess().
  void PreventAssociationWithSpareProcess();

  // Returns the special site URL used by the default SiteInstance.
  static const GURL& GetDefaultSiteURL();

  // Get the effective URL for the given actual URL.  This allows the
  // ContentBrowserClient to override the SiteInstance's site for certain URLs.
  // For example, Chrome uses this to replace hosted app URLs with extension
  // hosts.
  // Only public so that we can make a consistent process swap decision in
  // RenderFrameHostManager.
  static GURL GetEffectiveURL(BrowserContext* browser_context, const GURL& url);

  // True if |url| resolves to an effective URL that is different from |url|.
  // See GetEffectiveURL().  This will be true for hosted apps as well as NTP
  // URLs.
  static bool HasEffectiveURL(BrowserContext* browser_context, const GURL& url);

  // Return an ID of the next BrowsingInstance to be created.  This ID is
  // guaranteed to be higher than any ID of an existing BrowsingInstance.
  // This is useful when process model decisions need to be scoped only to
  // future BrowsingInstances.  In particular, this can determine the cutoff in
  // BrowsingInstance IDs when adding a new isolated origin dynamically.
  static BrowsingInstanceId NextBrowsingInstanceId();

  // Return the IsolationContext associated with this SiteInstance.  This
  // specifies context for making process model decisions, such as information
  // about the current BrowsingInstance.
  const IsolationContext& GetIsolationContext();

  // Returns a process suitable for this SiteInstance if the
  // SiteInstanceGroupManager has one available. A null pointer will be returned
  // if this SiteInstance's group does not have a process yet or the
  // SiteInstanceGroupManager does not have a default process that can be reused
  // by this SiteInstance.
  RenderProcessHost* GetSiteInstanceGroupProcessIfAvailable();

  // Returns true if this object was constructed as a default site instance.
  bool IsDefaultSiteInstance() const;

  // Returns true if |site_url| is a site url that the BrowsingInstance has
  // associated with its default SiteInstance.
  bool IsSiteInDefaultSiteInstance(const GURL& site_url) const;

  // Returns true if the SiteInfo for |url_info| matches the SiteInfo for this
  // instance (i.e. GetSiteInfo()). Otherwise returns false.
  bool DoesSiteInfoForURLMatch(const UrlInfo& url_info);

  // Adds |origin| as having the default isolation state within this
  // BrowsingInstance due to an existing instance at the time of opt-in, so that
  // future instances of it here won't be origin isolated.
  void RegisterAsDefaultOriginIsolation(
      const url::Origin& previously_visited_origin);

  // Returns the web-exposed isolation status of the BrowsingInstance this
  // SiteInstance is part of.
  const WebExposedIsolationInfo& GetWebExposedIsolationInfo() const;

  // Simple helper function that returns the is_isolated property of the
  // WebExposedIsolationInfo of this BrowsingInstance.
  bool IsCrossOriginIsolated() const;

  // Finds an existing SiteInstance in this SiteInstance's BrowsingInstance that
  // matches this `url_info` but with the `is_sandboxed_` flag true. It's
  // assumed that `url_info.url` is 'about:srcdoc' here, so the new SiteInstance
  // will use `parent_origin`. If an existing SiteInstance isn't found, a new
  // one is created in the same BrowsingInstance. Note that this SiteInstance
  // must have had its SiteInfo already assigned via SetSite() before calling
  // this function.
  scoped_refptr<SiteInstanceImpl> GetCompatibleSandboxedSiteInstance(
      const UrlInfo& url_info,
      const url::Origin& parent_origin);

  // Returns the process used by non-isolated sites in this SiteInstance's
  // BrowsingInstance.
  RenderProcessHost* GetDefaultProcessForBrowsingInstance();

 private:
  friend class BrowsingInstance;
  friend class SiteInstanceTestBrowserClient;

  // Friend tests that need direct access to IsSameSite().
  friend class SiteInstanceTest;

  // Create a new SiteInstance.  Only BrowsingInstance should call this
  // directly; clients should use Create() or GetRelatedSiteInstance() instead.
  explicit SiteInstanceImpl(BrowsingInstance* browsing_instance);

  ~SiteInstanceImpl() override;

  // Returns true when |this| has a SiteInstanceGroup.
  bool has_group() { return group() != nullptr; }

  // Used to restrict a process' origin access rights. This method gets called
  // when a process gets assigned to this SiteInstance and when the
  // SiteInfo is explicitly set. If the SiteInfo hasn't been set yet and
  // the current process lock is invalid, then this method sets the process
  // to an "allow_any_site" lock. If the SiteInfo gets set to something that
  // restricts access to a specific site, then the lock will be upgraded to a
  // "lock_to_site" lock.
  void LockProcessIfNeeded();

  // If kProcessSharingWithStrictSiteInstances is enabled, this will check
  // whether both a site and a process have been assigned to this SiteInstance,
  // and if this doesn't require a dedicated process, will offer process_ to
  // BrowsingInstance as the default process for SiteInstances that don't need
  // a dedicated process.
  void MaybeSetBrowsingInstanceDefaultProcess();

  // Sets the SiteInfo and other fields so that this instance becomes a
  // default SiteInstance.
  void SetSiteInfoToDefault(
      const StoragePartitionConfig& storage_partition_config);

  // Sets |site_info_| with |site_info| and registers this object with
  // |browsing_instance_|. SetSite() calls this method to set the site and lock
  // for a user provided URL. This method should only be called by code that
  // need to set the site and lock directly without any "url to site URL"
  // transformation.
  void SetSiteInfoInternal(const SiteInfo& site_info);

  // Helper method to set the process of this SiteInstance, only in cases
  // where it is safe. It is not generally safe to change the process of a
  // SiteInstance, unless the RenderProcessHost itself is entirely destroyed and
  // a new one later replaces it.
  void SetProcessInternal(RenderProcessHost* process);

  // Returns true if |original_url()| is the same site as
  // |dest_url_info| or this object is a default SiteInstance and can be
  // considered the same site as |dest_url_info|.
  bool IsOriginalUrlSameSite(const UrlInfo& dest_url_info,
                             bool should_compare_effective_urls);

  // Add |site_info| to the set that tracks what sites have been allowed
  // to be handled by this default SiteInstance.
  void AddSiteInfoToDefault(const SiteInfo& site_info);

  // Return whether both UrlInfos must share a process to preserve script
  // relationships.  The decision is based on a variety of factors such as
  // the registered domain of the URLs (google.com, bbc.co.uk), the scheme
  // (https, http), and isolated origins.  Note that if the destination is a
  // blank page, we consider that to be part of the same web site for the
  // purposes for process assignment.  |should_compare_effective_urls| allows
  // comparing URLs without converting them to effective URLs first.  This is
  // useful for avoiding OOPIFs when otherwise same-site URLs may look
  // cross-site via their effective URLs.
  // Note: This method is private because it is an internal detail of this class
  // and there is subtlety around how it can be called because of hosted
  // apps. Most code outside this class should call
  // RenderFrameHostImpl::IsNavigationSameSite() instead.
  static bool IsSameSite(const IsolationContext& isolation_context,
                         const UrlInfo& src_url_info,
                         const UrlInfo& dest_url_info,
                         bool should_compare_effective_urls);

  // Returns true if |url| and its |site_url| can be placed inside a default
  // SiteInstance.
  //
  // Note: |url| and |site_info| must be consistent with each other. In contexts
  // where the caller only has |url| it can use
  // SiteInfo::Create() to generate |site_info|. This call is
  // intentionally not set as a default value to encourage the caller to reuse
  // a SiteInfo computation if they already have one.
  static bool CanBePlacedInDefaultSiteInstance(
      const IsolationContext& isolation_context,
      const GURL& url,
      const SiteInfo& site_info);

  // A unique ID for this SiteInstance.
  SiteInstanceId id_;

  // Determines which RenderViewHosts, RenderWidgetHosts, and
  // RenderFrameProxyHosts it uses.
  // `site_instance_group_` is set when a RenderProcessHost is set for this
  // SiteInstance, and will be how `this` gets its RenderProcessHost and
  // AgentSchedulingGroup.
  // If the RenderProcessHost goes away, `site_instance_group_` will get reset.
  // It can be set to another group later on as needed.
  // See the class-level comment of SiteInstanceGroup for more details.
  scoped_refptr<SiteInstanceGroup> site_instance_group_;

  // BrowsingInstance to which this SiteInstance belongs.
  scoped_refptr<BrowsingInstance> browsing_instance_;

  // Describes the desired behavior when GetProcess() method needs to find a new
  // process to associate with the current SiteInstanceImpl.  If |false|, then
  // prevents the spare RenderProcessHost from being taken and stored in
  // |process_|.
  bool can_associate_with_spare_process_;

  // The SiteInfo that this SiteInstance is rendering pages for.
  SiteInfo site_info_;

  // Whether SetSite has been called.
  bool has_site_;

  // The URL which was used to set the |site_info_| for this SiteInstance.
  GURL original_url_;

  // The ProcessReusePolicy to use when creating a RenderProcessHost for this
  // SiteInstance.
  ProcessReusePolicy process_reuse_policy_;

  // Whether the SiteInstance was created for a service worker.
  bool is_for_service_worker_;

  // How |this| was last assigned to a renderer process.
  SiteInstanceProcessAssignment process_assignment_;

  // Contains the state that is only required for default SiteInstances.
  class DefaultSiteInstanceState;
  std::unique_ptr<DefaultSiteInstanceState> default_site_instance_state_;

  // Keeps track of whether we need to verify that the StoragePartition
  // information does not change when `site_info_` is set.
  bool verify_storage_partition_info_ = false;
};

}  // namespace content

#endif  // CONTENT_BROWSER_SITE_INSTANCE_IMPL_H_
