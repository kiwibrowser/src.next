// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/network_service_instance_impl.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/feature_list.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/message_loop/message_pump_type.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/sequence_local_storage_slot.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "content/browser/browser_main_loop.h"
#include "content/browser/first_party_sets/first_party_sets_handler_impl.h"
#include "content/browser/net/http_cache_backend_file_operations_factory.h"
#include "content/browser/net/socket_broker_impl.h"
#include "content/browser/network_sandbox_grant_result.h"
#include "content/browser/network_service_client.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "content/public/common/network_service_util.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/base/features.h"
#include "net/log/net_log_util.h"
#include "sandbox/policy/features.h"
#include "services/cert_verifier/cert_verifier_service_factory.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"
#include "services/network/network_service.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/network_switches.h"
#include "services/network/public/mojom/net_log.mojom.h"
#include "services/network/public/mojom/network_change_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "services/network/public/mojom/network_service_test.mojom.h"
#include "services/network/public/mojom/socket_broker.mojom.h"

#if !BUILDFLAG(IS_ANDROID)
#include "content/browser/network_sandbox.h"
#endif

namespace content {

namespace {

#if BUILDFLAG(IS_POSIX)
// Environment variable pointing to credential cache file.
constexpr char kKrb5CCEnvName[] = "KRB5CCNAME";
// Environment variable pointing to Kerberos config file.
constexpr char kKrb5ConfEnvName[] = "KRB5_CONFIG";
#endif

bool g_force_create_network_service_directly = false;
mojo::Remote<network::mojom::NetworkService>* g_network_service_remote =
    nullptr;
network::NetworkConnectionTracker* g_network_connection_tracker;
bool g_network_service_is_responding = false;
base::Time g_last_network_service_crash;

// A directory name that is created below the http cache path and passed to the
// network context when creating a network context with cache enabled.
// This must be a directory below the main cache path so operations such as
// resetting the cache via HttpCacheParams.reset_cache can function correctly
// as they rely on having access to the parent directory of the cache.
const base::FilePath::CharType kCacheDataDirectoryName[] =
    FILE_PATH_LITERAL("Cache_Data");

std::unique_ptr<network::NetworkService>& GetLocalNetworkService() {
  static base::SequenceLocalStorageSlot<
      std::unique_ptr<network::NetworkService>>
      service;
  return service.GetOrCreateValue();
}

// If this feature is enabled, the Network Service will run on its own thread
// when running in-process; otherwise it will run on the IO thread.
//
// On Chrome OS, the Network Service must run on the IO thread because
// ProfileIOData and NetworkContext both try to set up NSS, which has to be
// called from the IO thread.
const base::Feature kNetworkServiceDedicatedThread {
  "NetworkServiceDedicatedThread",
#if BUILDFLAG(IS_CHROMEOS)
      base::FEATURE_DISABLED_BY_DEFAULT
#else
      base::FEATURE_ENABLED_BY_DEFAULT
#endif
};

base::Thread& GetNetworkServiceDedicatedThread() {
  static base::NoDestructor<base::Thread> thread{"NetworkService"};
  DCHECK(base::FeatureList::IsEnabled(kNetworkServiceDedicatedThread));
  return *thread;
}

// The instance NetworkService used when hosting the service in-process. This is
// set up by |CreateInProcessNetworkServiceOnThread()| and destroyed by
// |ShutDownNetworkService()|.
network::NetworkService* g_in_process_instance = nullptr;

static NetworkServiceClient* g_client = nullptr;

void CreateInProcessNetworkServiceOnThread(
    mojo::PendingReceiver<network::mojom::NetworkService> receiver) {
  // The test interface doesn't need to be implemented in the in-process case.
  auto registry = std::make_unique<service_manager::BinderRegistry>();
  registry->AddInterface(base::BindRepeating(
      [](mojo::PendingReceiver<network::mojom::NetworkServiceTest>) {}));
  g_in_process_instance = new network::NetworkService(
      std::move(registry), std::move(receiver),
      true /* delay_initialization_until_set_client */);
}

// A utility function to make it clear what behavior is expected by the network
// context instance depending on the various errors that can happen during data
// migration.
//
// If this function returns 'true' then the `data_directory` should be used (if
// specified in the network context params). If this function returns 'false'
// then the `unsandboxed_data_path` should be used.
bool IsSafeToUseDataPath(SandboxGrantResult result) {
  switch (result) {
    case SandboxGrantResult::kSuccess:
      // A migration occurred, and it was successful.
      return true;
    case SandboxGrantResult::kFailedToGrantSandboxAccessToCache:
    case SandboxGrantResult::kFailedToCreateCacheDirectory:
      // A failure to grant create or grant access to the cache dir does not
      // affect the providence of the data contained in `data_directory` as the
      // migration could have still occurred.
      //
      // These cases are handled internally and so this case should never be
      // hit. It is undefined behavior to proceed in this case so CHECK here.
      IMMEDIATE_CRASH();
      return false;
    case SandboxGrantResult::kFailedToCreateDataDirectory:
      // A failure to create the `data_directory` is fatal, and the
      // `unsandboxed_data_path` should be used.
      return false;
    case SandboxGrantResult::kFailedToCopyData:
      // A failure to copy the data from `unsandboxed_data_path` to the
      // `data_directory` is fatal, and the `unsandboxed_data_path` should be
      // used.
      return false;
    case SandboxGrantResult::kFailedToDeleteOldData:
      // This is not fatal, as the new data has been correctly migrated, and the
      // deletion will be retried at a later time.
      return true;
    case SandboxGrantResult::kFailedToGrantSandboxAccessToData:
      // If the sandbox could not be granted access to the new data dir, then
      // don't attempt to migrate. This means that the old
      // `unsandboxed_data_path` should be used.
      return false;
    case SandboxGrantResult::kDidNotAttemptToGrantSandboxAccess:
      // No migration was attempted either because of platform constraints or
      // because the network context had no valid data paths (e.g. in-memory or
      // incognito), or `unsandboxed_data_path` was not specified.
      // `data_directory` should be used in this case (if present).
      return true;
    case SandboxGrantResult::kFailedToCreateCheckpointFile:
      // This is fatal, as a failure to create the checkpoint file means that
      // the next time the same network context is used, the data in
      // `unsandboxed_data_path` will be re-copied to the new `data_directory`
      // and thus any changes to the data will be discarded. So in this case,
      // `unsandboxed_data_path` should be used.
      return false;
    case SandboxGrantResult::kNoMigrationRequested:
      // The caller supplied an `unsandboxed_data_path` but did not trigger a
      // migration so the data should be read from the `unsandboxed_data_path`.
      return false;
    case SandboxGrantResult::kMigrationAlreadySucceeded:
      // Migration has already taken place, so `data_directory` contains the
      // valid data.
      return true;
    case SandboxGrantResult::kMigrationAlreadySucceededWithNoAccess:
      // If the sandbox could not be granted access to the new data dir, but the
      // migration has already happened to `data_directory`. This means that the
      // sandbox might not have access to the data but `data_directory` should
      // still be used because it's been migrated.
      return true;
  }
}

// Takes a cache dir and deletes all files in it except those in 'Cache_Data'
// directory. This can be removed once all caches have been moved to the new
// sub-directory, around M99.
void MaybeDeleteOldCache(const base::FilePath& cache_dir) {
  bool deleted_old_files = false;
  base::FileEnumerator enumerator(
      cache_dir, /*recursive=*/false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);

  for (auto name = enumerator.Next(); !name.empty(); name = enumerator.Next()) {
    base::FileEnumerator::FileInfo info = enumerator.GetInfo();
    DCHECK_EQ(info.GetName(), name.BaseName());

    if (info.IsDirectory()) {
      if (name.BaseName().value() == kCacheDataDirectoryName)
        continue;
    }
    base::DeletePathRecursively(name);
    deleted_old_files = true;
  }

  base::UmaHistogramBoolean("NetworkService.DeletedOldCacheData",
                            deleted_old_files);
}

void CreateNetworkContextInternal(
    mojo::PendingReceiver<network::mojom::NetworkContext> context,
    network::mojom::NetworkContextParamsPtr params,
    SandboxGrantResult grant_access_result) {
  TRACE_EVENT0("loading", "CreateNetworkContextInternal");
  // These two histograms are logged from elsewhere, so don't log them twice.
  DCHECK(grant_access_result !=
         SandboxGrantResult::kFailedToCreateCacheDirectory);
  DCHECK(grant_access_result !=
         SandboxGrantResult::kFailedToGrantSandboxAccessToCache);
  base::UmaHistogramEnumeration("NetworkService.GrantSandboxResult",
                                grant_access_result);

  if (grant_access_result != SandboxGrantResult::kSuccess &&
      grant_access_result !=
          SandboxGrantResult::kDidNotAttemptToGrantSandboxAccess &&
      grant_access_result != SandboxGrantResult::kNoMigrationRequested &&
      grant_access_result != SandboxGrantResult::kMigrationAlreadySucceeded) {
    PLOG(ERROR) << "Encountered error while migrating network context data or "
                   "granting sandbox access for "
                << (params->file_paths
                        ? params->file_paths->data_directory.path()
                        : base::FilePath())
                << ". Result: " << static_cast<int>(grant_access_result);
  }

  if (!IsSafeToUseDataPath(grant_access_result)) {
    // Unsafe to use new `data_directory`. This means that a migration was
    // attempted, and `unsandboxed_data_path` contains the still-valid set of
    // data. Swap the parameters to instruct the network service to use this
    // path for the network context. This of course will mean that if the
    // network service is running sandboxed then this data might not be
    // accessible, but does provide a pathway to user recovery, as the sandbox
    // can just be disabled in this case.
    DCHECK(params->file_paths->unsandboxed_data_path.has_value());
    params->file_paths->data_directory =
        *params->file_paths->unsandboxed_data_path;
  }

  if (network::TransferableDirectory::IsOpenForTransferRequired()) {
    if (params->file_paths) {
      params->file_paths->data_directory.OpenForTransfer();
    }
    if (params->http_cache_directory) {
      params->http_cache_directory->OpenForTransfer();
    }
  }

  GetNetworkService()->CreateNetworkContext(std::move(context),
                                            std::move(params));
}

scoped_refptr<base::SequencedTaskRunner>& GetNetworkTaskRunnerStorage() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> storage;
  return *storage;
}

void CreateInProcessNetworkService(
    mojo::PendingReceiver<network::mojom::NetworkService> receiver) {
  TRACE_EVENT0("loading", "CreateInProcessNetworkService");
  scoped_refptr<base::SingleThreadTaskRunner> task_runner;
  if (base::FeatureList::IsEnabled(kNetworkServiceDedicatedThread)) {
    base::Thread::Options options(base::MessagePumpType::IO, 0);
    GetNetworkServiceDedicatedThread().StartWithOptions(std::move(options));
    task_runner = GetNetworkServiceDedicatedThread().task_runner();
  } else {
    task_runner = GetIOThreadTaskRunner({});
  }

  GetNetworkTaskRunnerStorage() = std::move(task_runner);

  GetNetworkTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(&CreateInProcessNetworkServiceOnThread,
                                std::move(receiver)));
}

