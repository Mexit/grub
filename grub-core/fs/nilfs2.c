/* 
 *  nilfs2.c - New Implementation of Log filesystem 
 *
 *  Written by Jiro SEKIBA <jir@unicus.jp>
 *
 *  Copyright (C) 2003,2004,2005,2007,2008,2010  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */


/* Filetype information as used in inodes.  */
#define FILETYPE_INO_MASK	0170000
#define FILETYPE_INO_REG	0100000
#define FILETYPE_INO_DIRECTORY	0040000
#define FILETYPE_INO_SYMLINK	0120000

#include <grub/err.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/disk.h>
#include <grub/dl.h>
#include <grub/types.h>
#include <grub/fshelp.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define NILFS_INODE_BMAP_SIZE	7

#define NILFS_SUPORT_REV	2

/* Magic value used to identify an nilfs2 filesystem.  */
#define	NILFS2_SUPER_MAGIC	0x3434
/* nilfs btree node flag.  */
#define NILFS_BTREE_NODE_ROOT   0x01

/* nilfs btree node level. */
#define NILFS_BTREE_LEVEL_DATA          0
#define NILFS_BTREE_LEVEL_NODE_MIN      (NILFS_BTREE_LEVEL_DATA + 1)

/* nilfs 1st super block posission from beginning of the partition
   in 512 block size */
#define NILFS_1ST_SUPER_BLOCK	2
/* nilfs 2nd super block posission from beginning of the partition
   in 512 block size */
#define NILFS_2ND_SUPER_BLOCK(devsize)	(((devsize >> 3) - 1) << 3)

#define LOG_INODE_SIZE 7
struct grub_nilfs2_inode
{
  grub_uint64_t i_blocks;
  grub_uint64_t i_size;
  grub_uint64_t i_ctime;
  grub_uint64_t i_mtime;
  grub_uint32_t i_ctime_nsec;
  grub_uint32_t i_mtime_nsec;
  grub_uint32_t i_uid;
  grub_uint32_t i_gid;
  grub_uint16_t i_mode;
  grub_uint16_t i_links_count;
  grub_uint32_t i_flags;
  grub_uint64_t i_bmap[NILFS_INODE_BMAP_SIZE];
#define i_device_code	i_bmap[0]
  grub_uint64_t i_xattr;
  grub_uint32_t i_generation;
  grub_uint32_t i_pad;
};

struct grub_nilfs2_super_root
{
  grub_uint32_t sr_sum;
  grub_uint16_t sr_bytes;
  grub_uint16_t sr_flags;
  grub_uint64_t sr_nongc_ctime;
  struct grub_nilfs2_inode sr_dat;
  struct grub_nilfs2_inode sr_cpfile;
  struct grub_nilfs2_inode sr_sufile;
};

struct grub_nilfs2_super_block
{
  grub_uint32_t s_rev_level;
  grub_uint16_t s_minor_rev_level;
  grub_uint16_t s_magic;
  grub_uint16_t s_bytes;
  grub_uint16_t s_flags;
  grub_uint32_t s_crc_seed;
  grub_uint32_t s_sum;
  grub_uint32_t s_log_block_size;
  grub_uint64_t s_nsegments;
  grub_uint64_t s_dev_size;
  grub_uint64_t s_first_data_block;
  grub_uint32_t s_blocks_per_segment;
  grub_uint32_t s_r_segments_percentage;
  grub_uint64_t s_last_cno;
  grub_uint64_t s_last_pseg;
  grub_uint64_t s_last_seq;
  grub_uint64_t s_free_blocks_count;
  grub_uint64_t s_ctime;
  grub_uint64_t s_mtime;
  grub_uint64_t s_wtime;
  grub_uint16_t s_mnt_count;
  grub_uint16_t s_max_mnt_count;
  grub_uint16_t s_state;
  grub_uint16_t s_errors;
  grub_uint64_t s_lastcheck;
  grub_uint32_t s_checkinterval;
  grub_uint32_t s_creator_os;
  grub_uint16_t s_def_resuid;
  grub_uint16_t s_def_resgid;
  grub_uint32_t s_first_ino;
  grub_uint16_t s_inode_size;
  grub_uint16_t s_dat_entry_size;
  grub_uint16_t s_checkpoint_size;
  grub_uint16_t s_segment_usage_size;
  grub_uint8_t s_uuid[16];
  char s_volume_name[80];
  grub_uint32_t s_c_interval;
  grub_uint32_t s_c_block_max;
  grub_uint32_t s_reserved[192];
};

struct grub_nilfs2_dir_entry
{
  grub_uint64_t inode;
  grub_uint16_t rec_len;
#define MAX_NAMELEN 255
  grub_uint8_t name_len;
  grub_uint8_t file_type;
#if 0				/* followed by file name. */
  char name[NILFS_NAME_LEN];
  char pad;
#endif
} GRUB_PACKED;

enum
{
  NILFS_FT_UNKNOWN,
  NILFS_FT_REG_FILE,
  NILFS_FT_DIR,
  NILFS_FT_CHRDEV,
  NILFS_FT_BLKDEV,
  NILFS_FT_FIFO,
  NILFS_FT_SOCK,
  NILFS_FT_SYMLINK,
  NILFS_FT_MAX
};

