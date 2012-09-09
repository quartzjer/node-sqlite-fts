#include <windows.h>

#define CEIL(size,to)	(((size)+(to)-1)&~((to)-1))
#define FLOOR(size,to)	((size)&~((to)-1))
#define SBRK_SCALE  0
#define SBRK_FAILURE NULL

/* Wait for spin lock */
int slwait (int *sl) {
    while (InterlockedCompareExchange ((void **) sl, (void *) 1, (void *) 0) != 0) 
	Sleep (0);
    return 0;
}
/* Release spin lock */
int slrelease (int *sl) {
    InterlockedExchange (sl, 0);
    return 0;
}

static int g_sl;

long getregionsize (void) {
    static long g_regionsize = 0;
    if (! g_regionsize) {
        SYSTEM_INFO system_info;
        GetSystemInfo (&system_info);
        g_regionsize = system_info.dwAllocationGranularity;
    }
    return g_regionsize;
}

long getpagesize (void) {
    static long g_pagesize = 0;
    if (! g_pagesize) {
        SYSTEM_INFO system_info;
        GetSystemInfo (&system_info);
        g_pagesize = system_info.dwPageSize;
    }
    return g_pagesize;
}

/* A region list entry */
typedef struct _region_list_entry {
    void *top_allocated;
    void *top_committed;
    void *top_reserved;
    long reserve_size;
    struct _region_list_entry *previous;
} region_list_entry;

/* Allocate and link a region entry in the region list */
static int region_list_append (region_list_entry **last, void *base_reserved, long reserve_size) {
    region_list_entry *next = HeapAlloc (GetProcessHeap (), 0, sizeof (region_list_entry));
    if (! next)
        return FALSE;
    next->top_allocated = (char *) base_reserved;
    next->top_committed = (char *) base_reserved;
    next->top_reserved = (char *) base_reserved + reserve_size;
    next->reserve_size = reserve_size;
    next->previous = *last;
    *last = next;
    return TRUE;
}
/* Free and unlink the last region entry from the region list */
static int region_list_remove (region_list_entry **last) {
    region_list_entry *previous = (*last)->previous;
    if (! HeapFree (GetProcessHeap (), sizeof (region_list_entry), *last))
        return FALSE;
    *last = previous;
    return TRUE;
}

void *sbrk (long size) {
    static long g_pagesize, g_my_pagesize;
    static long g_regionsize, g_my_regionsize;
    static region_list_entry *g_last;
    void *result = SBRK_FAILURE;
    /* Wait for spin lock */
    slwait (&g_sl);
    /* First time initialization */
    if (! g_pagesize) {
        g_pagesize = getpagesize ();
        g_my_pagesize = g_pagesize << SBRK_SCALE;
    }
    if (! g_regionsize) {
        g_regionsize = getregionsize ();
        g_my_regionsize = g_regionsize << SBRK_SCALE;
    }
    if (! g_last) {
        if (! region_list_append (&g_last, 0, 0)) 
           goto sbrk_exit;
    }
    /* Allocation requested? */
    if (size >= 0) {
        /* Allocation size is the requested size */
        long allocate_size = size;
        /* Compute the size to commit */
        long to_reserve = (char *) g_last->top_allocated + allocate_size - (char *) g_last->top_reserved;
        /* Do we reach the commit limit? */
        if (to_reserve > 0) {
            /* Now we are going to search and reserve. */
            int contiguous = -1;
            int found = FALSE;
            MEMORY_BASIC_INFORMATION memory_info;
            void *base_reserved;
            long reserve_size;
            do {
                /* Assume contiguous memory */
                contiguous = TRUE;
                /* Round size to reserve */
                reserve_size = CEIL (to_reserve, g_my_regionsize);
                /* Start with the current region's top */
                memory_info.BaseAddress = g_last->top_reserved;
                while (VirtualQuery (memory_info.BaseAddress, &memory_info, sizeof (memory_info))) {
                    /* Region is free, well aligned and big enough: we are done */
                    if (memory_info.State == MEM_FREE &&
                        (unsigned) memory_info.BaseAddress % g_regionsize == 0 &&
                        memory_info.RegionSize >= (unsigned) reserve_size) {
                        found = TRUE;
                        break;
                    }
                    /* From now on we can't get contiguous memory! */
                    contiguous = FALSE;
                    /* Recompute size to reserve */
                    reserve_size = CEIL (allocate_size, g_my_regionsize);
                    memory_info.BaseAddress = (char *) memory_info.BaseAddress + memory_info.RegionSize;
                }
                /* Search failed? */
                if (! found) 
                    goto sbrk_exit;
                /* Try to reserve this */
                base_reserved = VirtualAlloc (memory_info.BaseAddress, reserve_size, 
					      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (! base_reserved) {
                    int rc = GetLastError ();
                    if (rc != ERROR_INVALID_ADDRESS) 
                        goto sbrk_exit;
                }
                /* A null pointer signals (hopefully) a race condition with another thread. */
                /* In this case, we try again. */
            } while (! base_reserved);
            /* Check returned pointer for consistency */
            if (memory_info.BaseAddress && base_reserved != memory_info.BaseAddress)
                goto sbrk_exit;
            /* Did we get contiguous memory? */
            if (contiguous) {
                long start_size = (char *) g_last->top_reserved - (char *) g_last->top_allocated;
                /* Adjust allocation size */
                allocate_size -= start_size;
                /* Adjust the regions allocation top */
                g_last->top_allocated = g_last->top_reserved;
            } 
            /* Append the new region to the list */
            if (! region_list_append (&g_last, base_reserved, reserve_size))
                goto sbrk_exit;
        } 
        /* Adjust the regions allocation top */
        g_last->top_allocated = (char *) g_last->top_allocated + allocate_size;
        result = (char *) g_last->top_allocated - size;
    /* Deallocation requested? */
    } else if (size < 0) {
        long deallocate_size = - size;
        /* As long as we have a region to release */
        while ((char *) g_last->top_allocated - deallocate_size < (char *) g_last->top_reserved - g_last->reserve_size) {
            /* Get the size to release */
            long release_size = g_last->reserve_size;
            /* Get the base address */
            void *base_reserved = (char *) g_last->top_reserved - release_size;
            /* Release this */
            int rc = VirtualFree (base_reserved, 0, 
                                  MEM_RELEASE);
            /* Check returned code for consistency */
            if (! rc)
                goto sbrk_exit;
            /* Adjust deallocation size */
            deallocate_size -= (char *) g_last->top_allocated - (char *) base_reserved;
            /* Remove the old region from the list */
            if (! region_list_remove (&g_last))
                goto sbrk_exit;
        }
        /* Adjust regions allocate top */
        g_last->top_allocated = (char *) g_last->top_allocated - deallocate_size;
        /* Check for underflow */
        if ((char *) g_last->top_reserved - g_last->reserve_size > (char *) g_last->top_allocated ||
            g_last->top_allocated > g_last->top_reserved) {
            /* Adjust regions allocate top */
            g_last->top_allocated = (char *) g_last->top_reserved - g_last->reserve_size;
            goto sbrk_exit;
        }
        result = g_last->top_allocated;
    }
sbrk_exit:
    /* Release spin lock */
    slrelease (&g_sl);
    return result;
}
