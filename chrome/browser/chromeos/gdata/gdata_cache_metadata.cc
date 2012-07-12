// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/gdata/gdata_cache_metadata.h"

#include "base/file_util.h"
#include "chrome/browser/chromeos/gdata/gdata.pb.h"
#include "chrome/browser/chromeos/gdata/gdata_util.h"

namespace gdata {

namespace {

// A map table of resource ID to file path.
typedef std::map<std::string, FilePath> ResourceIdToFilePathMap;

// Returns true if |file_path| is a valid symbolic link as |sub_dir_type|.
// Otherwise, returns false with the reason.
bool IsValidSymbolicLink(const FilePath& file_path,
                         GDataCache::CacheSubDirectoryType sub_dir_type,
                         const std::vector<FilePath>& cache_paths,
                         std::string* reason) {
  DCHECK(sub_dir_type == GDataCache::CACHE_TYPE_PINNED ||
         sub_dir_type == GDataCache::CACHE_TYPE_OUTGOING);

  FilePath destination;
  if (!file_util::ReadSymbolicLink(file_path, &destination)) {
    *reason = "failed to read the symlink (maybe not a symlink)";
    return false;
  }

  if (!file_util::PathExists(destination)) {
    *reason = "pointing to a non-existent file";
    return false;
  }

  // pinned-but-not-fetched files are symlinks to kSymLinkToDevNull.
  if (sub_dir_type == GDataCache::CACHE_TYPE_PINNED &&
      destination == FilePath::FromUTF8Unsafe(util::kSymLinkToDevNull)) {
    return true;
  }

  // The destination file should be in the persistent directory.
  if (!cache_paths[GDataCache::CACHE_TYPE_PERSISTENT].IsParent(destination)) {
    *reason = "pointing to a file outside of persistent directory";
    return false;
  }

  return true;
}

// Remove invalid files from persistent directory.
//
// 1) dirty-but-not-committed files. The dirty files should be committed
// (i.e. symlinks created in 'outgoing' directory) before shutdown, but the
// symlinks may not be created if the system shuts down unexpectedly.
//
// 2) neither dirty nor pinned. Files in the persistent directory should be
// in the either of the states.
void RemoveInvalidFilesFromPersistentDirectory(
    const ResourceIdToFilePathMap& persistent_file_map,
    const ResourceIdToFilePathMap& outgoing_file_map,
    GDataCacheMetadataMap::CacheMap* cache_map) {
  for (ResourceIdToFilePathMap::const_iterator iter =
           persistent_file_map.begin();
       iter != persistent_file_map.end(); ++iter) {
    const std::string& resource_id = iter->first;
    const FilePath& file_path = iter->second;

    GDataCacheMetadataMap::CacheMap::iterator cache_map_iter =
        cache_map->find(resource_id);
    if (cache_map_iter != cache_map->end()) {
      const GDataCacheEntry& cache_entry = cache_map_iter->second;
      // If the file is dirty but not committed, remove it.
      if (cache_entry.is_dirty() &&
          outgoing_file_map.count(resource_id) == 0) {
        LOG(WARNING) << "Removing dirty-but-not-committed file: "
                     << file_path.value();
        file_util::Delete(file_path, false);
        cache_map->erase(cache_map_iter);
      } else if (!cache_entry.is_dirty() &&
                 !cache_entry.is_pinned()) {
        // If the file is neither dirty nor pinned, remove it.
        LOG(WARNING) << "Removing persistent-but-dangling file: "
                     << file_path.value();
        file_util::Delete(file_path, false);
        cache_map->erase(cache_map_iter);
      }
    }
  }
}

// Scans cache subdirectory and build or update |cache_map|
// with found file blobs or symlinks.
//
// The resource IDs and file paths of discovered files are collected as a
// ResourceIdToFilePathMap, if these are processed properly.
void ScanCacheDirectory(
    const std::vector<FilePath>& cache_paths,
    GDataCache::CacheSubDirectoryType sub_dir_type,
    GDataCacheMetadata::CacheMap* cache_map,
    ResourceIdToFilePathMap* processed_file_map) {
  DCHECK(cache_map);
  DCHECK(processed_file_map);

  file_util::FileEnumerator enumerator(
      cache_paths[sub_dir_type],
      false,  // not recursive
      static_cast<file_util::FileEnumerator::FileType>(
          file_util::FileEnumerator::FILES |
          file_util::FileEnumerator::SHOW_SYM_LINKS),
      util::kWildCard);
  for (FilePath current = enumerator.Next(); !current.empty();
       current = enumerator.Next()) {
    // Extract resource_id and md5 from filename.
    std::string resource_id;
    std::string md5;
    std::string extra_extension;
    util::ParseCacheFilePath(current, &resource_id, &md5, &extra_extension);

    // Determine cache state.
    GDataCacheEntry cache_entry;
    cache_entry.set_md5(md5);
    // If we're scanning pinned directory and if entry already exists, just
    // update its pinned state.
    if (sub_dir_type == GDataCache::CACHE_TYPE_PINNED) {
      std::string reason;
      if (!IsValidSymbolicLink(current, sub_dir_type, cache_paths, &reason)) {
        LOG(WARNING) << "Removing an invalid symlink: " << current.value()
                     << ": " << reason;
        file_util::Delete(current, false);
        continue;
      }

      GDataCacheMetadata::CacheMap::iterator iter =
          cache_map->find(resource_id);
      if (iter != cache_map->end()) {  // Entry exists, update pinned state.
        iter->second.set_is_pinned(true);

        processed_file_map->insert(std::make_pair(resource_id, current));
        continue;
      }
      // Entry doesn't exist, this is a special symlink that refers to
      // /dev/null; follow through to create an entry with the PINNED but not
      // PRESENT state.
      cache_entry.set_is_pinned(true);
    } else if (sub_dir_type == GDataCache::CACHE_TYPE_OUTGOING) {
      std::string reason;
      if (!IsValidSymbolicLink(current, sub_dir_type, cache_paths, &reason)) {
        LOG(WARNING) << "Removing an invalid symlink: " << current.value()
                     << ": " << reason;
        file_util::Delete(current, false);
        continue;
      }

      // If we're scanning outgoing directory, entry must exist and be dirty.
      // Otherwise, it's a logic error from previous execution, remove this
      // outgoing symlink and move on.
      GDataCacheMetadata::CacheMap::iterator iter =
          cache_map->find(resource_id);
      if (iter == cache_map->end() || !iter->second.is_dirty()) {
        LOG(WARNING) << "Removing an symlink to a non-dirty file: "
                     << current.value();
        file_util::Delete(current, false);
        continue;
      }

      processed_file_map->insert(std::make_pair(resource_id, current));
      continue;
    } else if (sub_dir_type == GDataCache::CACHE_TYPE_PERSISTENT ||
               sub_dir_type == GDataCache::CACHE_TYPE_TMP) {
      if (sub_dir_type == GDataCache::CACHE_TYPE_PERSISTENT)
        cache_entry.set_is_persistent(true);

      if (file_util::IsLink(current)) {
        LOG(WARNING) << "Removing a symlink in persistent/tmp directory"
                     << current.value();
        file_util::Delete(current, false);
        continue;
      }
      if (extra_extension == util::kMountedArchiveFileExtension) {
        // Mounted archives in cache should be unmounted upon logout/shutdown.
        // But if we encounter a mounted file at start, delete it and create an
        // entry with not PRESENT state.
        DCHECK(sub_dir_type == GDataCache::CACHE_TYPE_PERSISTENT);
        file_util::Delete(current, false);
      } else {
        // The cache file is present.
        cache_entry.set_is_present(true);

        // Adds the dirty bit if |md5| indicates that the file is dirty, and
        // the file is in the persistent directory.
        if (md5 == util::kLocallyModifiedFileExtension) {
          if (sub_dir_type == GDataCache::CACHE_TYPE_PERSISTENT) {
            cache_entry.set_is_dirty(true);
          } else {
            LOG(WARNING) << "Removing a dirty file in tmp directory: "
                         << current.value();
            file_util::Delete(current, false);
            continue;
          }
        }
      }
    } else {
      NOTREACHED() << "Unexpected sub directory type: " << sub_dir_type;
    }

    // Create and insert new entry into cache map.
    cache_map->insert(std::make_pair(resource_id, cache_entry));
    processed_file_map->insert(std::make_pair(resource_id, current));
  }
}

void ScanCachePaths(const std::vector<FilePath>& cache_paths,
                    GDataCacheMetadata::CacheMap* cache_map) {
  if (cache_paths.size() < GDataCache::NUM_CACHE_TYPES) {
    LOG(ERROR) << "Size of cache_paths is invalid.";
    return;
  }

  if (!GDataCache::CreateCacheDirectories(cache_paths))
    return;

  // Change permissions of cache persistent directory to u+rwx,og+x in order to
  // allow archive files in that directory to be mounted by cros-disks.
  if (!file_util::SetPosixFilePermissions(
        cache_paths[GDataCache::CACHE_TYPE_PERSISTENT],
        S_IRWXU | S_IXGRP | S_IXOTH))
    return;

  DVLOG(1) << "Scanning directories";

  // Scan cache persistent and tmp directories to enumerate all files and create
  // corresponding entries for cache map.
  ResourceIdToFilePathMap persistent_file_map;
  ScanCacheDirectory(cache_paths,
                     GDataCache::CACHE_TYPE_PERSISTENT,
                     cache_map,
                     &persistent_file_map);
  ResourceIdToFilePathMap tmp_file_map;
  ScanCacheDirectory(cache_paths,
                     GDataCache::CACHE_TYPE_TMP,
                     cache_map,
                     &tmp_file_map);

  // Then scan pinned directory to update existing entries in cache map, or
  // create new ones for pinned symlinks to /dev/null which target nothing.
  //
  // Pinned directory should be scanned after the persistent directory as
  // we'll add PINNED states to the existing files in the persistent
  // directory per the contents of the pinned directory.
  ResourceIdToFilePathMap pinned_file_map;
  ScanCacheDirectory(cache_paths,
                     GDataCache::CACHE_TYPE_PINNED,
                     cache_map,
                     &pinned_file_map);
  // Then scan outgoing directory to check if dirty-files are committed
  // properly (i.e. symlinks created in outgoing directory).
  ResourceIdToFilePathMap outgoing_file_map;
  ScanCacheDirectory(cache_paths,
                     GDataCache::CACHE_TYPE_OUTGOING,
                     cache_map,
                     &outgoing_file_map);

  RemoveInvalidFilesFromPersistentDirectory(persistent_file_map,
                                            outgoing_file_map,
                                            cache_map);
  DVLOG(1) << "Directory scan finished";
}

// Returns true if |md5| matches the one in |cache_entry| with some
// exceptions. See the function definition for details.
bool CheckIfMd5Matches(
    const std::string& md5,
    const GDataCacheEntry& cache_entry) {
  if (cache_entry.is_dirty()) {
    // If the entry is dirty, its MD5 may have been replaced by "local"
    // during cache initialization, so we don't compare MD5.
    return true;
  } else if (cache_entry.is_pinned() && cache_entry.md5().empty()) {
    // If the entry is pinned, it's ok for the entry to have an empty
    // MD5. This can happen if the pinned file is not fetched. MD5 for pinned
    // files are collected from files in "persistent" directory, but the
    // persistent files do not exisit if these are not fetched yet.
    return true;
  } else if (md5.empty()) {
    // If the MD5 matching is not requested, don't check MD5.
    return true;
  } else if (md5 == cache_entry.md5()) {
    // Otherwise, compare the MD5.
    return true;
  }
  return false;
}

}  // namespace

GDataCacheMetadata::GDataCacheMetadata(
    base::SequencedWorkerPool* pool,
    const base::SequencedWorkerPool::SequenceToken& sequence_token)
    : pool_(pool),
      sequence_token_(sequence_token) {
  AssertOnSequencedWorkerPool();
}

GDataCacheMetadata::~GDataCacheMetadata() {
  AssertOnSequencedWorkerPool();
}

void GDataCacheMetadata::AssertOnSequencedWorkerPool() {
  DCHECK(!pool_ || pool_->IsRunningSequenceOnCurrentThread(sequence_token_));
}

////////////////////////////////////////////////////////////////////////////////
// GDataCacheMetadataMap

GDataCacheMetadataMap::GDataCacheMetadataMap(
    base::SequencedWorkerPool* pool,
    const base::SequencedWorkerPool::SequenceToken& sequence_token)
    : GDataCacheMetadata(pool, sequence_token) {
  AssertOnSequencedWorkerPool();
}

GDataCacheMetadataMap::~GDataCacheMetadataMap() {
  AssertOnSequencedWorkerPool();
}

void GDataCacheMetadataMap::Initialize(
    const std::vector<FilePath>& cache_paths) {
  AssertOnSequencedWorkerPool();

  ScanCachePaths(cache_paths, &cache_map_);
}

void GDataCacheMetadataMap::AddOrUpdateCacheEntry(
    const std::string& resource_id,
    const GDataCacheEntry& cache_entry) {
  AssertOnSequencedWorkerPool();

  CacheMap::iterator iter = cache_map_.find(resource_id);
  if (iter == cache_map_.end()) {  // New resource, create new entry.
    cache_map_.insert(std::make_pair(resource_id, cache_entry));
  } else {  // Resource exists.
    cache_map_[resource_id] = cache_entry;
  }
}

void GDataCacheMetadataMap::RemoveCacheEntry(const std::string& resource_id) {
  AssertOnSequencedWorkerPool();

  CacheMap::iterator iter = cache_map_.find(resource_id);
  if (iter != cache_map_.end()) {
    // Delete the CacheEntry and remove it from the map.
    cache_map_.erase(iter);
  }
}

bool GDataCacheMetadataMap::GetCacheEntry(const std::string& resource_id,
                                          const std::string& md5,
                                          GDataCacheEntry* entry) {
  DCHECK(entry);
  AssertOnSequencedWorkerPool();

  CacheMap::iterator iter = cache_map_.find(resource_id);
  if (iter == cache_map_.end()) {
    DVLOG(1) << "Can't find " << resource_id << " in cache map";
    return false;
  }

  const GDataCacheEntry& cache_entry = iter->second;

  if (!CheckIfMd5Matches(md5, cache_entry)) {
    return false;
  }

  *entry = cache_entry;
  return true;
}

void GDataCacheMetadataMap::RemoveTemporaryFiles() {
  AssertOnSequencedWorkerPool();

  CacheMap::iterator iter = cache_map_.begin();
  while (iter != cache_map_.end()) {
    if (!iter->second.is_persistent()) {
      // Post-increment the iterator to avoid iterator invalidation.
      cache_map_.erase(iter++);
    } else {
      ++iter;
    }
  }
}

void GDataCacheMetadataMap::Iterate(const IterateCallback& callback) {
  AssertOnSequencedWorkerPool();

  for (CacheMap::const_iterator iter = cache_map_.begin();
       iter != cache_map_.end(); ++iter) {
    callback.Run(iter->first, iter->second);
  }
}

}  // namespace gdata