struct grub_nilfs2_finfo
{
  grub_uint64_t fi_ino;
  grub_uint64_t fi_cno;
  grub_uint32_t fi_nblocks;
  grub_uint32_t fi_ndatablk;
};

struct grub_nilfs2_binfo_v
{
  grub_uint64_t bi_vblocknr;
  grub_uint64_t bi_blkoff;
};

struct grub_nilfs2_binfo_dat
{
  grub_uint64_t bi_blkoff;
  grub_uint8_t bi_level;
  grub_uint8_t bi_pad[7];
};

union grub_nilfs2_binfo
{
  struct grub_nilfs2_binfo_v bi_v;
  struct grub_nilfs2_binfo_dat bi_dat;
};

struct grub_nilfs2_segment_summary
{
  grub_uint32_t ss_datasum;
  grub_uint32_t ss_sumsum;
  grub_uint32_t ss_magic;
  grub_uint16_t ss_bytes;
  grub_uint16_t ss_flags;
  grub_uint64_t ss_seq;
  grub_uint64_t ss_create;
  grub_uint64_t ss_next;
  grub_uint32_t ss_nblocks;
  grub_uint32_t ss_nfinfo;
  grub_uint32_t ss_sumbytes;
  grub_uint32_t ss_pad;
};

struct grub_nilfs2_btree_node
{
  grub_uint8_t bn_flags;
  grub_uint8_t bn_level;
  grub_uint16_t bn_nchildren;
  grub_uint32_t bn_pad;
  grub_uint64_t keys[0];
};

struct grub_nilfs2_palloc_group_desc
{
  grub_uint32_t pg_nfrees;
};

#define LOG_SIZE_GROUP_DESC 2

#define LOG_NILFS_DAT_ENTRY_SIZE 5
struct grub_nilfs2_dat_entry
{
  grub_uint64_t de_blocknr;
  grub_uint64_t de_start;
  grub_uint64_t de_end;
  grub_uint64_t de_rsv;
};

struct grub_nilfs2_snapshot_list
{
  grub_uint64_t ssl_next;
  grub_uint64_t ssl_prev;
};

struct grub_nilfs2_cpfile_header
{
  grub_uint64_t ch_ncheckpoints;
  grub_uint64_t ch_nsnapshots;
  struct grub_nilfs2_snapshot_list ch_snapshot_list;
};

struct grub_nilfs2_checkpoint
{
  grub_uint32_t cp_flags;
  grub_uint32_t cp_checkpoints_count;
  struct grub_nilfs2_snapshot_list cp_snapshot_list;
  grub_uint64_t cp_cno;
  grub_uint64_t cp_create;
  grub_uint64_t cp_nblk_inc;
  grub_uint64_t cp_inodes_count;
  grub_uint64_t cp_blocks_count;
  struct grub_nilfs2_inode cp_ifile_inode;
};


#define NILFS_BMAP_LARGE	0x1
#define NILFS_BMAP_SIZE	(NILFS_INODE_BMAP_SIZE * sizeof(grub_uint64_t))

/* nilfs extra padding for nonroot btree node. */
#define NILFS_BTREE_NODE_EXTRA_PAD_SIZE (sizeof(grub_uint64_t))
#define NILFS_BTREE_ROOT_SIZE	NILFS_BMAP_SIZE
#define NILFS_BTREE_ROOT_NCHILDREN_MAX \
 ((NILFS_BTREE_ROOT_SIZE - sizeof(struct nilfs_btree_node)) / \
  (sizeof(grub_uint64_t) + sizeof(grub_uint64_t)) )


struct grub_fshelp_node
{
  struct grub_nilfs2_data *data;
  struct grub_nilfs2_inode inode;
  grub_uint64_t ino;
  int inode_read;
};

struct grub_nilfs2_data
{
  struct grub_nilfs2_super_block sblock;
  struct grub_nilfs2_super_root sroot;
  struct grub_nilfs2_inode ifile;
  grub_disk_t disk;
  struct grub_nilfs2_inode *inode;
  struct grub_fshelp_node diropen;
};

/* Log2 size of nilfs2 block in 512 blocks.  */
#define LOG2_NILFS2_BLOCK_SIZE(data)			\
	(grub_le_to_cpu32 (data->sblock.s_log_block_size) + 1)

/* Log2 size of nilfs2 block in bytes.  */
#define LOG2_BLOCK_SIZE(data)					\
	(grub_le_to_cpu32 (data->sblock.s_log_block_size) + 10)

/* The size of an nilfs2 block in bytes.  */
#define NILFS2_BLOCK_SIZE(data)		(1 << LOG2_BLOCK_SIZE (data))

static grub_uint64_t
grub_nilfs2_dat_translate (struct grub_nilfs2_data *data, grub_uint64_t key);
static grub_dl_t my_mod;



static inline unsigned long
grub_nilfs2_log_palloc_entries_per_group (struct grub_nilfs2_data *data)
{
  return LOG2_BLOCK_SIZE (data) + 3;
}

