// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_EVENT_LISTENER_MAP_H_
#define EXTENSIONS_BROWSER_EVENT_LISTENER_MAP_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "extensions/common/event_filter.h"
#include "extensions/common/extension_id.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_database.mojom-forward.h"
#include "url/gurl.h"

namespace base {
class DictionaryValue;
}

namespace content {
class BrowserContext;
class RenderProcessHost;
}

namespace extensions {
struct Event;

// A listener for an extension event. A listener is essentially an endpoint
// that an event can be dispatched to.
//
// This is a lazy listener if |IsLazy| is returns true, and a filtered listener
// if |filter| is defined.
//
// A lazy listener is added to an event to indicate that a lazy background page
// is listening to the event. It is associated with no process, so to dispatch
// an event to a lazy listener one must start a process running the associated
// extension and dispatch the event to that.
class EventListener {
 public:
  // Constructs EventListeners for either an Extension or a URL.
  //
  // |filter| represents a generic filter structure that EventFilter knows how
  // to filter events with. A typical filter instance will look like
  //
  // {
  //   url: [{hostSuffix: 'google.com'}],
  //   tabId: 5
  // }
  static std::unique_ptr<EventListener> ForExtension(
      const std::string& event_name,
      const std::string& extension_id,
      content::RenderProcessHost* process,
      std::unique_ptr<base::DictionaryValue> filter);
  static std::unique_ptr<EventListener> ForURL(
      const std::string& event_name,
      const GURL& listener_url,
      content::RenderProcessHost* process,
      std::unique_ptr<base::DictionaryValue> filter);
  // Constructs EventListener for an Extension service worker.
  // Similar to ForExtension above with the only difference that
  // |worker_thread_id_| contains a valid worker thread, as opposed to
  // kMainThreadId and |service_worker_version_id| contains a valid service
  // worker version id instead of kInvalidServiceWorkerVersionId.
  static std::unique_ptr<EventListener> ForExtensionServiceWorker(
      const std::string& event_name,
      const std::string& extension_id,
      content::RenderProcessHost* process,
      const GURL& service_worker_scope,
      int64_t service_worker_version_id,
      int worker_thread_id,
      std::unique_ptr<base::DictionaryValue> filter);

  EventListener(const EventListener&) = delete;
  EventListener& operator=(const EventListener&) = delete;

  ~EventListener();

  bool Equals(const EventListener* other) const;

  std::unique_ptr<EventListener> Copy() const;

  // Returns true if the listener is for a lazy context: e.g. a background page
  // or an extension service worker. This listener does not have |process_|.
  bool IsLazy() const;

  // Returns true if this listener (lazy or not) was registered for an extension
  // service worker.
  bool is_for_service_worker() const { return is_for_service_worker_; }

  // Modifies this listener to be a lazy listener, clearing process references.
  void MakeLazy();

  // Returns the browser context associated with the listener, or NULL if
  // IsLazy.
  content::BrowserContext* GetBrowserContext() const;

  const std::string& event_name() const { return event_name_; }
  const std::string& extension_id() const { return extension_id_; }
  const GURL& listener_url() const { return listener_url_; }
  content::RenderProcessHost* process() const { return process_; }
  base::DictionaryValue* filter() const { return filter_.get(); }
  EventFilter::MatcherID matcher_id() const { return matcher_id_; }
  void set_matcher_id(EventFilter::MatcherID id) { matcher_id_ = id; }
  int64_t service_worker_version_id() const {
    return service_worker_version_id_;
  }
  int worker_thread_id() const { return worker_thread_id_; }

 private:
  EventListener(const std::string& event_name,
                const std::string& extension_id,
                const GURL& listener_url,
                content::RenderProcessHost* process,
                bool is_for_service_worker,
                int64_t service_worker_version_id,
                int worker_thread_id,
                std::unique_ptr<base::DictionaryValue> filter);

  const std::string event_name_;
  const std::string extension_id_;
  const GURL listener_url_;
  raw_ptr<content::RenderProcessHost> process_ = nullptr;

  const bool is_for_service_worker_ = false;

  int64_t service_worker_version_id_ =
      blink::mojom::kInvalidServiceWorkerVersionId;

