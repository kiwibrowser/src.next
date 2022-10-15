// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/content_verifier.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/containers/contains.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/timer/elapsed_timer.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/storage_partition.h"
#include "extensions/browser/content_hash_fetcher.h"
#include "extensions/browser/content_hash_reader.h"
#include "extensions/browser/content_verifier/content_verifier_utils.h"
#include "extensions/browser/content_verifier_delegate.h"
#include "extensions/browser/extension_file_task_runner.h"
#include "extensions/common/api/declarative_net_request/dnr_manifest_data.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension_l10n_util.h"
#include "extensions/common/file_util.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/manifest_handlers/content_scripts_handler.h"
#include "extensions/common/utils/base_string.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/mojom/network_context.mojom.h"

namespace extensions {

namespace {

ContentVerifier::TestObserver* g_content_verifier_test_observer = nullptr;
using content_verifier_utils::CanonicalRelativePath;

// This function converts paths like "//foo/bar", "./foo/bar", and
// "/foo/bar" to "foo/bar". It also converts path separators to "/".
base::FilePath NormalizeRelativePath(const base::FilePath& path) {
  if (path.ReferencesParent())
    return base::FilePath();

  std::vector<base::FilePath::StringType> parts = path.GetComponents();
  if (parts.empty())
    return base::FilePath();

  // Remove the first component if it is '.' or '/' or '//'.
  const base::FilePath::StringType separators(
      base::FilePath::kSeparators, base::FilePath::kSeparatorsLength);
  if (!parts[0].empty() &&
      (parts[0] == base::FilePath::kCurrentDirectory ||
       parts[0].find_first_not_of(separators) == std::string::npos))
    parts.erase(parts.begin());

  // Note that elsewhere we always normalize path separators to '/' so this
  // should work for all platforms.
  base::FilePath::StringType normalized_relative_path =
      base::JoinString(parts, base::FilePath::StringType(1, '/'));
  // Preserve trailing separator, if present.
  if (path.EndsWithSeparator())
    normalized_relative_path.append(1, '/');
  return base::FilePath(normalized_relative_path);
}

bool HasScriptFileExt(const base::FilePath& requested_path) {
  return requested_path.MatchesExtension(FILE_PATH_LITERAL(".js"));
}

bool HasPageFileExt(const base::FilePath& requested_path) {
  base::FilePath::StringType file_extension = requested_path.Extension();
  return requested_path.MatchesExtension(FILE_PATH_LITERAL(".html")) ||
         requested_path.MatchesExtension(FILE_PATH_LITERAL(".htm"));
}

std::unique_ptr<ContentVerifierIOData::ExtensionData> CreateIOData(
    const Extension* extension,
    ContentVerifierDelegate* delegate) {
  ContentVerifierDelegate::VerifierSourceType source_type =
      delegate->GetVerifierSourceType(*extension);
  if (source_type == ContentVerifierDelegate::VerifierSourceType::NONE)
    return nullptr;

  // The browser image paths from the extension may not be relative (eg
  // they might have leading '/' or './'), so we strip those to make
  // comparing to actual relative paths work later on.
  std::set<base::FilePath> original_image_paths =
      delegate->GetBrowserImagePaths(extension);
  auto canonicalize_path = [](const base::FilePath& relative_path) {
    return content_verifier_utils::CanonicalizeRelativePath(
        NormalizeRelativePath(relative_path));
  };

  auto image_paths = std::make_unique<std::set<CanonicalRelativePath>>();
  for (const auto& path : original_image_paths) {
    image_paths->insert(canonicalize_path(path));
  }

  auto background_or_content_paths =
      std::make_unique<std::set<CanonicalRelativePath>>();
  for (const std::string& script :
       BackgroundInfo::GetBackgroundScripts(extension)) {
    background_or_content_paths->insert(
        canonicalize_path(extension->GetResource(script).relative_path()));
  }
  if (BackgroundInfo::HasBackgroundPage(extension)) {
    background_or_content_paths->insert(
        canonicalize_path(extensions::file_util::ExtensionURLToRelativeFilePath(
            BackgroundInfo::GetBackgroundURL(extension))));
  }
  for (const std::unique_ptr<UserScript>& script :
       ContentScriptsInfo::GetContentScripts(extension)) {
    for (const std::unique_ptr<UserScript::File>& js_file :
         script->js_scripts()) {
      background_or_content_paths->insert(
          canonicalize_path(js_file->relative_path()));
    }
  }

  auto indexed_ruleset_paths =
      std::make_unique<std::set<CanonicalRelativePath>>();
  using DNRManifestData = declarative_net_request::DNRManifestData;
  for (const DNRManifestData::RulesetInfo& info :
       DNRManifestData::GetRulesets(*extension)) {
    indexed_ruleset_paths->insert(canonicalize_path(
        file_util::GetIndexedRulesetRelativePath(info.id.value())));
  }

  return std::make_unique<ContentVerifierIOData::ExtensionData>(
      std::move(image_paths), std::move(background_or_content_paths),
      std::move(indexed_ruleset_paths), extension->version(), source_type);
}
}  // namespace

struct ContentVerifier::CacheKey {
  CacheKey(const ExtensionId& extension_id,
           const base::Version& version,
           bool needs_force_missing_computed_hashes_creation)
      : extension_id(extension_id),
        version(version),
        needs_force_missing_computed_hashes_creation(
            needs_force_missing_computed_hashes_creation) {}

