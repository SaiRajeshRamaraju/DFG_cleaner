# Distributed File Storage — File Upload Architecture

**Status:** Working design  
**Scope:** Upload registration, manifest management, chunk transfer, integrity verification, metadata coordination, deduplication, upload completion, local chunk persistence, and upload-related cleanup.

> This document consolidates the decisions made during the architecture design. It is a **design specification**, not a claim that every feature is already implemented. Values described as *proposed* or *illustrative* are not yet finalized configuration defaults.

## 1. Scope and boundaries

### Included

- Client discovery and head-server assignment.
- Lightweight custom RPC transport over TCP.
- File metadata and manifest registration.
- Bounded parallel chunk uploads and streaming to chunk servers.
- Replica integrity verification, repair, and acknowledgment.
- Shared metadata database consistency and availability behavior.
- Resume, completion, whole-file SHA-512 verification, and file visibility.
- Whole-file content deduplication and filename aliases.
- Expiration, deletion references, and asynchronous garbage collection.
- Chunk-server local persistence guarantees and bounded group commit.

### Intentionally outside the current design

- Cluster-wide redundancy policy and choice of replication factor.
- Replica placement, rebalancing, and cluster-wide failure recovery.
- Cross-head takeover of an established upload after the assigned head server fails.
- Full download protocol and download data path (only metadata lookup requirements are included).
- User accounts, per-file access permissions, and multi-tenant isolation.
- Dynamic chunk-size negotiation (planned later; fixed size now).
- A finalized chunk-server filesystem layout (Question 43 is not yet answered).

The current trust assumption is **fully shared storage with trusted clients**. Any connected client may list, upload, or download shared files. A filename such as `super_secret.txt` does **not** imply confidentiality in this version.

## 2. High-level architecture

```text
                         Initial discovery only
+------------+     PING RPC      +------------------------+
|   Client   | ----------------> | Custom Load Balancer   |
+------------+                   +------------------------+
       |                                  |
       |                   resource-aware head assignment
       |                                  |
       | <----------------------- PONG / head endpoint
       |
       |  Direct connections; bounded, persistent TCP pool
       v
+---------------------+           +--------------------------+
| Assigned Head Server| <-------> | Shared Metadata Database |
|                     |           | - filename references    |
| - RPC coordination  |           | - content records        |
| - manifest lookup   |           | - chunk/replica states   |
| - splice()/tee()    |           | - physical locations     |
| - bounded queue     |           | - leases / GC state      |
| - SHA-512 verifier  |           +--------------------------+
+---------------------+
       |
       | CHUNK_REPLICATION (framed header + streamed bytes)
       | via Linux pipes / splice() / tee()
       |
       +------------+---------------+----------------+
       |            |               |                |
       v            v               v                v
  +---------+  +---------+     +---------+     ... required replicas
  | Chunk A |  | Chunk B |     | Chunk C |
  +---------+  +---------+     +---------+
       |            |               |
       +-- stream to temp storage; SHA-256 verification
           durable sync; atomic publish; CHUNK_ACCEPTED
```

**Roles:**

| Component | Responsibilities |
|---|---|
| Client | Hash the file and chunks, register metadata, upload through a bounded TCP pool, recover missing chunks, request completion, and poll status. |
| Custom load balancer | Handle initial `PING`, discover eligible head servers using periodically reported capacity and health, and return a reachable head-server endpoint. It is not on the chunk data path after assignment. |
| Head server | Own coordination for an assigned active upload; validate RPC headers and metadata; stream chunk payloads without full-chunk buffering; coordinate replica verification/repair; persist authoritative status; initiate ordered full-file verification. |
| Shared metadata database | Store global filename-to-content references, content records, manifest, per-chunk/per-replica state, physical locations, ownership of the active upload, and lifecycle/garbage-collection state. |
| Chunk server | Receive chunk payloads, calculate SHA-256 incrementally, write to temporary storage, synchronize data, publish verified chunks atomically, report durable acceptance, and participate in replica repair. |

## 3. File identity and deduplication

### 3.1 Three distinct concepts

1. **Filename:** The user-facing lookup and download key, e.g., `backup.zip`.
2. **Complete-file content hash:** A raw **SHA-512 digest (512 bits / 64 bytes)** used as the content identifier, whole-file deduplication key, and end-to-end integrity commitment.
3. **Chunk identity:** The ordered pair **`(file_hash, chunk_index)`** used by chunk servers as the logical physical-storage key. A chunk's SHA-256 digest is for integrity checking, **not** chunk-level deduplication.

