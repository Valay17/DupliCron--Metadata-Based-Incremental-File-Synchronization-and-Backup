<div align="center">

<h1> <p>Metadata Based Incremental File Backup</p> </h1>
</div>

FileSync is a fast and highly configurable file synchronization utility for local and external backups, available on both Windows and Linux. It supports multiple source directories and uses a metadata-based incremental sync approach to avoid redundant copying. File changes are detected using a combination of file size and modification time, which are hashed and stored in a binary cache to ensure accurate and efficient updates across runs.

Built in modern C++, the tool leverages multithreaded scanning and hashing to accelerate sync decisions across large directories. A dedicated copy manager serializes I/O operations per source to prevent disk contention and ensure stable performance. FileSync includes robust failure tracking: unsuccessful copy operations are detected and logged, and metadata is updated post-copy to reflect actual copy status, allowing precise reporting and recovery on future syncs. This makes it particularly well-suited for repeated backups, local mirroring, and any scenario requiring reliable, high-throughput file synchronization with built-in integrity tracking.

**Note:** FileSync scans only the configured source directories during synchronization. It does not perform a full scan or analysis of the destination directory’s contents, although it does verify the existence of destination folders and files as needed during the copy process. Updated files are overwritten in the destination along with their metadata. Files deleted from the source can be removed from the destination after a configurable number of sync runs, enabling automatic cleanup of stale files. This behavior is controlled by a configurable flag in the config file, providing flexibility between non-destructive and fully synchronized modes.

#
### Features

- **Metadata-Based Incremental Sync**  
  Uses a combination of file size and modification time, which are hashed and stored in a binary cache to detect changed files across sync runs. This avoids unnecessary file copies and speeds up subsequent synchronizations.

- **Multi Source and Multi Destination Support**  
  Supports syncing from multiple user defined source directories to a single destination per run. To sync to multiple destinations, separate runs of the tool are performed, with each destination managed independently, including its own metadata cache and sync state. This design allows clean separation between backup targets (e.g., external drives, network shares, secondary partitions) without cross contamination of metadata.

- **Per Source Serialized Copying**  
  Files within each source are copied one at a time, in the order they were discovered. This approach minimizes disk thrashing and significantly improves performance on HDDs. For SSDs or high performance environments(NAS), this behavior can be optionally disabled to enable parallel file copying per source.

- **Post Copy Metadata Update**  
  Metadata is updated only after a file has been physically and verifiably copied, ensuring that the cache reflects the true state of the destination. This prevents corrupt or incomplete sync states from being recorded and maintains cache integrity across runs.

- **Failure Detection and Recovery Support**  
  Copy failures such as permission issues, disk errors, or missing files are recorded during the sync. These failures are surfaced to the user via logs and excluded from metadata updates, allowing the tool to automatically retry them in subsequent runs without reprocessing successful files.

- **Multithreaded Scanning and Hashing**  
  Directories are scanned and files are hashed in parallel using a custom thread pool. This significantly improves performance on large directories while preserving system responsiveness through controlled concurrency.

- **Config File Driven Operation**  
  All behavior of FileSync is controlled via a simple, text based configuration file. This file defines source/destination paths, exclusions, sync mode, logging, stale file handling, and other advanced flags. Users can fully customize how the sync operates by modifying the config file.

- **Thread Safe Copy Manager**  
  A dedicated I/O thread handles copy execution by dequeuing per source copy queues, ensuring one at a time copy per source for consistent disk behavior.

- **Full Overlap Between Sync and Copy Phases**  
  Sync decisions (scanning and hashing) for one source can run while another source's files are being copied, maximizing throughput.

- **Efficient Queue Synchronization**  
  Thread safe mechanisms using mutexes and condition variables coordinate sync threads and the global copy manager.

- **Safe Path Handling and Validation**  
  Only absolute paths are supported and resolved; invalid or inaccessible paths are detected and reported before syncing begins.

- **Duplicate, Nested Folder, and Symlink Handling**  
  Detects duplicate source directories and nested folder relationships to avoid redundant operations. Symbolic links are ignored to prevent circular references and unintended copies.