  bool operator<(const CacheKey& other) const {
    return std::tie(extension_id, version,
                    needs_force_missing_computed_hashes_creation) <
           std::tie(other.extension_id, other.version,
                    other.needs_force_missing_computed_hashes_creation);
  }

  ExtensionId extension_id;
  base::Version version;
  // TODO(lazyboy): This shouldn't be necessary as key. For the common
  // case, we'd only want to cache successful ContentHash instances regardless
  // of whether force creation was requested.
  bool needs_force_missing_computed_hashes_creation = false;
};

// A class to retrieve ContentHash for ContentVerifier.
//
// All public calls originate and terminate on IO, making it suitable for
// ContentVerifier to cache ContentHash instances easily.
//
// This class makes sure we do not have more than one ContentHash request in
// flight for a particular version of an extension. If a call to retrieve an
// extensions's ContentHash is made while another retieval for the same
// version of the extension is in flight, this class will queue up the
// callback(s) and respond to all of them when ContentHash is available.
class ContentVerifier::HashHelper {
 public:
  explicit HashHelper(ContentVerifier* content_verifier)
      : content_verifier_(content_verifier) {}

  HashHelper(const HashHelper&) = delete;
  HashHelper& operator=(const HashHelper&) = delete;

  ~HashHelper() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
    // TODO(lazyboy): Do we need to Cancel() the callacks?
  }