network::mojom::NetworkServiceParamsPtr CreateNetworkServiceParams() {
  network::mojom::NetworkServiceParamsPtr network_service_params =
      network::mojom::NetworkServiceParams::New();
  network_service_params->initial_connection_type =
      network::mojom::ConnectionType(
          net::NetworkChangeNotifier::GetConnectionType());
  network_service_params->initial_connection_subtype =
      network::mojom::ConnectionSubtype(
          net::NetworkChangeNotifier::GetConnectionSubtype());
  network_service_params->default_observer =
      g_client->BindURLLoaderNetworkServiceObserver();
  network_service_params->first_party_sets_enabled =
      GetContentClient()->browser()->IsFirstPartySetsEnabled();

#if BUILDFLAG(IS_POSIX)
  // Send Kerberos environment variables to the network service.
  if (IsOutOfProcessNetworkService()) {
    std::unique_ptr<base::Environment> env(base::Environment::Create());
    std::string value;
    if (env->HasVar(kKrb5CCEnvName)) {
      env->GetVar(kKrb5CCEnvName, &value);
      network_service_params->environment.push_back(
          network::mojom::EnvironmentVariable::New(kKrb5CCEnvName, value));
    }
    if (env->HasVar(kKrb5ConfEnvName)) {
      env->GetVar(kKrb5ConfEnvName, &value);
      network_service_params->environment.push_back(
          network::mojom::EnvironmentVariable::New(kKrb5ConfEnvName, value));
    }
  }
#endif
  return network_service_params;
}

