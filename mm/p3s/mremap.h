/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_P3S_MREMAP_H
#define __MM_P3S_MREMAP_H

#include <linux/cacheflush.h>
#include <linux/highmem.h>
#include <linux/memcontrol.h>
#include <linux/mm.h>
#include <linux/p3s/mremap.h>
#include <linux/rmap.h>
#include <linux/sched/signal.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/uaccess.h>

#include "rmap.h"
#include "../swap.h"

/*
 * Helpers of mm/mremap.c, the only file that includes this header, so they can
 * stay static there and keep dynamic user page-table geometry.
 */
static int move_ptes(struct vm_area_struct *vma, pmd_t *old_pmd,
		     unsigned long old_addr, unsigned long old_end,
		     struct vm_area_struct *new_vma, pmd_t *new_pmd,
		     unsigned long new_addr, bool need_rmap_locks);

#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE
static pmd_t *get_old_pmd(struct mm_struct *mm, unsigned long addr);
static pte_t move_soft_dirty_pte(pte_t pte);

/* The 16KB destination folio that p3s_move_ptes() is filling */
struct p3s_mremap_copy {
	struct folio *dst_folio;
	unsigned long dst_window;
};

/**
 * struct p3s_mremap_cursor - State of one 4KB slice of a cross-slice mremap()
 * @old_vma: Source VMA
 * @new_vma: Destination VMA, or NULL for the check in p3s_mremap_can_copy()
 * @old_pmd: Source PMD entry
 * @new_pmd: Destination PMD entry
 * @src_addr: Source address of the slice
 * @dst_addr: Destination address of the slice
 * @copy: Destination folio packing state of p3s_move_ptes(), or NULL for
 *        the check
 * @locked_src: Source folio held locked, with a reference, across a retry
 * @ptep: Source PTE while it is mapped and locked, or NULL
 * @ptl: Source PTE table lock while @ptep is mapped
 * @old_pte: Source PTE read under @ptl
 * @prev_pte: Source PTE when the last swap read started
 * @ref_retries: Reference test retries of the check
 * @swap_retries: Swap reads of the copy
 * @swap_failed: Whether the last swap read failed
 */
struct p3s_mremap_cursor {
	struct vm_area_struct *old_vma;
	struct vm_area_struct *new_vma;
	pmd_t *old_pmd;
	pmd_t *new_pmd;
	unsigned long src_addr;
	unsigned long dst_addr;
	struct p3s_mremap_copy *copy;
	struct folio *locked_src;
	pte_t *ptep;
	spinlock_t *ptl;
	pte_t old_pte;
	pte_t prev_pte;
	unsigned int ref_retries;
	unsigned int swap_retries;
	bool swap_failed;
};

static inline bool p3s_mremap_folio_supported(struct folio *folio)
{
	return !folio_test_large(folio) && !folio_test_ksm(folio) &&
	       !folio_test_hwpoison(folio) && !folio_maybe_dma_pinned(folio);
}

static inline bool p3s_mremap_folio_exclusive(struct vm_area_struct *vma,
					      struct folio *folio,
					      unsigned long addr, pte_t *ptep)
{
	/*
	 * p3s_migration_share_slices() clears PageAnonExclusive when migrating
	 * packed slices; p3s_folio_slices_in_window() with refcount == mapcount
	 * under the PTL and folio lock proves exclusive ownership. Only the
	 * pre-check may test this: by the time of the copy, the unmap of a
	 * shrunk tail in mremap_to() may have split the VMA inside the window
	 * and migration may have cleared PageAnonExclusive.
	 */
	return PageAnonExclusive(&folio->page) ||
	       p3s_folio_slices_in_window(vma, folio, addr, ptep);
}

static inline void p3s_mremap_put_locked_folio(struct folio **locked_folio)
{
	struct folio *folio = *locked_folio;

	if (!folio)
		return;

	*locked_folio = NULL;
	folio_unlock(folio);
	folio_put(folio);
}

static inline void p3s_mremap_put_dst_folio(struct folio **dst_folio)
{
	struct folio *folio = *dst_folio;
	bool mapped;

	if (!folio)
		return;

	mapped = folio_test_anon(folio);
	*dst_folio = NULL;
	folio_unlock(folio);
	if (!mapped)
		folio_put(folio);
}