There is **no separately generated `upload_id`** in the current model. Registration is idempotent using filename plus SHA-512, while the content hash identifies the one active uploader for that content. This is a deliberate simplification for the current trusted, globally shared namespace.

### 3.2 Filename rules

- Filenames are globally unique in the shared namespace.
- Registering a filename that already refers to a **committed** file returns `FILE_ALREADY_EXISTS`, even if the hash is identical.
- Retrying the **same incomplete** registration with the same normalized filename and SHA-512 returns the existing state and assigned head server.
- The same filename with a different hash is a conflict and is rejected.
- Different filenames may refer to the same verified content hash and therefore the same physical chunks.
- Filename uniqueness and reservation are enforced by an **atomic database constraint/transaction**, not only by an application-level pre-check.
- Filename normalization, encoding, length, and path-traversal restrictions must be specified and enforced consistently across head servers.

### 3.3 Whole-file deduplication only

- If content with the same SHA-512 is already **COMMITTED**, a new, different filename is linked to that content; no chunk bytes need to be uploaded again.
- If content with the same SHA-512 is currently being uploaded, the next registration becomes a `WAITING_FOR_CONTENT` filename reference.
- The shared database atomically elects **one active uploader per complete-file SHA-512**. Additional clients do not start another simultaneous transfer of the same content.
- On successful verification/commit, all waiting filename aliases become visible through the committed content record.
- If the elected uploader expires/fails, waiting clients can retry and one can be promoted through an atomic state transition.
- **No chunk-level deduplication:** Different complete-file hashes store independent physical chunks even if individual SHA-256 chunk hashes match.
- A matching claimed file hash for **uncommitted** content is not sufficient to expose any bytes or mark the content verified.

### 3.4 Filename-based discovery

Clients should be able to retrieve content by filename rather than memorizing a 512-bit hash. The desired listing capability is paginated (for example, 10 or 40 files per page) and may expose both filename and SHA-512 to trusted clients.

Suggested interface shape (not a finalized HTTP API contract):

```http
GET /files?limit=40&cursor=<opaque_cursor>
GET /files/<encoded_filename>
```

Actual download streaming remains out of scope. Any head server can resolve a filename to the committed content record and then to ordered chunk/replica locations via shared metadata.

## 4. Chunk sizing and hash algorithms

| Parameter | Current choice |
|---|---|
| Default/fixed chunk size | **16 MiB** = 16,777,216 bytes |
| Whole-file digest | **SHA-512** (64 bytes) |
| Individual chunk digest | **SHA-256** (32 bytes) |
| Chunk order | Zero-based `chunk_index`; independent of upload arrival/completion order |
| Last chunk | May contain fewer than 16 MiB, but must equal its calculated expected length |
| Empty file | Zero chunks; represented only by metadata; verify standard SHA-512 digest of empty input |
| Small nonempty file | One chunk of exactly the file's byte length |
| Future chunk-size negotiation | Deferred; selected size remains immutable for the active upload |

For a file of `file_size` bytes:

```text
chunk_count = ceil(file_size / 16_777_216)
chunk_count = 0 if file_size = 0
chunk_size(i) = 16_777_216 for each non-final chunk
final_chunk_size = file_size - (chunk_count - 1) * 16_777_216
```

Hashing is performed on the **raw chunk bytes only**, excluding the file-hash prefix and RPC framing. The manifest records the hash algorithms so stored digests are never interpreted ambiguously.

## 5. Registration and manifest protocol

### 5.1 Discovery (`PING` / `PONG`)

1. Client sends `PING` to the custom load balancer.
2. The load balancer selects a healthy head server using periodically refreshed capacity/health metrics. It does not synchronously query every server for every request.
3. `PONG` returns the selected head server's reachable connection information.
4. Client uses direct connections to that server for registration, upload, and completion.
5. If the chosen server becomes unreachable **before registration is established**, the client re-requests an assignment using bounded retries.
6. If the `REGISTER_UPLOAD` outcome is uncertain, the existing record must be checked before switching servers, because the registration might have committed even though its acknowledgment was lost.

Once registration exists, resume is pinned to the **original assigned head server** for now. Shared global metadata does not, by itself, implement safe cross-head ownership transfer of an active upload.

### 5.2 Initial registration (`REGISTER_UPLOAD`)

The client first computes the file hash, size, chunk count, and the ordered SHA-256 hashes of its chunks, then starts registration.

