#include <rtems/score/mntContainer.h>
#include <rtems/score/container.h>
#include <rtems/score/chain.h>
#include <rtems/score/threadimpl.h>
#include <rtems/libio_.h>
#include <string.h>
#include <stdlib.h>

#include <stdio.h>
#include <inttypes.h>
#include <rtems/rtems/intr.h>

static int g_mntContainerId = 1;
static const char *current_eval_path = NULL;
static const char *adjusted_eval_path = NULL;


void set_current_eval_path(const char *path)
{
    current_eval_path = path;
    adjusted_eval_path = NULL;
}


void clear_current_eval_path(void)
{
    current_eval_path = NULL;
    adjusted_eval_path = NULL;
}

const char *get_adjusted_eval_path(void)
{
    return adjusted_eval_path;
}

int rtems_mnt_container_initialize_root(MntContainer **mntContainer)
{
    static MntContainer *rootMntContainer = NULL;
    if (!mntContainer)
        return -1;

    if (rootMntContainer == NULL)
    {
        rootMntContainer = (MntContainer *)malloc(sizeof(MntContainer));
        if (!rootMntContainer)
            return -1;
        rootMntContainer->rc = 1;
        rootMntContainer->ID = 1;
        rtems_chain_initialize_empty(&rootMntContainer->mountList);
    }

    *mntContainer = rootMntContainer;
    return 0;
}

static rtems_filesystem_mount_table_entry_t *clone_mount_entry(
    rtems_filesystem_mount_table_entry_t *original)
{
    if (!original)
        return NULL;

    rtems_filesystem_mount_table_entry_t *clone = 
        (rtems_filesystem_mount_table_entry_t *)malloc(sizeof(rtems_filesystem_mount_table_entry_t));
    if (!clone)
        return NULL;

    memcpy(clone, original, sizeof(rtems_filesystem_mount_table_entry_t));
    

    if (original->target) {
        clone->target = strdup(original->target);
        if (!clone->target) {
            free(clone);
            return NULL;
        }
    }
    
    if (original->type) {
        clone->type = strdup(original->type);
        if (!clone->type) {
            free((void *)clone->target);
            free(clone);
            return NULL;
        }
    }
    
    if (original->mt_fs_root && !rtems_filesystem_global_location_is_null(original->mt_fs_root)) {
        rtems_filesystem_global_location_t *temp_loc = original->mt_fs_root;
        clone->mt_fs_root = rtems_filesystem_global_location_obtain(&temp_loc);
        if (clone->mt_fs_root && !rtems_filesystem_global_location_is_null(clone->mt_fs_root)) {
            clone->mounted = true;
            clone->unmount_task = 0;
        } else {
            clone->mt_fs_root = NULL;
        }
    } else {
        clone->mt_fs_root = NULL;
    }
    
    rtems_chain_set_off_chain(&clone->mt_node);
    
    return clone;
}

static int inherit_mount_points(MntContainer *child, MntContainer *parent)
{
    if (!child || !parent)
        return -1;
        
    int inherited_count = 0;
    rtems_chain_node *node;
    
    for (node = rtems_chain_first(&parent->mountList);
         !rtems_chain_is_tail(&parent->mountList, node);
         node = rtems_chain_next(node))
    {
        rtems_filesystem_mount_table_entry_t *parent_entry =
            RTEMS_CONTAINER_OF(node, rtems_filesystem_mount_table_entry_t, mt_node);
        
        rtems_filesystem_mount_table_entry_t *child_entry = clone_mount_entry(parent_entry);
        if (child_entry) {
            rtems_chain_append_unprotected(&child->mountList, &child_entry->mt_node);
            inherited_count++;
        }
    }
    
    return inherited_count;
}

MntContainer *rtems_mnt_container_create(void)
{
    MntContainer *mntContainer = (MntContainer *)malloc(sizeof(MntContainer));
    if (!mntContainer)
        return NULL;

    mntContainer->rc = 1;
    mntContainer->ID = ++g_mntContainerId;
    rtems_chain_initialize_empty(&mntContainer->mountList);

    return mntContainer;
}

MntContainer *rtems_mnt_container_create_with_inheritance(MntContainer *parent)
{
    MntContainer *child = rtems_mnt_container_create();
    if (!child)
        return NULL;
    
    if (parent) {
        inherit_mount_points(child, parent);
    }
    
    return child;
}

static bool switch_to_root(Thread_Control *thread, void *arg)
{
    MntContainer *mntContainer = (MntContainer *)arg;
    Container *container = rtems_container_get_root();
    MntContainer *root = container->mntContainer;

    if (thread->container && thread->container->mntContainer == mntContainer)
    {
        thread->container->mntContainer = root;
        root->rc++;
        mntContainer->rc--;
    }
    return true;
}