static inline void p3s_mremap_unmap_src(struct p3s_mremap_cursor *cur)
{
	if (!cur->ptep)
		return;

	pte_unmap_unlock(cur->ptep, cur->ptl);
	cur->ptep = NULL;
}

/*
 * Lock a present source folio while holding @cur->ptl. On contention or a stale
 * @cur->locked_src from an earlier pass, drops the source PTL and blocks on the
 * folio lock (or drops the stale folio) so the caller can retry with -EAGAIN.
 */
static inline bool p3s_mremap_lock_source_folio(struct p3s_mremap_cursor *cur,
						struct folio *src)
{
	if (cur->locked_src == src)
		return true;

	if (cur->locked_src) {
		p3s_mremap_unmap_src(cur);
		p3s_mremap_put_locked_folio(&cur->locked_src);
		return false;
	}
	if (folio_trylock(src))
		return true;

	folio_get(src);
	cur->locked_src = src;
	p3s_mremap_unmap_src(cur);
	folio_lock(src);

	return false;
}

static inline int p3s_mremap_wait_migration(struct p3s_mremap_cursor *cur)
{
	p3s_mremap_unmap_src(cur);
	p3s_mremap_put_locked_folio(&cur->locked_src);
	migration_entry_wait(cur->old_vma->vm_mm, cur->old_pmd, cur->src_addr);

	return -EAGAIN;
}

static inline struct folio *
p3s_mremap_read_swap_folio(struct vm_area_struct *vma, unsigned long addr,
			   swp_entry_t swp)
{
#ifdef CONFIG_SWAP
	struct folio *folio;

	folio = read_swap_cache_async(swp, GFP_HIGHUSER_MOVABLE | __GFP_CMA,
				      vma, addr, NULL);
	if (folio)
		folio_lock(folio);

	return folio;
#else
	return NULL;
#endif
}

static inline int p3s_mremap_ensure_dst_folio(struct p3s_mremap_cursor *cur)
{
	struct folio *dst;

	if (cur->copy->dst_folio)
		return 0;

	p3s_mremap_unmap_src(cur);
	p3s_mremap_put_locked_folio(&cur->locked_src);

	dst = vma_alloc_zeroed_movable_folio(cur->new_vma, cur->dst_addr);
	if (!dst)
		return -ENOMEM;
	if (mem_cgroup_charge(dst, cur->new_vma->vm_mm, GFP_KERNEL)) {
		folio_put(dst);
		return -ENOMEM;
	}
	folio_throttle_swaprate(dst, GFP_KERNEL);
	folio_lock(dst);
	__folio_mark_uptodate(dst);
	cur->copy->dst_folio = dst;
	cur->copy->dst_window = cur->dst_addr & PAGE_MASK_KERNEL;

	return -EAGAIN;
}

static inline int p3s_mremap_copy_slice_data(struct p3s_mremap_cursor *cur,
					     struct folio *src, pte_t *entryp)
{
	unsigned long src_off = vma_folio_offset(cur->old_vma, cur->src_addr);
	unsigned long dst_off = vma_folio_offset(cur->new_vma, cur->dst_addr);
	struct folio *dst = cur->copy->dst_folio;
	pte_t old_pte = cur->old_pte, entry;
	phys_addr_t dst_phys;

	if (pte_present(old_pte)) {
		/*
		 * Write-protect and flush the source PTE before copying so a
		 * sibling thread cannot modify @src between the copy and the
		 * final zap; racing write faults block on the VMA/mm write
		 * locks held by move_vma().
		 */
		if (pte_write(old_pte)) {
			ptep_set_wrprotect(cur->old_vma->vm_mm, cur->src_addr,
					   cur->ptep);
			flush_tlb_page(cur->old_vma, cur->src_addr);
			old_pte = pte_mkwrite(ptep_get(cur->ptep),
					      cur->new_vma);
		}
		entry = move_soft_dirty_pte(old_pte);
	} else {
		/*
		 * Leave the swap PTE non-present so sibling threads fault onto
		 * mmap_write_lock instead of writing to @src; the final zap
		 * frees the swap entry and updates MM_SWAPENTS.
		 */
		entry = mk_pte(&dst->page, cur->new_vma->vm_page_prot);
		entry = move_soft_dirty_pte(entry);
		if (pte_swp_uffd_wp(old_pte))
			entry = pte_mkuffd_wp(entry);
		else
			entry = maybe_mkwrite(pte_mkdirty(entry), cur->new_vma);
	}
	if (copy_mc_to_kernel(folio_address(dst) + dst_off,
			      folio_address(src) + src_off, PAGE_SIZE_4KB))
		return -EHWPOISON;

