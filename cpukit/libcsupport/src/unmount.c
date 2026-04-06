/* SPDX-License-Identifier: BSD-2-Clause */

/**
 *  @file
 *
 *  @brief Unmount a File System
 *  @ingroup libcsupport
 */

/*
 *  COPYRIGHT (c) 1989-2010.
 *  On-Line Applications Research Corporation (OAR).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>

#include <rtems/libio_.h>
#ifdef RTEMSCFG_MNT_CONTAINER
#include <rtems/score/threadimpl.h>
#endif

static bool contains_root_or_current_directory(
  const rtems_filesystem_mount_table_entry_t *mt_entry
)
{
#ifdef RTEMSCFG_MNT_CONTAINER
  rtems_filesystem_global_location_t *container_root = get_container_root_location();
  rtems_filesystem_global_location_t *container_current = get_container_current_location();
  
  const rtems_filesystem_location_info_t *root =
    &container_root->location;
  const rtems_filesystem_location_info_t *current =
    &container_current->location;

  bool is_root_fs = (mt_entry->target == NULL || 
                     (mt_entry->target[0] == '/' && mt_entry->target[1] == '\0'));
  
  return (is_root_fs && mt_entry == root->mt_entry) || mt_entry == current->mt_entry;
#else
  const rtems_filesystem_location_info_t *root =
    &rtems_filesystem_root->location;
  const rtems_filesystem_location_info_t *current =
    &rtems_filesystem_current->location;

  return mt_entry == root->mt_entry || mt_entry == current->mt_entry;
#endif
}

/**
 *  This routine is not defined in the POSIX 1003.1b standard but
 *  in some form is supported on most UNIX and POSIX systems.  This
 *  routine is necessary to mount instantiations of a file system
 *  into the file system name space.
 */
int unmount( const char *path )
{
  int rv = 0;
  rtems_filesystem_eval_path_context_t ctx;
  int eval_flags = RTEMS_FS_FOLLOW_LINK;
  const rtems_filesystem_location_info_t *currentloc =
    rtems_filesystem_eval_path_start( &ctx, path, eval_flags );
  rtems_filesystem_mount_table_entry_t *mt_entry = currentloc->mt_entry;

  if ( rtems_filesystem_location_is_instance_root( currentloc ) ) {
    if ( !contains_root_or_current_directory( mt_entry ) ) {
      const rtems_filesystem_operations_table *mt_point_ops =
        mt_entry->mt_point_node->location.mt_entry->ops;

#ifdef RTEMSCFG_MNT_CONTAINER
      Thread_Control *executing = _Thread_Get_executing();
      if (executing && executing->container && executing->container->mntContainer) {
        rv = 0;
      } else {
        rv = (*mt_point_ops->unmount_h)( mt_entry );
      }
#else
      rv = (*mt_point_ops->unmount_h)( mt_entry );
#endif

      if ( rv == 0 ) {
        rtems_id self_task_id = rtems_task_self();
        rtems_filesystem_mt_entry_declare_lock_context( lock_context );
#ifdef RTEMSCFG_MNT_CONTAINER
        Thread_Control *executing = _Thread_Get_executing();
        bool is_container_unmount = false;
        if (executing && executing->container && executing->container->mntContainer) {
          is_container_unmount = true;

          rtems_filesystem_global_location_t *fs_root = mt_entry->mt_fs_root;
          bool has_other_refs = (fs_root && fs_root->reference_count >= 2);
          
          if (has_other_refs) {
            rtems_filesystem_mt_entry_lock( lock_context );
            rtems_chain_extract_unprotected(&mt_entry->mt_node);
            mt_entry->unmount_task = self_task_id;
            mt_entry->mounted = false;
            
            if (fs_root) {
              fs_root->reference_count--;
            }
            rtems_filesystem_mt_entry_unlock( lock_context );
          } else {
            rtems_filesystem_mt_entry_lock( lock_context );
            rtems_chain_extract_unprotected(&mt_entry->mt_node);
            mt_entry->unmount_task = self_task_id;
            mt_entry->mounted = false;
            rtems_filesystem_mt_entry_unlock( lock_context );
          }
        } else {
#endif
        rtems_filesystem_mt_entry_lock( lock_context );
        mt_entry->unmount_task = self_task_id;
        mt_entry->mounted = false;
        rtems_filesystem_mt_entry_unlock( lock_context );
#ifdef RTEMSCFG_MNT_CONTAINER
        }
#endif      
      }
    } else {
      errno = EBUSY;
      rv = -1;
    }
  } else {
    errno = EACCES;
    rv = -1;
  }

  rtems_filesystem_eval_path_cleanup( &ctx );

  if ( rv == 0 ) {
    rtems_status_code sc = rtems_event_transient_receive(
      RTEMS_WAIT,
      RTEMS_NO_TIMEOUT
    );

    if ( sc != RTEMS_SUCCESSFUL ) {
      rtems_fatal_error_occurred( 0xdeadbeef );
    }
  }

  return rv;
}