- **INFO / ERROR Log Levels**  
  Logs are categorized by severity, helping users trace sync events or failure points clearly.

- **Live Console Feedback**  
  Real time sync and copy updates are shown in the terminal.

- **Failure Mode**  
  Ensures safe, resumable syncing in case of critical failures such as disk full, I/O errors, or destination disconnection. Copy operations are verified per source before being marked complete, and any failed files are automatically retried on the next run without affecting already synced files, allowing users to fix the issue and resume syncing cleanly using the existing cache.

#
### Why Use This Tool?

- Backing up personal files and documents to any storage medium accessible via the local file system such external hard drives, network-mounted drives or USB drives.
- Syncing development projects and code repositories across multiple workstations or laptops.
- Maintaining up to date media libraries (photos, videos, music) without redundant copying.
- Running scheduled incremental backups with minimal impact on system resources.

#
### What Can I Back Up?

FileSync supports backing up a wide range of files and directories, including:

- Regular files of any type, such as documents, images, videos, and executables.
- Hidden files and folders (e.g., dotfiles and dotfolders on Linux like `.bashrc`, and hidden files/folders on Windows like `desktop.ini` or hidden system folders).
- Multiple source directories, allowing complex backup configurations.
- Entire folders, including nested subdirectories, while avoiding redundant scanning of nested or duplicate sources.

**Note:**  
- Only absolute paths are supported for both sources and destinations.
- Symbolic links are ignored to prevent circular references and unintended copies.
- Empty directories are not copied.
- Tool assumes that no failure occurs during the first sync run. A successful initial sync ensures accurate metadata and smooth operation in future runs.
- Tool does not check for available disk space on the destination before copying. If the destination runs out of space during transfer, the sync will stop and mark pending files as incomplete. Simply rerun the program and it will automatically enter failure recovery mode to continue any pending or partially completed file transfers.
- If you have very large or deeply nested directory structures, it is recommended to split your sources into separate entries in the config file. This ensures quicker recovery from failures on a per source basis, enables better parallelism, improves isolation of errors, and minimizes the risk of a single failure affecting the entire source's sync process.
- The tool is designed to operate on any storage medium accessible via a standard file system path including local disks, external drives, mounted network shares (such as SMB/CIFS or NFS), iSCSI volumes, and FUSE mounted systems. Protocol based sources like FTP, SFTP, or HTTP are not natively supported unless mounted into the local file system using 3rd party tools.
- In case of a system crash, cache integrity is crucial. It is highly recommended to keep the backup cache flag (`EnableCacheRestoreFromBackup`) enabled so that in the event of corruption, you can restore the cache and avoid full rescans or recopies.
  
#
### Installation

Prebuilt statically linked binaries are provided as `.zip` files for Windows and `.tar.gz` files for Linux in the Releases section. (Architecture `x64`)

#### Building from Source

Requirements:
- CMake 3.13 or higher
- Any C++20 compiler supported by CMake

#### Build using CMake:

*Dynamically Linked - Default Build*

```cmd
cmake ..
cmake --build . --config Release
```

*Statically Linked - Portable Build*

```cmd
cmake -DUSE_STATIC_RUNTIME=ON ..
cmake --build . --config Release
```
*Note* for Linux: Even with static linking, the binary still depends on system library `libc`(exists on all Linux Distros that are C based). For musl-based or non-glibc systems: CMake doesn’t natively support musl; you need to manually configure the toolchain to build with musl.

*Creating Distributable Archives*

```cmd
cmake --install . --config Release
cpack -C Release
```
This generates `.zip` archive on Windows, `.tar.gz` archive on Linux. (build or dist directory under the CPack output)

#
### Configuration File Info

FileSync uses a simple text based configuration file to control its behavior.
- The order of entries in the config file does not matter. Sources, destination, excludes and flags can appear in any sequence.
- Spaces in Paths are supported, no need for any quotes or escape characters (Both Win and Linux).
- Flags and their Values are Case Sensitive.
- Comments are not Supported.
- Spaces and Empty Lines are ignored.