void CreateNetworkServiceOnIOForTesting(
    mojo::PendingReceiver<network::mojom::NetworkService> receiver,
    base::WaitableEvent* completion_event) {
  if (GetLocalNetworkService()) {
    GetLocalNetworkService()->Bind(std::move(receiver));
    return;
  }

  GetLocalNetworkService() = std::make_unique<network::NetworkService>(
      nullptr /* registry */, std::move(receiver),
      true /* delay_initialization_until_set_client */);
  GetLocalNetworkService()->Initialize(
      network::mojom::NetworkServiceParams::New(),
      true /* mock_network_change_notifier */);
  if (completion_event)
    completion_event->Signal();
}

void BindNetworkChangeManagerReceiver(
    mojo::PendingReceiver<network::mojom::NetworkChangeManager> receiver) {
  GetNetworkService()->GetNetworkChangeManager(std::move(receiver));
}

base::RepeatingClosureList& GetCrashHandlersList() {
  static base::NoDestructor<base::RepeatingClosureList> s_list;
  return *s_list;
}

void OnNetworkServiceCrash() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(g_network_service_remote);
  DCHECK(g_network_service_remote->is_bound());
  DCHECK(!g_network_service_remote->is_connected());
  g_last_network_service_crash = base::Time::Now();
  GetCrashHandlersList().Notify();
}

