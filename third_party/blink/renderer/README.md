## Blink architecture overview

See [this "How Blink works" document](https://docs.google.com/document/d/1aitSOucL0VHZa9Z2vbRJSyAIsAz24kX8LFByQ5xQnUg/edit#).

## `blink/renderer` directory structure

This section describes a high-level architecture of `blink/renderer`,
which contains most of the Web Platform implementation, and runs exclusively
in the renderer process.
On the other hand, [`common/`](../common) and [`public/common`](../public/common)
also run in the browser process.

All code in `blink/renderer` is an implementation detail of Blink
and should not be used outside of it. Use [Blink's public API](../public)
in code outside of Blink.

### [`core/`](core/README.md)

The `core/` directory implements the essence of the Web Platform defined by specs
and IDL interfaces. Due to historical reasons, `core/` contains a lot of features with
complex inter-dependencies and hence can be perceived as a single monolithic entity.

### `modules/`

The `modules/` directory is a collection of self-contained, well-defined features
of the Web Platform that are factored out of a monolithic `core/`. These features are:
 - large, tens to hundreds of files, with rare exceptions;
 - self-contained with fine-grained responsibilities and `README.md`;
 - have dependencies outlined with DEPS explicitly;
 - can depend on other features under `platform/`, `core/` or `modules/`,
   forming a healthy dependency tree.

`modules/` OWNERS are responsible for making sure only features
that satisfy requirements above are added.

For example, `modules/crypto` implements WebCrypto API.

### [`platform/`](platform/README.md)

The `platform/` directory is a collection of lower level features of Blink that are factored
out of a monolithic `core/`. These features follow the same principles as `modules/`,
but with different dependencies allowed:
 - large, tens to hundreds of files, with rare exceptions;
 - self-contained with fine-grained responsibilities and `README.md`;
 - have dependencies outlined with DEPS explicitly;
 - can depend on other features under `platform/` (but not `core/` or `modules/`),
   forming a healthy dependency tree.

`platform/` OWNERS are responsible for making sure only features
that satisfy requirements above are added.

For example, `platform/scheduler` implements a task scheduler for all tasks
posted by Blink, while `platform/wtf` implements Blink-specific containers
(e.g., `WTF::Vector`, `WTF::HashTable`, `WTF::String`).

### `core` vs `modules` vs `platform` split

Note that specs do not have a notion of "core", "platform" or "modules".
The distinction between them is for implementation
convenience to avoid putting everything in a single `core/` entity
(which decreases code modularity and increases build time):
  - features that are tightly coupled with HTML, CSS and other fundamental parts
    of DOM should go to `core/`;
  - features which conceptually depend on the features from "core"
    should go to `modules/`;
  - features which the "core" depends upon should go to `platform/`.

Note that some of these guidelines are violated (at the time of writing this),
but the code should gradually change and eventually conform.

### `bindings/`

The `bindings/` directory contains files that heavily use V8 APIs.
The rationale for splitting bindings out is: V8 APIs are complex, error-prone and
security-sensitive, so we want to put V8 API usage separately from other code.

In terms of dependencies, `bindings/core` and `core/` are in the same link unit.
The only difference is how heavily they are using V8 APIs.
If a given file is using a lot of V8 APIs, it should go to `bindings/core`.
Otherwise, it should go to `core/`. Consult `bindings/` OWNERS when in doubt.

Note that over time `bindings/core` should move to `core/bindings` and become
just a part of a larger "core".

All of the above applies to `bindings/modules` and `modules/`.

### `extensions/`

The `extensions/` directory contains embedder-specific, not-web-exposed APIs (e.g., not-web-exposed APIs for Chromium OS etc).
The directory is useful to implement embedder-specific, not-web-exposed APIs
using Blink technologies for web-exposed APIs like WebIDL, V8 bindings and Oilpan.

Remember that you should not implement web-exposed APIs in `extensions/`. Web-exposed APIs should go through the standardization process and be implemented in `core/` or `modules/`. Also, per [the Chromium contributor guideline](https://chromium.googlesource.com/chromium/src/+/main/docs/contributing.md#code-guidelines), code that is not used by Chromium should not be added to `extensions/`.

In terms of dependencies, `extensions/` can depend on `modules/`, `core/` and `platform/`, but not vice versa.

### `controller/`

The `controller/` directory contains the system infrastructure
that uses or drives Blink. Functionality that implements the Web Platform
should not go to `controller/`, but instead reside in `platform/`, `core/`
or `modules/`.

If the sole purpose of higher level functionality is to drive the Web Platform
or to implement API for the embedder, it goes to `controller/`,
however most of the features should go to other directories.
Consult `controller/` OWNERS when in doubt.

In terms of dependencies, `controller/` can depend on `extensions/`, `modules/`, `core/` and `platform/`, but not vice versa.

### `build/`

The `build/` directory contains scripts to build Blink.

In terms of dependencies, `build/` is a stand-alone directory.

## Dependencies

Dependencies only flow in the following order:

- `public/web`
- `controller/`
- `extensions/`
- `modules/` and `bindings/modules`
- `core/` and `bindings/core`
- `platform/`
- `public/platform`
- `public/common`
- `//base`, V8 etc.

See [this diagram](https://docs.google.com/document/d/1yYei-V76q3Mb-5LeJfNUMitmj6cqfA5gZGcWXoPaPYQ/edit).

`build/` is a stand-alone directory.

### Type dependencies

Member variables of the following types are strongly discouraged in Blink:
  - STL strings and containers. Use `WTF::String` and WTF containers instead.
  - `GURL` and `url::Origin`. Use `KURL` and `SecurityOrigin` respectively.
  - Any `//base` type which has a matching type in `platform/wtf`. The number of
  duplicated types between WTF and base is continuously shrinking,
  but always look at WTF first.

The types above could only be used at the boundary to interoperate
with `//base`, `//services`, `//third_party/blink/common` and other
Chromium-side or third-party code. It is also allowed to use local variables
of these types when convenient, as long as the result is not stored
in a member variable.
For example, calling an utility function on an `std::string` which came
from `//net` and then converting to `WTF::String` to store in a field
is allowed.

We try to share as much code between Chromium and Blink as possible,
so the number of these types should go down. However, some types
really need to be optimized for Blink's workload (e.g., `Vector`,
`HashTable`, `AtomicString`).

Exceptions to this rule:
  - Code in `//third_party/blink/common` and `//third_party/blink/public/common`
  also runs in the browser process, and should use STL and base instead of WTF.
  - Selected types in `public/platform` and `public/web`,
  whole purpose of which is conversion between WTF and STL,
  for example `WebString` or `WebVector`.

To prevent use of random types, we control allowed types by allow listing them
in DEPS and a [presubmit
script](../tools/blinkpy/presubmit/audit_non_blink_usage.py).

### Mojo

Blink can use Mojo and directly talk to the browser process. This allows removal of unnecessary
public APIs and abstraction layers and it is highly recommended.

### Threading model

When you need to use threads in Blink, cross-thread communication should be
done with a message passing model (i.e.,
call cross_thread_task_runner->PostTask() with cloned POD input parameters).

A shared memory model (e.g., using mutex locks or atomics) is strongly
discouraged. The rationale is that mutex locks and atomics are really
hard to use correctly, and even if it appears to be manageable initially, it
gets out of control easily. Historically, shared memory programming patterns
in Blink have been one of the major sources of use-after-free security bugs and
stability issues (e.g., WebAudio, memory access via CrossThreadPersistent).
Remember that, unlike V8, Blink does not have a strict API boundary and is
touched by many developers, and thus it's more important to adopt a less
error-prone programming pattern. There are existing instances of shared and
concurrent memory access in blink, but they should not be extended or
cargo-culted. Just because you see a shared memory pattern in the code does
not mean it's okay to use the pattern elsewhere.

Introducing a few mutex locks or atomics in simple classes (e.g., shared
counters) is fine. However, when you need to introduce a non-trivial number
of mutex locks and atomics, the architecture needs to be designed and
reviewed carefully. In that case, please get approval from
platform-architecture-dev@chromium.org.

## Contact

If you have any questions about the directory architecture and dependencies,
reach out to platform-architecture-dev@chromium.org!