Basic metadata includes:

```text
file_name
file_size_bytes
file_hash_sha512          # 64 bytes
chunk_count
chunk_size_bytes          # 16 MiB for this version
file_hash_algorithm       # SHA-512
chunk_hash_algorithm      # SHA-256
```

Registration atomically reserves the filename, coordinates the one-uploader-per-hash rule, and creates or links to a content record. If the hash already references a committed content record, the new filename can be linked immediately without sending a manifest or chunks. If the content is being uploaded, the filename becomes a waiting reference.

### 5.3 Paginated manifest registration

Large manifests must **not** require a single unbounded RPC. The client uses bounded `REGISTER_MANIFEST_PART` requests. A batch contains a starting chunk index and ordered SHA-256 digests. **4,096 hashes per batch** (128 KiB of raw digests) is an illustrative initial setting, not a permanently fixed limit.

```text
REGISTER_MANIFEST_PART
  file_hash_sha512       : 64 bytes
  start_chunk_index     : integer
  hash_count            : integer
  chunk_hashes          : SHA-256[hash_count]
```

Rules:

- Batches may arrive **out of order**.
- Identical retries/overlapping entries are **idempotent** and accepted.
- Conflicting entries for a previously registered index are rejected **atomically**.
- A reconnecting client can query missing manifest ranges.
- `FINALIZE_MANIFEST` succeeds only if **every expected index** is present exactly and consistent with the file size, chunk count, and chunk boundaries.
- A successful finalization seals the manifest **immutable** and transitions the content to `READY_FOR_UPLOAD` (or an equivalent state).
- `UPLOAD_CHUNK` is not accepted before manifest finalization.
- Enforce limits on file size, chunk count, batch size, and total metadata resources.

## 6. Chunk upload and local storage

### 6.1 Client-to-head connections

- Use a **bounded pool of persistent TCP connections** to the assigned head server.
- The initial concurrency example is **four** connections; the limit is configurable and enforced server-side.
- Each connection sends **one outstanding chunk at a time** and waits for its acknowledgment before reusing that connection.
- Different connections may upload chunks in parallel; chunks can be received and acknowledged out of order.
- The head server must associate every request/response with the expected file hash and chunk index.

### 6.2 Lightweight custom RPC framing

Retain the project's **existing lightweight custom RPC protocol**; do not replace it with gRPC. The exact binary frame schema, endianness, bounds, and versioning have **not yet been finalized**.

Required logical `UPLOAD_CHUNK` information:

```text
OUTER RPC FRAME  (format to finalize)
  command = UPLOAD_CHUNK
  header / request framing

UPLOAD_CHUNK REQUEST BODY
  file_hash_sha512 : 64 raw bytes    # first field of the body
  chunk_index     : integer
  payload_length  : integer
  payload         : raw chunk bytes
```

The **512-bit complete-file SHA-512 appears at the beginning of each upload-chunk request body**, after any transport/RPC envelope required by the chosen framing. This hash prefix is **not part of the chunk payload** and is not included in SHA-256 verification.

The head server uses the hash and index to find the registered, finalized manifest. It validates the expected chunk size and retrieves the expected SHA-256 digest from shared metadata. The client does not have to repeat that per-chunk digest for every transfer.

Required logical downstream header:

```text
CHUNK_REPLICATION
  file_hash_sha512    : 64 bytes
  chunk_index        : integer
  expected_sha256    : 32 bytes (from immutable manifest)
  payload_length     : integer
  payload            : streamed bytes
```

### 6.3 Head-server zero-copy forwarding

- Parse only bounded headers into userspace.
- Forward the **payload**, of exactly the declared and validated length, using Linux kernel pipes, `splice()`, and (where needed) `tee()` to multiple required destinations.
- Do not store the full chunk in head-server RAM or on its filesystem.
- Manage short/partial transfers, `EAGAIN`, interruptions, pipe-buffer ownership, partial destination progress, and response framing.
- Use **bounded kernel-pipe backpressure**. A slow destination must not cause unbounded buffering.
- Apply a configurable **lack-of-progress timeout**, not merely an absolute duration that rejects legitimate slow transfers.
- On a bounded transfer failure, retry safely. Replicas already accepted should be reused rather than retransmitted unnecessarily.

Zero-copy forwarding does **not** mean zero kernel buffering, zero network-copy amplification, or zero cost for replication. With R required replica destinations, the head server may transmit approximately R times the incoming chunk bytes over its outbound network path. TLS/transport choice also affects real copy behavior and must be benchmarked separately.

