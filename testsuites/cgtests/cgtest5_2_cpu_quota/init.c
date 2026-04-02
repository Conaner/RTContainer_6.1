#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#include <inttypes.h>

#include <rtems/test.h>
#include <rtems/score/statesimpl.h>
#include <rtems/score/watchdogimpl.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/event.h>
#include <rtems/rtems/support.h>
#include <rtems/rtems/tasks.h>

const char rtems_test_name[] = "CGTEST5-2 CPU QUOTA";

#define TEST_TASK_COUNT 4
#define MONITOR_EVENT RTEMS_EVENT_4

static rtems_id Init_task_id;
static rtems_id Monitor_task_id;
static rtems_id Cgroup_id;
static Per_CPU_Control *test_cpu;
static bool Task_completed[ TEST_TASK_COUNT ];
static bool Task_waiting_for_quota[ TEST_TASK_COUNT ];

static uint64_t current_ticks( void )
{
  return test_cpu->Watchdog.ticks;
}

static void busy_cpu_for_ticks( uint64_t ticks )
{
  uint64_t ticks_per_second = _Watchdog_Ticks_per_second;
  time_t seconds = (time_t) ( ticks / ticks_per_second );
  uint64_t remainder = ticks % ticks_per_second;
  long nanoseconds = (long) ( ( remainder * 1000000000ULL ) / ticks_per_second );

  rtems_test_busy_cpu_usage( seconds, nanoseconds );
}

static rtems_event_set completion_mask( void )
{
  return RTEMS_EVENT_0 | RTEMS_EVENT_1 | RTEMS_EVENT_2 |
         RTEMS_EVENT_3 | MONITOR_EVENT;
}

static void log_task_quota_state_change( uint32_t task_index, bool waiting )
{
  printf(
    "[Ticks:%" PRIu64 "] Task %" PRIu32 " %s cgroup CPU quota throttling in CG1\n",
    current_ticks(),
    task_index,
    waiting ? "entered" : "left"
  );
}

static rtems_task monitor_task( rtems_task_argument ignored )
{
  (void) ignored;

  while ( true ) {
    bool all_completed = true;

    for ( uint32_t i = 0; i < TEST_TASK_COUNT; ++i ) {
      ISR_lock_Context lock_context;
      Thread_Control *thread;
      States_Control state = STATES_READY;
      bool waiting;

      if ( !Task_completed[ i ] ) {
        all_completed = false;
      }

      if ( Task_id[ i ] == 0 ) {
        continue;
      }

      thread = _Thread_Get( Task_id[ i ], &lock_context );
      if ( thread != NULL ) {
        state = thread->current_state;
      }
      _ISR_lock_ISR_enable( &lock_context );

      waiting = ( state & STATES_WAITING_FOR_CGROUP_CPU_QUOTA ) != 0;
      if ( waiting != Task_waiting_for_quota[ i ] ) {
        Task_waiting_for_quota[ i ] = waiting;
        log_task_quota_state_change( i, waiting );
      }
    }

    if ( all_completed ) {
      break;
    }

    rtems_task_wake_after( 1 );
  }

  rtems_event_send( Init_task_id, MONITOR_EVENT );
  rtems_task_exit();
}

static rtems_task task_entry( rtems_task_argument arg )
{
  uint32_t task_index = (uint32_t) arg;
  ISR_lock_Context lock_context;
  Cgroup_Control *cgroup;
  CORE_cgroup_Control *core_cg;
  uint64_t cpu_quota_available;

  printf( "[Ticks:%" PRIu64 "] Task %" PRIu32 " started in CG1\n", current_ticks(), task_index );

  busy_cpu_for_ticks( ( task_index + 1 ) * _Watchdog_Ticks_per_second );

  cgroup = _Cgroup_Get( Cgroup_id, &lock_context );
  rtems_test_assert( cgroup != NULL );
  _ISR_lock_ISR_enable( &lock_context );
  core_cg = &cgroup->cgroup;
  cpu_quota_available = core_cg->cpu_quota_available;

  printf(
    "[Ticks:%" PRIu64 "] Task %" PRIu32 " finished in CG1, cgroup quota available=%" PRIu64 " ticks\n",
    current_ticks(),
    task_index,
    cpu_quota_available
  );

  Task_completed[ task_index ] = true;
  rtems_event_send( Init_task_id, RTEMS_EVENT_0 << task_index );
  rtems_task_exit();
}

rtems_task Init( rtems_task_argument ignored )
{
  (void) ignored;

  rtems_print_printer_fprintf_putc( &rtems_test_printer );
  TEST_BEGIN();

  test_cpu = _Per_CPU_Get_by_index( 0 );

  uint32_t ticks_per_second = _Watchdog_Ticks_per_second;
  CORE_cgroup_config config = {
    .cpu_shares = 0,
    .cpu_quota = ticks_per_second,
    .cpu_period = ticks_per_second * 3,
    .memory_limit = 0,
    .blkio_limit = 0
  };
  rtems_status_code status;
  rtems_event_set received;

  status = rtems_cgroup_create(
    rtems_build_name( 'C', 'G', '1', ' ' ),
    &Cgroup_id,
    &config
  );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  printf(
    "[Ticks:%" PRIu64 "] Config: 1 cgroup, %d tasks, quota=%" PRIu64 " ticks, period=%" PRIu64 " ticks\n",
    current_ticks(),
    TEST_TASK_COUNT,
    config.cpu_quota,
    config.cpu_period
  );

  Init_task_id = rtems_task_self();

  for ( uint32_t i = 0; i < TEST_TASK_COUNT; ++i ) {
    ISR_lock_Context lock_context;
    Cgroup_Control *cgroup;
    CORE_cgroup_Control *core_cg;

    status = rtems_task_create(
      rtems_build_name( '5', '2', 'T', '0' + i ),
      20 - i,
      RTEMS_MINIMUM_STACK_SIZE,
      RTEMS_DEFAULT_MODES,
      RTEMS_DEFAULT_ATTRIBUTES,
      &Task_id[ i ]
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    status = rtems_cgroup_add_task( Cgroup_id, Task_id[ i ] );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    cgroup = _Cgroup_Get( Cgroup_id, &lock_context );
    rtems_test_assert( cgroup != NULL );
    _ISR_lock_ISR_enable( &lock_context );
    core_cg = &cgroup->cgroup;
    printf(
      "[Ticks:%" PRIu64 "] Task %" PRIu32 " assigned to CG1, quota available=%" PRIu64 " ticks\n",
      current_ticks(),
      i,
      core_cg->cpu_quota_available
    );

    status = rtems_task_start( Task_id[ i ], task_entry, i );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );
  }

  status = rtems_task_create(
    rtems_build_name( 'M', 'O', 'N', '2' ),
    8,
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &Monitor_task_id
  );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  status = rtems_task_start( Monitor_task_id, monitor_task, 0 );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  printf( "[Ticks:%" PRIu64 "] Init waiting for all tasks and monitor events\n", current_ticks() );
  status = rtems_event_receive(
    completion_mask(),
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  printf( "[Ticks:%" PRIu64 "] All %d tasks finished in CGTEST5-2\n", current_ticks(), TEST_TASK_COUNT );

  TEST_END();
  rtems_test_exit( 0 );
}