	flush_dcache_folio(dst);

	/* Order the copy before the PTE, as __folio_mark_uptodate() does. */
	smp_wmb();

	dst_phys = ((phys_addr_t)folio_pfn(dst) << PAGE_SHIFT_KERNEL) + dst_off;
	*entryp = pte_advance_phys(entry, dst_phys - __pte_to_phys(entry));

	return 0;
}

static inline int p3s_mremap_install_dst_pte(struct p3s_mremap_cursor *cur,
					     struct folio *dst_folio,
					     pte_t entry)
{
	struct vm_area_struct *new_vma = cur->new_vma;
	struct mm_struct *mm = new_vma->vm_mm;
	spinlock_t *ptl = NULL;
	pte_t *ptep;

	p3s_mremap_unmap_src(cur);
	p3s_mremap_put_locked_folio(&cur->locked_src);

	ptep = pte_offset_map_lock(mm, cur->new_pmd, cur->dst_addr, &ptl);
	if (WARN_ON_ONCE(!ptep))
		return -EINVAL;
	if (WARN_ON_ONCE(!pte_none(ptep_get(ptep)))) {
		pte_unmap_unlock(ptep, ptl);
		return -EINVAL;
	}
	flush_tlb_batched_pending(mm);
	if (dst_folio) {
		if (!folio_test_anon(dst_folio)) {
			folio_add_new_anon_rmap(dst_folio, new_vma,
						cur->dst_addr, RMAP_EXCLUSIVE);
			folio_add_lru_vma(dst_folio, new_vma);
		} else {
			folio_get(dst_folio);
			folio_add_anon_rmap_pte(dst_folio, &dst_folio->page,
						new_vma, cur->dst_addr,
						RMAP_EXCLUSIVE);
		}
		inc_mm_counter(mm, MM_ANONPAGES);
	}
	set_pte_at(mm, cur->dst_addr, ptep, entry);
	update_mmu_cache(new_vma, cur->dst_addr, ptep);
	pte_unmap_unlock(ptep, ptl);

	return 0;
}

static inline int
p3s_mremap_validate_exclusive_folio(struct p3s_mremap_cursor *cur,
				    struct folio *src)
{
	unsigned int extra_refs = cur->locked_src == src;

	if (!folio_test_anon(src) || !p3s_mremap_folio_supported(src) ||
	    !p3s_mremap_folio_exclusive(cur->old_vma, src, cur->src_addr,
					cur->ptep))
		return -EINVAL;

	if (folio_test_swapcache(src)) {
		folio_free_swap(src);
		if (folio_test_swapcache(src))
			return -EINVAL;
	}
	if (folio_ref_count(src) != folio_mapcount(src) + extra_refs)
		return -EBUSY;

	return 0;
}

static inline int p3s_mremap_check_present_folio(struct p3s_mremap_cursor *cur,
						 struct folio *src)
{
	bool can_retry;
	int err;

	if (!p3s_mremap_lock_source_folio(cur, src))
		return -EAGAIN;

	err = p3s_mremap_validate_exclusive_folio(cur, src);
	can_retry = err == -EBUSY &&
		    (!cur->ref_retries || !folio_test_lru(src)) &&
		    cur->ref_retries < 8 && !fatal_signal_pending(current);
	if (cur->locked_src != src)
		folio_unlock(src);
	if (!can_retry)
		return err ? -EINVAL : 0;

	p3s_mremap_unmap_src(cur);
	p3s_mremap_put_locked_folio(&cur->locked_src);
	if (cur->ref_retries++)
		schedule_timeout_killable(1);
	lru_add_drain_all();

	return -EAGAIN;
}

static inline int p3s_mremap_copy_present_folio(struct p3s_mremap_cursor *cur,
						struct folio *src)
{
	pte_t entry;
	int err;

	err = p3s_mremap_ensure_dst_folio(cur);
	if (err)
		return err;

	if (!p3s_mremap_lock_source_folio(cur, src))
		return -EAGAIN;