### 6.4 Chunk-server receive, hash, and atomic publication

Chunk servers use **bounded userspace buffers** for the receive/write path (distinct from head-server zero-copy forwarding):

1. Receive exactly the expected payload length into **temporary storage**.
2. Incrementally update SHA-256 on the raw received bytes while writing them to the temporary file.
3. Reject short/extra-length or checksum-mismatched payloads.
4. Synchronize verified file contents to persistent storage (`fdatasync()`/`fsync()` as appropriate).
5. Atomically publish under **`(file_hash, chunk_index)`**, using no-replace semantics so a conflicting retry cannot overwrite an already valid chunk.
6. Synchronize the containing directory (and any newly created necessary namespace components) to satisfy the selected local persistence guarantee.
7. Return `CHUNK_ACCEPTED` only after verification, publication, and required durable syncs succeed.
8. Return `CHUNK_ALREADY_PRESENT` for an existing valid, identically identified, matching chunk.

Temporary and final publication paths must be compatible with atomic same-filesystem publication. Incomplete or failed temporary files are not publicly valid and are cleaned up later. `rename()` can replace an existing destination on many filesystems, so explicitly require a **no-replace** atomic mechanism where needed.

### 6.5 Bounded group commit

Chunk servers may batch compatible publication/synchronization work, but never weaken the preceding guarantee:

- Synchronize each affected chunk's data as required.
- Group compatible directory syncs where correct.
- Acknowledge each chunk only after **its** persistence barrier succeeds.
- Bound both batch size and maximum batching wait.
- **Illustrative benchmark starting points:** up to 16 chunks, up to 10 ms batching delay. These numbers were proposals, not confirmed hard defaults.

### 6.6 Replica acknowledgment and repair

- All replicas required by the current acceptance policy must report **verified and durably accepted** before the chunk can be globally accepted.
- When **some** replicas are invalid, direct another chunk server with a verified copy to repair the invalid destination; the receiver independently recomputes SHA-256.
- When **all** replicas are invalid/corrupt, request bounded chunk retransmission from the client.
- Treat network timeouts, checksum mismatches, and permanent storage rejection differently; do not blindly retry permanent failures.
- Avoid infinite retry loops. Apply configurable attempt ceilings and exponential backoff where appropriate.
- If a replica already holds the correct durable content, use its idempotent acknowledgment instead of writing the chunk again.

### 6.7 Authoritative per-chunk acceptance

After the required replicas report acceptance:

1. The head server atomically persists each replica's verified/accepted status in the shared database.
2. The chunk becomes `ACCEPTED` only when all **required** replicas meet the acceptance condition.
3. The shared database transaction **must commit** before the head server sends `UPLOAD_ACK` to the client.
4. If the database write fails or has an ambiguous outcome, reconcile the authoritative database state and use idempotent retry semantics.
5. Never hold an open shared-database transaction while streaming chunks across the network.

An accepted replica and a fully accepted chunk are **different states**. `UPLOAD_ACK` is a stronger statement than “the head server forwarded the bytes.”

## 7. Completion and end-to-end integrity

### 7.1 Completion request

The client sends `COMPLETE_UPLOAD(file_hash)` (exact parameter shape to be finalized) **after all expected chunk acknowledgments**. The request is an **idempotent request to finalize**, not the client unilaterally declaring success.

The head server atomically verifies that every expected chunk is accepted under the replica policy and transitions the content to `VERIFYING`. Repeated completion requests must not launch duplicate verification jobs. The client receives an initial status and polls `GET_UPLOAD_STATUS`.

The client disconnecting during verification does not cancel it. A separate verification lease/ownership mechanism protects work from upload-inactivity cleanup. The verified final commit is a short atomic database operation; hashing does **not** happen inside a long-running database transaction.

### 7.2 Complete-file SHA-512 verification

The assigned head server:

1. Retrieves stored chunks from chunk servers in **chunk-index order**.
2. Streams their bytes through a bounded userspace buffer into an incremental **SHA-512** computation.
3. Validates reconstructed byte count and order against the immutable manifest.
4. Compares the computed SHA-512 digest with the client-registered 64-byte digest.
5. On success, atomically updates the shared content record to `COMMITTED`.

This is a **second data path** distinct from zero-copy upload forwarding. It reads the complete logical file again over the network, but never materializes the full file in head-server memory or disk.

