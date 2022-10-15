// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_CHANGE_NOTIFIER_WIN_H_
#define NET_BASE_NETWORK_CHANGE_NOTIFIER_WIN_H_

#include <netlistmgr.h>
#include <ocidl.h>
#include <windows.h>
#include <wrl.h>
#include <wrl/client.h>

#include <memory>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "base/timer/timer.h"
#include "base/win/object_watcher.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace net {

class NetworkCostManagerEventSink;

// NetworkChangeNotifierWin uses a SequenceChecker, as all its internal
// notification code must be called on the sequence it is created and destroyed
// on.  All the NetworkChangeNotifier methods it implements are threadsafe.
class NET_EXPORT_PRIVATE NetworkChangeNotifierWin
    : public NetworkChangeNotifier,
      public base::win::ObjectWatcher::Delegate {
 public:
  NetworkChangeNotifierWin();
  NetworkChangeNotifierWin(const NetworkChangeNotifierWin&) = delete;
  NetworkChangeNotifierWin& operator=(const NetworkChangeNotifierWin&) = delete;
  ~NetworkChangeNotifierWin() override;

  // Begins listening for a single subsequent address change.  If it fails to
  // start watching, it retries on a timer.  Must be called only once, on the
  // sequence |this| was created on.  This cannot be called in the constructor,
  // as WatchForAddressChangeInternal is mocked out in unit tests.
  // TODO(mmenke): Consider making this function a part of the
  //               NetworkChangeNotifier interface, so other subclasses can be
  //               unit tested in similar fashion, as needed.
  void WatchForAddressChange();

 protected:
  // For unit tests only.
  bool is_watching() const { return is_watching_; }
  void set_is_watching(bool is_watching) { is_watching_ = is_watching; }
  int sequential_failures() const { return sequential_failures_; }

 private:
  friend class NetworkChangeNotifierWinTest;
  friend class TestNetworkChangeNotifierWin;

  // NetworkChangeNotifier methods:
  ConnectionCost GetCurrentConnectionCost() override;

  ConnectionType GetCurrentConnectionType() const override;

  // ObjectWatcher::Delegate methods:
  // Must only be called on the sequence |this| was created on.
  void OnObjectSignaled(HANDLE object) override;

  // Does the actual work to determine the current connection type.
  // It is not thread safe, see crbug.com/324913.
  static ConnectionType RecomputeCurrentConnectionType();

  // Calls RecomputeCurrentConnectionTypeImpl on the DNS sequence and runs
  // |reply_callback| with the type on the calling sequence.
  virtual void RecomputeCurrentConnectionTypeOnBlockingSequence(
      base::OnceCallback<void(ConnectionType)> reply_callback) const;

  void SetCurrentConnectionType(ConnectionType connection_type);

  // Notifies IP address change observers of a change immediately, and notifies
  // network state change observers on a delay.  Must only be called on the
  // sequence |this| was created on.
  void NotifyObservers(ConnectionType connection_type);

  // Forwards connection type notifications to parent class.
  void NotifyParentOfConnectionTypeChange();
  void NotifyParentOfConnectionTypeChangeImpl(ConnectionType connection_type);

  // Tries to start listening for a single subsequent address change.  Returns
  // false on failure.  The caller is responsible for updating |is_watching_|.
  // Virtual for unit tests.  Must only be called on the sequence |this| was
  // created on.
  virtual bool WatchForAddressChangeInternal();

  static NetworkChangeCalculatorParams NetworkChangeCalculatorParamsWin();

  // Gets the current network connection cost (if possible) and caches it.
  void InitializeConnectionCost();
  // Does the work of initializing for thread safety.
  bool InitializeConnectionCostOnce();
  // Retrieves the current network connection cost from the OS's Cost Manager.
  HRESULT UpdateConnectionCostFromCostManager();
  // Converts the OS enum values to the enum values used in our code.
  static ConnectionCost ConnectionCostFromNlmCost(NLM_CONNECTION_COST cost);
  // Sets the cached network connection cost value.
  void SetCurrentConnectionCost(ConnectionCost connection_cost);
  // Callback method for the notification event sink.
  void OnCostChanged();
  // Tells this class that an observer was added and therefore this class needs
  // to register for notifications.
  void ConnectionCostObserverAdded() override;
  // Since ConnectionCostObserverAdded() can be called on any thread and we
  // don't want to do a bunch of work on an arbitrary thread, this method used
  // to post task to do the work.
  void OnConnectionCostObserverAdded();

  // All member variables may only be accessed on the sequence |this| was
  // created on.

  // False when not currently watching for network change events.  This only
  // happens on initialization and when WatchForAddressChangeInternal fails and
  // there is a pending task to try again.  Needed for safe cleanup.
  bool is_watching_ = false;

  base::win::ObjectWatcher addr_watcher_;
  OVERLAPPED addr_overlapped_;

  base::OneShotTimer timer_;

  // Number of times WatchForAddressChange has failed in a row.
  int sequential_failures_ = 0;

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;

  mutable base::Lock last_computed_connection_type_lock_;
  ConnectionType last_computed_connection_type_;

  std::atomic<ConnectionCost> last_computed_connection_cost_ =
      ConnectionCost::CONNECTION_COST_UNKNOWN;

  // Result of IsOffline() when NotifyObserversOfConnectionTypeChange()
  // was last called.
  bool last_announced_offline_;
  // Number of times polled to check if still offline.
  int offline_polls_;

  Microsoft::WRL::ComPtr<INetworkCostManager> network_cost_manager_;
  Microsoft::WRL::ComPtr<NetworkCostManagerEventSink>
      network_cost_manager_event_sink_;

  // Used to ensure that all registration actions are properly sequenced on the
  // same thread regardless of which thread was used to call into the
  // NetworkChangeNotifier API.
  scoped_refptr<base::SequencedTaskRunner> sequence_runner_for_registration_;

  SEQUENCE_CHECKER(sequence_checker_);

  // Used for calling WatchForAddressChange again on failure.
  base::WeakPtrFactory<NetworkChangeNotifierWin> weak_factory_{this};
};

}  // namespace net

#endif  // NET_BASE_NETWORK_CHANGE_NOTIFIER_WIN_H_
