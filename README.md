<div align="center">

<h1> <p>Metadata Based Incremental File Backup</p> </h1>
</div>

FileSync is a fast and highly configurable file synchronization utility for local and external backups, available on both Windows and Linux. It supports multiple source directories and uses a metadata-based incremental sync approach to avoid redundant copying. File changes are detected using a combination of file size and modification time, which are hashed and stored in a binary cache to ensure accurate and efficient updates across runs.

Built in modern C++, the tool leverages multithreaded scanning and hashing to accelerate sync decisions across large directories. A dedicated copy manager serializes I/O operations per source to prevent disk contention and ensure stable performance. FileSyncTool includes robust failure tracking: unsuccessful copy operations are detected and logged, and metadata is updated post-copy to reflect actual copy status, allowing precise reporting and recovery on future syncs. This makes it particularly well-suited for repeated backups, local mirroring, and any scenario requiring reliable, high-throughput file synchronization with built-in integrity tracking.

#
### Features

- **Metadata-Based Incremental Sync**  
  Uses a combination of file size and modification time, which are hashed and stored in a binary cache to detect changed files across sync runs. This avoids unnecessary file copies and speeds up subsequent synchronizations.

- **Multi-Source and Multi-Destination Support**  
  Supports syncing from multiple user-defined source directories to a single destination per run. To sync to multiple destinations, separate runs of the tool are performed, with each destination managed independently, including its own metadata cache and sync state. This design allows clean separation between backup targets (e.g., external drives, network shares, secondary partitions) without cross-contamination of metadata.

- **Per-Source Serialized Copying**  
  Files within each source are copied one at a time, in the order they were discovered. This approach minimizes disk thrashing and significantly improves performance on HDDs. For SSDs or high-performance environments(NAS), this behavior can be optionally disabled to enable parallel file copying per source.

- **Post-Copy Metadata Update**  
  Metadata is updated only after a file has been physically and verifiably copied, ensuring that the cache reflects the true state of the destination. This prevents corrupt or incomplete sync states from being recorded and maintains cache integrity across runs.

- **Failure Detection and Recovery Support**  
  Copy failures such as permission issues, disk errors, or missing files are recorded during the sync. These failures are surfaced to the user via logs and excluded from metadata updates, allowing the tool to automatically retry them in subsequent runs without reprocessing successful files.

- **Multithreaded Scanning and Hashing**  
  Directories are scanned and files are hashed in parallel using a custom thread pool. This significantly improves performance on large directories while preserving system responsiveness through controlled concurrency.

- **Configurable Sync Modes**  
  Supports multiple operational modes: **BG (Background)**, **Inter (Intermediate)**, and **GodSpeed** that let users control the tool’s performance profile. Modes adjust threading intensity and resource usage to fit different priorities, from low-impact background syncing to maximum-speed batch operations.

- **Thread-Safe Copy Manager**  
  A dedicated I/O thread handles copy execution by dequeuing per-source copy queues, ensuring one-at-a-time copy per source for consistent disk behavior.

- **Full Overlap Between Sync and Copy Phases**  
  Sync decisions (scanning and hashing) for one source can run while another source's files are being copied — maximizing throughput.

- **Efficient Queue Synchronization**  
  Thread-safe mechanisms using mutexes and condition variables coordinate sync threads and the global copy manager.

- **Config File Driven Operation**  
  Uses a configuration file to define key parameters: source directories and destination paths (absolute only), explicit file and folder exclusions by name (no pattern matching), sync mode (`BG`, `Inter`, `GodSpeed`) and maximum number of log files to retain (`maxLogFiles`).

- **Safe Path Handling and Validation**  
  Only absolute paths are supported and resolved; invalid or inaccessible paths are detected and reported before syncing begins.

- **Duplicate, Nested Folder, and Symlink Handling**  
  Detects duplicate source directories and nested folder relationships to avoid redundant operations. Symbolic links are ignored to prevent circular references and unintended copies.

- **INFO / ERROR Log Levels**  
  Logs are categorized by severity, helping users trace sync events or failure points clearly.

- **Live Console Feedback**  
  Real-time sync and copy updates are shown in the terminal, with optional log file support.

#
### Why Use This Tool?

- Backing up personal files and documents to external hard drives or USB drives.
- Syncing development projects and code repositories across multiple workstations or laptops.
- Maintaining up-to-date media libraries (photos, videos, music) without redundant copying.
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

#
### Installation

Prebuilt binaries are provided as `.zip` files for Windows and `.tar.gz` files for Linux in the Releases section.  

#### Building from Source

Build using CMake:

```cmd
cmake ..
cmake --build . --config Release
```

#
### Configuration File Info


FileSyncTool uses a simple text-based configuration file to control its behavior. The config file supports defining:

- Source directories (absolute paths only)
- Destination directory (absolute path)
- File and folder exclusions by name (absolute path only)
- Sync mode (`BG`, `Inter`, `GodSpeed`)
- Maximum number of log files to retain (`MaxLogFiles`)

**Note:** The order of entries in the config file does not matter. Sources, excludes, and options can appear in any sequence.

#### Sample Configuration Files
Windows
```
Source = C:\Users\YourName\Documents
Source = D:\Projects

Destination = D:\Backup

Exclude = C:\Users\YourName\Documents\help.txt
Exclude = D:\Projects\Python

Mode = Inter
MaxLogFiles = 5
```
Linux
```
Source = /home/username/Documents
Source = /var/media
Source = /mnt/c/users/YourName/Documents

Destination = /mnt/backup

Mode = BG
MaxLogFiles = 10
```

#
### Usage
Simply run the FileSyncTool executable. It automatically loads the configuration file named `Config.txt` located in the same directory as the binary.
```
FileSync.exe
```
```
./FileSync
```

#
### Customization Locations in Code
Below are the places where you can edit the following things:

- **Thread Count**  
  Configure the number of threads defined for BG, Inter and GodSpeed. Defaults are 2, 4 and Hardware Max Supported Thread Count.

- **Metadata Cache File Location**  
  Modify the path and filename used for storing metadata cache files.

- **Sync Log File Location**  
  Modify the path and filename used for storing log files.

- **Configuration File Location**  
  Modify the path and filename used for storing log files.

- Add more here
