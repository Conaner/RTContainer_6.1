#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#include <inttypes.h>

#include <rtems/test.h>
#include <rtems/score/watchdogimpl.h>
#include <rtems/score/corecgroup.h>
#include <rtems/score/isrlock.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/event.h>
#include <rtems/rtems/tasks.h>

const char rtems_test_name[] = "CGTEST6-4 MEM CALC";

#define TEST_TASK_COUNT   6
#define TEST_CGROUP_COUNT 3
#define SHARED_BUFFER_SIZE ( 2 * 1024 * 1024 )

/*
 * Three cgroup scenario:
 *   CG1 (limit=6MB): task 0 alloc 2MB shared + 2MB own = 4MB,
 *                    task 1 alloc 1MB -> total 5MB < 6MB -> NO shrink
 *   CG2 (limit=5MB): task 2 alloc 2MB shared + 2MB own = 4MB,
 *                    task 3 alloc 2MB -> 6MB > 5MB -> shrink frees 2MB -> 4MB -> OK
 *   CG3 (limit=8MB): task 4 alloc 2MB shared + 3MB own = 5MB,
 *                    task 5 alloc 2MB -> total 7MB < 8MB -> NO shrink
 */
typedef struct {
  const char *name;
  rtems_id    id;
  void       *shared_buffer;
  bool        shared_allocated;
  bool        shrink_called;
} Memcg_Test_Context;

static rtems_id Init_task_id;
static Memcg_Test_Context Groups[ TEST_CGROUP_COUNT ] = {
  { .name = "CG1" },
  { .name = "CG2" },
  { .name = "CG3" }
};
static void *Task_buffer[ TEST_TASK_COUNT ];
static const uint32_t Task_to_group[ TEST_TASK_COUNT ]       = { 0, 0, 1, 1, 2, 2 };
static const uint32_t Shared_owner_task[ TEST_CGROUP_COUNT ] = { 0, 2, 4 };
static const size_t   Task_alloc_size[ TEST_TASK_COUNT ] = {
  2 * 1024 * 1024,   /* task 0: CG1 own (+ 2MB shared) */
  1 * 1024 * 1024,   /* task 1: CG1 own                */
  2 * 1024 * 1024,   /* task 2: CG2 own (+ 2MB shared) */
  2 * 1024 * 1024,   /* task 3: CG2 own -> triggers shrink */
  3 * 1024 * 1024,   /* task 4: CG3 own (+ 2MB shared) */
  2 * 1024 * 1024    /* task 5: CG3 own                */
};

static rtems_event_set completion_mask( void )
{
  rtems_event_set mask = 0;

  for ( uint32_t i = 0; i < TEST_TASK_COUNT; ++i ) {
    mask |= RTEMS_EVENT_0 << i;
  }

  return mask;
}

static void group_shrinker( void *arg, uintptr_t target )
{
  Memcg_Test_Context *ctx = arg;

  ctx->shrink_called = true;
  printf(
    "\033[33m>>> [%s Shrink Callback] Triggered! Need %lu bytes <<<\033[0m\n",
    ctx->name,
    (unsigned long) target
  );

  if ( ctx->shared_buffer != NULL ) {
    free( ctx->shared_buffer );
    ctx->shared_buffer = NULL;
    ctx->shared_allocated = false;
    printf( "\033[33m[%s Shrink Callback] Freed shared 2MB buffer\033[0m\n", ctx->name );
  } else {
    printf( "\033[33m[%s Shrink Callback] No memory to free\033[0m\n", ctx->name );
  }
}

