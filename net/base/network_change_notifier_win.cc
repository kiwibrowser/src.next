// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_change_notifier_win.h"

#include <iphlpapi.h>
#include <winsock2.h>

#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/task_runner_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/win/windows_version.h"
#include "net/base/winsock_init.h"
#include "net/base/winsock_util.h"

namespace net {

namespace {

// Time between NotifyAddrChange retries, on failure.
const int kWatchForAddressChangeRetryIntervalMs = 500;

HRESULT GetConnectionPoints(IUnknown* manager,
                            REFIID IIDSyncInterface,
                            IConnectionPoint** connection_point_raw) {
  *connection_point_raw = nullptr;
  Microsoft::WRL::ComPtr<IConnectionPointContainer> connection_point_container;
  HRESULT hr =
      manager->QueryInterface(IID_PPV_ARGS(&connection_point_container));
  if (FAILED(hr))
    return hr;

  // Find the interface
  Microsoft::WRL::ComPtr<IConnectionPoint> connection_point;
  hr = connection_point_container->FindConnectionPoint(IIDSyncInterface,
                                                       &connection_point);
  if (FAILED(hr))
    return hr;

  *connection_point_raw = connection_point.Get();
  (*connection_point_raw)->AddRef();

  return hr;
}

}  // namespace

// This class is used as an event sink to register for notifications from the
// INetworkCostManagerEvents interface. In particular, we are focused on getting
// notified when the Connection Cost changes. This is only supported on Win10+.
class NetworkCostManagerEventSink
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          INetworkCostManagerEvents> {
 public:
  using CostChangedCallback = base::RepeatingCallback<void()>;

  NetworkCostManagerEventSink(INetworkCostManager* cost_manager,
                              const CostChangedCallback& callback)
      : network_cost_manager_(cost_manager), cost_changed_callback_(callback) {}
  ~NetworkCostManagerEventSink() override = default;

  // INetworkCostManagerEvents members
  IFACEMETHODIMP CostChanged(_In_ DWORD cost,
                             _In_opt_ NLM_SOCKADDR* /*pSockAddr*/) override {
    cost_changed_callback_.Run();
    return S_OK;
  }

  IFACEMETHODIMP DataPlanStatusChanged(
      _In_opt_ NLM_SOCKADDR* /*pSockAddr*/) override {
    return S_OK;
  }

  HRESULT RegisterForNotifications() {
    Microsoft::WRL::ComPtr<IUnknown> unknown;
    HRESULT hr = QueryInterface(IID_PPV_ARGS(&unknown));
    if (hr != S_OK)
      return hr;

    hr = GetConnectionPoints(network_cost_manager_.Get(),
                             IID_INetworkCostManagerEvents, &connection_point_);
    if (hr != S_OK)
      return hr;

    hr = connection_point_->Advise(unknown.Get(), &cookie_);
    return hr;
  }

  void UnRegisterForNotifications() {
    if (connection_point_) {
      connection_point_->Unadvise(cookie_);
      connection_point_ = nullptr;
      cookie_ = 0;
    }
  }

 private:
  Microsoft::WRL::ComPtr<INetworkCostManager> network_cost_manager_;
  Microsoft::WRL::ComPtr<IConnectionPoint> connection_point_;
  DWORD cookie_ = 0;
  CostChangedCallback cost_changed_callback_;
};

NetworkChangeNotifierWin::NetworkChangeNotifierWin()
    : NetworkChangeNotifier(NetworkChangeCalculatorParamsWin()),
      blocking_task_runner_(
          base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})),
      last_computed_connection_type_(RecomputeCurrentConnectionType()),
      last_announced_offline_(last_computed_connection_type_ ==
                              CONNECTION_NONE),
      sequence_runner_for_registration_(
          base::SequencedTaskRunnerHandle::Get()) {
  memset(&addr_overlapped_, 0, sizeof addr_overlapped_);
  addr_overlapped_.hEvent = WSACreateEvent();
}