###  Configuration Flags - Definition and Usage

- **Mode**  
  - Controls how aggressively system resources are used
    - BG - Low Load, run as background process so you can continue your work while it syncs
    - Inter - Medium Load
    - GodSpeed - Max Performance, run as the primary process maximizing hardware utilization
  - Default Value is BG

- **DiskType**  
  - Choose based on your actual hardware for optimal I/O strategy
    - HDD - Disk Thrashing Disabled, performs sequential writes, reducing seek times
    - SSD - Disk Thrashing Enabled, performs random writes, maximizing speed
  - Default Value is HDD

- **SSDMode**  
  - Only applies when DiskType = `SSD`
  - Choose SSD-specific copy strategy:
    - Sequential - Sequential Writes, Suitable for large(size) files
    - Parallel - Parallel Writes, Suitable for small(size) files
    - Balanced - Balanced and Optimized Mode, Suitable for mix sized loads
    - GodSpeed - Maximum Performance, fully parallel, aggressive, and high-load mode
  - Default Value is Balanced
  - Refer to Copy Mechanism and SSD Mode Flags below for more info.

- **GodSpeedParallelSourcesCount**  
  - How many sources copy simultaneously in `GodSpeed` SSDMode
  - Default Value is 8
 
- **GodSpeedParallelFilesPerSourcesCount**  
  - How many sources copy simultaneously in `GodSpeed` SSDMode
  - Default Value is 8

- **ParallelFilesPerSourceCount**  
  - How many sources copy simultaneously in `Parallel` SSDMode
  - Default Value is 8
 
- **StaleEntries**  
  - How many runs a file must be missing from source before it's marked stale
  - Default Value is 5

- **DeleteStaleFromDest**  
  - Whether to delete stale files from destination
  - This process does not delete empty directories that may remain after file deletion. This ensures directory structure integrity is preserved and user is expected to delete the directory
  - Default Value is NO

- **EnableBackupCopyAfterRun**  
  - Choose if a backup of the metadata cache is saved to destination after each successful run for added integrity protection
  - Saved as `.BackupCache` hidden directory.
  - Default Value is YES

- **EnableCacheRestoreFromBackup**  
  - Choose if the metadata cache is restored from backup if original gets corrupted
  - This runs only when program enters *Failure Mode*.
  - Default Value is YES

- **MaxLogFiles**  
  - Max number of logs before older ones are deleted
  - Default Value is 10


###  Configuration Flags - Acceptable Values

```
Source = (Absolute Source Path of file or directory) [Local, UNC, POSIX and Mapped Paths]
Destination = (Absolute Destination Path)
Exclude = (Absolute Path of file or directory to be excluded)
Mode = (BG/Inter/GodSpeed)
DiskType = (SSD/HDD)
SSDMode = (GodSpeed/Parallel/Sequential/Balanced)
GodSpeedParallelSourcesCount = (integer value)
GodSpeedParallelFilesPerSourcesCount = (integer value)
ParallelFilesPerSourceCount = (integer value)
StaleEntries = (integer value)
DeleteStaleFromDest = (YES/NO)
EnableBackupCopyAfterRun = (YES/NO)
EnableCacheRestoreFromBackup = (YES/NO)
MaxLogFiles = (integer value)
```

#### Sample Configuration Files
Windows
```
Source = C:\Users\YourName\Documents
Source = C:\Users\YourName\Desktop\To-Do.txt
Source = C:\My Data\Some Random Name
Source = D:\Projects

Destination = D:\Backup

Exclude = C:\Users\YourName\Documents\help.txt
Exclude = D:\Projects\Python

Mode = BG
MaxLogFiles = 20
DiskType = SSD
StaleEntries = 3
SSDMode = GodSpeed
GodSpeedSourceThreadCount = 16
GodSpeedWorkerThreadCount = 32
DeleteStaleFromDest = YES
EnableCacheRestoreFromBackup = YES
EnableBackupCopyAfterRun = YES

```
Linux
```
Source = /home/username/Documents
Source = /var/media
Source = /usr/include/zlib.h
Source = /mnt/c/users/YourName/Documents

Destination = /mnt/backup

Exclude = /mnt/c/users/YourName/Documents/help.txt

Mode = BG
MaxLogFiles = 100
DiskType = SSD
SSDMode = Parallel
SourceThreadCount = 2
StaleEntries = 10
DeleteStaleFromDest = NO
EnableCacheRestoreFromBackup = NO
EnableBackupCopyAfterRun = NO
```