static rtems_task worker_task( rtems_task_argument arg )
{
  uint32_t task_index = (uint32_t) arg;
  uint32_t group_index = Task_to_group[ task_index ];
  Memcg_Test_Context *ctx = &Groups[ group_index ];
  const Thread_Control *self = _Thread_Get_executing();
  CORE_cgroup_Control *cg =
    ( self != NULL && self->is_added_to_cgroup ) ? self->cgroup : NULL;
  unsigned long quota_now = cg != NULL ? (unsigned long) cg->mem_quota_available : 0UL;

  printf( "[Task %lu] started in %s, quota=%lu\n", arg, ctx->name, quota_now );

  if ( task_index == Shared_owner_task[ group_index ] ) {
    ctx->shared_buffer = malloc( SHARED_BUFFER_SIZE );
    ctx->shared_allocated = ctx->shared_buffer != NULL;
    printf(
      ctx->shared_buffer != NULL
        ? "\033[32m[%s] shared 2MB allocation succeeded\033[0m\n"
        : "\033[31m[%s] shared 2MB allocation failed\033[0m\n",
      ctx->name
    );
    rtems_test_assert( ctx->shared_buffer != NULL );
  }

  Task_buffer[ task_index ] = malloc( Task_alloc_size[ task_index ] );
  if ( Task_buffer[ task_index ] != NULL ) {
    printf(
      "\033[32m[Task %lu] %s %zuMB allocation succeeded\033[0m\n",
      arg,
      ctx->name,
      Task_alloc_size[ task_index ] >> 20
    );
  } else {
    printf(
      "\033[31m[Task %lu] %s %zuMB allocation failed\033[0m\n",
      arg,
      ctx->name,
      Task_alloc_size[ task_index ] >> 20
    );
  }
  rtems_test_assert( Task_buffer[ task_index ] != NULL );

  rtems_event_send( Init_task_id, RTEMS_EVENT_0 << task_index );
  rtems_task_exit();
}

rtems_task Init( rtems_task_argument ignored )
{
  (void) ignored;

  rtems_print_printer_fprintf_putc( &rtems_test_printer );
  TEST_BEGIN();

  CORE_cgroup_config configs[ TEST_CGROUP_COUNT ] = {
    {
      .cpu_shares = 1,
      .cpu_quota  = _Watchdog_Ticks_per_second * 60,
      .cpu_period = _Watchdog_Ticks_per_second * 60,
      .memory_limit = 6 * 1024 * 1024,
      .blkio_limit  = 0
    },
    {
      .cpu_shares = 1,
      .cpu_quota  = _Watchdog_Ticks_per_second * 60,
      .cpu_period = _Watchdog_Ticks_per_second * 60,
      .memory_limit = 5 * 1024 * 1024,
      .blkio_limit  = 0
    },
    {
      .cpu_shares = 1,
      .cpu_quota  = _Watchdog_Ticks_per_second * 60,
      .cpu_period = _Watchdog_Ticks_per_second * 60,
      .memory_limit = 8 * 1024 * 1024,
      .blkio_limit  = 0
    }
  };
  rtems_status_code status;

  for ( uint32_t i = 0; i < TEST_CGROUP_COUNT; ++i ) {
    status = rtems_cgroup_create(
      rtems_build_name( 'C', 'G', '1' + i, ' ' ),
      &Groups[ i ].id,
      &configs[ i ]
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );
    printf( "Config: %s memory_limit=%" PRIu64 " bytes\n", Groups[ i ].name, configs[ i ].memory_limit );

    ISR_lock_Context lock_context;
    Cgroup_Control *the_cgroup = _Cgroup_Get( Groups[ i ].id, &lock_context );
    rtems_test_assert( the_cgroup != NULL );
    the_cgroup->cgroup.shrink_callback = group_shrinker;
    the_cgroup->cgroup.shrink_arg = &Groups[ i ];
    _ISR_lock_ISR_enable( &lock_context );
  }

  Init_task_id = rtems_task_self();

  for ( uint32_t i = 0; i < TEST_TASK_COUNT; ++i ) {
    uint32_t group_index = Task_to_group[ i ];

    status = rtems_task_create(
      rtems_build_name( '6', '4', 'T', '0' + i ),
      9 - i,
      RTEMS_MINIMUM_STACK_SIZE,
      RTEMS_DEFAULT_MODES,
      RTEMS_DEFAULT_ATTRIBUTES,
      &Task_id[ i ]
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    status = rtems_cgroup_add_task( Groups[ group_index ].id, Task_id[ i ] );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    printf( "Assign task %lu -> %s\n", (unsigned long) i, Groups[ group_index ].name );

    status = rtems_task_start( Task_id[ i ], worker_task, i );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );
  }

  printf( "Init waiting for %d completion events\n", TEST_TASK_COUNT );
  rtems_event_set received;
  status = rtems_event_receive(
    completion_mask(),
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );
  rtems_test_assert( !Groups[ 0 ].shrink_called );
  rtems_test_assert(  Groups[ 1 ].shrink_called );
  rtems_test_assert( !Groups[ 2 ].shrink_called );

  printf(
    "CGTEST6-4 completed: CG1 and CG3 stayed within quota, CG2 required shrink and still completed\n"
  );

  TEST_END();
  rtems_test_exit( 0 );
}