static inline grub_uint64_t
grub_nilfs2_palloc_group (struct grub_nilfs2_data *data,
			  grub_uint64_t nr, grub_uint64_t * offset)
{
  *offset = nr & ((1 << grub_nilfs2_log_palloc_entries_per_group (data)) - 1);
  return nr >> grub_nilfs2_log_palloc_entries_per_group (data);
}

static inline grub_uint32_t
grub_nilfs2_palloc_log_groups_per_desc_block (struct grub_nilfs2_data *data)
{
  return LOG2_BLOCK_SIZE (data) - LOG_SIZE_GROUP_DESC;

  COMPILE_TIME_ASSERT (sizeof (struct grub_nilfs2_palloc_group_desc)
		       == (1 << LOG_SIZE_GROUP_DESC));
}

static inline grub_uint32_t
grub_nilfs2_log_entries_per_block_log (struct grub_nilfs2_data *data,
				       unsigned long log_entry_size)
{
  return LOG2_BLOCK_SIZE (data) - log_entry_size;
}


static inline grub_uint32_t
grub_nilfs2_blocks_per_group_log (struct grub_nilfs2_data *data,
				  unsigned long log_entry_size)
{
  return (1 << (grub_nilfs2_log_palloc_entries_per_group (data)
		- grub_nilfs2_log_entries_per_block_log (data,
							 log_entry_size))) + 1;
}

static inline grub_uint32_t
grub_nilfs2_blocks_per_desc_block_log (struct grub_nilfs2_data *data,
				       unsigned long log_entry_size)
{
  return(grub_nilfs2_blocks_per_group_log (data, log_entry_size)
	 << grub_nilfs2_palloc_log_groups_per_desc_block (data)) + 1;
}

static inline grub_uint32_t
grub_nilfs2_palloc_desc_block_offset_log (struct grub_nilfs2_data *data,
					  unsigned long group,
					  unsigned long log_entry_size)
{
  grub_uint32_t desc_block =
    group >> grub_nilfs2_palloc_log_groups_per_desc_block (data);
  return desc_block * grub_nilfs2_blocks_per_desc_block_log (data,
							     log_entry_size);
}

static inline grub_uint32_t
grub_nilfs2_palloc_bitmap_block_offset (struct grub_nilfs2_data *data,
					unsigned long group,
					unsigned long log_entry_size)
{
  unsigned long desc_offset = group
    & ((1 << grub_nilfs2_palloc_log_groups_per_desc_block (data)) - 1);

  return grub_nilfs2_palloc_desc_block_offset_log (data, group, log_entry_size)
    + 1
    + desc_offset * grub_nilfs2_blocks_per_group_log (data, log_entry_size);
}

static inline grub_uint32_t
grub_nilfs2_palloc_entry_offset_log (struct grub_nilfs2_data *data,
				     grub_uint64_t nr,
				     unsigned long log_entry_size)
{
  unsigned long group;
  grub_uint64_t group_offset;

  group = grub_nilfs2_palloc_group (data, nr, &group_offset);

  return grub_nilfs2_palloc_bitmap_block_offset (data, group,
						 log_entry_size) + 1 +
    (group_offset >> grub_nilfs2_log_entries_per_block_log (data,
							    log_entry_size));

}

static inline struct grub_nilfs2_btree_node *
grub_nilfs2_btree_get_root (struct grub_nilfs2_inode *inode)
{
  return (struct grub_nilfs2_btree_node *) &inode->i_bmap[0];
}

static inline int
grub_nilfs2_btree_get_level (struct grub_nilfs2_btree_node *node)
{
  return node->bn_level;
}

static inline grub_uint64_t *
grub_nilfs2_btree_node_dkeys (struct grub_nilfs2_btree_node *node)
{
  return (node->keys +
	  ((node->bn_flags & NILFS_BTREE_NODE_ROOT) ?
	   0 : (NILFS_BTREE_NODE_EXTRA_PAD_SIZE / sizeof (grub_uint64_t))));
}

static inline grub_uint64_t
grub_nilfs2_btree_node_get_key (struct grub_nilfs2_btree_node *node,
				int index)
{
  return grub_le_to_cpu64 (*(grub_nilfs2_btree_node_dkeys (node) + index));
}

static inline int
grub_nilfs2_btree_node_lookup (struct grub_nilfs2_btree_node *node,
			       grub_uint64_t key, int *indexp)
{
  grub_uint64_t nkey;
  int index, low, high, s;

  low = 0;
  high = grub_le_to_cpu16 (node->bn_nchildren) - 1;
  index = 0;
  s = 0;
  while (low <= high)
    {
      index = (low + high) / 2;
      nkey = grub_nilfs2_btree_node_get_key (node, index);
      if (nkey == key)
	{
	  *indexp = index;
	  return 1;
	}
      else if (nkey < key)
	{
	  low = index + 1;
	  s = -1;
	}
      else
	{
	  high = index - 1;
	  s = 1;
	}
    }

  if (node->bn_level > NILFS_BTREE_LEVEL_NODE_MIN)
    {
      if (s > 0 && index > 0)
	index--;
    }
  else if (s < 0)
    index++;

  *indexp = index;
  return s == 0;
}