// Parses the desired granularity of NetLog capturing specified by the command
// line.
net::NetLogCaptureMode GetNetCaptureModeFromCommandLine(
    const base::CommandLine& command_line) {
  base::StringPiece switch_name = network::switches::kNetLogCaptureMode;

  if (command_line.HasSwitch(switch_name)) {
    std::string value = command_line.GetSwitchValueASCII(switch_name);

    if (value == "Default")
      return net::NetLogCaptureMode::kDefault;
    if (value == "IncludeSensitive")
      return net::NetLogCaptureMode::kIncludeSensitive;
    if (value == "Everything")
      return net::NetLogCaptureMode::kEverything;

    // Warn when using the old command line switches.
    if (value == "IncludeCookiesAndCredentials") {
      LOG(ERROR) << "Deprecated value for --" << switch_name
                 << ". Use IncludeSensitive instead";
      return net::NetLogCaptureMode::kIncludeSensitive;
    }
    if (value == "IncludeSocketBytes") {
      LOG(ERROR) << "Deprecated value for --" << switch_name
                 << ". Use Everything instead";
      return net::NetLogCaptureMode::kEverything;
    }

    LOG(ERROR) << "Unrecognized value for --" << switch_name;
  }

  return net::NetLogCaptureMode::kDefault;
}

}  // namespace

class NetworkServiceInstancePrivate {
 public:
  // Opens the specified file, blocking until the file is open. Used to open
  // files specified by network::switches::kLogNetLog or
  // network::switches::kSSLKeyLogFile. Since these arguments can be used to
  // debug startup behavior, asynchronously opening the file on another thread
  // would result in losing data, hence the need for blocking open operations.
  // |file_flags| specifies the flags passed to the base::File constructor call.
  //
  // ThreadRestrictions needs to be able to friend the class/method to allow
  // blocking, but can't friend CONTENT_EXPORT methods, so have it friend
  // NetworkServiceInstancePrivate instead of GetNetworkService().
  static base::File BlockingOpenFile(const base::FilePath& path,
                                     int file_flags) {
    base::ScopedAllowBlocking allow_blocking;
    return base::File(path, file_flags);
  }
};