  // Cancels any ongoing computed_hashes.json disk write for an extension.
  void Cancel(const ExtensionId& extension_id,
              const base::Version& extension_version) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
    auto callback_key = std::make_pair(extension_id, extension_version);
    auto iter = callback_infos_.find(callback_key);
    if (iter == callback_infos_.end())
      return;
    iter->second.Cancel();
    callback_infos_.erase(iter);
  }

  // Retrieves the ContentHash of an extension and responds via |callback|.
  //
  // Must be called on IO thread. The method responds through |callback| on IO
  // thread.
  void GetContentHash(ContentHash::FetchKey fetch_key,
                      ContentVerifierDelegate::VerifierSourceType source_type,
                      bool force_missing_computed_hashes_creation,
                      ContentHashCallback callback) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
    auto callback_key =
        std::make_pair(fetch_key.extension_id, fetch_key.extension_version);
    auto iter = callback_infos_.find(callback_key);
    if (iter != callback_infos_.end()) {
      iter->second.callbacks.push_back(std::move(callback));
      iter->second.force_missing_computed_hashes_creation |=
          force_missing_computed_hashes_creation;
      return;
    }
    scoped_refptr<IsCancelledChecker> checker =
        base::MakeRefCounted<IsCancelledChecker>();
    auto iter_pair = callback_infos_.emplace(
        callback_key, CallbackInfo(checker, std::move(callback)));
    DCHECK(iter_pair.second);
    iter_pair.first->second.force_missing_computed_hashes_creation |=
        force_missing_computed_hashes_creation;

    GetExtensionFileTaskRunner()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &HashHelper::ReadHashOnFileTaskRunner, std::move(fetch_key),
            source_type,
            base::BindRepeating(&IsCancelledChecker::IsCancelled, checker),
            base::BindOnce(&HashHelper::DidReadHash, weak_factory_.GetWeakPtr(),
                           callback_key, checker)));
  }

 private:
  using CallbackKey = std::pair<ExtensionId, base::Version>;

  class IsCancelledChecker
      : public base::RefCountedThreadSafe<IsCancelledChecker> {
   public:
    IsCancelledChecker() {}

    IsCancelledChecker(const IsCancelledChecker&) = delete;
    IsCancelledChecker& operator=(const IsCancelledChecker&) = delete;

    // Safe to call from any thread.
    void Cancel() {
      base::AutoLock autolock(cancelled_lock_);
      cancelled_ = true;
    }
    // Safe to call from any thread.
    bool IsCancelled() {
      base::AutoLock autolock(cancelled_lock_);
      return cancelled_;
    }

   private:
    friend class base::RefCountedThreadSafe<IsCancelledChecker>;

    ~IsCancelledChecker() {}

    // Note: this may be accessed from multiple threads, so all access should
    // be protected by |cancelled_lock_|.
    bool cancelled_ = false;

    // A lock for synchronizing access to |cancelled_|.
    base::Lock cancelled_lock_;
  };

  // Holds information about each call to HashHelper::GetContentHash(), for a
  // particular extension (id and version).
  //
  // |callbacks| are the callbacks that callers to GetContentHash() passed us.
  // |cancelled_checker| is used to cancel an extension's task from any thread.
  // |force_missing_computed_hashes_creation| is true if any callback (from
  //     ContentVerifyJob) requested to recompute computed_hashes.json file in
  //     case the file is missing or cannot be read.
  struct CallbackInfo {
    CallbackInfo(const scoped_refptr<IsCancelledChecker>& cancelled_checker,
                 ContentHashCallback callback)
        : cancelled_checker(cancelled_checker) {
      callbacks.push_back(std::move(callback));
    }

    void Cancel() { cancelled_checker->Cancel(); }

    base::TimeDelta elapsed() const { return elapsed_timer.Elapsed(); }

    scoped_refptr<IsCancelledChecker> cancelled_checker;
    // TODO(lazyboy): Use std::list?
    std::vector<ContentHashCallback> callbacks;
    bool force_missing_computed_hashes_creation = false;
    base::ElapsedTimer elapsed_timer;
  };

  using IsCancelledCallback = base::RepeatingCallback<bool(void)>;

  static void ForwardToIO(ContentHash::CreatedCallback callback,
                          scoped_refptr<ContentHash> content_hash,
                          bool was_cancelled) {
    // If the request was cancelled, then we don't have a corresponding entry
    // for the request in |callback_infos_| anymore.
    if (was_cancelled)
      return;

    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), content_hash, was_cancelled));
  }

  static void ReadHashOnFileTaskRunner(
      ContentHash::FetchKey fetch_key,
      ContentVerifierDelegate::VerifierSourceType source_type,
      const IsCancelledCallback& is_cancelled,
      ContentHash::CreatedCallback created_callback) {
    ContentHash::Create(
        std::move(fetch_key), source_type, is_cancelled,
        base::BindOnce(&HashHelper::ForwardToIO, std::move(created_callback)));
  }

  static void ForceBuildComputedHashesOnFileTaskRuner(
      const scoped_refptr<ContentHash> content_hash,
      const IsCancelledCallback& is_cancelled,
      ContentHash::CreatedCallback created_callback) {
    content_hash->ForceBuildComputedHashes(
        is_cancelled,
        base::BindOnce(&HashHelper::ForwardToIO, std::move(created_callback)));
  }

  void DidReadHash(const CallbackKey& key,
                   const scoped_refptr<IsCancelledChecker>& checker,
                   scoped_refptr<ContentHash> content_hash,
                   bool was_cancelled) {
    DCHECK(checker);
    if (was_cancelled ||
        // The request might have been cancelled on IO after |content_hash| was
        // built.
        // TODO(lazyboy): Add a specific test case for this. See
        // https://crbug.com/825470 for a likely example of this.
        checker->IsCancelled()) {
      return;
    }

    auto iter = callback_infos_.find(key);
    DCHECK(iter != callback_infos_.end());
    auto& callback_info = iter->second;

    // Force creation of computed_hashes.json if all of the following are true:
    //   - any caller(s) has explicitly requested it.
    //   - hash retrieval failed due to invalid computed_hashes.json and
    //     re-creating the file might make the hash retrieval successful.
    if (callback_info.force_missing_computed_hashes_creation &&
        content_hash->might_require_computed_hashes_force_creation()) {
      GetExtensionFileTaskRunner()->PostTask(
          FROM_HERE,
          base::BindOnce(&HashHelper::ForceBuildComputedHashesOnFileTaskRuner,
                         content_hash,
                         base::BindRepeating(&IsCancelledChecker::IsCancelled,
                                             callback_info.cancelled_checker),
                         base::BindOnce(&HashHelper::CompleteDidReadHash,
                                        weak_factory_.GetWeakPtr(), key,
                                        callback_info.cancelled_checker)));
      return;
    }

    CompleteDidReadHash(key, callback_info.cancelled_checker,
                        std::move(content_hash), was_cancelled);
  }

  void CompleteDidReadHash(const CallbackKey& key,
                           const scoped_refptr<IsCancelledChecker>& checker,
                           scoped_refptr<ContentHash> content_hash,
                           bool was_cancelled) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
    DCHECK(checker);
    if (was_cancelled ||
        // The request might have been cancelled on IO after |content_hash| was
        // built.
        checker->IsCancelled()) {
      return;
    }

    auto iter = callback_infos_.find(key);
    DCHECK(iter != callback_infos_.end());
    auto& callback_info = iter->second;
    UMA_HISTOGRAM_TIMES("Extensions.ContentVerification.ReadContentHashTime",
                        callback_info.elapsed());

    for (auto& callback : callback_info.callbacks)
      std::move(callback).Run(content_hash);
    callback_infos_.erase(iter);

    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

    // OnFetchComplete will check content_hash->hash_mismatch_unix_paths():
    content_verifier_->OnFetchComplete(content_hash);
  }

  // List of pending callbacks of GetContentHash().
  std::map<CallbackKey, CallbackInfo> callback_infos_;

  const raw_ptr<ContentVerifier> content_verifier_ = nullptr;

  base::WeakPtrFactory<HashHelper> weak_factory_{this};
};