static inline int
grub_nilfs2_btree_node_nchildren_max (struct grub_nilfs2_data *data,
				      struct grub_nilfs2_btree_node *node)
{
  int node_children_max = ((NILFS2_BLOCK_SIZE (data) -
			    sizeof (struct grub_nilfs2_btree_node) -
			    NILFS_BTREE_NODE_EXTRA_PAD_SIZE) /
			   (sizeof (grub_uint64_t) + sizeof (grub_uint64_t)));

  return (node->bn_flags & NILFS_BTREE_NODE_ROOT) ? 3 : node_children_max;
}

static inline grub_uint64_t *
grub_nilfs2_btree_node_dptrs (struct grub_nilfs2_data *data,
			      struct grub_nilfs2_btree_node *node)
{
  return (grub_uint64_t *) (grub_nilfs2_btree_node_dkeys (node) +
			    grub_nilfs2_btree_node_nchildren_max (data,
								  node));
}

static inline grub_uint64_t
grub_nilfs2_btree_node_get_ptr (struct grub_nilfs2_data *data,
				struct grub_nilfs2_btree_node *node,
				int index)
{
  return
    grub_le_to_cpu64 (*(grub_nilfs2_btree_node_dptrs (data, node) + index));
}

static inline int
grub_nilfs2_btree_get_nonroot_node (struct grub_nilfs2_data *data,
				    grub_uint64_t ptr, void *block)
{
  grub_disk_t disk = data->disk;
  unsigned int nilfs2_block_count = (1 << LOG2_NILFS2_BLOCK_SIZE (data));

  return grub_disk_read (disk, ptr * nilfs2_block_count, 0,
			 NILFS2_BLOCK_SIZE (data), block);
}

static grub_uint64_t
grub_nilfs2_btree_lookup (struct grub_nilfs2_data *data,
			  struct grub_nilfs2_inode *inode,
			  grub_uint64_t key, int need_translate)
{
  struct grub_nilfs2_btree_node *node;
  void *block;
  grub_uint64_t ptr;
  int level, found = 0, index;

  block = grub_malloc (NILFS2_BLOCK_SIZE (data));
  if (!block)
    return -1;

  node = grub_nilfs2_btree_get_root (inode);
  level = grub_nilfs2_btree_get_level (node);

  found = grub_nilfs2_btree_node_lookup (node, key, &index);
  ptr = grub_nilfs2_btree_node_get_ptr (data, node, index);
  if (need_translate)
    ptr = grub_nilfs2_dat_translate (data, ptr);

  for (level--; level >= NILFS_BTREE_LEVEL_NODE_MIN; level--)
    {
      grub_nilfs2_btree_get_nonroot_node (data, ptr, block);
      if (grub_errno)
	{
	  goto fail;
	}
      node = (struct grub_nilfs2_btree_node *) block;

      if (node->bn_level != level)
	{
	  grub_error (GRUB_ERR_BAD_FS, "btree level mismatch\n");
	  goto fail;
	}

      if (!found)
	found = grub_nilfs2_btree_node_lookup (node, key, &index);
      else
	index = 0;

      if (index < grub_nilfs2_btree_node_nchildren_max (data, node))
	{
	  ptr = grub_nilfs2_btree_node_get_ptr (data, node, index);
	  if (need_translate)
	    ptr = grub_nilfs2_dat_translate (data, ptr);
	}
      else
	{
	  grub_error (GRUB_ERR_BAD_FS, "btree corruption\n");
	  goto fail;
	}
    }

  grub_free (block);

  if (!found)
    return -1;

  return ptr;
 fail:
  grub_free (block);
  return -1;
}

static inline grub_uint64_t
grub_nilfs2_direct_lookup (struct grub_nilfs2_inode *inode, grub_uint64_t key)
{
  return grub_le_to_cpu64 (inode->i_bmap[1 + key]);
}

static inline grub_uint64_t
grub_nilfs2_bmap_lookup (struct grub_nilfs2_data *data,
			 struct grub_nilfs2_inode *inode,
			 grub_uint64_t key, int need_translate)
{
  struct grub_nilfs2_btree_node *root = grub_nilfs2_btree_get_root (inode);
  if (root->bn_flags & NILFS_BMAP_LARGE)
    return grub_nilfs2_btree_lookup (data, inode, key, need_translate);
  else
    {
      grub_uint64_t ptr;
      ptr = grub_nilfs2_direct_lookup (inode, key);
      if (need_translate)
	ptr = grub_nilfs2_dat_translate (data, ptr);
      return ptr;
    }
}

static grub_uint64_t
grub_nilfs2_dat_translate (struct grub_nilfs2_data *data, grub_uint64_t key)
{
  struct grub_nilfs2_dat_entry entry;
  grub_disk_t disk = data->disk;
  grub_uint64_t pptr;
  grub_uint64_t blockno, offset;
  unsigned int nilfs2_block_count = (1 << LOG2_NILFS2_BLOCK_SIZE (data));

  blockno = grub_nilfs2_palloc_entry_offset_log (data, key,
						 LOG_NILFS_DAT_ENTRY_SIZE);

  offset = ((key * sizeof (struct grub_nilfs2_dat_entry))
	    & ((1 << LOG2_BLOCK_SIZE (data)) - 1));

  pptr = grub_nilfs2_bmap_lookup (data, &data->sroot.sr_dat, blockno, 0);
  if (pptr == (grub_uint64_t) - 1)
    {
      grub_error (GRUB_ERR_BAD_FS, "btree lookup failure");
      return -1;
    }

  grub_disk_read (disk, pptr * nilfs2_block_count, offset,
		  sizeof (struct grub_nilfs2_dat_entry), &entry);

  return grub_le_to_cpu64 (entry.de_blocknr);
}


