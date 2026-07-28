//! Physical-page reclaim for freed shared-memory blocks.
//!
//! The TLSF pool lives in a `MAP_SHARED` tmpfs mapping (`/dev/shm/agnocast@<pid>`), so a
//! freed block's physical pages stay resident until they are explicitly punched out with
//! `madvise(MADV_REMOVE)`. This module computes which pages of a just-freed block are safe
//! to punch and issues the syscall.
//!
//! Safety hinges on never zeroing rlsf's own metadata, which lives in the first bytes of a
//! block (the `FreeBlockHdr`) and at the boundary with the next block. We therefore punch
//! only the page-aligned interior, skipping the first and last page of the block.

use std::os::raw::c_void;

/// x86_64 base page size. The pool is base-page backed (confirmed via `/proc/<pid>/smaps`);
/// Agnocast is x86_64-only for now (see `MIN_ALIGN` in `lib.rs`).
const PAGE_SIZE: usize = 4096;

/// The page-aligned `[start, end)` interior of a block that is safe to punch, or `None`
/// when the block is too small to contain a full interior page after the metadata guards.
///
/// `payload_start` is the block's usable start (the pointer rlsf handed out) and
/// `block_end` its physical end. The first and last full page are skipped so rlsf's
/// block-boundary metadata is never touched.
fn reclaim_range(payload_start: usize, block_end: usize) -> Option<(usize, usize)> {
    let first_full = payload_start.wrapping_add(PAGE_SIZE - 1) & !(PAGE_SIZE - 1);
    let last_full = block_end & !(PAGE_SIZE - 1);
    let start = first_full.checked_add(PAGE_SIZE)?; // skip the block-start metadata page
    let end = last_full.checked_sub(PAGE_SIZE)?; // skip the block-end metadata page
    (end > start).then_some((start, end))
}