For a zero-byte file, calculate SHA-512 of the empty byte stream with zero chunks.

### 7.3 Verification failure

If the complete-file hash does not match:

- Diagnose chunk ordering, expected lengths, manifest entries, and retrieved chunk SHA-256 hashes.
- If a retrieved copy is corrupted, retry using a different independently verified replica and perform bounded recovery/reverification.
- If all chunks satisfy their recorded SHA-256 hashes and ordering but the whole-file SHA-512 still differs, mark `VERIFICATION_FAILED`.
- Never replace the registered expected SHA-512 with the newly calculated digest.
- Never expose content in `VERIFYING` or `VERIFICATION_FAILED` as a committed downloadable file.
- Avoid blindly retransmitting all bytes when the client supplied an internally inconsistent complete-file hash.

### 7.4 Atomic visibility for filename aliases

Read visibility is **derived from the shared content record**:

- If content is not `COMMITTED`, none of its waiting filename aliases is available as a completed file.
- After a single atomic transition to `COMMITTED`, all valid waiting filename aliases referencing that content become visible to file listing and downloads.
- The system does **not** need a separate transaction updating every alias's state to committed.
- Head servers must apply the same committed-content eligibility check on both file listing and download lookup.

## 8. Shared metadata and suggested logical model

The following entities are conceptual. This is **not yet a finalized SQL schema**.

### `file_content`

| Field | Meaning |
|---|---|
| `file_hash_sha512` (PK) | 64-byte complete-file content identity |
| `file_size_bytes` | Expected logical file size |
| `chunk_size_bytes` | Immutable per registered file, currently 16 MiB |
| `chunk_count` | Expected number of chunks |
| `file_hash_algorithm` / `chunk_hash_algorithm` | SHA-512 / SHA-256 |
| `status` | Registration, upload, verifying, committed, failed, expiration, deletion lifecycle |
| `assigned_head_server_id` | Original head server for the active upload |
| `upload_lease_expires_at` | Inactivity-based upload expiry (when applicable) |
| `verification_owner`, `verification_lease` | Coordinate asynchronous full-file verification |
| `created_at`, `updated_at` | Lifecycle tracking |

### `file_names`

| Field | Meaning |
|---|---|
| `normalized_file_name` (UNIQUE) | Globally unique user-facing filename |
| `file_hash_sha512` (FK) | Shared content record |
| `registration_status` (if needed) | Normal registered / waiting-reference state |
| `created_at` | Time of filename creation |

A filename points to a content record. A content record can have many filename references. A new alias for already committed content is a metadata-only operation.

### `content_chunks`

| Field | Meaning |
|---|---|
| `file_hash_sha512`, `chunk_index` (composite key) | Ordered chunk identity |
| `expected_sha256` | Digest from the immutable manifest |
| `expected_size_bytes` | Expected raw byte count |
| `chunk_status` | E.g., pending / accepted |

### `chunk_replicas`

| Field | Meaning |
|---|---|
| `file_hash_sha512`, `chunk_index`, `chunk_server_id` | Identifies a replica record |
| `replica_status` | Pending, verified/accepted, rejected, etc. |
| `accepted_at` | Confirmation timestamp |
| `storage_location` or stable server ID | Location resolution for later reads/repairs |

Use **stable chunk-server IDs**, not embedded IP addresses in immutable file manifests. Resolve IDs to current server endpoints separately. Logical chunk order comes from the index, not network location or physical arrival order.

### Reference tracking and garbage collection

- Deleting a filename removes its reference; it does not delete underlying chunks while other aliases or active operations still need them.
- When no references or protected active operations remain, content becomes eligible for garbage collection after a configurable grace period.
- A background process atomically marks eligible content `DELETING`, rechecks/prevents new references to it, performs idempotent physical cleanup, then records completion.
- New dedup references must not attach to content whose physical deletion has already begun.
- Cleanup cannot remove data being uploaded, verified, or used by another valid filename.
- Physical reference counts may be maintained explicitly or derived transactionally; the invariant is more important than a specific SQL representation.

## 9. Lifecycle state machines

### 9.1 Content upload (conceptual)