static void cleanup_container_mounts(MntContainer *mntContainer)
{
    if (!mntContainer)
        return;
    
    rtems_chain_node *node;
    while (!rtems_chain_is_empty(&mntContainer->mountList))
    {
        node = rtems_chain_get_first_unprotected(&mntContainer->mountList);
        rtems_filesystem_mount_table_entry_t *mt_entry =
            RTEMS_CONTAINER_OF(node, rtems_filesystem_mount_table_entry_t, mt_node);
        
        if (mt_entry->mt_fs_root) {
            rtems_filesystem_global_location_release(mt_entry->mt_fs_root, true);
            mt_entry->mt_fs_root = NULL;
        }
        
        /*
         * target/type may point into the same allocation as mt_entry
         * (see alloc_mount_table_entry()), so freeing them separately can
         * trigger INVALID_HEAP_FREE. Release the mount entry as one object.
         */
        mt_entry->target = NULL;
        mt_entry->type = NULL;
        free(mt_entry);
    }
}

void rtems_mnt_container_delete(MntContainer *mntContainer)
{
    if (!mntContainer)
        return;
    Container *container = rtems_container_get_root();
    if (!container || !container->mntContainer)
        return;
    MntContainer *root = container->mntContainer;
    if (mntContainer == root)
        return;

    rtems_task_iterate(switch_to_root, mntContainer);

    Thread_Control *self = (Thread_Control *)_Thread_Get_executing();
    if (self && self->container && self->container->mntContainer == mntContainer)
    {
        self->container->mntContainer = root;
        root->rc++;
        mntContainer->rc--;
    }

    cleanup_container_mounts(mntContainer);

    rtems_mnt_container_remove_from_list(mntContainer);
    free(mntContainer);
}

void rtems_mnt_container_move_task(MntContainer *srcContainer, MntContainer *destContainer, Thread_Control *thread)
{
    if (!srcContainer || !destContainer || !thread)
        return;

    if (thread->container &&
        (thread->container->mntContainer == srcContainer ||
         (srcContainer->ID == 1 && thread->container->mntContainer->ID == 1)))
    {

        thread->container->mntContainer = destContainer;
        srcContainer->rc--;
        if (srcContainer->rc <= 0 && srcContainer->ID != 1)
        {
            rtems_mnt_container_delete(srcContainer);
        }
        destContainer->rc++;
    }
}

void rtems_mnt_container_add_to_list(MntContainer *mntContainer)
{
    if (!mntContainer)
        return;
    Container *container = rtems_container_get_root();
    if (!container)
        return;

    MntContainerNode **head = (MntContainerNode **)&container->mntContainerListHead;
    MntContainerNode *node = (MntContainerNode *)malloc(sizeof(MntContainerNode));
    if (!node)
        return;
    node->mntContainer = mntContainer;
    node->next = *head;
    *head = node;
}

void rtems_mnt_container_remove_from_list(MntContainer *mntContainer)
{
    if (!mntContainer)
        return;
    Container *container = rtems_container_get_root();
    if (!container)
        return;

    MntContainerNode **head = (MntContainerNode **)&container->mntContainerListHead;
    MntContainerNode **cur = head;
    while (*cur)
    {
        if ((*cur)->mntContainer == mntContainer)
        {
            MntContainerNode *toRemove = *cur;
            *cur = toRemove->next;
            free(toRemove);
            break;
        }
        cur = &(*cur)->next;
    }
}

int rtems_mnt_container_get_id(MntContainer *mntContainer)
{
    return mntContainer ? mntContainer->ID : -1;
}

int rtems_mnt_container_get_rc(MntContainer *mntContainer)
{
    return mntContainer ? mntContainer->rc : 0;
}

MntContainer *get_current_thread_mnt_container(void)
{
    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    Thread_Control *self = (Thread_Control *)_Thread_Get_executing();
    rtems_interrupt_enable(level);
    if (self && self->container)
    {
        return self->container->mntContainer;
    }
    return NULL;
}

MntContainer *get_current_mnt_container(void)
{
    return get_current_thread_mnt_container();
}

static bool is_mount_entry_valid(rtems_filesystem_mount_table_entry_t *mt_entry)
{
    if (!mt_entry)
        return false;
        
    if (!mt_entry->mt_fs_root)
        return false;
    
    if (!mt_entry->mounted) {
        return false;
    }

    bool has_valid_location = !rtems_filesystem_global_location_is_null(mt_entry->mt_fs_root);
    bool has_positive_refcount = mt_entry->mt_fs_root->reference_count > 0;
    bool has_valid_mt_entry = (mt_entry->mt_fs_root->location.mt_entry != NULL);
    
    if (has_positive_refcount && has_valid_location) {
        return true;
    }
    
    if (has_positive_refcount && mt_entry->target) {
        return true;
    }
    
    if (has_valid_mt_entry) {
        return true;
    }
    
    if (has_positive_refcount && has_valid_mt_entry) {
        return true;
    }
        
    return false;
}