NetworkChangeNotifierWin::~NetworkChangeNotifierWin() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ClearGlobalPointer();
  if (is_watching_) {
    CancelIPChangeNotify(&addr_overlapped_);
    addr_watcher_.StopWatching();
  }
  WSACloseEvent(addr_overlapped_.hEvent);

  if (network_cost_manager_event_sink_) {
    network_cost_manager_event_sink_->UnRegisterForNotifications();
    network_cost_manager_event_sink_ = nullptr;
  }
}

// static
NetworkChangeNotifier::NetworkChangeCalculatorParams
NetworkChangeNotifierWin::NetworkChangeCalculatorParamsWin() {
  NetworkChangeCalculatorParams params;
  // Delay values arrived at by simple experimentation and adjusted so as to
  // produce a single signal when switching between network connections.
  params.ip_address_offline_delay_ = base::Milliseconds(1500);
  params.ip_address_online_delay_ = base::Milliseconds(1500);
  params.connection_type_offline_delay_ = base::Milliseconds(1500);
  params.connection_type_online_delay_ = base::Milliseconds(500);
  return params;
}

// This implementation does not return the actual connection type but merely
// determines if the user is "online" (in which case it returns
// CONNECTION_UNKNOWN) or "offline" (and then it returns CONNECTION_NONE).
// This is challenging since the only thing we can test with certainty is
// whether a *particular* host is reachable.
//
// While we can't conclusively determine when a user is "online", we can at
// least reliably recognize some of the situtations when they are clearly
// "offline". For example, if the user's laptop is not plugged into an ethernet
// network and is not connected to any wireless networks, it must be offline.
//
// There are a number of different ways to implement this on Windows, each with
// their pros and cons. Here is a comparison of various techniques considered:
//
// (1) Use InternetGetConnectedState (wininet.dll). This function is really easy
// to use (literally a one-liner), and runs quickly. The drawback is it adds a
// dependency on the wininet DLL.
//
// (2) Enumerate all of the network interfaces using GetAdaptersAddresses
// (iphlpapi.dll), and assume we are "online" if there is at least one interface
// that is connected, and that interface is not a loopback or tunnel.
//
// Safari on Windows has a fairly simple implementation that does this:
// http://trac.webkit.org/browser/trunk/WebCore/platform/network/win/NetworkStateNotifierWin.cpp.
//
// Mozilla similarly uses this approach:
// http://mxr.mozilla.org/mozilla1.9.2/source/netwerk/system/win32/nsNotifyAddrListener.cpp
//
// The biggest drawback to this approach is it is quite complicated.
// WebKit's implementation for example doesn't seem to test for ICS gateways
// (internet connection sharing), whereas Mozilla's implementation has extra
// code to guess that.
//
// (3) The method used in this file comes from google talk, and is similar to
// method (2). The main difference is it enumerates the winsock namespace
// providers rather than the actual adapters.
//
// I ran some benchmarks comparing the performance of each on my Windows 7
// workstation. Here is what I found:
//   * Approach (1) was pretty much zero-cost after the initial call.
//   * Approach (2) took an average of 3.25 milliseconds to enumerate the
//     adapters.
//   * Approach (3) took an average of 0.8 ms to enumerate the providers.
//
// In terms of correctness, all three approaches were comparable for the simple
// experiments I ran... However none of them correctly returned "offline" when
// executing 'ipconfig /release'.
//
// static
NetworkChangeNotifier::ConnectionType
NetworkChangeNotifierWin::RecomputeCurrentConnectionType() {
  EnsureWinsockInit();

  // The following code was adapted from:
  // http://src.chromium.org/viewvc/chrome/trunk/src/chrome/common/net/notifier/base/win/async_network_alive_win32.cc?view=markup&pathrev=47343
  // The main difference is we only call WSALookupServiceNext once, whereas
  // the earlier code would traverse the entire list and pass LUP_FLUSHPREVIOUS
  // to skip past the large results.

  HANDLE ws_handle;
  WSAQUERYSET query_set = {0};
  query_set.dwSize = sizeof(WSAQUERYSET);
  query_set.dwNameSpace = NS_NLA;
  // Initiate a client query to iterate through the
  // currently connected networks.
  if (0 != WSALookupServiceBegin(&query_set, LUP_RETURN_ALL, &ws_handle)) {
    LOG(ERROR) << "WSALookupServiceBegin failed with: " << WSAGetLastError();
    return NetworkChangeNotifier::CONNECTION_UNKNOWN;
  }

  bool found_connection = false;

  // Retrieve the first available network. In this function, we only
  // need to know whether or not there is network connection.
  // Allocate 256 bytes for name, it should be enough for most cases.
  // If the name is longer, it is OK as we will check the code returned and
  // set correct network status.
  char result_buffer[sizeof(WSAQUERYSET) + 256] = {0};
  DWORD length = sizeof(result_buffer);
  reinterpret_cast<WSAQUERYSET*>(&result_buffer[0])->dwSize =
      sizeof(WSAQUERYSET);
  int result =
      WSALookupServiceNext(ws_handle, LUP_RETURN_NAME, &length,
                           reinterpret_cast<WSAQUERYSET*>(&result_buffer[0]));

  if (result == 0) {
    // Found a connection!
    found_connection = true;
  } else {
    DCHECK_EQ(SOCKET_ERROR, result);
    result = WSAGetLastError();

    // Error code WSAEFAULT means there is a network connection but the
    // result_buffer size is too small to contain the results. The
    // variable "length" returned from WSALookupServiceNext is the minimum
    // number of bytes required. We do not need to retrieve detail info,
    // it is enough knowing there was a connection.
    if (result == WSAEFAULT) {
      found_connection = true;
    } else if (result == WSA_E_NO_MORE || result == WSAENOMORE) {
      // There was nothing to iterate over!
    } else {
      LOG(WARNING) << "WSALookupServiceNext() failed with:" << result;
    }
  }

  result = WSALookupServiceEnd(ws_handle);
  LOG_IF(ERROR, result != 0) << "WSALookupServiceEnd() failed with: " << result;

  // TODO(droger): Return something more detailed than CONNECTION_UNKNOWN.
  return found_connection ? ConnectionTypeFromInterfaces()
                          : NetworkChangeNotifier::CONNECTION_NONE;
}