```text
[NOT_REGISTERED]
      |
      | REGISTER_UPLOAD (one uploader per SHA-512)
      v
[REGISTERING_MANIFEST]
      |
      | FINALIZE_MANIFEST (complete / immutable)
      v
[READY_FOR_UPLOAD]
      |
      | UPLOAD_CHUNK (bounded parallel)
      v
[UPLOADING] <--- retry / query missing chunks / resume same head
      |
      | all required chunks ACCEPTED; COMPLETE_UPLOAD
      v
[VERIFYING]  --hash mismatch or unrecoverable error--> [VERIFICATION_FAILED]
      |
      | ordered streamed SHA-512 matches
      v
[COMMITTED]
      |
      | last reference removed and grace/GC conditions satisfied
      v
[GC_ELIGIBLE] --> [DELETING] --> [DELETED]

Incomplete registration or upload:
[REGISTERING_MANIFEST | READY_FOR_UPLOAD | UPLOADING]
      --inactivity expiry--> [EXPIRED] --> async cleanup
```

Waiting filenames reference the uncommitted content but cannot expose its bytes. Successful content commit makes all eligible waiting names visible. `VERIFYING` is protected by its own lease and not expired using the normal upload inactivity timer.

### 9.2 Replica/chunk status (conceptual)

```text
Replica: PENDING -> RECEIVING -> VERIFIED_AND_DURABLE
                           \-> REJECTED / RETRYABLE_FAILURE

Chunk:   PENDING -> ACCEPTED
         only when every required replica is VERIFIED_AND_DURABLE
         and the shared database has durably recorded the result
```

These are conceptual states; the final wire error codes and SQL enum values are still to be defined.

## 10. Resumability, timeouts, and recoverability

### Client disconnect

- Preserve the active upload and its acknowledged chunks.
- Client reconnects to the **same assigned head server**.
- `GET_UPLOAD_STATUS` reports accepted and missing chunks; already accepted chunks must be safe to retry.
- A lost `UPLOAD_ACK` must not cause duplicate physical writes. Reconcile existing replica acceptances and current database state.
- The system can resume incomplete manifest registration by reporting missing hash ranges.

### Inactivity expiration

- Use configurable **inactivity-based upload-session expiry**; an initial **24-hour** timeout was suggested.
- Valid session activity extends its lease.
- Expired incomplete uploads are cleaned up **asynchronously**; do not delete content on an ordinary client disconnect.
- Expiration/recovery state changes must be coordinated with the shared database; avoid racing a resumed upload or verification.
- Never treat an unreceived final client response as proof that a committed file should be deleted.

### Shared-database outage (modified decision 29)

- Head servers attempt bounded database recovery and remain operational where safe.
- Keep a **bounded in-memory backlog of metadata updates**, **not raw chunk bytes**, while the database is unavailable.
- Continue already validated/registered chunk transfers while the head server has safe bounded capacity and sufficient authoritative manifest information.
- When buffer/resource thresholds are reached, stop admitting work and apply backpressure / return retryable errors, before reaching out-of-memory conditions.
- Once the database becomes available, reconcile and **gradually drain** pending updates.
- Do not issue `UPLOAD_ACK` until the shared database has actually committed the required acceptance state, even if all replicas are already durable.
- Suspend operations needing global authoritative checks, such as new registrations, dedup links, final commit, and deletion transitions, while DB availability is insufficient.
- In-memory backlog is **not crash durable**: a head-process crash may lose queued metadata. Recovery must reconcile physical chunk states with authoritative database records; unacknowledged operations can be retried.
- Use separate limits for DB-outage backlog, active transfers, and admission queues; do not wait for critical memory exhaustion.

### Head-server selection and loss

- Use resource-aware load balancing for **new** assignments (CPU, memory pressure, active transfers, available capacity, bandwidth, health as possible inputs).
- The selected head server still enforces admission control.
- If a head server is unreachable **before registration**, re-run discovery through the LB.
- If the registration result is uncertain, first check shared metadata, since an ACK can be lost after the registration commits.
- Established uploads are pinned to the selected head server for now. Cross-head recovery of an active upload is **explicitly deferred**.

## 11. Resource management, backpressure, and retry policy

### Head-server capacity

- Configure limits for concurrent chunk streams, active TCP connections, file descriptors/kernel pipes, memory and metadata buffers.
- Maintain a **bounded queue of lightweight request descriptors**, not complete chunk payloads.
- At soft pressure limits, stop admission and apply backpressure while allowing in-flight work to finish.
- At hard limits, return `SERVER_BUSY` plus retry guidance.
- Existing sessions should receive fair access instead of being starved by new registrations.
- Monitor the in-memory DB-outage backlog separately from transfer concurrency.

### Failure-specific retries

