
### DFG RPC Command Table

| Command Name | `msg_type` ID | Target Server | Payload Structure | What it Does |
| --- | --- | --- | --- | --- |
| **`PING`** | `0x00` | Either | None (`payload_len = 0`) | Instant liveness pulse check to verify node availability. |
| **`PONG`** | `0x01` | Either | None (`payload_len = 0`) | Response packet sent back immediately to acknowledge a `PING`. |
| **`REGISTER_UPLOAD`** | `0x10` | Head Server | File ID + Total Size + Array of Chunk Hashes | Client initiates a new file upload handshake, sending the metadata manifest. |
| **`REGISTER_UPLOAD_ACK`** | `0x11` | Head Server | Assigned Node IP/Ports for each chunk | Head Server replies with the routing ticket mapping chunks to specific Chunk Servers. |
| **`GET_METADATA`** | `0x12` | Head Server | File ID string | Client requests the block location map to download a file. |
| **`METADATA_RESP`** | `0x13` | Head Server | Chunk count + list of assigned Chunk Server addresses & hashes | Head Server returns the blueprint required for the client to pull chunks. |
| **`UPLOAD_CHUNK`** | `0x20` | Chunk Server | Chunk ID string + Raw binary block bytes | Client streams a 4MB file block directly to the assigned Chunk Server. |
| **`UPLOAD_ACK`** | `0x21` | Chunk Server | Status code / Error string | Chunk Server confirms the block was written to disk and verified via SHA-256. |
| **`DOWNLOAD_CHUNK`** | `0x22` | Chunk Server | Chunk ID string | Client requests a specific chunk block by ID. |
| **`DOWNLOAD_RESP`** | `0x23` | Chunk Server | Raw binary block bytes stream | Chunk Server streams the requested block data back down the TCP pipe. |
| **`DELETE_FILE`** | `0x30` | Chunk Server | Chunk ID string | Tells the Chunk Server to wipe a specific block file off its local SSD. |
| **`DELETE_ACK`** | `0x31` | Chunk Server | Status code | Confirms the file block has been successfully deleted. |
| **`ERROR_RESP`** | `0xFF` | Either | Error message string | Standardized failure response for checksum mismatches, missing chunks,delete failure, or disk full errors. |


| Error_code | Type |
|--- | --- |
| 0x0_ | simple server level commands | 
| 0x1_ | metadata data commands       |
| 0x2_ | chunk level commands         |
| 0x3_ | File lifecycle commands      |
| 0xF_ | errors                       |