	/*
	 * p3s_mremap_can_copy() ran the exclusive, swap cache and reference
	 * tests under this mmap write lock, and nothing since then can share
	 * or pin the folio: fork and GUP need the mmap lock, and 4KB processes
	 * have neither GUP-fast nor KSM. Reclaim and migration take no lasting
	 * reference, and a swap entry that reclaim allocates is only used in
	 * this window.
	 */
	err = -EINVAL;
	if (folio_test_anon(src) && p3s_mremap_folio_supported(src)) {
		if (folio_test_swapcache(src))
			folio_free_swap(src);
		err = p3s_mremap_copy_slice_data(cur, src, &entry);
	}
	if (cur->locked_src != src)
		folio_unlock(src);
	if (err)
		return err;

	return p3s_mremap_install_dst_pte(cur, cur->copy->dst_folio, entry);
}

static inline bool p3s_mremap_folio_matches_swap(const struct folio *folio,
						 swp_entry_t entry)
{
	if (!folio_test_swapcache(folio))
		return false;

	return folio->swap.val == round_down(entry.val, folio_nr_pages(folio));
}

static inline int p3s_mremap_copy_swapped_folio(struct p3s_mremap_cursor *cur,
						swp_entry_t swp)
{
	struct folio *src;
	pte_t entry;
	int err;

	err = p3s_mremap_ensure_dst_folio(cur);
	if (err)
		return err;

	src = cur->locked_src;
	if (!src || !p3s_mremap_folio_matches_swap(src, swp)) {
		if (fatal_signal_pending(current))
			return -EINTR;
		if (++cur->swap_retries > 8)
			return -ENOMEM;

		cur->prev_pte = cur->old_pte;
		p3s_mremap_unmap_src(cur);
		p3s_mremap_put_locked_folio(&cur->locked_src);
		cur->locked_src = p3s_mremap_read_swap_folio(cur->old_vma,
							     cur->src_addr,
							     swp);
		cur->swap_failed = !cur->locked_src;
		return -EAGAIN;
	}

	if (folio_test_hwpoison(src))
		return -EHWPOISON;
	if (!folio_test_uptodate(src))
		return -EFAULT;
	if (!p3s_mremap_folio_supported(src))
		return -EINVAL;

	err = p3s_mremap_copy_slice_data(cur, src, &entry);
	if (err)
		return err;

	return p3s_mremap_install_dst_pte(cur, cur->copy->dst_folio, entry);
}

static inline int p3s_mremap_step_nonpresent_pte(struct p3s_mremap_cursor *cur)
{
	swp_entry_t swp;

	if (!is_swap_pte(cur->old_pte))
		return -EINVAL;

	swp = pte_to_swp_entry(cur->old_pte);
	if (is_migration_entry(swp))
		return p3s_mremap_wait_migration(cur);

	if (is_pte_marker_entry(swp)) {
		if (!cur->new_vma)
			return 0;
		return p3s_mremap_install_dst_pte(cur, NULL, cur->old_pte);
	}
	if (!cur->new_vma || non_swap_entry(swp))
		return -EINVAL;

	return p3s_mremap_copy_swapped_folio(cur, swp);
}

static inline int p3s_mremap_step_pte(struct p3s_mremap_cursor *cur)
{
	struct mm_struct *mm = cur->old_vma->vm_mm;
	struct folio *src;
	pte_t entry;

	cur->ptep = pte_offset_map_lock(mm, cur->old_pmd, cur->src_addr,
					&cur->ptl);
	if (!cur->ptep)
		return pmd_none(*cur->old_pmd) ? 0 : -EINVAL;

	if (cur->new_vma)
		flush_tlb_batched_pending(mm);
	cur->old_pte = ptep_get(cur->ptep);
	if (cur->swap_failed && pte_same(cur->old_pte, cur->prev_pte))
		return -ENOMEM;
	cur->swap_failed = false;

	if (pte_none(cur->old_pte))
		return 0;
	if (!pte_present(cur->old_pte))
		return p3s_mremap_step_nonpresent_pte(cur);

	src = vm_normal_folio(cur->old_vma, cur->src_addr, cur->old_pte);
	if (!src) {
		if (!is_zero_pfn(pte_pfn(cur->old_pte)))
			return -EINVAL;
		if (!cur->new_vma)
			return 0;
		entry = move_soft_dirty_pte(cur->old_pte);
		return p3s_mremap_install_dst_pte(cur, NULL, entry);
	}
	if (!cur->new_vma)
		return p3s_mremap_check_present_folio(cur, src);

	return p3s_mremap_copy_present_folio(cur, src);
}