void NetworkChangeNotifierWin::RecomputeCurrentConnectionTypeOnBlockingSequence(
    base::OnceCallback<void(ConnectionType)> reply_callback) const {
  // Unretained is safe in this call because this object owns the thread and the
  // thread is stopped in this object's destructor.
  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(), FROM_HERE,
      base::BindOnce(&NetworkChangeNotifierWin::RecomputeCurrentConnectionType),
      std::move(reply_callback));
}

NetworkChangeNotifier::ConnectionCost
NetworkChangeNotifierWin::GetCurrentConnectionCost() {
  InitializeConnectionCost();

  // Pre-Win10 use the default logic.
  if (base::win::GetVersion() < base::win::Version::WIN10)
    return NetworkChangeNotifier::GetCurrentConnectionCost();

  // If we don't have the event sink we aren't registered for automatic updates.
  // In that case, we need to update the value at the time it is requested.
  if (!network_cost_manager_event_sink_)
    UpdateConnectionCostFromCostManager();

  return last_computed_connection_cost_;
}

bool NetworkChangeNotifierWin::InitializeConnectionCostOnce() {
  // Pre-Win10 this information cannot be retrieved and cached.
  if (base::win::GetVersion() < base::win::Version::WIN10) {
    SetCurrentConnectionCost(CONNECTION_COST_UNKNOWN);
    return true;
  }

  HRESULT hr =
      ::CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL,
                         IID_INetworkCostManager, &network_cost_manager_);
  if (FAILED(hr)) {
    SetCurrentConnectionCost(CONNECTION_COST_UNKNOWN);
    return true;
  }

  UpdateConnectionCostFromCostManager();

  return true;
}