| Failure | Behavior |
|---|---|
| Transient network error / no downstream progress | Bounded retries with backoff; apply timeout and backpressure |
| One or more invalid replicas, another verified replica exists | Perform replica-to-replica repair; receiver verifies SHA-256 |
| All required replicas invalid | Request bounded client retransmission of the chunk |
| Existing valid identical replica | Reuse it / `CHUNK_ALREADY_PRESENT` |
| Permanent storage rejection | Fail the affected attempt; do not blindly retry |
| Conflicting metadata or malformed frame | Reject immediately |
| Database update unavailable or uncertain | Buffer within limits, reconcile authoritative state, retry safely; no premature client ACK |
| Full-file SHA-512 mismatch | Diagnose chunks/order, attempt bounded alternate-replica recovery, then `VERIFICATION_FAILED` |

Counts, timeout durations, backoff ranges, jitter, and `SERVER_BUSY` retry intervals remain configurable and are not yet assigned final numeric defaults.

## 12. Proposed RPC catalog

These are logical commands implied or agreed by the design; their exact byte-level serialization and response schemas require a separate wire-protocol specification.

| Command / response | Purpose |
|---|---|
| `PING` / `PONG` | Initial load-balancer discovery and assigned-head information |
| `REGISTER_UPLOAD` / `REGISTER_UPLOAD_ACK` | Create or idempotently find a registration; elect an uploader or create a filename alias |
| `REGISTER_MANIFEST_PART` | Register indexed batch of SHA-256 chunk digests |
| `FINALIZE_MANIFEST` | Ensure all manifest hashes are complete and immutable |
| `UPLOAD_CHUNK` | Client sends one chunk (SHA-512 prefix, index, length, data) |
| `CHUNK_REPLICATION` | Head forwards to required chunk servers with expected SHA-256 |
| `CHUNK_ACCEPTED` | Chunk-server acceptance after verification and local durability |
| `CHUNK_ALREADY_PRESENT` | Idempotent acknowledgment of matching preexisting durable chunk |
| `UPLOAD_ACK` | Client acknowledgment only after required replica durability and DB commit |
| `GET_UPLOAD_STATUS` | Resume missing chunks, discover current upload / verification state |
| `COMPLETE_UPLOAD` | Idempotently initiate asynchronous full-file verification and commit |
| `FILE_ALREADY_EXISTS` | Filename conflicts with an existing committed or incompatible registration |
| `SERVER_BUSY` | Admission/retryable capacity rejection |
| `METADATA_UNAVAILABLE` | Shared metadata cannot currently support the requested operation |

The name `UPLOAD_SUCCESSFUL` from the original draft is **replaced** by client-initiated `COMPLETE_UPLOAD` followed by server-authoritative verification and commit. An upload's completion is not trusted merely because the client reports success.

## 13. Operational sizing and preliminary parameters

| Setting | Status | Value / direction |
|---|---|---|
| Whole-file hashing | Confirmed | SHA-512 |
| Per-chunk hashing | Confirmed | SHA-256 |
| Fixed chunk size | Confirmed | 16 MiB |
| Client-to-head protocol | Confirmed | Existing small custom RPC over TCP |
| Parallel transfer style | Confirmed | Bounded persistent TCP connection pool; one in-flight chunk per connection |
| Initial parallel connections | Chosen illustrative starting point | 4; configurable |
| Manifest batching | Confirmed | Indexed, out-of-order, idempotent batches |
| Suggested manifest batch size | Proposed, tune later | 4,096 SHA-256 digests (~128 KiB raw) |
| Incomplete-upload inactivity timeout | Initial suggested setting | 24 hours; configurable |
| Chunk-server durability | Confirmed | File data sync + atomic no-replace publication + required directory sync before ACK |
| Group commit | Confirmed | Bounded batching without early ACK |
| Group-commit initial benchmark values | Proposed only | Max 16 chunks / max 10 ms delay |
| Replica count and placement | Out of scope | Determined by server policy, not arbitrary client input |
| Maximum file/chunk count, memory, queue, retry thresholds | To tune | Server-configurable, finite limits required |

**Manifest digest example:** A 100 GiB file contains 6,400 chunks at 16 MiB each; raw SHA-256 digest metadata is 200 KiB, excluding protocol and database overhead.

## 14. Cross-cutting correctness invariants

These rules should be encoded in implementation contracts and tested under failures:

