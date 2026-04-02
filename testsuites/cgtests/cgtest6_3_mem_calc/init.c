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

const char rtems_test_name[] = "CGTEST6-3 MEM CALC";

#define TEST_TASK_COUNT 4
#define TEST_CGROUP_COUNT 2
#define SHARED_BUFFER_SIZE ( 2 * 1024 * 1024 )

typedef struct {
  const char *name;
  rtems_id id;
  void *shared_buffer;
  bool shared_allocated;
  bool shrink_called;
} Memcg_Test_Context;

static rtems_id Init_task_id;
static Memcg_Test_Context Groups[ TEST_CGROUP_COUNT ] = {
  { .name = "CG1" },
  { .name = "CG2" }
};
static void *Task_buffer[ TEST_TASK_COUNT ];
static const uint32_t Task_to_group[ TEST_TASK_COUNT ] = { 0, 0, 1, 1 };
static const uint32_t Shared_owner_task[ TEST_CGROUP_COUNT ] = { 0, 2 };
static const size_t Task_alloc_size[ TEST_TASK_COUNT ] = {
  3 * 1024 * 1024,
  1 * 1024 * 1024,
  4 * 1024 * 1024,
  1 * 1024 * 1024
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
  printf( ">>> [%s Shrink Callback] Triggered! Need %lu bytes <<<\n", ctx->name, (unsigned long) target );

  if ( ctx->shared_buffer != NULL ) {
    free( ctx->shared_buffer );
    ctx->shared_buffer = NULL;
    ctx->shared_allocated = false;
    printf( "[%s Shrink Callback] Freed shared 2MB buffer\n", ctx->name );
  } else {
    printf( "[%s Shrink Callback] No memory to free\n", ctx->name );
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
      "[%s] shared 2MB allocation %s\n",
      ctx->name,
      ctx->shared_buffer != NULL ? "succeeded" : "failed"
    );
    rtems_test_assert( ctx->shared_buffer != NULL );
  }

  Task_buffer[task_index] = malloc( Task_alloc_size[task_index] );
  printf(
    "[Task %lu] %s %zuMB allocation %s\n",
    arg,
    ctx->name,
    Task_alloc_size[task_index] >> 20,
    Task_buffer[task_index] != NULL ? "succeeded" : "failed"
  );
  rtems_test_assert( Task_buffer[task_index] != NULL );

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
      .cpu_quota = _Watchdog_Ticks_per_second * 60,
      .cpu_period = _Watchdog_Ticks_per_second * 60,
      .memory_limit = 6 * 1024 * 1024,
      .blkio_limit = 0
    },
    {
      .cpu_shares = 1,
      .cpu_quota = _Watchdog_Ticks_per_second * 60,
      .cpu_period = _Watchdog_Ticks_per_second * 60,
      .memory_limit = 6 * 1024 * 1024,
      .blkio_limit = 0
    }
  };
  rtems_status_code status;

  for ( uint32_t i = 0; i < TEST_CGROUP_COUNT; ++i ) {
    status = rtems_cgroup_create(
      rtems_build_name( 'C', 'G', '1' + i, ' ' ),
      &Groups[i].id,
      &configs[i]
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );
    printf( "Config: %s memory_limit=%" PRIu64 " bytes\n", Groups[i].name, configs[i].memory_limit );

    ISR_lock_Context lock_context;
    Cgroup_Control *the_cgroup = _Cgroup_Get( Groups[i].id, &lock_context );
    rtems_test_assert( the_cgroup != NULL );
    the_cgroup->cgroup.shrink_callback = group_shrinker;
    the_cgroup->cgroup.shrink_arg = &Groups[i];
    _ISR_lock_ISR_enable( &lock_context );
  }

  Init_task_id = rtems_task_self();

  for ( uint32_t i = 0; i < TEST_TASK_COUNT; ++i ) {
    uint32_t group_index = Task_to_group[i];

    status = rtems_task_create(
      rtems_build_name( '6', '3', 'T', '0' + i ),
      9 - i,
      RTEMS_MINIMUM_STACK_SIZE,
      RTEMS_DEFAULT_MODES,
      RTEMS_DEFAULT_ATTRIBUTES,
      &Task_id[i]
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    status = rtems_cgroup_add_task( Groups[group_index].id, Task_id[i] );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    printf( "Assign task %lu -> %s\n", (unsigned long) i, Groups[group_index].name );

    status = rtems_task_start( Task_id[i], worker_task, i );
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
  rtems_test_assert( !Groups[0].shrink_called );
  rtems_test_assert( Groups[1].shrink_called );

  printf( "CGTEST6-3 completed: CG1 stayed within quota, CG2 required shrink and still completed\n" );

  TEST_END();
  rtems_test_exit( 0 );
}
