
# Chime 

Chime is a chat application that supports text, audio, and video call communication.
## Context and Scope

A general user is any individual, team, or business. 

Any user will have access to:
- Private messaging with another user
- Group messaging within a server
- Chat history
- File sharing
- Voice and video calls 

Messaging will be asynchronous, with preference given to the server in conflict resolution.

Chime is designed to handle:
- 1M users per server
- 100 servers per user

Chime plans to support both Linux and Windows platforms, but will prioritize Linux due to developer preference.
## Design

### Architecture

Chime uses a hub-and-spoke architecture exposing an internal API interfacing between its Next.js front-end and C back-end, storing messages in a MySQL database. 
### Messaging Protocol

Messages will be sent via a Publish and Subscribe pattern, where clients subscribe to any number of servers, each publishing its user and channel contents, including chat history.
#### Chat History and Synchronization

##### Delta Log

The server will keep an updated log of all changes and revisions to the global chat history as a series of revisions (deltas). This includes newly sent messages as well as deleted messages. For each revision, the server will also keep a unique sequence id, to keep track of all changes in the order the server receives them. Servers will process these events first come first serve, and propagate these to the clients. Once a delta is recorded, it is never altered within the server. 

##### Delta Log Partitioning

To prioritize scalability, delta logs will be partitioned depending on size, and only the newest created partition will be added to, until it reaches a specified size determined by the load capabilities of the server (see hardware specifications under Performance)
##### Synchronizing clients

Should the server or client ever believe they are out of sync, either party can send a SYNC request to the other. Both parties will respond with the ID of their most recent delta. If the client's delta ID does not match the server's delta ID, the client should then request all deltas up to the server's current delta ID. Each delta is processed as a transaction, and each message ID is unique, and clients will always prefer the server's delta log, so that conflicts between clients will not occur.
##### Backup/Restore

There are a few situations that warrant completely restoring a user's history with a server, such as newly joined users.

In the event that a server deems a request for restoration valid, it will send a backup file of the entire delta log to the user. Since this is a potentially expensive operation, it will be rate limited depending on a combination of the server's current traffic, the size of the delta log's most recent partition, and the number of partitions the user is currently requesting to restore (see Performance) TODO.

### APIs 

#### SYNC 
Client -> Server
Endpoint: `/api/sync/`
Description: Synchronizes the client's delta log with the server
**Request**
```json
{
  "action": "SYNC",
  "data": {
    "client_delta_id": "12345"  // Last delta ID the client has
  }
}
```

**Response**:

```json
{
  "action": "SYNC_RESPONSE",
  "data": {
    "server_delta_id": "12350",   // Current server delta ID
    "missing_deltas": [
      {
        "delta_id": "12346",
        "type": "message",
        "content": "Hello World!",
        "timestamp": "2024-11-19T12:00:00Z"
      }
    ]
  }
}

```

#### POST
Client -> Server
Endpoint: `/api/post/`
Description: Send a message to a chat room.
**Request**
```json
{
  "action": "POST_MESSAGE",
  "data": {
    "channel-id": "123",
    "content": "Hello, World!"
  }
}

```

**Response**
```json
{
  "action": "POST_MESSAGE_RESPONSE",
  "data": {
    "status": "success"
  }
}

```


#### PUBLISH
Server -> Client
Endpoint: `/api/publish/`
Description: Push new deltas to subscribed clients.
**Content**
```json
{
  "action": "PUBLISH_DELTA",
  "data": {
    "delta_id": "12346",
    "type": "message",
    "channel_id": "chat-room-123",
    "content": "Hello, World!",
    "timestamp": "2024-11-19T12:34:56Z"
  }
}

```


#### RESTORE
Client -> Server
Endpoint: `/api/restore/`
Description: Retrieve chat history for a room.
**Request**
```json
{
  "action": "RESTORE_HISTORY",
  "data": {
    "channel_id": "chat-room-123"
  }
}
```

**Response**
```json
{
  "action": "RESTORE_HISTORY_RESPONSE",
  "data": {
    "deltas": [
      {
        "delta_id": "12345",
        "type": "message",
        "content": "Hello, World!",
        "timestamp": "2024-11-19T12:00:00Z"
      },
      {
        "delta_id": "12346",
        "type": "deleted_message",
        "timestamp": "2024-11-19T12:10:00Z"
      }
    ]
  }
}

```

#### Files
##### UPLOAD
Client -> Server
Endpoint: `/api/files/upload/`
Description: Upload files (max size: 100MB). 
TODO verify how HTTP file upload works
**Request**:
```json
{
	file_metadata: "TODO",
	channel_id: "chat-room-123"
}
```
**Response**:
HTTP 200 OK:
```json
{
	"response": "OK",
	"file_id": "file123",
	"upload_url": "https://example.com/uploads/file123"
}
```