// static
void ContentVerifier::SetObserverForTests(TestObserver* observer) {
  g_content_verifier_test_observer = observer;
}

ContentVerifier::ContentVerifier(
    content::BrowserContext* context,
    std::unique_ptr<ContentVerifierDelegate> delegate)
    : context_(context), delegate_(std::move(delegate)) {}

ContentVerifier::~ContentVerifier() {
}

void ContentVerifier::Start() {
  ExtensionRegistry* registry = ExtensionRegistry::Get(context_);
  observation_.Observe(registry);
}

void ContentVerifier::Shutdown() {
  shutdown_on_ui_ = true;
  delegate_->Shutdown();
  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&ContentVerifier::ShutdownOnIO, this));
  observation_.Reset();
}

void ContentVerifier::ShutdownOnIO() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  shutdown_on_io_ = true;
  io_data_.Clear();
  hash_helper_.reset();
}

scoped_refptr<ContentVerifyJob> ContentVerifier::CreateAndStartJobFor(
    const std::string& extension_id,
    const base::FilePath& extension_root,
    const base::FilePath& relative_path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  const ContentVerifierIOData::ExtensionData* data =
      io_data_.GetData(extension_id);
  // The absence of |data| generally means that we don't have to verify the
  // extension resource. However, it could also mean that
  // OnExtensionLoadedOnIO didn't get a chance to fire yet.
  // See https://crbug.com/826584 for an example of how this can happen from
  // ExtensionUserScriptLoader. Currently, ExtensionUserScriptLoader performs a
  // thread hopping to work around this problem.
  // TODO(lazyboy): Prefer queueing up jobs in these case instead of the thread
  // hopping solution, but that requires a substantial change in
  // ContnetVerifier/ContentVerifyJob.
  if (!data)
    return nullptr;

  base::FilePath normalized_unix_path = NormalizeRelativePath(relative_path);

  if (!ShouldVerifyAnyPaths(extension_id, extension_root,
                            {normalized_unix_path})) {
    return nullptr;
  }

  // TODO(asargent) - we can probably get some good performance wins by having
  // a cache of ContentHashReader's that we hold onto past the end of each job.
  scoped_refptr<ContentVerifyJob> job = base::MakeRefCounted<ContentVerifyJob>(
      extension_id, data->version, extension_root, normalized_unix_path,
      base::BindOnce(&ContentVerifier::VerifyFailed, this, extension_id));
  job->Start(this);
  return job;
}

