# BitTorrent Protocol
Simple peer-to-peer file-sharing system using the MPI library. The system consists of a tracker process (rank 0) and multiple peer processes (ranks 1 and above). The tracker manages the file swarm, and peers can request and download segments of files from each other.

------------------------------------------------------------------------

## Table of Contents:
- Data Structures
- Functions
------------------------------------------------------------------------

## Data Structures

- `client_sp`: Structure representing files for a specific client.
- `client_data`: Structure representing client information as provided by the tracker.
- `file_data`: Structure representing file swarm.

------------------------------------------------------------------------
## Functions

#### receive_swarm

Responsible for receiving and storing the file swarm information from the tracker. It receives the number of members in the swarm, the swarm structure, and the requested file hash segments.

-  `num_members` - Pointer to the number of members in the file swarm.
* `swarm` - Pointer to the file_data structure to store the swarm.
------------------------------------------------------------------------
#### request_client

Sends requests to a specific client for file segments and manages the download process. Returns the last sent segment.

* `last_segm` - The last downloaded segment for a specific file. 
* `swarm` - Pointer to the file_data structure representing the swarm.
* `filename` - Name of the file being requested.
*  `requested_client` - Index of the requested client in the swarm.
*  `cmd_msg` - Pointer to the command message to track the communication status.
------------------------------------------------------------------------

#### download_thread_func

Represents the download thread for each peer.

* `files` -  A map containing the files of the current client. 
*  `rank` - The rank of the peer.

------------------------------------------------------------------------
#### get_client_data

Gets file information from clients, including the file name, number of hash segments, and the hash strings. It updates the client database accordingly.

* `numfiles` - Number of files for a specific client.
* `char_file_name` - Pointer to a character array to store the file name. 
* `client_no` -  The rank of the client. 
* `database`  - Pointer to the unordered_map representing the client database.

------------------------------------------------------------------------

#### send_swarm


Sends swarm information and data to a client.

* `size_members` - The number of members in the swarm.
* `status` - Pointer to the MPI_Status structure. 
* `swarm` -  Pointer to the file_data structure representing the swarm.

------------------------------------------------------------------------
#### update_database

This function updates the client database based on information received from a client.

* `last_segm` - The last downloaded segment for a specific file.
*  `status` -  Pointer to the MPI_Status structure.
*  `database` - Pointer to the unordered_map representing the client database.
*  `finished_file_name` - Name of the file that finished downloading.

------------------------------------------------------------------------