network::mojom::NetworkService* GetNetworkService() {
  if (!g_network_service_remote)
    g_network_service_remote = new mojo::Remote<network::mojom::NetworkService>;
  if (!g_network_service_remote->is_bound() ||
      !g_network_service_remote->is_connected()) {
    bool service_was_bound = g_network_service_remote->is_bound();
    g_network_service_remote->reset();
    if (GetContentClient()->browser()->IsShuttingDown()) {
      // This happens at system shutdown, since in other scenarios the network
      // process would only be torn down once the message loop stopped running.
      // We don't want to start the network service again so just create message
      // pipe that's not bound to stop consumers from requesting creation of the
      // service.
      auto receiver = g_network_service_remote->BindNewPipeAndPassReceiver();
      auto leaked_pipe = receiver.PassPipe().release();
    } else {
      if (!g_force_create_network_service_directly) {
        mojo::PendingReceiver<network::mojom::NetworkService> receiver =
            g_network_service_remote->BindNewPipeAndPassReceiver();
        g_network_service_remote->set_disconnect_handler(
            base::BindOnce(&OnNetworkServiceCrash));
        if (IsInProcessNetworkService()) {
          CreateInProcessNetworkService(std::move(receiver));
        } else {
          if (service_was_bound)
            LOG(ERROR) << "Network service crashed, restarting service.";
          ServiceProcessHost::Launch(std::move(receiver),
                                     ServiceProcessHost::Options()
                                         .WithDisplayName(u"Network Service")
                                         .Pass());
        }
      } else {
        // This should only be reached in unit tests.
        if (BrowserThread::CurrentlyOn(BrowserThread::IO)) {
          CreateNetworkServiceOnIOForTesting(
              g_network_service_remote->BindNewPipeAndPassReceiver(),
              /*completion_event=*/nullptr);
        } else {
          base::WaitableEvent event;
          GetIOThreadTaskRunner({})->PostTask(
              FROM_HERE,
              base::BindOnce(
                  CreateNetworkServiceOnIOForTesting,
                  g_network_service_remote->BindNewPipeAndPassReceiver(),
                  base::Unretained(&event)));
          event.Wait();
        }
      }

      delete g_client;  // In case we're recreating the network service.
      g_client = new NetworkServiceClient();

      // Call SetClient before creating NetworkServiceClient, as the latter
      // might make requests to NetworkService that depend on initialization.
      (*g_network_service_remote)->SetParams(CreateNetworkServiceParams());
      g_client->OnNetworkServiceInitialized(g_network_service_remote->get());

      g_network_service_is_responding = false;
      g_network_service_remote->QueryVersion(base::BindOnce(
          [](base::Time start_time, uint32_t) {
            g_network_service_is_responding = true;
            base::TimeDelta delta = base::Time::Now() - start_time;
            UMA_HISTOGRAM_MEDIUM_TIMES("NetworkService.TimeToFirstResponse",
                                       delta);
            if (g_last_network_service_crash.is_null()) {
              UMA_HISTOGRAM_MEDIUM_TIMES(
                  "NetworkService.TimeToFirstResponse.OnStartup", delta);
            } else {
              UMA_HISTOGRAM_MEDIUM_TIMES(
                  "NetworkService.TimeToFirstResponse.AfterCrash", delta);
            }
          },
          base::Time::Now()));

      const base::CommandLine* command_line =
          base::CommandLine::ForCurrentProcess();
      if (command_line->HasSwitch(network::switches::kLogNetLog)) {
        base::FilePath log_path =
            command_line->GetSwitchValuePath(network::switches::kLogNetLog);
        if (log_path.empty()) {
          log_path = GetContentClient()->browser()->GetNetLogDefaultDirectory();
          if (!log_path.empty())
            log_path = log_path.Append(FILE_PATH_LITERAL("netlog.json"));
        }

        base::File file = NetworkServiceInstancePrivate::BlockingOpenFile(
            log_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
        if (!file.IsValid()) {
          LOG(ERROR) << "Failed opening NetLog: " << log_path.value();
        } else {
          (*g_network_service_remote)
              ->StartNetLog(
                  std::move(file),
                  GetNetCaptureModeFromCommandLine(*command_line),
                  GetContentClient()->browser()->GetNetLogConstants());
        }
      }

      base::FilePath ssl_key_log_path;
      if (command_line->HasSwitch(network::switches::kSSLKeyLogFile)) {
        UMA_HISTOGRAM_ENUMERATION(kSSLKeyLogFileHistogram,
                                  SSLKeyLogFileAction::kSwitchFound);
        ssl_key_log_path =
            command_line->GetSwitchValuePath(network::switches::kSSLKeyLogFile);
        LOG_IF(WARNING, ssl_key_log_path.empty())
            << "ssl-key-log-file argument missing";
      } else {
        std::unique_ptr<base::Environment> env(base::Environment::Create());
        std::string env_str;
        if (env->GetVar("SSLKEYLOGFILE", &env_str)) {
          UMA_HISTOGRAM_ENUMERATION(kSSLKeyLogFileHistogram,
                                    SSLKeyLogFileAction::kEnvVarFound);
#if BUILDFLAG(IS_WIN)
          // base::Environment returns environment variables in UTF-8 on
          // Windows.
          ssl_key_log_path = base::FilePath(base::UTF8ToWide(env_str));
#else
          ssl_key_log_path = base::FilePath(env_str);
#endif
        }
      }

      if (!ssl_key_log_path.empty()) {
        base::File file = NetworkServiceInstancePrivate::BlockingOpenFile(
            ssl_key_log_path,
            base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);
        if (!file.IsValid()) {
          LOG(ERROR) << "Failed opening SSL key log file: "
                     << ssl_key_log_path.value();
        } else {
          UMA_HISTOGRAM_ENUMERATION(kSSLKeyLogFileHistogram,
                                    SSLKeyLogFileAction::kLogFileEnabled);
          (*g_network_service_remote)->SetSSLKeyLogFile(std::move(file));
        }
      }

      if (FirstPartySetsHandlerImpl::GetInstance()->IsEnabled()) {
        if (absl::optional<FirstPartySetsHandlerImpl::FlattenedSets> sets =
                FirstPartySetsHandlerImpl::GetInstance()->GetSets(
                    base::BindOnce(
                        [](FirstPartySetsHandlerImpl::FlattenedSets sets) {
                          GetNetworkService()->SetFirstPartySets(
                              std::move(sets));
                        }));
            sets.has_value()) {
          g_network_service_remote->get()->SetFirstPartySets(
              std::move(sets.value()));
        }
      }

      GetContentClient()->browser()->OnNetworkServiceCreated(
          g_network_service_remote->get());
    }
  }
  return g_network_service_remote->get();
}

base::CallbackListSubscription RegisterNetworkServiceCrashHandler(
    base::RepeatingClosure handler) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!handler.is_null());

  return GetCrashHandlersList().Add(std::move(handler));
}