void NetworkChangeNotifierWin::InitializeConnectionCost() {
  static bool g_connection_cost_initialized = InitializeConnectionCostOnce();
  DCHECK(g_connection_cost_initialized);
}

HRESULT NetworkChangeNotifierWin::UpdateConnectionCostFromCostManager() {
  if (!network_cost_manager_)
    return E_ABORT;

  DWORD cost = NLM_CONNECTION_COST_UNKNOWN;
  HRESULT hr = network_cost_manager_->GetCost(&cost, nullptr);
  if (FAILED(hr)) {
    SetCurrentConnectionCost(CONNECTION_COST_UNKNOWN);
  } else {
    SetCurrentConnectionCost(
        ConnectionCostFromNlmCost((NLM_CONNECTION_COST)cost));
  }
  return hr;
}

// static
NetworkChangeNotifier::ConnectionCost
NetworkChangeNotifierWin::ConnectionCostFromNlmCost(NLM_CONNECTION_COST cost) {
  if (cost == NLM_CONNECTION_COST_UNKNOWN)
    return CONNECTION_COST_UNKNOWN;
  else if ((cost & NLM_CONNECTION_COST_UNRESTRICTED) != 0)
    return CONNECTION_COST_UNMETERED;
  else
    return CONNECTION_COST_METERED;
}

void NetworkChangeNotifierWin::SetCurrentConnectionCost(
    ConnectionCost connection_cost) {
  last_computed_connection_cost_ = connection_cost;
}

void NetworkChangeNotifierWin::OnCostChanged() {
  ConnectionCost old_cost = last_computed_connection_cost_;
  // It is possible to get multiple notifications in a short period of time.
  // Rather than worrying about whether this notification represents the latest,
  // just get the current value from the CostManager so we know that we're
  // actually getting the correct value.
  UpdateConnectionCostFromCostManager();
  // Only notify if there's actually a change.
  if (old_cost != GetCurrentConnectionCost())
    NotifyObserversOfConnectionCostChange();
}

void NetworkChangeNotifierWin::ConnectionCostObserverAdded() {
  sequence_runner_for_registration_->PostTask(
      FROM_HERE,
      base::BindOnce(&NetworkChangeNotifierWin::OnConnectionCostObserverAdded,
                     weak_factory_.GetWeakPtr()));
}

void NetworkChangeNotifierWin::OnConnectionCostObserverAdded() {
  DCHECK(sequence_runner_for_registration_->RunsTasksInCurrentSequence());
  InitializeConnectionCost();

  // No need to register if we don't have a cost manager or if we're already
  // registered.
  if (!network_cost_manager_ || network_cost_manager_event_sink_)
    return;

  network_cost_manager_event_sink_ =
      Microsoft::WRL::Make<net::NetworkCostManagerEventSink>(
          network_cost_manager_.Get(),
          base::BindRepeating(&NetworkChangeNotifierWin::OnCostChanged,
                              weak_factory_.GetWeakPtr()));
  HRESULT hr = network_cost_manager_event_sink_->RegisterForNotifications();
  if (hr != S_OK) {
    // If registration failed for any reason, just destroy the event sink. The
    // observer will remain connected but will not receive any updates. If
    // another observer gets added later, we can re-attempt registration.
    network_cost_manager_event_sink_ = nullptr;
  }
}

NetworkChangeNotifier::ConnectionType
NetworkChangeNotifierWin::GetCurrentConnectionType() const {
  base::AutoLock auto_lock(last_computed_connection_type_lock_);
  return last_computed_connection_type_;
}

void NetworkChangeNotifierWin::SetCurrentConnectionType(
    ConnectionType connection_type) {
  base::AutoLock auto_lock(last_computed_connection_type_lock_);
  last_computed_connection_type_ = connection_type;
}

void NetworkChangeNotifierWin::OnObjectSignaled(HANDLE object) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(is_watching_);
  is_watching_ = false;

  // Start watching for the next address change.
  WatchForAddressChange();

  RecomputeCurrentConnectionTypeOnBlockingSequence(base::BindOnce(
      &NetworkChangeNotifierWin::NotifyObservers, weak_factory_.GetWeakPtr()));
}