void ContentVerifier::GetContentHash(
    const ExtensionId& extension_id,
    const base::FilePath& extension_root,
    const base::Version& extension_version,
    bool force_missing_computed_hashes_creation,
    ContentHashCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  if (shutdown_on_io_) {
    // NOTE: Release |callback| asynchronously, so that we don't release ref of
    // ContentVerifyJob and possibly destroy it synchronously here while
    // ContentVerifyJob is holding a lock. The lock destroyer would fail DCHECK
    // in that case.
    // TODO(lazyboy): Make CreateJobFor return a scoped_refptr instead of raw
    // pointer to fix this. Also add unit test to exercise this code path
    // explicitly.
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce([](ContentHashCallback) {}, std::move(callback)));
    return;
  }

  CacheKey cache_key(extension_id, extension_version,
                     force_missing_computed_hashes_creation);
  auto cache_iter = cache_.find(cache_key);
  if (cache_iter != cache_.end()) {
    // Currently, we expect |callback| to be called asynchronously.
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), cache_iter->second));
    return;
  }

  const ContentVerifierIOData::ExtensionData* data =
      io_data_.GetData(extension_id);
  DCHECK(data);
  ContentHash::FetchKey fetch_key =
      GetFetchKey(extension_id, extension_root, extension_version);
  // Since |shutdown_on_io_| = false, GetOrCreateHashHelper() must return
  // non-nullptr instance of HashHelper.
  GetOrCreateHashHelper()->GetContentHash(
      std::move(fetch_key), data->source_type,
      force_missing_computed_hashes_creation,
      base::BindOnce(&ContentVerifier::DidGetContentHash, this, cache_key,
                     std::move(callback)));
}

bool ContentVerifier::ShouldComputeHashesOnInstall(const Extension& extension) {
  return delegate_->GetVerifierSourceType(extension) ==
         ContentVerifierDelegate::VerifierSourceType::UNSIGNED_HASHES;
}

void ContentVerifier::VerifyFailed(const ExtensionId& extension_id,
                                   ContentVerifyJob::FailureReason reason) {
  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&ContentVerifier::VerifyFailed, this,
                                  extension_id, reason));
    return;
  }
  if (shutdown_on_ui_)
    return;

  VLOG(1) << "VerifyFailed " << extension_id << " reason:" << reason;
  DCHECK_NE(ContentVerifyJob::NONE, reason);

  delegate_->VerifyFailed(extension_id, reason);
}

void ContentVerifier::OnExtensionLoaded(
    content::BrowserContext* browser_context,
    const Extension* extension) {
  if (shutdown_on_ui_)
    return;

  std::unique_ptr<ContentVerifierIOData::ExtensionData> io_data =
      CreateIOData(extension, delegate_.get());
  if (io_data) {
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&ContentVerifier::OnExtensionLoadedOnIO, this,
                                  extension->id(), extension->path(),
                                  extension->version(), std::move(io_data)));
  }
}

void ContentVerifier::OnExtensionLoadedOnIO(
    const ExtensionId& extension_id,
    const base::FilePath& extension_root,
    const base::Version& extension_version,
    std::unique_ptr<ContentVerifierIOData::ExtensionData> data) {
  if (shutdown_on_io_)
    return;

  io_data_.AddData(extension_id, std::move(data));
  GetContentHash(extension_id, extension_root, extension_version,
                 false /* force_missing_computed_hashes_creation */,
                 // HashHelper will respond directly to OnFetchComplete().
                 base::DoNothing());
}