static grub_disk_addr_t
grub_nilfs2_read_block (grub_fshelp_node_t node, grub_disk_addr_t fileblock)
{
  struct grub_nilfs2_data *data = node->data;
  struct grub_nilfs2_inode *inode = &node->inode;
  grub_uint64_t pptr = -1;

  pptr = grub_nilfs2_bmap_lookup (data, inode, fileblock, 1);
  if (pptr == (grub_uint64_t) - 1)
    {
      grub_error (GRUB_ERR_BAD_FS, "btree lookup failure");
      return -1;
    }

  return pptr;
}

/* Read LEN bytes from the file described by DATA starting with byte
   POS.  Return the amount of read bytes in READ.  */
static grub_ssize_t
grub_nilfs2_read_file (grub_fshelp_node_t node,
		       grub_disk_read_hook_t read_hook, void *read_hook_data,
		       grub_off_t pos, grub_size_t len, char *buf)
{
  return grub_fshelp_read_file (node->data->disk, node,
				read_hook, read_hook_data,
				pos, len, buf, grub_nilfs2_read_block,
				grub_le_to_cpu64 (node->inode.i_size),
				LOG2_NILFS2_BLOCK_SIZE (node->data), 0);

}

static grub_err_t
grub_nilfs2_read_checkpoint (struct grub_nilfs2_data *data,
			     grub_uint64_t cpno,
			     struct grub_nilfs2_checkpoint *cpp)
{
  grub_uint64_t blockno;
  grub_uint64_t offset;
  grub_uint64_t pptr;
  grub_disk_t disk = data->disk;
  unsigned int nilfs2_block_count = (1 << LOG2_NILFS2_BLOCK_SIZE (data));

  /* Assume sizeof(struct grub_nilfs2_cpfile_header) < 
     sizeof(struct grub_nilfs2_checkpoint).
   */
  blockno = grub_divmod64 (cpno, NILFS2_BLOCK_SIZE (data) /
                          sizeof (struct grub_nilfs2_checkpoint), &offset);
  
  pptr = grub_nilfs2_bmap_lookup (data, &data->sroot.sr_cpfile, blockno, 1);
  if (pptr == (grub_uint64_t) - 1)
    {
      return grub_error (GRUB_ERR_BAD_FS, "btree lookup failure");
    }

  return grub_disk_read (disk, pptr * nilfs2_block_count,
			 offset * sizeof (struct grub_nilfs2_checkpoint),
			 sizeof (struct grub_nilfs2_checkpoint), cpp);
}

static inline grub_err_t
grub_nilfs2_read_last_checkpoint (struct grub_nilfs2_data *data,
				  struct grub_nilfs2_checkpoint *cpp)
{
  return grub_nilfs2_read_checkpoint (data,
				      grub_le_to_cpu64 (data->
							sblock.s_last_cno),
				      cpp);
}

/* Read the inode INO for the file described by DATA into INODE.  */
static grub_err_t
grub_nilfs2_read_inode (struct grub_nilfs2_data *data,
			grub_uint64_t ino, struct grub_nilfs2_inode *inodep)
{
  grub_uint64_t blockno;
  grub_uint64_t offset;
  grub_uint64_t pptr;
  grub_disk_t disk = data->disk;
  unsigned int nilfs2_block_count = (1 << LOG2_NILFS2_BLOCK_SIZE (data));

  blockno = grub_nilfs2_palloc_entry_offset_log (data, ino,
						 LOG_INODE_SIZE);

  offset = ((sizeof (struct grub_nilfs2_inode) * ino)
	    & ((1 << LOG2_BLOCK_SIZE (data)) - 1));
  pptr = grub_nilfs2_bmap_lookup (data, &data->ifile, blockno, 1);
  if (pptr == (grub_uint64_t) - 1)
    {
      return grub_error (GRUB_ERR_BAD_FS, "btree lookup failure");
    }

  return grub_disk_read (disk, pptr * nilfs2_block_count, offset,
			 sizeof (struct grub_nilfs2_inode), inodep);
}

static int
grub_nilfs2_valid_sb (struct grub_nilfs2_super_block *sbp)
{
  if (grub_le_to_cpu16 (sbp->s_magic) != NILFS2_SUPER_MAGIC)
    return 0;

  if (grub_le_to_cpu32 (sbp->s_rev_level) != NILFS_SUPORT_REV)
    return 0;

  /* 20 already means 1GiB blocks. We don't want to deal with blocks overflowing int32. */
  if (grub_le_to_cpu32 (sbp->s_log_block_size) > 20)
    return 0;

  return 1;
}