**Source and Destination are Mandatory, Rest all are Optional**

#
### Destination Folder Structure
The destination folder mirrors the full absolute path of each source directory inside it, rather than dumping source contents directly into the destination root.

For example:
```
Source = C:/Users/YourName/Desktop

Source = C:/Users/YourName/Documents

Destination = D:/Backup
```
The resulting structure will be:
```
D:/Backup/C/Users/YourName/Desktop
D:/Backup/C/Users/YourName/Documents
```
This approach helps avoid conflicts and overwriting when different sources contain files or folders with the same name. It also preserves the original folder hierarchy, making it easier to locate backed-up files and maintain clear separation between sources.

#
### Copy Mechanism and SSD Mode Flags
FileSync’s copy modes under SSDMode only work when DiskType is set to SSD. If DiskType is set to HDD, these modes are ignored.

*Note:* You can technically use any SSDMode with any disk type (HDD/SSD). But the behavior and performance were optimized with SSDs in mind. If you're unsure — stick to the defaults.

- **Sequential**  
  - Source-Level: Only one source is copied at a time.
  - File-Level: Files are copied one-by-one, sequentially.
  - Use this if your sources mostly contain very large files. It maximizes per-file bandwidth and reduces I/O contention, which speeds up transfers and avoids unnecessary overhead.
  - Example: Backing up videos, ISO files, archives etc.

- **Parallel**  
  - Source-Level: Only one source is copied at a time.
  - File-Level: Files within the source copied in parallel.
  - Use this if your sources mostly contain a large number of small files. Copying them in parallel improves I/O throughput by keeping the pipeline full.
  - Example: Backing up a photo collection, logs, documents, source code folders, etc.
 
- **Balanced**  
  - Source-Level: One source at a time, but starts processing the next source’s small/large files if the current one still has large/small files being copied.
  - File-Level: Files within the source copied in parallel.
  - Use this if your sources mostly contain a large number of small files. Copying them in parallel improves I/O throughput by keeping the pipeline full.
  - This is the recommended and default mode. It’s designed for mixed workloads — where some sources contain large files, others small. It maximizes disk usage and ensures high throughput.
  - Example: General-purpose backups with a mix of documents, videos, installers, etc.
 
- **GodSpeed**  
  - Source-Level: All sources are processed in parallel
  - File-Level: Files within each source are also copied in parallel
  - Unlike Parallel Mode, which processes one source at a time, Godspeed handles multiple sources and their internal files simultaneously. It pushes the system to full throughput limits.
  - This is the most aggressive mode — multiple sources and multiple files from each source are copied all at once. It’s very fast, but can consume a lot of system resources.
  - Performance depends based on the hardware and the nature of files, so you may need to optimize this to get the best results(using the Flags for `GodSpeedParallelSourcesCount` & `GodSpeedParallelFilesPerSourceCount`).
  - Use if speed is prioritized over system load and you are willing to optimize it, otherwise use balanced mode. Difference might be significant only if optimized, this mode may actually perform worse than Balanced due to resource contention.
    

#
### Usage
Simply run the FileSync executable. It automatically loads the configuration file named `Config.txt` located in the same directory as the binary.
```
FileSync.exe
```
```
./FileSync
```

#
### Customization Locations in Code
Below are the places where you can edit the following things:

Values that can be configured via Flags but if you wish to change them to defaults or edit them overall:
- **Sync Mode and Thread Count**  
  Default is `BG` and `2`. ConfigGlobal.cpp `Line 37` and `Line 38`
  