void ContentVerifier::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const Extension* extension,
    UnloadedExtensionReason reason) {
  if (shutdown_on_ui_)
    return;
  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&ContentVerifier::OnExtensionUnloadedOnIO, this,
                                extension->id(), extension->version()));
}

ContentVerifierKey ContentVerifier::GetContentVerifierKey() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return delegate_->GetPublicKey();
}

GURL ContentVerifier::GetSignatureFetchUrlForTest(
    const ExtensionId& extension_id,
    const base::Version& extension_version) {
  return delegate_->GetSignatureFetchUrl(extension_id, extension_version);
}

void ContentVerifier::VerifyFailedForTest(
    const ExtensionId& extension_id,
    ContentVerifyJob::FailureReason reason) {
  VerifyFailed(extension_id, reason);
}

void ContentVerifier::ClearCacheForTesting() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  cache_.clear();
}

void ContentVerifier::OnExtensionUnloadedOnIO(
    const ExtensionId& extension_id,
    const base::Version& extension_version) {
  if (shutdown_on_io_)
    return;
  io_data_.RemoveData(extension_id);

  // Remove all possible cache entries for this extension version.
  cache_.erase(CacheKey(extension_id, extension_version, true));
  cache_.erase(CacheKey(extension_id, extension_version, false));

  HashHelper* hash_helper = GetOrCreateHashHelper();
  if (hash_helper)
    hash_helper->Cancel(extension_id, extension_version);
}

void ContentVerifier::OnFetchComplete(
    const scoped_refptr<const ContentHash>& content_hash) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  ExtensionId extension_id = content_hash->extension_id();
  VLOG(1) << "OnFetchComplete " << extension_id
          << " success:" << content_hash->succeeded();

  const bool did_hash_mismatch =
      ShouldVerifyAnyPaths(extension_id, content_hash->extension_root(),
                           content_hash->hash_mismatch_unix_paths());
  if (g_content_verifier_test_observer) {
    g_content_verifier_test_observer->OnFetchComplete(content_hash,
                                                      did_hash_mismatch);
  }

  if (!did_hash_mismatch)
    return;

  VerifyFailed(extension_id, ContentVerifyJob::HASH_MISMATCH);
}

ContentHash::FetchKey ContentVerifier::GetFetchKey(
    const ExtensionId& extension_id,
    const base::FilePath& extension_root,
    const base::Version& extension_version) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  const ContentVerifierIOData::ExtensionData* data =
      io_data_.GetData(extension_id);
  DCHECK(data);
  if (data->source_type ==
      ContentVerifierDelegate::VerifierSourceType::UNSIGNED_HASHES) {
    return ContentHash::FetchKey(extension_id, extension_root,
                                 extension_version, mojo::NullRemote(),
                                 GURL::EmptyGURL(), ContentVerifierKey());
  }

  // Create a new mojo pipe. It's safe to pass this around and use immediately,
  // even though it needs to finish initialization on the UI thread.
  mojo::PendingRemote<network::mojom::URLLoaderFactory>
      url_loader_factory_remote;
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(
          &ContentVerifier::BindURLLoaderFactoryReceiverOnUIThread, this,
          url_loader_factory_remote.InitWithNewPipeAndPassReceiver()));
  return ContentHash::FetchKey(
      extension_id, extension_root, extension_version,
      std::move(url_loader_factory_remote),
      delegate_->GetSignatureFetchUrl(extension_id, extension_version),
      delegate_->GetPublicKey());
}

void ContentVerifier::DidGetContentHash(
    const CacheKey& cache_key,
    ContentHashCallback original_callback,
    scoped_refptr<const ContentHash> content_hash) {
  cache_[cache_key] = content_hash;
  std::move(original_callback).Run(content_hash);
}

void ContentVerifier::BindURLLoaderFactoryReceiverOnUIThread(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory>
        url_loader_factory_receiver) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (shutdown_on_ui_)
    return;

  context_->GetDefaultStoragePartition()
      ->GetURLLoaderFactoryForBrowserProcess()
      ->Clone(std::move(url_loader_factory_receiver));
}