static rtems_filesystem_mount_table_entry_t *find_best_mount_for_path(
    MntContainer *mnt, const char *path, bool *found_but_invalid)
{
    if (!mnt || !path)
        return NULL;

    if (found_but_invalid)
        *found_but_invalid = false;

    rtems_filesystem_mount_table_entry_t *best_match = NULL;
    size_t best_match_len = 0;
    bool found_invalid = false;

    rtems_chain_node *node;
    for (node = rtems_chain_first(&mnt->mountList);
         !rtems_chain_is_tail(&mnt->mountList, node);
         node = rtems_chain_next(node))
    {
        rtems_filesystem_mount_table_entry_t *mt_entry =
            RTEMS_CONTAINER_OF(node, rtems_filesystem_mount_table_entry_t, mt_node);

        if (!mt_entry->target)
            continue;

        size_t target_len = strlen(mt_entry->target);

        if (strncmp(path, mt_entry->target, target_len) == 0)
        {
            if (path[target_len] == '\0' || path[target_len] == '/')
            {
                if (target_len > best_match_len)
                {
                    bool valid = is_mount_entry_valid(mt_entry);
                    if (valid)
                    {
                        best_match = mt_entry;
                        best_match_len = target_len;
                        found_invalid = false;
                    }
                    else
                    {
                        if (best_match_len == 0)
                        {
                            found_invalid = true;
                            best_match_len = target_len;
                        }
                    }
                }
            }
        }
    }
    
    if (found_but_invalid)
        *found_but_invalid = found_invalid;
    
    return best_match;
}

rtems_filesystem_global_location_t *get_container_root_location(void)
{
#ifdef RTEMSCFG_MNT_CONTAINER
    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    Thread_Control *executing = _Thread_Get_executing();
    rtems_interrupt_enable(level);
    if (executing && executing->container && executing->container->mntContainer)
    {
        MntContainer *mnt = executing->container->mntContainer;

        if (current_eval_path)
        {
            bool found_but_invalid = false;
            rtems_filesystem_mount_table_entry_t *best_mount =
                find_best_mount_for_path(mnt, current_eval_path, &found_but_invalid);

            if (best_mount && is_mount_entry_valid(best_mount))
            {
                rtems_filesystem_global_location_t *fs_root = best_mount->mt_fs_root;
                
                size_t mount_path_len = strlen(best_mount->target);
                if (strlen(current_eval_path) > mount_path_len)
                {
                    adjusted_eval_path = current_eval_path + mount_path_len;
                }
                else if (strlen(current_eval_path) == mount_path_len)
                {
                    adjusted_eval_path = "/";
                }
                else
                {
                    adjusted_eval_path = current_eval_path;
                }
                
                if (fs_root == NULL || rtems_filesystem_global_location_is_null(fs_root)) {
                    return &rtems_filesystem_global_location_null;
                }
                ++fs_root->reference_count;
                return fs_root;
            }
            
            if (found_but_invalid)
            {
                return &rtems_filesystem_global_location_null;
            }
        }

        rtems_chain_node *node;
        for (node = rtems_chain_first(&mnt->mountList);
             !rtems_chain_is_tail(&mnt->mountList, node);
             node = rtems_chain_next(node))
        {
            rtems_filesystem_mount_table_entry_t *mt_entry =
                RTEMS_CONTAINER_OF(node, rtems_filesystem_mount_table_entry_t, mt_node);

            if (mt_entry->target && strcmp(mt_entry->target, "/") == 0 && is_mount_entry_valid(mt_entry))
            {
                rtems_filesystem_global_location_t *fs_root = mt_entry->mt_fs_root;
                if (fs_root == NULL || rtems_filesystem_global_location_is_null(fs_root)) {
                    return &rtems_filesystem_global_location_null;
                }
                ++fs_root->reference_count;
                return fs_root;
            }
        }

        return &rtems_filesystem_global_location_null;
    }
    
    ++rtems_filesystem_root->reference_count;
    return rtems_filesystem_root;
#endif
    ++rtems_filesystem_root->reference_count;
    return rtems_filesystem_root;
}

rtems_filesystem_global_location_t *get_container_current_location(void)
{
#ifdef RTEMSCFG_MNT_CONTAINER
    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    Thread_Control *executing = _Thread_Get_executing();
    rtems_interrupt_enable(level);
    
    if (executing && executing->user_environment)
    {
        rtems_filesystem_global_location_t *current_dir = 
            executing->user_environment->current_directory;
        if (current_dir != NULL &&
            !rtems_filesystem_global_location_is_null(current_dir) &&
            current_dir->location.mt_entry != NULL)
        {
            ++current_dir->reference_count;
            return current_dir;
        }
    }
    
    // 如果没有设置当前目录,回退到容器根目录作为默认当前目录
    return get_container_root_location();
#endif
    
    // 非容器模式下,返回全局 current_directory
    rtems_user_env_t *env = rtems_current_user_env_get();
    rtems_filesystem_global_location_t *current = env->current_directory;
    ++current->reference_count;
    return current;
}