static grub_err_t
grub_nilfs2_load_sb (struct grub_nilfs2_data *data)
{
  grub_disk_t disk = data->disk;
  struct grub_nilfs2_super_block sb2;
  grub_uint64_t partition_size;
  int valid[2];
  int swp = 0;
  grub_err_t err;

  /* Read first super block. */
  err = grub_disk_read (disk, NILFS_1ST_SUPER_BLOCK, 0,
			sizeof (struct grub_nilfs2_super_block), &data->sblock);
  if (err)
    return err;
  /* Make sure if 1st super block is valid.  */
  valid[0] = grub_nilfs2_valid_sb (&data->sblock);

  if (valid[0])
    partition_size = (grub_le_to_cpu64 (data->sblock.s_dev_size)
		      >> GRUB_DISK_SECTOR_BITS);
  else
    partition_size = grub_disk_native_sectors (disk);
  if (partition_size != GRUB_DISK_SIZE_UNKNOWN)
    {
      /* Read second super block. */
      err = grub_disk_read (disk, NILFS_2ND_SUPER_BLOCK (partition_size), 0,
			    sizeof (struct grub_nilfs2_super_block), &sb2);
      if (err)
	{
	  valid[1] = 0;
	  grub_errno = GRUB_ERR_NONE;
	}
      else
	/* Make sure if 2nd super block is valid.  */
	valid[1] = grub_nilfs2_valid_sb (&sb2);
    }
  else
    /* 2nd super block may not exist, so it's invalid. */
    valid[1] = 0;

  if (!valid[0] && !valid[1])
    return grub_error (GRUB_ERR_BAD_FS, "not a nilfs2 filesystem");

  swp = valid[1] && (!valid[0] ||
		     grub_le_to_cpu64 (data->sblock.s_last_cno) <
		     grub_le_to_cpu64 (sb2.s_last_cno));

  /* swap if first super block is invalid or older than second one. */
  if (swp)
    grub_memcpy (&data->sblock, &sb2,
		 sizeof (struct grub_nilfs2_super_block));

  return GRUB_ERR_NONE;
}

static struct grub_nilfs2_data *
grub_nilfs2_mount (grub_disk_t disk)
{
  struct grub_nilfs2_data *data;
  struct grub_nilfs2_segment_summary ss;
  struct grub_nilfs2_checkpoint last_checkpoint;
  grub_uint64_t last_pseg;
  grub_uint32_t nblocks;
  unsigned int nilfs2_block_count;

  data = grub_malloc (sizeof (struct grub_nilfs2_data));
  if (!data)
    return 0;

  data->disk = disk;

  /* Read the superblock.  */
  grub_nilfs2_load_sb (data);
  if (grub_errno)
    goto fail;

  nilfs2_block_count = (1 << LOG2_NILFS2_BLOCK_SIZE (data));

  /* Read the last segment summary. */
  last_pseg = grub_le_to_cpu64 (data->sblock.s_last_pseg);
  grub_disk_read (disk, last_pseg * nilfs2_block_count, 0,
		  sizeof (struct grub_nilfs2_segment_summary), &ss);

  if (grub_errno)
    goto fail;

  /* Read the super root block. */
  nblocks = grub_le_to_cpu32 (ss.ss_nblocks);
  grub_disk_read (disk, (last_pseg + (nblocks - 1)) * nilfs2_block_count, 0,
		  sizeof (struct grub_nilfs2_super_root), &data->sroot);

  if (grub_errno)
    goto fail;

  grub_nilfs2_read_last_checkpoint (data, &last_checkpoint);

  if (grub_errno)
    goto fail;

  grub_memcpy (&data->ifile, &last_checkpoint.cp_ifile_inode,
	       sizeof (struct grub_nilfs2_inode));

  data->diropen.data = data;
  data->diropen.ino = 2;
  data->diropen.inode_read = 1;
  data->inode = &data->diropen.inode;

  grub_nilfs2_read_inode (data, 2, data->inode);

  return data;

fail:
  if (grub_errno == GRUB_ERR_OUT_OF_RANGE)
    grub_error (GRUB_ERR_BAD_FS, "not a nilfs2 filesystem");

  grub_free (data);
  return 0;
}

static char *
grub_nilfs2_read_symlink (grub_fshelp_node_t node)
{
  char *symlink;
  struct grub_fshelp_node *diro = node;

  if (!diro->inode_read)
    {
      grub_nilfs2_read_inode (diro->data, diro->ino, &diro->inode);
      if (grub_errno)
	return 0;
    }

  symlink = grub_malloc (grub_le_to_cpu64 (diro->inode.i_size) + 1);
  if (!symlink)
    return 0;

  grub_nilfs2_read_file (diro, 0, 0, 0,
			 grub_le_to_cpu64 (diro->inode.i_size), symlink);
  if (grub_errno)
    {
      grub_free (symlink);
      return 0;
    }

  symlink[grub_le_to_cpu64 (diro->inode.i_size)] = '\0';
  return symlink;
}