#if BUILDFLAG(IS_CHROMEOS)
net::NetworkChangeNotifier* GetNetworkChangeNotifier() {
  return BrowserMainLoop::GetInstance()->network_change_notifier();
}
#endif

void FlushNetworkServiceInstanceForTesting() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (g_network_service_remote)
    g_network_service_remote->FlushForTesting();
}

network::NetworkConnectionTracker* GetNetworkConnectionTracker() {
  DCHECK(!BrowserThread::IsThreadInitialized(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!g_network_connection_tracker) {
    g_network_connection_tracker = new network::NetworkConnectionTracker(
        base::BindRepeating(&BindNetworkChangeManagerReceiver));
  }
  return g_network_connection_tracker;
}

void GetNetworkConnectionTrackerFromUIThread(
    base::OnceCallback<void(network::NetworkConnectionTracker*)> callback) {
  GetUIThreadTaskRunner({base::TaskPriority::BEST_EFFORT})
      ->PostTaskAndReplyWithResult(FROM_HERE,
                                   base::BindOnce(&GetNetworkConnectionTracker),
                                   std::move(callback));
}

network::NetworkConnectionTrackerAsyncGetter
CreateNetworkConnectionTrackerAsyncGetter() {
  return base::BindRepeating(&content::GetNetworkConnectionTrackerFromUIThread);
}

void SetNetworkConnectionTrackerForTesting(
    network::NetworkConnectionTracker* network_connection_tracker) {
  DCHECK(!BrowserThread::IsThreadInitialized(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (g_network_connection_tracker != network_connection_tracker) {
    DCHECK(!g_network_connection_tracker || !network_connection_tracker);
    g_network_connection_tracker = network_connection_tracker;
  }
}

const scoped_refptr<base::SequencedTaskRunner>& GetNetworkTaskRunner() {
  DCHECK(IsInProcessNetworkService());
  return GetNetworkTaskRunnerStorage();
}

void ForceCreateNetworkServiceDirectlyForTesting() {
  g_force_create_network_service_directly = true;
}

void ResetNetworkServiceForTesting() {
  ShutDownNetworkService();
}

void ShutDownNetworkService() {
  delete g_network_service_remote;
  g_network_service_remote = nullptr;
  delete g_client;
  g_client = nullptr;
  if (g_in_process_instance) {
    GetNetworkTaskRunner()->DeleteSoon(FROM_HERE, g_in_process_instance);
    g_in_process_instance = nullptr;
  }
  GetNetworkTaskRunnerStorage().reset();
}

NetworkServiceAvailability GetNetworkServiceAvailability() {
  if (!g_network_service_remote)
    return NetworkServiceAvailability::NOT_CREATED;
  else if (!g_network_service_remote->is_bound())
    return NetworkServiceAvailability::NOT_BOUND;
  else if (!g_network_service_remote->is_connected())
    return NetworkServiceAvailability::ENCOUNTERED_ERROR;
  else if (!g_network_service_is_responding)
    return NetworkServiceAvailability::NOT_RESPONDING;
  else
    return NetworkServiceAvailability::AVAILABLE;
}

base::TimeDelta GetTimeSinceLastNetworkServiceCrash() {
  if (g_last_network_service_crash.is_null())
    return base::TimeDelta();
  return base::Time::Now() - g_last_network_service_crash;
}

void PingNetworkService(base::OnceClosure closure) {
  GetNetworkService();
  // Unfortunately, QueryVersion requires a RepeatingCallback.
  g_network_service_remote->QueryVersion(base::BindOnce(
      [](base::OnceClosure closure, uint32_t) {
        if (closure)
          std::move(closure).Run();
      },
      std::move(closure)));
}

namespace {

cert_verifier::mojom::CertVerifierServiceFactory*
    g_cert_verifier_service_factory_for_testing = nullptr;

mojo::PendingRemote<cert_verifier::mojom::CertVerifierService>
GetNewCertVerifierServiceRemote(
    cert_verifier::mojom::CertVerifierServiceFactory*
        cert_verifier_service_factory,
    cert_verifier::mojom::CertVerifierCreationParamsPtr creation_params) {
  mojo::PendingRemote<cert_verifier::mojom::CertVerifierService>
      cert_verifier_remote;
  cert_verifier_service_factory->GetNewCertVerifier(
      cert_verifier_remote.InitWithNewPipeAndPassReceiver(),
      std::move(creation_params));
  return cert_verifier_remote;
}

void RunInProcessCertVerifierServiceFactory(
    mojo::PendingReceiver<cert_verifier::mojom::CertVerifierServiceFactory>
        receiver) {
#if BUILDFLAG(IS_CHROMEOS)
  // See the comment in GetCertVerifierServiceFactory() for the thread-affinity
  // of the CertVerifierService.
  DCHECK(!BrowserThread::IsThreadInitialized(BrowserThread::IO) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
#else
  DCHECK(!BrowserThread::IsThreadInitialized(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
#endif
  static base::SequenceLocalStorageSlot<
      std::unique_ptr<cert_verifier::CertVerifierServiceFactoryImpl>>
      service_factory_slot;
  service_factory_slot.GetOrCreateValue() =
      std::make_unique<cert_verifier::CertVerifierServiceFactoryImpl>(
          std::move(receiver));
}

// Owns the CertVerifierServiceFactory used by the browser.
// Lives on the UI thread.
mojo::Remote<cert_verifier::mojom::CertVerifierServiceFactory>&
GetCertVerifierServiceFactoryRemoteStorage() {
  static base::SequenceLocalStorageSlot<
      mojo::Remote<cert_verifier::mojom::CertVerifierServiceFactory>>
      cert_verifier_service_factory_remote;
  return cert_verifier_service_factory_remote.GetOrCreateValue();
}

}  // namespace

// Returns a pointer to a CertVerifierServiceFactory usable on the UI thread.
cert_verifier::mojom::CertVerifierServiceFactory*
GetCertVerifierServiceFactory() {
  DCHECK(!BrowserThread::IsThreadInitialized(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (g_cert_verifier_service_factory_for_testing)
    return g_cert_verifier_service_factory_for_testing;

  mojo::Remote<cert_verifier::mojom::CertVerifierServiceFactory>&
      factory_remote_storage = GetCertVerifierServiceFactoryRemoteStorage();
  if (!factory_remote_storage.is_bound() ||
      !factory_remote_storage.is_connected()) {
    factory_remote_storage.reset();
#if BUILDFLAG(IS_CHROMEOS)
    // In-process CertVerifierService in Ash and Lacros should run on the IO
    // thread because it interacts with IO-bound NSS and ChromeOS user slots.
    // See for example InitializeNSSForChromeOSUser() or
    // CertDbInitializerIOImpl.
    GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&RunInProcessCertVerifierServiceFactory,
                       factory_remote_storage.BindNewPipeAndPassReceiver()));
#else
    RunInProcessCertVerifierServiceFactory(
        factory_remote_storage.BindNewPipeAndPassReceiver());
#endif
  }
  return factory_remote_storage.get();
}

network::mojom::CertVerifierServiceRemoteParamsPtr GetCertVerifierParams(
    cert_verifier::mojom::CertVerifierCreationParamsPtr
        cert_verifier_creation_params) {
  return network::mojom::CertVerifierServiceRemoteParams::New(
      GetNewCertVerifierServiceRemote(
          GetCertVerifierServiceFactory(),
          std::move(cert_verifier_creation_params)));
}

void SetCertVerifierServiceFactoryForTesting(
    cert_verifier::mojom::CertVerifierServiceFactory* service_factory) {
  g_cert_verifier_service_factory_for_testing = service_factory;
}

void MaybeCleanCacheDirectory(network::mojom::NetworkContextParams* params) {
  if (params->http_cache_enabled && params->http_cache_directory) {
    // Delete any old data except for the "Cache_Data" directory.
    base::ThreadPool::PostTask(
        FROM_HERE,
        {base::TaskPriority::BEST_EFFORT, base::MayBlock(),
         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
        base::BindOnce(MaybeDeleteOldCache,
                       params->http_cache_directory->path()));

    params->http_cache_directory =
        params->http_cache_directory->path().Append(kCacheDataDirectoryName);
  }
}

void CreateNetworkContextInNetworkService(
    mojo::PendingReceiver<network::mojom::NetworkContext> context,
    network::mojom::NetworkContextParamsPtr params) {
  TRACE_EVENT0("loading", "CreateNetworkContextInNetworkService");
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  MaybeCleanCacheDirectory(params.get());

  const bool has_valid_http_cache_path =
      params->http_cache_enabled && params->http_cache_directory &&
      !params->http_cache_directory->path().empty();
  const bool brokering_is_enabled =
      IsOutOfProcessNetworkService() &&
      base::FeatureList::IsEnabled(
          features::kBrokerFileOperationsOnDiskCacheInNetworkService);
  if (has_valid_http_cache_path && brokering_is_enabled) {
    mojo::MakeSelfOwnedReceiver(
        std::make_unique<HttpCacheBackendFileOperationsFactory>(
            params->http_cache_directory->path()),
        params->http_cache_file_operations_factory
            .InitWithNewPipeAndPassReceiver());
  }

#if BUILDFLAG(IS_ANDROID)

  if (sandbox::policy::features::IsNetworkSandboxEnabled() &&
      !params->socket_broker) {
    params->socket_broker = g_client->BindSocketBroker();
  }
  // On Android, if a cookie_manager pending receiver was passed then migration
  // should not be attempted as the cookie file is already being accessed by the
  // browser instance.
  if (params->cookie_manager) {
    if (params->file_paths) {
      // No migration should ever be attempted under this configuration.
      DCHECK(!params->file_paths->unsandboxed_data_path);
    }
    CreateNetworkContextInternal(
        std::move(context), std::move(params),
        SandboxGrantResult::kDidNotAttemptToGrantSandboxAccess);
    return;
  }

  // Note: This logic is duplicated from MaybeGrantAccessToDataPath to this fast
  // path. This should be kept in sync if there are any changes to the logic.
  SandboxGrantResult grant_result = SandboxGrantResult::kNoMigrationRequested;
  if (!params->file_paths) {
    // No file paths (e.g. in-memory context) so nothing to do.
    grant_result = SandboxGrantResult::kDidNotAttemptToGrantSandboxAccess;
  } else {
    // If no `unsandboxed_data_path` is supplied, it means this is network
    // context has been created by Android Webview, which does not understand
    // the concept of `unsandboxed_data_path`. In this case, `data_directory`
    // should always be used, if present.
    if (!params->file_paths->unsandboxed_data_path)
      grant_result = SandboxGrantResult::kDidNotAttemptToGrantSandboxAccess;
  }
  // Create network context immediately without thread hops.
  CreateNetworkContextInternal(std::move(context), std::move(params),
                               grant_result);
#else
  // Restrict disk access to a certain path (on another thread) and continue
  // with network context creation.
  GrantSandboxAccessOnThreadPool(
      std::move(params),
      base::BindOnce(&CreateNetworkContextInternal, std::move(context)));
#endif  // BUILDFLAG(IS_ANDROID)
}

}  // namespace content
