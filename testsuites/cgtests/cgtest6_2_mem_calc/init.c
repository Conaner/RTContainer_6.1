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

const char rtems_test_name[] = "CGTEST6-2 MEM CALC";

#define TEST_TASK_COUNT 3
#define SHARED_BUFFER_OWNER 0
#define SHARED_BUFFER_SIZE ( 2 * 1024 * 1024 )

static rtems_id Init_task_id;
static bool Shrink_called;
static bool Shared_buffer_allocated;
static void *Shared_buffer;
static void *Task_buffer[ TEST_TASK_COUNT ];
static const size_t Task_alloc_size[ TEST_TASK_COUNT ] = {
  3 * 1024 * 1024,
  2 * 1024 * 1024,
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

static void unexpected_shrinker( void *arg, uintptr_t target )
{
  (void) arg;
  Shrink_called = true;
  printf( "\033[31mUnexpected shrink callback triggered, need %lu bytes\033[0m\n", (unsigned long) target );
}

static rtems_task worker_task( rtems_task_argument arg )
{
  uint32_t task_index = (uint32_t) arg;
  const Thread_Control *self = _Thread_Get_executing();
  CORE_cgroup_Control *cg =
    ( self != NULL && self->is_added_to_cgroup ) ? self->cgroup : NULL;
  unsigned long quota_now = cg != NULL ? (unsigned long) cg->mem_quota_available : 0UL;

  printf( "Worker task %lu started, quota=%lu\n", arg, quota_now );

  if ( task_index == SHARED_BUFFER_OWNER ) {
    Shared_buffer = malloc( SHARED_BUFFER_SIZE );
    Shared_buffer_allocated = Shared_buffer != NULL;
    printf(
      Shared_buffer != NULL
        ? "\033[32mShared 2MB allocation succeeded\033[0m\n"
        : "\033[31mShared 2MB allocation failed\033[0m\n"
    );
    rtems_test_assert( Shared_buffer != NULL );
  }

  Task_buffer[task_index] = malloc( Task_alloc_size[task_index] );
  if ( Task_buffer[task_index] != NULL ) {
    printf(
      "\033[32mTask %lu %zuMB allocation succeeded\033[0m\n",
      arg,
      Task_alloc_size[task_index] >> 20
    );
  } else {
    printf(
      "\033[31mTask %lu %zuMB allocation failed\033[0m\n",
      arg,
      Task_alloc_size[task_index] >> 20
    );
  }
  rtems_test_assert( Task_buffer[task_index] != NULL );

  rtems_event_send( Init_task_id, RTEMS_EVENT_0 << task_index );
  rtems_task_exit();
}

rtems_task Init( rtems_task_argument ignored )
{
  (void) ignored;

  rtems_print_printer_fprintf_putc( &rtems_test_printer );
  TEST_BEGIN();

  CORE_cgroup_config config = {
    .cpu_shares = 1,
    .cpu_quota = _Watchdog_Ticks_per_second * 60,
    .cpu_period = _Watchdog_Ticks_per_second * 60,
    .memory_limit = 8 * 1024 * 1024,
    .blkio_limit = 0
  };
  rtems_id cg_id;
  rtems_status_code status;

  status = rtems_cgroup_create( rtems_build_name( 'C', 'G', '2', ' ' ), &cg_id, &config );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );
  printf( "Config: 1 cgroup, %d tasks, memory_limit=%" PRIu64 " bytes\n", TEST_TASK_COUNT, config.memory_limit );

  ISR_lock_Context lock_context;
  Cgroup_Control *the_cgroup = _Cgroup_Get( cg_id, &lock_context );
  rtems_test_assert( the_cgroup != NULL );
  the_cgroup->cgroup.shrink_callback = unexpected_shrinker;
  the_cgroup->cgroup.shrink_arg = NULL;
  _ISR_lock_ISR_enable( &lock_context );

  Init_task_id = rtems_task_self();

  for ( uint32_t i = 0; i < TEST_TASK_COUNT; ++i ) {
    status = rtems_task_create(
      rtems_build_name( '6', '2', 'T', '0' + i ),
      9 - i,
      RTEMS_MINIMUM_STACK_SIZE,
      RTEMS_DEFAULT_MODES,
      RTEMS_DEFAULT_ATTRIBUTES,
      &Task_id[i]
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    status = rtems_cgroup_add_task( cg_id, Task_id[i] );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

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
  rtems_test_assert( !Shrink_called );

  printf( "All allocations finished without shrink in CGTEST6-2\n" );

  TEST_END();
  rtems_test_exit( 0 );
}