static int
grub_nilfs2_iterate_dir (grub_fshelp_node_t dir,
			 grub_fshelp_iterate_dir_hook_t hook, void *hook_data)
{
  grub_off_t fpos = 0;
  struct grub_fshelp_node *diro = (struct grub_fshelp_node *) dir;

  if (!diro->inode_read)
    {
      grub_nilfs2_read_inode (diro->data, diro->ino, &diro->inode);
      if (grub_errno)
	return 0;
    }

  /* Iterate files.  */
  while (fpos < grub_le_to_cpu64 (diro->inode.i_size))
    {
      struct grub_nilfs2_dir_entry dirent;

      grub_nilfs2_read_file (diro, 0, 0, fpos,
			     sizeof (struct grub_nilfs2_dir_entry),
			     (char *) &dirent);
      if (grub_errno)
	return 0;

      if (dirent.rec_len == 0)
	return 0;

      if (dirent.name_len != 0)
	{
	  char filename[MAX_NAMELEN + 1];
	  struct grub_fshelp_node *fdiro;
	  enum grub_fshelp_filetype type = GRUB_FSHELP_UNKNOWN;

	  grub_nilfs2_read_file (diro, 0, 0,
				 fpos + sizeof (struct grub_nilfs2_dir_entry),
				 dirent.name_len, filename);
	  if (grub_errno)
	    return 0;

	  fdiro = grub_malloc (sizeof (struct grub_fshelp_node));
	  if (!fdiro)
	    return 0;

	  fdiro->data = diro->data;
	  fdiro->ino = grub_le_to_cpu64 (dirent.inode);

	  filename[dirent.name_len] = '\0';

	  if (dirent.file_type != NILFS_FT_UNKNOWN)
	    {
	      fdiro->inode_read = 0;

	      if (dirent.file_type == NILFS_FT_DIR)
		type = GRUB_FSHELP_DIR;
	      else if (dirent.file_type == NILFS_FT_SYMLINK)
		type = GRUB_FSHELP_SYMLINK;
	      else if (dirent.file_type == NILFS_FT_REG_FILE)
		type = GRUB_FSHELP_REG;
	    }
	  else
	    {
	      /* The filetype can not be read from the dirent, read
	         the inode to get more information.  */
	      grub_nilfs2_read_inode (diro->data,
				      grub_le_to_cpu64 (dirent.inode),
				      &fdiro->inode);
	      if (grub_errno)
		{
		  grub_free (fdiro);
		  return 0;
		}

	      fdiro->inode_read = 1;

	      if ((grub_le_to_cpu16 (fdiro->inode.i_mode)
		   & FILETYPE_INO_MASK) == FILETYPE_INO_DIRECTORY)
		type = GRUB_FSHELP_DIR;
	      else if ((grub_le_to_cpu16 (fdiro->inode.i_mode)
			& FILETYPE_INO_MASK) == FILETYPE_INO_SYMLINK)
		type = GRUB_FSHELP_SYMLINK;
	      else if ((grub_le_to_cpu16 (fdiro->inode.i_mode)
			& FILETYPE_INO_MASK) == FILETYPE_INO_REG)
		type = GRUB_FSHELP_REG;
	    }

	  if (hook (filename, type, fdiro, hook_data))
	    return 1;
	}

      fpos += grub_le_to_cpu16 (dirent.rec_len);
    }

  return 0;
}

/* Open a file named NAME and initialize FILE.  */
static grub_err_t
grub_nilfs2_open (struct grub_file *file, const char *name)
{
  struct grub_nilfs2_data *data = NULL;
  struct grub_fshelp_node *fdiro = 0;

  grub_dl_ref (my_mod);

  data = grub_nilfs2_mount (file->device->disk);
  if (!data)
    goto fail;

  grub_fshelp_find_file (name, &data->diropen, &fdiro,
			 grub_nilfs2_iterate_dir, grub_nilfs2_read_symlink,
			 GRUB_FSHELP_REG);
  if (grub_errno)
    goto fail;

  if (!fdiro->inode_read)
    {
      grub_nilfs2_read_inode (data, fdiro->ino, &fdiro->inode);
      if (grub_errno)
	goto fail;
    }

  grub_memcpy (data->inode, &fdiro->inode, sizeof (struct grub_nilfs2_inode));
  grub_free (fdiro);

  file->size = grub_le_to_cpu64 (data->inode->i_size);
  file->data = data;
  file->offset = 0;

  return 0;

fail:
  if (fdiro != &data->diropen)
    grub_free (fdiro);
  grub_free (data);

  grub_dl_unref (my_mod);

  return grub_errno;
}

static grub_err_t
grub_nilfs2_close (grub_file_t file)
{
  grub_free (file->data);

  grub_dl_unref (my_mod);

  return GRUB_ERR_NONE;
}

/* Read LEN bytes data from FILE into BUF.  */
static grub_ssize_t
grub_nilfs2_read (grub_file_t file, char *buf, grub_size_t len)
{
  struct grub_nilfs2_data *data = (struct grub_nilfs2_data *) file->data;

  return grub_nilfs2_read_file (&data->diropen,
				file->read_hook, file->read_hook_data,
				file->offset, len, buf);
}

/* Context for grub_nilfs2_dir.  */
struct grub_nilfs2_dir_ctx
{
  grub_fs_dir_hook_t hook;
  void *hook_data;
  struct grub_nilfs2_data *data;
};