  // If this listener is for a service worker (i.e.
  // is_for_service_worker_ = true) and the worker is in running state, then
  // this is the worker's thread id in the worker |process_|. For lazy service
  // worker events, this will be kMainThreadId.
  int worker_thread_id_;

  std::unique_ptr<base::DictionaryValue> filter_;
  EventFilter::MatcherID matcher_id_;  // -1 if unset.
};

// Holds listeners for extension events and can answer questions about which
// listeners are interested in what events.
class EventListenerMap {
 public:
  using ListenerList = std::vector<std::unique_ptr<EventListener>>;
  // The key here is an event name.
  using ListenerMap = std::unordered_map<std::string, ListenerList>;

  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnListenerAdded(const EventListener* listener) = 0;
    virtual void OnListenerRemoved(const EventListener* listener) = 0;
  };

  explicit EventListenerMap(Delegate* delegate);

  EventListenerMap(const EventListenerMap&) = delete;
  EventListenerMap& operator=(const EventListenerMap&) = delete;

  ~EventListenerMap();

  // Add a listener for a particular event. GetEventListeners() will include a
  // weak pointer to |listener| in its results if passed a relevant
  // extensions::Event.
  // Returns true if the listener was added (in the case that it has never been
  // seen before).
  bool AddListener(std::unique_ptr<EventListener> listener);

  // Remove a listener that .Equals() |listener|.
  // Returns true if the listener was removed .
  bool RemoveListener(const EventListener* listener);

  // Get the map of all EventListeners.
  const ListenerMap& listeners() const { return listeners_; }

  // Returns the set of listeners that want to be notified of |event|.
  std::set<const EventListener*> GetEventListeners(const Event& event);

  const ListenerList& GetEventListenersByName(const std::string& event_name) {
    return listeners_[event_name];
  }

  // Removes all listeners with process equal to |process|.
  void RemoveListenersForProcess(const content::RenderProcessHost* process);

  // Returns true if there are any listeners on the event named |event_name|.
  bool HasListenerForEvent(const std::string& event_name) const;

  // Returns true if there are any listeners on |event_name| from
  // |extension_id|.
  bool HasListenerForExtension(const std::string& extension_id,
                               const std::string& event_name) const;

  // Returns true if this map contains an EventListener that .Equals()
  // |listener|.
  bool HasListener(const EventListener* listener) const;

  // Returns true if there is a listener for |extension_id| in |process|.
  // |worker_thread_id| is the thread id of the service worker the listener is
  // for, or kMainThreadId if the listener is not for a service worker.
  bool HasProcessListener(content::RenderProcessHost* process,
                          int worker_thread_id,
                          const std::string& extension_id) const;

  // Removes any listeners that |extension_id| has added, both lazy and regular.
  void RemoveListenersForExtension(const std::string& extension_id);

  // Adds unfiltered lazy listeners as described their serialised descriptions.
  // |event_names| the names of the lazy events.
  // Note that we can only load lazy listeners in this fashion, because there
  // is no way to serialise a RenderProcessHost*.
  void LoadUnfilteredLazyListeners(const std::string& extension_id,
                                   const std::set<std::string>& event_names);
  // Similar as above, but applies to extension service workers.
  void LoadUnfilteredWorkerListeners(const std::string& extension_id,
                                     const std::set<std::string>& event_names);

  // Adds filtered lazy listeners as described their serialised descriptions.
  // |is_for_service_worker| is true for extension service worker event
  // listeners.
  // |filtered| contains a map from event names to filters, each pairing
  // defining a lazy filtered listener.
  void LoadFilteredLazyListeners(const std::string& extension_id,
                                 bool is_for_service_worker,
                                 const base::DictionaryValue& filtered);

 private:

  void CleanupListener(EventListener* listener);
  bool IsFilteredEvent(const Event& event) const;
  std::unique_ptr<EventMatcher> ParseEventMatcher(
      base::DictionaryValue* filter_dict);

  // Listens for removals from this map.
  const raw_ptr<Delegate> delegate_;

  std::set<std::string> filtered_events_;
  ListenerMap listeners_;

  std::map<EventFilter::MatcherID, EventListener*> listeners_by_matcher_id_;

  EventFilter event_filter_;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_EVENT_LISTENER_MAP_H_