/**
 * p3s_mremap_do_pte - Check or copy one 4KB slice of a cross-slice mremap()
 * @cur: Cursor of the slice
 *
 * p3s_mremap_step_pte() and its helpers return -EAGAIN once they have
 * dropped the source PTE lock to lock or drop a source folio, wait for a
 * migration entry, allocate the destination folio, read a page from swap
 * or drain the LRU caches. The slice is then looked up again from its
 * PTE. -EAGAIN never leaves this function: move_page_tables() would take
 * it as a request to redo the whole PMD extent, whose first slices may
 * already be copied.
 *
 * Return: 0 if the slice passed the check, was copied or has nothing to
 * copy, or a negative errno other than -EAGAIN.
 */
static inline int p3s_mremap_do_pte(struct p3s_mremap_cursor *cur)
{
	int err;

	do {
		err = p3s_mremap_step_pte(cur);
		p3s_mremap_unmap_src(cur);
	} while (err == -EAGAIN);

	p3s_mremap_put_locked_folio(&cur->locked_src);

	return err;
}

static inline int p3s_mremap_check_pte(struct vm_area_struct *vma, pmd_t *pmd,
				       unsigned long addr)
{
	struct p3s_mremap_cursor cur = {
		.old_vma = vma,
		.old_pmd = pmd,
		.src_addr = addr,
	};

	return p3s_mremap_do_pte(&cur);
}

static inline int p3s_mremap_copy_pte(struct vm_area_struct *old_vma,
				      struct vm_area_struct *new_vma,
				      pmd_t *old_pmd, pmd_t *new_pmd,
				      unsigned long src_addr,
				      unsigned long dst_addr,
				      struct p3s_mremap_copy *copy)
{
	struct p3s_mremap_cursor cur = {
		.old_vma = old_vma,
		.new_vma = new_vma,
		.old_pmd = old_pmd,
		.new_pmd = new_pmd,
		.src_addr = src_addr,
		.dst_addr = dst_addr,
		.copy = copy,
	};

	return p3s_mremap_do_pte(&cur);
}

/**
 * p3s_mremap_can_copy - Check that a cross-slice mremap() can copy a range
 * @vma: Source VMA
 * @addr: Start of the range
 * @len: Length of the range
 *
 * Runs before the destination is unmapped. Write-locks @vma before the
 * walk, can sleep, frees the swap space of the swap-cache folios it checks
 * and can drain the LRU caches.
 *
 * Return: true if move_page_tables() can copy every page of the range.
 */
static inline bool p3s_mremap_can_copy(struct vm_area_struct *vma,
				       unsigned long addr, unsigned long len)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long end = addr + len;

	if ((vma->vm_flags & (VM_MTE | VM_LOCKED | VM_DROPPABLE)) ||
	    userfaultfd_armed(vma))
		return false;

	vma_start_write(vma);
	while (addr < end) {
		unsigned long next = mm_pmd_addr_end(mm, addr, end);
		pmd_t *pmd = get_old_pmd(mm, addr);

		if (pmd) {
			for (; addr < next; addr += PAGE_SIZE_4KB)
				if (p3s_mremap_check_pte(vma, pmd, addr))
					return false;
		}
		addr = next;
		cond_resched();
	}

	return true;
}

/**
 * p3s_move_ptes - Move or cross-slice copy one PMD extent of page tables
 * @vma: Source VMA
 * @old_pmd: Source PMD entry
 * @old_addr: Start of the source PMD extent
 * @old_end: End of the source PMD extent
 * @new_vma: Destination VMA
 * @new_pmd: Destination PMD entry with an allocated PTE table
 * @new_addr: Start of the destination PMD extent
 * @need_rmap_locks: Whether move_ptes() must acquire rmap locks
 * @copy: Cross-slice destination folio packing state
 *
 * Delegates same-slice moves to move_ptes(). For cross-slice anonymous moves,
 * copies each 4KB slice into packed 16KB destination folios while keeping the
 * source mapped until p3s_mremap_copy_finish() zaps it.
 *
 * Return: 0 on success, -EAGAIN to retry PMD setup, or a negative errno.
 */