/* Helper for grub_nilfs2_dir.  */
static int
grub_nilfs2_dir_iter (const char *filename, enum grub_fshelp_filetype filetype,
		      grub_fshelp_node_t node, void *data)
{
  struct grub_nilfs2_dir_ctx *ctx = data;
  struct grub_dirhook_info info;

  grub_memset (&info, 0, sizeof (info));
  if (!node->inode_read)
    {
      grub_nilfs2_read_inode (ctx->data, node->ino, &node->inode);
      if (!grub_errno)
	node->inode_read = 1;
      grub_errno = GRUB_ERR_NONE;
    }
  if (node->inode_read)
    {
      info.mtimeset = 1;
      info.mtime = grub_le_to_cpu64 (node->inode.i_mtime);
    }

  info.dir = ((filetype & GRUB_FSHELP_TYPE_MASK) == GRUB_FSHELP_DIR);
  grub_free (node);
  return ctx->hook (filename, &info, ctx->hook_data);
}

static grub_err_t
grub_nilfs2_dir (grub_device_t device, const char *path,
		 grub_fs_dir_hook_t hook, void *hook_data)
{
  struct grub_nilfs2_dir_ctx ctx = {
    .hook = hook,
    .hook_data = hook_data
  };
  struct grub_fshelp_node *fdiro = 0;

  grub_dl_ref (my_mod);

  ctx.data = grub_nilfs2_mount (device->disk);
  if (!ctx.data)
    goto fail;

  grub_fshelp_find_file (path, &ctx.data->diropen, &fdiro,
			 grub_nilfs2_iterate_dir, grub_nilfs2_read_symlink,
			 GRUB_FSHELP_DIR);
  if (grub_errno)
    goto fail;

  grub_nilfs2_iterate_dir (fdiro, grub_nilfs2_dir_iter, &ctx);

fail:
  if (fdiro != &ctx.data->diropen)
    grub_free (fdiro);
  grub_free (ctx.data);

  grub_dl_unref (my_mod);

  return grub_errno;
}

static grub_err_t
grub_nilfs2_label (grub_device_t device, char **label)
{
  struct grub_nilfs2_data *data;
  grub_disk_t disk = device->disk;

  grub_dl_ref (my_mod);

  data = grub_nilfs2_mount (disk);
  if (data)
    *label = grub_strndup (data->sblock.s_volume_name,
			   sizeof (data->sblock.s_volume_name));
  else
    *label = NULL;

  grub_dl_unref (my_mod);

  grub_free (data);

  return grub_errno;
}

static grub_err_t
grub_nilfs2_uuid (grub_device_t device, char **uuid)
{
  struct grub_nilfs2_data *data;
  grub_disk_t disk = device->disk;

  grub_dl_ref (my_mod);

  data = grub_nilfs2_mount (disk);
  if (data)
    {
      *uuid =
	grub_xasprintf
	("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	 data->sblock.s_uuid[0], data->sblock.s_uuid[1],
	 data->sblock.s_uuid[2], data->sblock.s_uuid[3],
	 data->sblock.s_uuid[4], data->sblock.s_uuid[5],
	 data->sblock.s_uuid[6], data->sblock.s_uuid[7],
	 data->sblock.s_uuid[8], data->sblock.s_uuid[9],
	 data->sblock.s_uuid[10], data->sblock.s_uuid[11],
	 data->sblock.s_uuid[12], data->sblock.s_uuid[13],
	 data->sblock.s_uuid[14], data->sblock.s_uuid[15]);
    }
  else
    *uuid = NULL;

  grub_dl_unref (my_mod);

  grub_free (data);

  return grub_errno;
}

/* Get mtime.  */
static grub_err_t
grub_nilfs2_mtime (grub_device_t device, grub_int32_t * tm)
{
  struct grub_nilfs2_data *data;
  grub_disk_t disk = device->disk;

  grub_dl_ref (my_mod);

  data = grub_nilfs2_mount (disk);
  if (!data)
    *tm = 0;
  else
    *tm = (grub_int32_t) grub_le_to_cpu64 (data->sblock.s_wtime);

  grub_dl_unref (my_mod);

  grub_free (data);

  return grub_errno;
}



static struct grub_fs grub_nilfs2_fs = {
  .name = "nilfs2",
  .fs_dir = grub_nilfs2_dir,
  .fs_open = grub_nilfs2_open,
  .fs_read = grub_nilfs2_read,
  .fs_close = grub_nilfs2_close,
  .fs_label = grub_nilfs2_label,
  .fs_uuid = grub_nilfs2_uuid,
  .fs_mtime = grub_nilfs2_mtime,
#ifdef GRUB_UTIL
  .reserved_first_sector = 1,
  .blocklist_install = 0,
#endif
  .next = 0
};

GRUB_MOD_INIT (nilfs2)
{
  COMPILE_TIME_ASSERT ((1 << LOG_NILFS_DAT_ENTRY_SIZE)
		       == sizeof (struct
				  grub_nilfs2_dat_entry));
  COMPILE_TIME_ASSERT (1 << LOG_INODE_SIZE
		       == sizeof (struct grub_nilfs2_inode));
  grub_fs_register (&grub_nilfs2_fs);
  my_mod = mod;
}

GRUB_MOD_FINI (nilfs2)
{
  grub_fs_unregister (&grub_nilfs2_fs);
}