##### DOWNLOAD
Client->Server
Endpoint: `/api/files/download/`
Description: Stream files back to the client.
TODO verify hot HTTP file downloads work
**Request**
```json
{
	"file_id": "file123",
}
```
**Response**:
HTTP 200 OK:
```json
{
	"response": "OK",
	"download_url": "https://example.com/download/file123"
}
```


#### Summary Table

| **Action**                | **Protocol** | **Latency** | **Description**                             |
| ------------------------- | ------------ | ----------- | ------------------------------------------- |
| SYNC                      | WebSocket    | <50ms       | Synchronize client's state with the server. |
| POST (Message)            | WebSocket    | <100ms      | Send a message to a chat room.              |
| PUBLISH (Processed Delta) | WebSocket    | <50ms       | Push new deltas to subscribed clients.      |
| RESTORE                   | WebSocket    | <500ms      | Retrieve chat history for a room.           |
| UPLOAD (File)             | HTTP POST    | <5s         | Upload files (max size: 100MB).             |
| DOWNLOAD (File)           | HTTP GET     | <2s         | Stream files back to the client.            |
|                           |              |             |                                             |
### Data Storage

### Database Schema

#### Delta Log
##### **Tables**

###### 1. **`deltas`**

Stores the changes (deltas) for chat history, including messages and other events (e.g., deletions, edits).

| **Column Name**     | **Data Type**                     | **Constraints**                     | **Description**                                      |
| ------------------- | --------------------------------- | ----------------------------------- | ---------------------------------------------------- |
| `delta_id`          | BIGINT                            | Primary Key, Auto Increment         | Unique identifier for each delta.                    |
| `channel_id`        | VARCHAR(255)                      | Foreign Key (`channels.channel_id`) | The ID of the chat room associated with the delta.   |
| `type`              | ENUM('message', 'edit', 'delete') | NOT NULL                            | Type of delta (message creation, deletion, etc.).    |
| `content`           | TEXT                              | NULLABLE                            | Content of the delta (only for message-type deltas). |
| `timestamp`         | TIMESTAMP                         | NOT NULL, DEFAULT CURRENT_TIMESTAMP | When the delta was created.                          |
| `user_id`           | VARCHAR(255)                      | Foreign Key (`users.user_id`)       | The ID of the user responsible for the delta.        |
| `attachments`       | JSON                              | NULLABLE                            | Array of file IDs (for deltas with attachments).     |
| `is_partition_head` | BOOLEAN                           | NOT NULL, DEFAULT FALSE             | Indicates if the delta starts a new partition.       |

 **Indexes**

- **`PRIMARY KEY (delta_id)`**: Unique identifier for each delta.
- **`INDEX (room_id, timestamp)`**: Optimizes queries for retrieving deltas by room and time range.
- **`INDEX (user_id)`**: Speeds up queries related to user-specific actions.

---

###### 2. **`channels`**

Stores metadata for chat rooms where deltas occur.

| **Column Name** | **Data Type** | **Constraints**                     | **Description**                       |
| --------------- | ------------- | ----------------------------------- | ------------------------------------- |
| `channel_id`    | VARCHAR(255)  | Primary Key                         | Unique identifier for the chat room.  |
| `name`          | VARCHAR(255)  | NOT NULL                            | Human-readable name of the chat room. |
| `created_at`    | TIMESTAMP     | NOT NULL, DEFAULT CURRENT_TIMESTAMP | When the room was created.            |

---

###### 3. **`delta_partitions`**

Tracks partitioning of the delta log for scalability and performance.

| **Column Name**  | **Data Type** | **Constraints**                           | **Description**                                    |
| ---------------- | ------------- | ----------------------------------------- | -------------------------------------------------- |
| `partition_id`   | BIGINT        | Primary Key, Auto Increment               | Unique identifier for the partition.               |
| `channel_id`     | VARCHAR(255)  | Foreign Key (`channels.channel_id`)       | The ID of the room associated with this partition. |
| `start_delta_id` | BIGINT        | Foreign Key (`deltas.delta_id`), NOT NULL | First delta in this partition.                     |
| `end_delta_id`   | BIGINT        | Foreign Key (`deltas.delta_id`), NOT NULL | Last delta in this partition.                      |
| `created_at`     | TIMESTAMP     | NOT NULL, DEFAULT CURRENT_TIMESTAMP       | When the partition was created.                    |

 **Indexes**