bool ContentVerifier::ShouldVerifyAnyPaths(
    const std::string& extension_id,
    const base::FilePath& extension_root,
    const std::set<base::FilePath>& relative_unix_paths) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  const ContentVerifierIOData::ExtensionData* data =
      io_data_.GetData(extension_id);
  if (!data)
    return false;

  const std::set<CanonicalRelativePath>& browser_images =
      *(data->canonical_browser_image_paths);
  const std::set<CanonicalRelativePath>& background_or_content_paths =
      *(data->canonical_background_or_content_paths);
  const std::set<CanonicalRelativePath>& indexed_ruleset_paths =
      *(data->canonical_indexed_ruleset_paths);

  std::set<std::string> all_locale_candidates;

  const CanonicalRelativePath manifest_file =
      content_verifier_utils::CanonicalizeRelativePath(
          base::FilePath(kManifestFilename));
  const base::FilePath messages_file(kMessagesFilename);
  const base::FilePath locales_relative_dir(kLocaleFolder);
  for (const base::FilePath& relative_unix_path : relative_unix_paths) {
    if (relative_unix_path.empty())
      continue;

    CanonicalRelativePath canonical_path_value =
        content_verifier_utils::CanonicalizeRelativePath(relative_unix_path);

    if (canonical_path_value == manifest_file)
      continue;

    // JavaScript and HTML files should always be verified.
    if (HasScriptFileExt(relative_unix_path) ||
        HasPageFileExt(relative_unix_path)) {
      return true;
    }

    // Background pages, scripts and content scripts should always be verified
    // regardless of their file type.
    if (base::Contains(background_or_content_paths, canonical_path_value))
      return true;

    if (base::Contains(browser_images, canonical_path_value))
      continue;

    // Skip indexed rulesets since these are generated.
    if (base::Contains(indexed_ruleset_paths, canonical_path_value))
      continue;

    const base::FilePath canonical_path(canonical_path_value.value());
    if (locales_relative_dir.IsParent(canonical_path)) {
      // TODO(asargent) - see if we can cache this list longer to avoid
      // having to fetch it more than once for a given run of the
      // browser. Maybe it can never change at runtime? (Or if it can, maybe
      // there is an event we can listen for to know to drop our cache).
      if (all_locale_candidates.empty()) {
        extension_l10n_util::GetAllLocales(&all_locale_candidates);
        DCHECK(!all_locale_candidates.empty());
      }

      // Since message catalogs get transcoded during installation, we want
      // to skip those paths. See if this path looks like
      // _locales/<some locale>/messages.json - if so then skip it.
      if (canonical_path.BaseName() == messages_file &&
          canonical_path.DirName().DirName() == locales_relative_dir &&
          ContainsStringIgnoreCaseASCII(
              all_locale_candidates,
              canonical_path.DirName().BaseName().MaybeAsASCII())) {
        continue;
      }
    }
    return true;
  }
  return false;
}

ContentVerifier::HashHelper* ContentVerifier::GetOrCreateHashHelper() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!shutdown_on_io_) << "Creating HashHelper after IO shutdown";
  // Just checking |hash_helper_| against nullptr isn't enough because we reset
  // hash_helper_ in Shutdown(), and we shouldn't be re-creating it in that
  // case.
  if (!hash_helper_created_) {
    DCHECK(!hash_helper_);
    hash_helper_ =
        std::unique_ptr<HashHelper, content::BrowserThread::DeleteOnIOThread>(
            new HashHelper(this));
    hash_helper_created_ = true;
  }
  return hash_helper_.get();
}

void ContentVerifier::ResetIODataForTesting(const Extension* extension) {
  io_data_.AddData(extension->id(), CreateIOData(extension, delegate_.get()));
}

base::FilePath ContentVerifier::NormalizeRelativePathForTesting(
    const base::FilePath& path) {
  return NormalizeRelativePath(path);
}

bool ContentVerifier::ShouldVerifyAnyPathsForTesting(
    const std::string& extension_id,
    const base::FilePath& extension_root,
    const std::set<base::FilePath>& relative_unix_paths) {
  return ShouldVerifyAnyPaths(extension_id, extension_root,
                              relative_unix_paths);
}

void ContentVerifier::OverrideDelegateForTesting(
    std::unique_ptr<ContentVerifierDelegate> delegate) {
  delegate_ = std::move(delegate);
}

}  // namespace extensions