/// Returns the freed physical pages of a just-deallocated block to the kernel.
///
/// Best-effort: `madvise` failures (e.g. a non-shmem mapping, where `MADV_REMOVE` returns
/// `EINVAL`) are ignored, since reclaim is an optimization and this runs inside the
/// allocator's `deallocate` and must not unwind.
///
/// # Safety
///
/// `[payload_start, block_end)` must be a block just deallocated from the pool that the
/// caller still holds the allocator lock over, so the pages cannot be handed out again
/// before this returns. The pool must be a `MAP_SHARED` mapping.
pub(crate) fn reclaim_pages(payload_start: usize, block_end: usize) {
    if let Some((start, end)) = reclaim_range(payload_start, block_end) {
        unsafe {
            libc::madvise(start as *mut c_void, end - start, libc::MADV_REMOVE);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{reclaim_range, PAGE_SIZE};
    use crate::tlsf::TLSFAllocator;
    use crate::SharedMemoryAllocator;
    use std::alloc::Layout;
    use std::mem::MaybeUninit;
    use std::os::raw::c_void;

    /// Number of pages of `[base, base + len)` currently resident in physical memory.
    fn resident_pages(base: *mut u8, len: usize) -> usize {
        let mut resident = vec![0u8; len / PAGE_SIZE];
        let rc = unsafe { libc::mincore(base as *mut c_void, len, resident.as_mut_ptr()) };
        assert_eq!(rc, 0, "mincore failed: {}", std::io::Error::last_os_error());
        resident.iter().filter(|b| *b & 1 == 1).count()
    }

    /// Map a `MAP_SHARED` tmpfs region. `MADV_REMOVE` only punches holes on shmem, so the
    /// anonymous private mapping used by the other tests cannot exercise reclaim.
    fn map_shared_pool(size: usize) -> *mut u8 {
        let name = b"agnocast_reclaim_test\0";
        let fd = unsafe { libc::memfd_create(name.as_ptr() as *const libc::c_char, 0) };
        assert!(fd >= 0, "memfd_create failed: {}", std::io::Error::last_os_error());
        assert_eq!(unsafe { libc::ftruncate(fd, size as libc::off_t) }, 0, "ftruncate failed");
        let p = unsafe {
            libc::mmap(
                std::ptr::null_mut(),
                size,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                fd,
                0,
            )
        };
        unsafe { libc::close(fd) }; // the mapping keeps the memfd alive
        assert_ne!(p, libc::MAP_FAILED, "mmap failed");
        p as *mut u8
    }

    #[test]
    fn returns_none_when_block_is_smaller_than_the_guards() {
        // Two pages: after skipping the first and last full page nothing remains.
        assert_eq!(reclaim_range(0x40000000, 0x40000000 + 2 * PAGE_SIZE), None);
    }

    #[test]
    fn page_aligned_block_skips_exactly_the_first_and_last_page() {
        let start = 0x40000000;
        let end = start + 10 * PAGE_SIZE;
        let (rs, re) = reclaim_range(start, end).unwrap();
        assert_eq!(rs, start + PAGE_SIZE, "first full page must be skipped");
        assert_eq!(re, end - PAGE_SIZE, "last full page must be skipped");
    }

    #[test]
    fn range_is_page_aligned_and_strictly_inside_the_block() {
        let start = 0x40000000 + 100; // deliberately not page-aligned
        let end = start + 10 * PAGE_SIZE;
        let (rs, re) = reclaim_range(start, end).unwrap();
        assert_eq!(rs % PAGE_SIZE, 0);
        assert_eq!(re % PAGE_SIZE, 0);
        assert!(rs > start && re < end);
        assert!(rs >= start + PAGE_SIZE, "at least one page of head guard");
    }

    /// Locks in the reclaim behaviour end to end: freeing a multi-page block must hand its
    /// physical pages back to the kernel, leave a live neighbouring allocation untouched,
    /// and leave the reclaimed region usable again.
    #[test]
    fn freeing_a_large_block_returns_physical_pages_and_preserves_data() {
        const POOL: usize = 64 * 1024 * 1024;
        const BIG: usize = 16 * 1024 * 1024;
        const NEIGHBOR: usize = 64 * 1024;

        let base = map_shared_pool(POOL);
        // SAFETY: freshly mapped and exclusively owned by this test; leaked as 'static.
        let pool: &'static mut [MaybeUninit<u8>] =
            unsafe { std::slice::from_raw_parts_mut(base as *mut MaybeUninit<u8>, POOL) };
        let alloc = TLSFAllocator::from_pool(pool);

        // A live allocation next to the big one; reclaim must not touch it.
        let neighbor = alloc
            .allocate(Layout::from_size_align(NEIGHBOR, 16).unwrap())
            .unwrap();
        unsafe { std::ptr::write_bytes(neighbor.as_ptr(), 0xA5, NEIGHBOR) };

        let big_layout = Layout::from_size_align(BIG, 16).unwrap();
        let big = alloc.allocate(big_layout).unwrap();
        unsafe { std::ptr::write_bytes(big.as_ptr(), 0x5A, BIG) };

        let before = resident_pages(base, POOL);
        assert!(
            before >= BIG / PAGE_SIZE,
            "the written block should be resident, got {before} pages"
        );

        alloc.deallocate(big);

        let after = resident_pages(base, POOL);
        assert!(
            after * 4 < before,
            "freed pages were not returned to the kernel: before={before} after={after}"
        );

        let kept = unsafe { std::slice::from_raw_parts(neighbor.as_ptr(), NEIGHBOR) };
        assert!(
            kept.iter().all(|&b| b == 0xA5),
            "reclaim corrupted a live neighbouring allocation"
        );

        let reused = alloc.allocate(big_layout).unwrap();
        unsafe { std::ptr::write_bytes(reused.as_ptr(), 0x3C, BIG) };
        let reread = unsafe { std::slice::from_raw_parts(reused.as_ptr(), BIG) };
        assert!(
            reread.iter().all(|&b| b == 0x3C),
            "the reclaimed region is unusable after reallocation"
        );

        alloc.deallocate(reused);
        alloc.deallocate(neighbor);
        drop(alloc);
        unsafe { libc::munmap(base as *mut c_void, POOL) };
    }
}