1. A filename is globally unique, and an incomplete identical registration is safely retryable.
2. Exactly one active upload owns a given complete-file SHA-512 at a time.
3. An uncommitted content record can never be downloaded as a completed file, even if alias names already exist.
4. The manifest is complete and immutable before any chunk transfer is accepted.
5. `(file_hash, chunk_index)` maps to the registered expected length and SHA-256; bytes outside the raw payload never enter the chunk checksum.
6. An incomplete, mismatched, or merely buffered temporary chunk is never published as valid.
7. A chunk server cannot replace a valid published chunk with conflicting bytes.
8. A chunk server reports `CHUNK_ACCEPTED` only after meeting local durability requirements.
9. The head server reports `UPLOAD_ACK` only after every required replica is accepted **and** the shared metadata has committed that state.
10. Database transactions do not span network streaming or full-file hash computation.
11. `COMPLETE_UPLOAD` is idempotent and commits only after recomputing the ordered file's SHA-512.
12. No client disconnect or lost acknowledgment may trigger deletion of a committed file.
13. In-memory outage buffering is bounded and does not weaken the authoritative ACK contract.
14. Removing one filename does not delete content referenced by another filename or protected active operation.
15. Garbage collection and new alias creation cannot race into a state where reused content is physically deleted.
16. All retries, transfer queues, pipe buffers, and upload leases must be bounded; no infinite waiting or unbounded memory growth.

## 15. Suggested validation and failure-injection tests

This is a **recommended test plan**, not a separate design decision.

- Lost `REGISTER_UPLOAD_ACK`; identical registration recovers with original head assignment.
- Two heads concurrently register the same filename with different hashes; only one wins.
- Two filenames concurrently register the same SHA-512; only one uploader is elected; the other waits.
- Out-of-order and duplicate `REGISTER_MANIFEST_PART` batches; conflicting overlap rejected atomically.
- Chunk upload completed on storage but client ACK lost; retry returns success without duplicating data.
- One chunk server returns a hash mismatch; peer-to-peer repair uses a verified source.
- All replicas mismatch; bounded client retry, then deterministic failure.
- Slow destination blocks a pipe; bounded backpressure and lack-of-progress timeout keep RAM bounded.
- Shared DB unavailable after physical storage success; no client ACK before durable metadata commit; buffered queue drains after recovery.
- Head server crashes with unflushed in-memory metadata; storage can be reconciled via idempotent retries.
- Chunk-server process crashes after partial temporary write; no partial chunk is visible.
- Power-loss test around file sync / atomic publication / directory sync boundaries.
- Full-file SHA-512 mismatch despite matching registered chunk hashes; content is not committed.
- Client disconnects during asynchronous `VERIFYING`; verification lease protects active work.
- Last filename deleted while another head attempts dedup alias creation; only a transactionally safe outcome is allowed.
- Empty file, exact multiple of 16 MiB, one-byte file, and final short chunk.
- Metadata-list pagination and direct filename lookup return only committed content.

## 16. Unresolved decisions and implementation details
LLM question need to be answered by future me.Too tired to read any more.
### Next architecture question — Question : physical chunk storage layout

**Not yet decided.** Options discussed but **not selected**:

- **A. Individual immutable files per chunk** in a hash-sharded directory hierarchy. Simplest for temporary write + atomic no-replace publish + garbage collection; creates many filesystem entries at scale.
- **B. Append-only segment files** containing multiple chunks with a local offset/length index. Fewer filesystem entries but more involved indexing, recovery, and garbage collection.
- **C. Hybrid storage**: individual chunk files first, then background compaction into segments. Supports both formats and migration complexity.

The previous recommendation was A for the initial version; **there has been no user selection yet**.

### Other outstanding details (not silently assumed decided)

- Exact binary RPC envelope, protocol version, endianness, header field widths, error codes, and maximum message sizes.
- Final numeric defaults for queue sizes, connection limits, retry budgets, progress timeouts, group commit, DB-outage memory caps, and maximum supported file size.
- Filename canonicalization/encoding policy and whether slash-separated directory paths are supported.
- Precise shared SQL schema, indexes, atomic state-transition queries, and verification/GC leases.
- Process-level recovery of in-memory outage buffers and reconciliation method against chunk-server acknowledgments.
- Exact API/transport shape for file list, filename-based download, and cursor pagination.
- Disk/filesystem-specific durability procedures and benchmarks of the 16 MiB chunk pipeline.
- Server-local temporary-file lifecycle and restart-time cleanup/reconciliation procedures.
- Full server failure tolerance, replica placement, and cross-head upload takeover (intentionally deferred).