- **`INDEX (room_id)`**: Optimizes queries to retrieve partitions for a specific room.

---

###### 4. **`users`**

Stores user metadata for participants in chat rooms.

| **Column Name** | **Data Type** | **Constraints**                     | **Description**                    |
| --------------- | ------------- | ----------------------------------- | ---------------------------------- |
| `user_id`       | VARCHAR(255)  | Primary Key                         | Unique identifier for the user.    |
| `username`      | VARCHAR(255)  | NOT NULL                            | User’s display name.               |


---

##### **Relationships**

- **`deltas.channel_id` → `rooms.channel_id`**: Each delta is associated with a specific chat room.
- **`deltas.user_id` → `users.user_id`**: Each delta references the user who created it.
- **`delta_partitions.room_id` → `rooms.room_id`**: Partitions belong to specific rooms.
- **`delta_partitions.start_delta_id` and `delta_partitions.end_delta_id`** → `deltas.delta_id`: Defines the range of deltas in a partition.

---

 **Partitioning Strategy**

- **Delta Partitioning**:
    - Each room's delta log is partitioned into ranges defined in the `delta_partitions` table.
    - New deltas are written to the most recent partition until a size threshold is reached (e.g., **10MB or 10,000 deltas**).
- **Indexing in Partitions**:
    - Queries within partitions are accelerated by indexing `timestamp` and `delta_id`.



#### Messages

##### **Tables**

---

###### 1. **`messages`**

Stores all messages sent within chat rooms.

| **Column Name** | **Data Type** | **Constraints**                                     | **Description**                                     |
| --------------- | ------------- | --------------------------------------------------- | --------------------------------------------------- |
| `message_id`    | BIGINT        | Primary Key, Auto Increment                         | Unique identifier for each message.                 |
| `room_id`       | VARCHAR(255)  | Foreign Key (`rooms.room_id`)                       | The ID of the chat room where the message was sent. |
| `user_id`       | VARCHAR(255)  | Foreign Key (`users.user_id`)                       | The ID of the user who sent the message.            |
| `content`       | TEXT          | NULLABLE                                            | The text content of the message.                    |
| `timestamp`     | TIMESTAMP     | NOT NULL, DEFAULT CURRENT_TIMESTAMP                 | The time when the message was sent.                 |
| `is_edited`     | BOOLEAN       | NOT NULL, DEFAULT FALSE                             | Indicates if the message was edited.                |
| `is_deleted`    | BOOLEAN       | NOT NULL, DEFAULT FALSE                             | Indicates if the message was deleted.               |
| `attachment_id` | BIGINT        | Foreign Key (`attachments.attachment_id`), NULLABLE | ID of the attachment associated with this message.  |

 **Indexes**

- **`PRIMARY KEY (message_id)`**: Unique identifier for each message.
- **`INDEX (room_id, timestamp)`**: Optimizes retrieval of messages by room and time.
- **`INDEX (user_id)`**: Speeds up queries related to user-specific actions.
- **`INDEX (is_deleted)`**: Helps quickly filter out deleted messages.

---

###### 2. **`attachments`**

Stores information about message attachments, such as files or multimedia.

| **Column Name** | **Data Type** | **Constraints**                     | **Description**                            |
| --------------- | ------------- | ----------------------------------- | ------------------------------------------ |
| `attachment_id` | BIGINT        | Primary Key, Auto Increment         | Unique identifier for the attachment.      |
| `message_id`    | BIGINT        | Foreign Key (`messages.message_id`) | The ID of the associated message.          |
| `file_url`      | TEXT          | NOT NULL                            | URL where the file is stored.              |
| `file_type`     | VARCHAR(50)   | NOT NULL                            | MIME type of the file (e.g., `image/png`). |
| `file_size`     | BIGINT        | NOT NULL                            | Size of the file in bytes.                 |
| `uploaded_at`   | TIMESTAMP     | NOT NULL, DEFAULT CURRENT_TIMESTAMP | When the file was uploaded.                |

 **Indexes**

- **`PRIMARY KEY (attachment_id)`**: Unique identifier for each attachment.
- **`INDEX (message_id)`**: Optimizes retrieval of attachments for a message.

---

###### 3. **`channels`**

Stores metadata about chat rooms.

| **Column Name** | **Data Type** | **Constraints** | **Description**                       |
| --------------- | ------------- | --------------- | ------------------------------------- |
| `channel_id`    | VARCHAR(255)  | Primary Key     | Unique identifier for the channel.    |
| `name`          | VARCHAR(255)  | NOT NULL        | Human-readable name of the chat room. |


---

###### 4. **`users`**