static inline int p3s_move_ptes(struct vm_area_struct *vma, pmd_t *old_pmd,
				unsigned long old_addr, unsigned long old_end,
				struct vm_area_struct *new_vma, pmd_t *new_pmd,
				unsigned long new_addr, bool need_rmap_locks,
				struct p3s_mremap_copy *copy)
{
	unsigned long extent = old_end - old_addr;
	unsigned long off;
	int err;

	if (mm_mremap_fixed_keeps_slice(vma, old_addr, new_addr))
		return move_ptes(vma, old_pmd, old_addr, old_end, new_vma,
				 new_pmd, new_addr, need_rmap_locks);

	if (fatal_signal_pending(current))
		return -EINTR;

	for (off = 0; off < extent; off += PAGE_SIZE_4KB) {
		unsigned long dst_addr = new_addr + off;

		if (copy->dst_folio &&
		    (dst_addr & PAGE_MASK_KERNEL) != copy->dst_window)
			p3s_mremap_put_dst_folio(&copy->dst_folio);
		if (off && !(dst_addr & ~PAGE_MASK_KERNEL)) {
			if (fatal_signal_pending(current))
				return -EINTR;
			cond_resched();
		}
		err = p3s_mremap_copy_pte(vma, new_vma, old_pmd, new_pmd,
					  old_addr + off, dst_addr, copy);
		if (err)
			return err;
	}
	if (!((new_addr + extent) & ~PAGE_MASK_KERNEL))
		p3s_mremap_put_dst_folio(&copy->dst_folio);

	return 0;
}

/**
 * p3s_mremap_copy_finish - Finish a cross-slice copy in move_page_tables()
 * @vma: Source VMA
 * @old_addr: Source address reached by move_page_tables()
 * @old_end: End of the source range
 * @len: Byte length of the move
 * @new_addr: Destination address corresponding to @old_addr
 * @copy: Destination folio packing state of p3s_move_ptes()
 * @progress: Bytes that move_page_tables() moved
 *
 * Unlocks the last destination folio and, once every slice is copied,
 * zaps the source range, as moving its PTEs would have.
 *
 * Return: @progress if the move kept the slice. Otherwise @len once the
 * source is zapped, or 0 so that mremap() fails with -ENOMEM and unmaps
 * the partial copy, leaving the source intact.
 */
static inline unsigned long
p3s_mremap_copy_finish(struct vm_area_struct *vma, unsigned long old_addr,
		       unsigned long old_end, unsigned long len,
		       unsigned long new_addr, struct p3s_mremap_copy *copy,
		       unsigned long progress)
{
	struct mm_struct *mm = vma->vm_mm;
	struct zap_details details = {
		.zap_flags = ZAP_FLAG_DROP_MARKER,
		.even_cows = true,
	};
	unsigned long hiwater_rss;

	p3s_mremap_put_dst_folio(&copy->dst_folio);
	if (mm_mremap_fixed_keeps_slice(vma, old_addr, new_addr))
		return progress;
	if (old_addr != old_end)
		return 0;

	/*
	 * Since RSS is raised while source and destination coexist, restore
	 * high watermark afterwards, as move_vma() does for hiwater_vm.
	 */
	hiwater_rss = mm->hiwater_rss;
	zap_page_range_single(vma, old_end - len, len, &details);
	mm->hiwater_rss = max(hiwater_rss, get_mm_rss(mm));

	return len;
}
#else
struct p3s_mremap_copy {};

static inline bool p3s_mremap_can_copy(struct vm_area_struct *vma,
				       unsigned long addr, unsigned long len)
{
	return true;
}

static inline int p3s_move_ptes(struct vm_area_struct *vma, pmd_t *old_pmd,
				unsigned long old_addr, unsigned long old_end,
				struct vm_area_struct *new_vma, pmd_t *new_pmd,
				unsigned long new_addr, bool need_rmap_locks,
				struct p3s_mremap_copy *copy)
{
	return move_ptes(vma, old_pmd, old_addr, old_end, new_vma, new_pmd,
			 new_addr, need_rmap_locks);
}

static inline unsigned long
p3s_mremap_copy_finish(struct vm_area_struct *vma, unsigned long old_addr,
		       unsigned long old_end, unsigned long len,
		       unsigned long new_addr, struct p3s_mremap_copy *copy,
		       unsigned long progress)
{
	return progress;
}
#endif /* CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

#endif /* __MM_P3S_MREMAP_H */