- **Disk Type Optimization**  
  Default is `HDD`.  ConfigGlobal.cpp `Line 39`

- **SSDMode**  
  Default is `Balanced`.  ConfigGlobal.cpp `Line 40`

- **GodSpeed Parallel Sources Count**  
  Default is `8`.  ConfigGlobal.cpp `Line 41`
  
- **GodSpeed Parallel Files Per Source Count**  
  Default is `8`.  ConfigGlobal.cpp `Line 42`

- **Parallel Files Per Source Count**  
  Default is `8`.  ConfigGlobal.cpp `Line 43`
    
- **Stale File Removal Threshold**  
  Default is `5`.  ConfigGlobal.cpp `Line 44`
  
- **Stale File Deletion from Destination**  
  Default is `NO`.  ConfigGlobal.cpp `Line 45`

- **EnableBackupCopyAfterRun**  
  Default is `YES`.  ConfigGlobal.cpp `Line 46`

- **EnableCacheRestoreFromBackup**  
  Default is `YES`.  ConfigGlobal.cpp `Line 47`

- **Max Log Files**  
  Default is `10`.  ConfigGlobal.cpp `Line 48`

  
Hardcoded Values(Change only if you know what you are doing):

- **Configuration File Location**  
  Modify the path and filename used for storing log files. Default is same directory as the binary and `Config.txt`.

  ConfigGlobal.cpp `Line 33`

- **Sync Log File Location**  
  Modify the path and filename used for storing log files. Default is same directory as the binary and `Sync_Logs`.

  ConfigGlobal.cpp `Line 34`

- **Metadata Cache File Location**  
  Modify the path and filename used for storing metadata cache files. Default is same directory as the binary and `Meta_Cache`.

  ConfigGlobal.cpp `Line 35`

- **Backup Directory File Name**  
  Modify the path and directory name used for storing metadata cache backup files. Default is same directory as the destination(do not change unless you have a sensible place to store the backup) and `.BackupCache` hidden directory.

  ControlFlow.cpp `Line 151`

- **Sync Mode and Thread Count**  
  ConfigGlobal.cpp `Line 37`,`Line 38` - Modify the default value the tool runs in. Default is `BG` and `2`.
  
  ConfigParser.cpp `Line 307` - Modify the number of threads defined for BG, Inter and GodSpeed. Defaults are 2, 4 and Hardware Max Supported Thread Count.
  
  **Note:** Thread count defines the number of sources being scanned parallely, this takes minimal time and is thus expected not to be modifed with much. May cause unexpected behavior if exceeding Hardware Max value.

- **Thread Count for Hasher**  
  Adjust the default number of threads used by the Hasher. Typically, this value is determined by the selected Mode, but you can modify it here if you want to specify a different number. Default is `2`.

  FileHasher.hpp `Line 18`, `Line 20`. Replace `ConfigGlobal::ThreadCount` with desired value(Change the Log `Line 14` a well if you update).

- **File Size Threshold for Small and Large File Queue**  
  Defines the size boundary used to classify files as small or large, determining how they are queued and processed during synchronization. Default is `2 GB`.

  SyncEngine.cpp `Line 19`
  
- **Flags for robocopy/dd commands**  
  Modify the default flags used by the robocopy/dd commands. Defaults are `/R:2 /W:5 /NFL /NDL /NJH` and `bs=4M status=progress conv=fsync` respectively.
  
  FileCopier.cpp `Line 87`,`Line 143` respectively

- **File Size Threshold for Small and Large File Copy Commands**  
  Defines the size boundary used to classify files as small or large, determining which command is used to copy them. Default is `2 GB`.
  
  **Note:** This setting directly influences how files are processed and which copy strategy is applied. Changing this may significantly affect sync performance. Proceed with caution.
  FileCopier.cpp `Line 63`

#
### License

You are free to use and modify this software for personal or internal purposes.  
However, redistribution or public distribution of this software or any modified versions is **not permitted** without explicit permission.
