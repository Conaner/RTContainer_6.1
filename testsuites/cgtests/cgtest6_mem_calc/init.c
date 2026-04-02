
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#include <rtems/test.h>
#include <rtems/score/chainimpl.h>
#include <rtems/score/corecgroup.h>
#include <rtems/score/corecgroupimpl.h>
#include <rtems/score/isrlock.h>
#include <rtems/score/statesimpl.h>
#include <rtems/rtems/status.h>
#include <string.h>

#include <rtems/score/watchdogimpl.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/support.h>
#include <rtems/rtems/event.h>
#include <rtems/rtems/tasks.h>
#include <rtems/assoc.h>

#include <inttypes.h>

const char rtems_test_name[] = "CGTEST6 MEM CALC";

#define CGROUP_WAIT_STATE STATES_WAITING_FOR_CGROUP_CPU_QUOTA

static CORE_cgroup_Control*  test_cgroup;
static rtems_id Init_task_id;

static CORE_cgroup_config  test_core_config = {
  .cpu_shares = 1,
  .cpu_quota = 1000,
  .cpu_period = 0,
  .memory_limit = 0,
  .blkio_limit = 0
};

static void *cache_pool[10];
static size_t cache_pool_sizes[10];
static bool is_buffer_allocated = false;

/* 
 * Shrink Callback Function
 * arg: Passed as &cache_pool[0]
 * target: Bytes needed to be freed
 */
void simple_cache_shrinker(void *arg, uintptr_t target) {
  void **pool = (void **)arg;
  printf("\n\033[33m>>> [Shrink Callback] Triggered! Need %lu bytes <<<\n", (unsigned long)target);

  if(pool[0] != NULL) {
    free(pool[0]);
    pool[0] = NULL;
    printf("\033[33m[Shrink Callback] Free memory successfully\033[0m\n");
  } else
  {
    printf("\033[33m[Shrink Callback]No memory to free\033[0m\n");
  }
}

static rtems_task worker_task(rtems_task_argument arg)
{
  /* Helper to read quota of the current thread's cgroup */
  const Thread_Control *_self = _Thread_Get_executing();
  CORE_cgroup_Control  *_cg   =
    ( _self != NULL && _self->is_added_to_cgroup ) ? _self->cgroup : NULL;

#define QUOTA_NOW() ( _cg ? (unsigned long)_cg->mem_quota_available : 0UL )

  printf("Worker task %lu started, quota=%lu\n", arg, QUOTA_NOW());

  if(!is_buffer_allocated) {
    printf("=======[Task %lu] quota before shared-2MB malloc: %lu\n", arg, QUOTA_NOW());
    cache_pool[0] = malloc( 2 * 1024 * 1024 );
    printf("=======[Task %lu] quota after  shared-2MB malloc: %lu\n", arg, QUOTA_NOW());
    if (cache_pool[0] == NULL) {
      printf("\033[31m[Task %lu]: shared buffer Memory allocation failed\033[0m\n", arg);
    } else {
      printf("\033[32m[Task %lu]: shared buffer Memory allocation succeeded\033[0m\n", arg);
      is_buffer_allocated = true;
    }
  }
  /** allocate memory */
  size_t alloc_size = cache_pool_sizes[arg];
  printf("[Task %lu]: quota before %zuMB malloc: %lu\n", arg, alloc_size>>20, QUOTA_NOW());
  cache_pool[arg] = malloc( alloc_size );
  printf("[Task %lu]: quota after  %zuMB malloc: %lu\n", arg, alloc_size>>20, QUOTA_NOW());
  if (cache_pool[arg] == NULL) {
    printf("\033[31m[Task %lu]: Memory allocation failed\033[0m\n", arg);
  } else {
    printf("\033[32m[Task %lu]: Memory allocation succeeded\033[0m\n", arg);
  }

#undef QUOTA_NOW

  rtems_event_send( Init_task_id, RTEMS_EVENT_0 << (uint32_t) arg );
  rtems_task_exit();
}

static void create_test_tasks()
{
  rtems_status_code status;

  for(int i = 0; i <= 5; i++) {
    cache_pool_sizes[i] = i * 1024 * 1024; /** 0MB, 1MB, ..., 5MB */
    status = rtems_task_create(
        rtems_build_name( 'T', 'E', 'S', 'T' ),
        9-i,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &Task_id[i]
    );
    rtems_test_assert(status == RTEMS_SUCCESSFUL);
    /* Do NOT start tasks here; they must be added to the cgroup first */
  }
}

rtems_task Init(rtems_task_argument argument)
{
  (void) argument;

  rtems_print_printer_fprintf_putc(&rtems_test_printer);
  TEST_BEGIN();

  // test_core_config.cpu_period = rtems_clock_get_ticks_per_second() * 5;
  test_core_config.cpu_quota = rtems_clock_get_ticks_per_second() * 500;
  test_core_config.cpu_period = _Watchdog_Ticks_per_second * 500;
  test_core_config.memory_limit = 10 * 1024 * 1024; /** 10 MB */
  printf("==== ticks per second are %lu ====\n", rtems_clock_get_ticks_per_second());

  /** Create a new cgroup */
  rtems_name cg_name = rtems_build_name( 'C', 'G', '2', ' ' );
  rtems_id cg_id;
  rtems_cgroup_create( cg_name, &cg_id, &test_core_config );

  ISR_lock_Context lock_context;
  Cgroup_Control*  the_cgroup = _Cgroup_Get(cg_id, &lock_context);
  if (the_cgroup) {
    the_cgroup->cgroup.shrink_callback = simple_cache_shrinker;
    the_cgroup->cgroup.shrink_arg = (void *)cache_pool;
    printf("Shrink callback registered.\n");
  }
  _ISR_lock_ISR_enable( &lock_context );

  create_test_tasks();

  rtems_status_code status;

  /* Add tasks to the cgroup BEFORE starting them.
   * Worker tasks have higher priority (4-9) than Init (10), so they would
   * preempt Init immediately upon being started.  If started first, they run
   * without cgroup membership and the memory quota / shrink callback never
   * triggers.  Create all tasks, add them to the cgroup, then start them. */
  for(int i = 0; i <= 5; i++) {
    status = rtems_cgroup_add_task(cg_id, Task_id[i]);
    rtems_test_assert(status == RTEMS_SUCCESSFUL);
  }

  test_cgroup = &the_cgroup->cgroup;
  Init_task_id = rtems_task_self();

  for(int i = 0; i <= 5; i++) {
    status = rtems_task_start(Task_id[i], worker_task, i);
    rtems_test_assert(status == RTEMS_SUCCESSFUL);
  }

  // verify_quota_consumption();

  printf("=====> Init waiting for all tasks to send completion events <=====\n");
  rtems_event_set received;
  rtems_event_receive(
    RTEMS_EVENT_0 | RTEMS_EVENT_1 | RTEMS_EVENT_2 |
    RTEMS_EVENT_3 | RTEMS_EVENT_4 | RTEMS_EVENT_5,
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );

  printf("---- Test Completed Successfully ----\n");

  TEST_END();
  rtems_test_exit(0);
}