Stores metadata about users participating in chats.

| **Column Name** | **Data Type** | **Constraints** | **Description**                 |
| --------------- | ------------- | --------------- | ------------------------------- |
| `user_id`       | VARCHAR(255)  | Primary Key     | Unique identifier for the user. |
| `username`      | VARCHAR(255)  | NOT NULL        | User’s display name.            |


---

##### **Relationships**

1. **`messages.room_id` → `rooms.room_id`**:
    - Each message is associated with a specific chat room.
2. **`messages.user_id` → `users.user_id`**:
    - Each message references the user who sent it.
3. **`attachments.message_id` → `messages.message_id`**:
    - Attachments belong to specific messages.

---

#### Servers
##### **Tables**

---

###### 1. **`servers`**

Stores metadata about servers.

|**Column Name**|**Data Type**|**Constraints**|**Description**|
|---|---|---|---|
|`server_id`|VARCHAR(255)|Primary Key|Unique identifier for the server.|
|`name`|VARCHAR(255)|NOT NULL|Human-readable name of the server.|
|`owner_id`|VARCHAR(255)|Foreign Key (`users.user_id`)|The ID of the user who owns the server.|
|`created_at`|TIMESTAMP|NOT NULL, DEFAULT CURRENT_TIMESTAMP|When the server was created.|
|`description`|TEXT|NULLABLE|A description or purpose of the server.|
|`max_users`|INT|NOT NULL, DEFAULT 1000000|Maximum number of users allowed on the server.|
|`is_public`|BOOLEAN|NOT NULL, DEFAULT TRUE|Whether the server is publicly discoverable.|

 **Indexes**

- **`PRIMARY KEY (server_id)`**: Unique identifier for each server.
- **`INDEX (owner_id)`**: Optimizes queries to retrieve servers owned by a user.
- **`INDEX (is_public)`**: Helps quickly filter public and private servers.

---

###### 2. **`server_users`**

Tracks the relationship between servers and their users, including roles and permissions.

|**Column Name**|**Data Type**|**Constraints**|**Description**|
|---|---|---|---|
|`server_user_id`|BIGINT|Primary Key, Auto Increment|Unique identifier for the relationship.|
|`server_id`|VARCHAR(255)|Foreign Key (`servers.server_id`)|The ID of the server.|
|`user_id`|VARCHAR(255)|Foreign Key (`users.user_id`)|The ID of the user.|
|`role`|ENUM('owner', 'admin', 'moderator', 'member')|NOT NULL|The role of the user within the server.|
|`joined_at`|TIMESTAMP|NOT NULL, DEFAULT CURRENT_TIMESTAMP|When the user joined the server.|

 **Indexes**

- **`PRIMARY KEY (server_user_id)`**: Unique identifier for each relationship.
- **`INDEX (server_id, user_id)`**: Optimizes lookups for user memberships in a server.
- **`INDEX (role)`**: Speeds up role-specific queries.

---

###### 3. **`server_channels`**

Tracks channels (e.g., text, voice) within a server.

|**Column Name**|**Data Type**|**Constraints**|**Description**|
|---|---|---|---|
|`channel_id`|VARCHAR(255)|Primary Key|Unique identifier for the channel.|
|`server_id`|VARCHAR(255)|Foreign Key (`servers.server_id`)|The ID of the server the channel belongs to.|
|`name`|VARCHAR(255)|NOT NULL|Human-readable name of the channel.|
|`type`|ENUM('text', 'voice')|NOT NULL|The type of channel (text or voice).|
|`created_at`|TIMESTAMP|NOT NULL, DEFAULT CURRENT_TIMESTAMP|When the channel was created.|

 **Indexes**

- **`PRIMARY KEY (channel_id)`**: Unique identifier for each channel.
- **`INDEX (server_id)`**: Optimizes retrieval of channels by server.


---

 **Relationships**

1. **`servers.owner_id` → `users.user_id`**:
    - A server is owned by a specific user.
2. **`server_users.server_id` → `servers.server_id`**:
    - Tracks users who are members of a server.
3. **`server_users.user_id` → `users.user_id`**:
    - Tracks user-specific memberships across servers.
4. **`server_channels.server_id` → `servers.server_id`**:
    - Each channel is associated with a specific server.

---

 **Partitioning Strategy**

- **Servers**: No partitioning is typically needed unless handling an extremely large number of servers. Indexing is sufficient for most cases.
- **Server Users**: Partitioning by `server_id` can improve performance in servers with high user counts.
- **Server Channels**: Similar partitioning by `server_id` for large-scale deployments.
## Performance

## Security

## Trade offs

## Maintenance