void NetworkChangeNotifierWin::NotifyObservers(ConnectionType connection_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SetCurrentConnectionType(connection_type);
  NotifyObserversOfIPAddressChange();

  // Calling GetConnectionType() at this very moment is likely to give
  // the wrong result, so we delay that until a little bit later.
  //
  // The one second delay chosen here was determined experimentally
  // by adamk on Windows 7.
  // If after one second we determine we are still offline, we will
  // delay again.
  offline_polls_ = 0;
  timer_.Start(FROM_HERE, base::Seconds(1), this,
               &NetworkChangeNotifierWin::NotifyParentOfConnectionTypeChange);
}

void NetworkChangeNotifierWin::WatchForAddressChange() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!is_watching_);

  // NotifyAddrChange occasionally fails with ERROR_OPEN_FAILED for unknown
  // reasons.  More rarely, it's also been observed failing with
  // ERROR_NO_SYSTEM_RESOURCES.  When either of these happens, we retry later.
  if (!WatchForAddressChangeInternal()) {
    ++sequential_failures_;

    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&NetworkChangeNotifierWin::WatchForAddressChange,
                       weak_factory_.GetWeakPtr()),
        base::Milliseconds(kWatchForAddressChangeRetryIntervalMs));
    return;
  }

  // Treat the transition from NotifyAddrChange failing to succeeding as a
  // network change event, since network changes were not being observed in
  // that interval.
  if (sequential_failures_ > 0) {
    RecomputeCurrentConnectionTypeOnBlockingSequence(
        base::BindOnce(&NetworkChangeNotifierWin::NotifyObservers,
                       weak_factory_.GetWeakPtr()));
  }

  is_watching_ = true;
  sequential_failures_ = 0;
}

bool NetworkChangeNotifierWin::WatchForAddressChangeInternal() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ResetEventIfSignaled(addr_overlapped_.hEvent);
  HANDLE handle = nullptr;
  DWORD ret = NotifyAddrChange(&handle, &addr_overlapped_);
  if (ret != ERROR_IO_PENDING)
    return false;

  addr_watcher_.StartWatchingOnce(addr_overlapped_.hEvent, this);
  return true;
}

void NetworkChangeNotifierWin::NotifyParentOfConnectionTypeChange() {
  RecomputeCurrentConnectionTypeOnBlockingSequence(base::BindOnce(
      &NetworkChangeNotifierWin::NotifyParentOfConnectionTypeChangeImpl,
      weak_factory_.GetWeakPtr()));
}

void NetworkChangeNotifierWin::NotifyParentOfConnectionTypeChangeImpl(
    ConnectionType connection_type) {
  SetCurrentConnectionType(connection_type);
  bool current_offline = IsOffline();
  offline_polls_++;
  // If we continue to appear offline, delay sending out the notification in
  // case we appear to go online within 20 seconds.  UMA histogram data shows
  // we may not detect the transition to online state after 1 second but within
  // 20 seconds we generally do.
  if (last_announced_offline_ && current_offline && offline_polls_ <= 20) {
    timer_.Start(FROM_HERE, base::Seconds(1), this,
                 &NetworkChangeNotifierWin::NotifyParentOfConnectionTypeChange);
    return;
  }
  if (last_announced_offline_)
    UMA_HISTOGRAM_CUSTOM_COUNTS("NCN.OfflinePolls", offline_polls_, 1, 50, 50);
  last_announced_offline_ = current_offline;

  NotifyObserversOfConnectionTypeChange();
  double max_bandwidth_mbps = 0.0;
  ConnectionType max_connection_type = CONNECTION_NONE;
  GetCurrentMaxBandwidthAndConnectionType(&max_bandwidth_mbps,
                                          &max_connection_type);
  NotifyObserversOfMaxBandwidthChange(max_bandwidth_mbps, max_connection_type);
}

}  // namespace net
