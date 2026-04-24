 #ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#include <inttypes.h>
#include <string.h>

#include <rtems/score/statesimpl.h>
#include <rtems/score/watchdogimpl.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/event.h>
#include <rtems/rtems/support.h>
#include <rtems/rtems/tasks.h>

#ifndef STATES_WAITING_FOR_CGROUP_CPU_QUOTA
#define STATES_WAITING_FOR_CGROUP_CPU_QUOTA 0x00040000
#endif

typedef struct {
  uint32_t case_index;
  const char *name;
  uint32_t task_count;
  uint32_t cgroup_count;
  bool use_start_barrier;
  CORE_cgroup_config configs[ 4 ];
  uint32_t task_to_group[ 8 ];
  uint32_t task_busy_multiplier[ 8 ];
} cpu_quota_case;

typedef struct cpu_quota_case_runtime cpu_quota_case_runtime;

typedef struct {
  cpu_quota_case_runtime *runtime;
  uint32_t task_index;
} cpu_quota_task_context;

struct cpu_quota_case_runtime {
  cpu_quota_case plan;
  rtems_id cgroup_id[ 4 ];
  rtems_id task_id[ 8 ];
  cpu_quota_task_context task_ctx[ 8 ];
  rtems_id monitor_task_id;
  bool task_completed[ 8 ];
  bool task_waiting_for_quota[ 8 ];
  bool timed_out;
};

static const char rtems_test_name[] = "CGTEST5 ALL CPU QUOTA";

#define START_BARRIER_EVENT RTEMS_EVENT_31

rtems_id   Task_id[ 16 ];
rtems_name Task_name[ 16 ];
rtems_name Semaphore_name[ 4 ];
rtems_id   Semaphore_id[ 4 ];
rtems_name Queue_name[ 3 ];
rtems_id   Queue_id[ 3 ];
rtems_name Port_name[ 2 ];
rtems_id   Port_id[ 2 ];
rtems_name Period_name[ 2 ];
rtems_id   Period_id[ 2 ];
rtems_id   Junk_id;

static rtems_id Init_task_id;
static Per_CPU_Control *test_cpu;
static bool Any_case_timeout;

static rtems_event_set task_completion_mask( const cpu_quota_case *tc )
{
  rtems_event_set mask = 0;

  for ( uint32_t i = 0; i < tc->task_count; ++i ) {
    mask |= RTEMS_EVENT_0 << i;
  }

  return mask;
}

static rtems_event_set full_completion_mask( const cpu_quota_case *tc )
{
  return task_completion_mask( tc ) | ( RTEMS_EVENT_0 << tc->task_count );
}

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

static void log_case2_task_snapshot(
  const cpu_quota_case_runtime *runtime,
  const cpu_quota_case *plan,
  uint32_t task_index,
  uint32_t group_index,
  const char *label
)
{
  ISR_lock_Context lock_context;
  Thread_Control *thread;
  States_Control state = STATES_READY;
  Cgroup_Control *snapshot_cgroup;

  thread = _Thread_Get( runtime->task_id[ task_index ], &lock_context );
  if ( thread != NULL ) {
    state = thread->current_state;
  }
  _ISR_lock_ISR_enable( &lock_context );

  snapshot_cgroup = _Cgroup_Get( runtime->cgroup_id[ group_index ], &lock_context );
  rtems_test_assert( snapshot_cgroup != NULL );
  _ISR_lock_ISR_enable( &lock_context );

  printf(
    "[Ticks:%" PRIu64 "] %s task %" PRIu32 " %s: state=0x%08" PRIx32 " completed=%d waiting_quota=%d quota_available=%" PRIu64 "\n",
    current_ticks(),
    plan->name,
    task_index,
    label,
    (uint32_t) state,
    runtime->task_completed[ task_index ] ? 1 : 0,
    runtime->task_waiting_for_quota[ task_index ] ? 1 : 0,
    snapshot_cgroup->cgroup.cpu_quota_available
  );
}

static void busy_cpu_for_ticks_with_step_logs(
  const cpu_quota_case_runtime *runtime,
  const cpu_quota_case *plan,
  uint32_t task_index,
  uint32_t group_index,
  uint64_t total_ticks
)
{
  uint64_t step_ticks = _Watchdog_Ticks_per_second / 20;
  uint64_t remaining_ticks = total_ticks;
  uint64_t step_index = 0;
  uint64_t step_count;

  if ( step_ticks == 0 ) {
    step_ticks = 1;
  }

  step_count = ( total_ticks + step_ticks - 1 ) / step_ticks;

  while ( remaining_ticks > 0 ) {
    uint64_t chunk_ticks = remaining_ticks < step_ticks ? remaining_ticks : step_ticks;

    log_case2_task_snapshot( runtime, plan, task_index, group_index, "before chunk" );

    printf(
      "[Ticks:%" PRIu64 "] %s task %" PRIu32 " busy chunk %" PRIu64 "/%" PRIu64 " start (%" PRIu64 " ticks)\n",
      current_ticks(),
      plan->name,
      task_index,
      step_index + 1,
      step_count,
      chunk_ticks
    );

    busy_cpu_for_ticks( chunk_ticks );

    log_case2_task_snapshot( runtime, plan, task_index, group_index, "after chunk" );

    printf(
      "[Ticks:%" PRIu64 "] %s task %" PRIu32 " busy chunk %" PRIu64 " done (%" PRIu64 " ticks)\n",
      current_ticks(),
      plan->name,
      task_index,
      step_index + 1,
      chunk_ticks
    );

    remaining_ticks -= chunk_ticks;
    ++step_index;
  }
}

static uint64_t case_cooldown_ticks( const cpu_quota_case *plan )
{
  uint64_t max_period = 0;

  for ( uint32_t i = 0; i < plan->cgroup_count; ++i ) {
    if ( plan->configs[ i ].cpu_period > max_period ) {
      max_period = plan->configs[ i ].cpu_period;
    }
  }

  if ( max_period == 0 ) {
    max_period = _Watchdog_Ticks_per_second;
  }

  return max_period * 2;
}

static void release_case_start_barrier( const cpu_quota_case_runtime *runtime )
{
  if ( !runtime->plan.use_start_barrier ) {
    return;
  }

  for ( uint32_t i = 0; i < runtime->plan.task_count; ++i ) {
    rtems_status_code status = rtems_event_send(
      runtime->task_id[ i ],
      START_BARRIER_EVENT
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );
  }

  printf(
    "[Ticks:%" PRIu64 "] %s start barrier released for %" PRIu32 " tasks\n",
    current_ticks(),
    runtime->plan.name,
    runtime->plan.task_count
  );
}

static rtems_task monitor_task( rtems_task_argument arg )
{
  cpu_quota_case_runtime *runtime = (cpu_quota_case_runtime *) arg;
  const cpu_quota_case *plan = &runtime->plan;
  uint64_t last_diag_tick = current_ticks();
  uint64_t diag_period_ticks = _Watchdog_Ticks_per_second;
  uint64_t last_progress_tick = current_ticks();
  uint32_t last_completed_count = 0;

  while ( true ) {
    bool all_completed = true;
    uint32_t completed_count = 0;

    for ( uint32_t i = 0; i < plan->task_count; ++i ) {
      ISR_lock_Context lock_context;
      Thread_Control *thread;
      States_Control state = STATES_READY;
      bool waiting;

      if ( !runtime->task_completed[ i ] ) {
        all_completed = false;
      } else {
        ++completed_count;
      }

      if ( runtime->task_id[ i ] == 0 ) {
        continue;
      }

      thread = _Thread_Get( runtime->task_id[ i ], &lock_context );
      if ( thread != NULL ) {
        state = thread->current_state;
      }
      _ISR_lock_ISR_enable( &lock_context );

      waiting = ( state & STATES_WAITING_FOR_CGROUP_CPU_QUOTA ) != 0;
      if ( waiting != runtime->task_waiting_for_quota[ i ] ) {
        runtime->task_waiting_for_quota[ i ] = waiting;
        log_task_quota_state_change( plan, i, waiting );
      }
    }

    if ( completed_count > last_completed_count ) {
      last_completed_count = completed_count;
      last_progress_tick = current_ticks();
    }

    if ( plan->case_index == 2 || plan->case_index == 4 ) {
      uint64_t now = current_ticks();

      if ( now - last_diag_tick >= diag_period_ticks ) {
        printf(
          "[Ticks:%" PRIu64 "] %s periodic snapshot\n",
          now,
          plan->name
        );

        for ( uint32_t i = 0; i < plan->task_count; ++i ) {
          ISR_lock_Context lock_context;
          Thread_Control *thread;
          States_Control state = STATES_READY;
          bool waiting = false;

          if ( runtime->task_id[ i ] != 0 ) {
            thread = _Thread_Get( runtime->task_id[ i ], &lock_context );
            if ( thread != NULL ) {
              state = thread->current_state;
            }
            _ISR_lock_ISR_enable( &lock_context );
            waiting = ( state & STATES_WAITING_FOR_CGROUP_CPU_QUOTA ) != 0;
          }

          printf(
            "  task %" PRIu32 ": completed=%d waiting_quota=%d state=0x%08" PRIx32 "\n",
            i,
            runtime->task_completed[ i ] ? 1 : 0,
            waiting ? 1 : 0,
            (uint32_t) state
          );
        }

        for ( uint32_t i = 0; i < plan->cgroup_count; ++i ) {
          ISR_lock_Context lock_context;
          Cgroup_Control *cgroup;

          cgroup = _Cgroup_Get( runtime->cgroup_id[ i ], &lock_context );
          if ( cgroup != NULL ) {
            printf(
              "  CG%" PRIu32 ": quota_available=%" PRIu64 " quota=%" PRIu64 " period=%" PRIu64 "\n",
              i + 1,
              cgroup->cgroup.cpu_quota_available,
              plan->configs[ i ].cpu_quota,
              plan->configs[ i ].cpu_period
            );
          }
          _ISR_lock_ISR_enable( &lock_context );
        }

        last_diag_tick = now;
      }

      if ( completed_count < plan->task_count &&
           now - last_progress_tick >= diag_period_ticks * 10 ) {
        printf(
          "[Ticks:%" PRIu64 "] %s no progress for %" PRIu64 " ticks (completed=%" PRIu32 "/%" PRIu32 ")\n",
          now,
          plan->name,
          now - last_progress_tick,
          completed_count,
          plan->task_count
        );

        for ( uint32_t i = 0; i < plan->task_count; ++i ) {
          ISR_lock_Context lock_context;
          Thread_Control *thread;
          States_Control state = STATES_READY;

          if ( runtime->task_id[ i ] != 0 ) {
            thread = _Thread_Get( runtime->task_id[ i ], &lock_context );
            if ( thread != NULL ) {
              state = thread->current_state;
            }
            _ISR_lock_ISR_enable( &lock_context );
          }

          printf(
            "  stall task %" PRIu32 ": completed=%d waiting_quota=%d state=0x%08" PRIx32 "\n",
            i,
            runtime->task_completed[ i ] ? 1 : 0,
            runtime->task_waiting_for_quota[ i ] ? 1 : 0,
            (uint32_t) state
          );
        }

        last_progress_tick = now;
      }
    }

    if ( all_completed ) {
      break;
    }

    rtems_task_wake_after( 1 );
  }

  rtems_event_send( Init_task_id, RTEMS_EVENT_0 << plan->task_count );
  rtems_task_exit();
}

static rtems_task task_entry( rtems_task_argument arg )
{
  cpu_quota_task_context *task_ctx = (cpu_quota_task_context *) arg;
  cpu_quota_case_runtime *runtime = task_ctx->runtime;
  const cpu_quota_case *plan = &runtime->plan;
  uint32_t task_index = task_ctx->task_index;
  uint32_t group_index = plan->task_to_group[ task_index ];
  ISR_lock_Context lock_context;
  Cgroup_Control *cgroup;
  CORE_cgroup_Control *core_cg;
  uint64_t cpu_quota_available;

  printf(
    "[Ticks:%" PRIu64 "] %s task %" PRIu32 " started in CG%" PRIu32 "\n",
    current_ticks(),
    plan->name,
    task_index,
    group_index + 1
  );

  if ( plan->case_index == 2 ) {
    printf(
      "[Ticks:%" PRIu64 "] %s task %" PRIu32 " entering busy for %" PRIu32 " seconds\n",
      current_ticks(),
      plan->name,
      task_index,
      plan->task_busy_multiplier[ task_index ]
    );
  }

  if ( plan->use_start_barrier ) {
    rtems_event_set received;
    rtems_status_code status;

    status = rtems_event_receive(
      START_BARRIER_EVENT,
      RTEMS_EVENT_ALL | RTEMS_WAIT,
      RTEMS_NO_TIMEOUT,
      &received
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    printf(
      "[Ticks:%" PRIu64 "] %s task %" PRIu32 " passed start barrier\n",
      current_ticks(),
      plan->name,
      task_index
    );

    rtems_task_wake_after( 1 );
  }

  if ( plan->case_index == 2 ) {
    ISR_lock_Context snapshot_lock_context;
    Thread_Control *thread;
    States_Control state = STATES_READY;
    Cgroup_Control *snapshot_cgroup;

    thread = _Thread_Get( runtime->task_id[ task_index ], &snapshot_lock_context );
    if ( thread != NULL ) {
      state = thread->current_state;
    }
    _ISR_lock_ISR_enable( &snapshot_lock_context );

    snapshot_cgroup = _Cgroup_Get( runtime->cgroup_id[ group_index ], &snapshot_lock_context );
    rtems_test_assert( snapshot_cgroup != NULL );
    _ISR_lock_ISR_enable( &snapshot_lock_context );

    printf(
      "[Ticks:%" PRIu64 "] %s task %" PRIu32 " pre-busy snapshot: state=0x%08" PRIx32 " completed=%d waiting_quota=%d quota_available=%" PRIu64 "\n",
      current_ticks(),
      plan->name,
      task_index,
      (uint32_t) state,
      runtime->task_completed[ task_index ] ? 1 : 0,
      runtime->task_waiting_for_quota[ task_index ] ? 1 : 0,
      snapshot_cgroup->cgroup.cpu_quota_available
    );
  }

  if ( plan->case_index == 2 ) {
    busy_cpu_for_ticks_with_step_logs(
      runtime,
      plan,
      task_index,
      group_index,
      plan->task_busy_multiplier[ task_index ] * _Watchdog_Ticks_per_second
    );
  } else {
    busy_cpu_for_ticks( plan->task_busy_multiplier[ task_index ] * _Watchdog_Ticks_per_second );
  }

  if ( plan->case_index == 2 ) {
    ISR_lock_Context snapshot_lock_context;
    Thread_Control *thread;
    States_Control state = STATES_READY;
    Cgroup_Control *snapshot_cgroup;

    thread = _Thread_Get( runtime->task_id[ task_index ], &snapshot_lock_context );
    if ( thread != NULL ) {
      state = thread->current_state;
    }
    _ISR_lock_ISR_enable( &snapshot_lock_context );

    snapshot_cgroup = _Cgroup_Get( runtime->cgroup_id[ group_index ], &snapshot_lock_context );
    rtems_test_assert( snapshot_cgroup != NULL );
    _ISR_lock_ISR_enable( &snapshot_lock_context );

    printf(
      "[Ticks:%" PRIu64 "] %s task %" PRIu32 " post-busy snapshot: state=0x%08" PRIx32 " completed=%d waiting_quota=%d quota_available=%" PRIu64 "\n",
      current_ticks(),
      plan->name,
      task_index,
      (uint32_t) state,
      runtime->task_completed[ task_index ] ? 1 : 0,
      runtime->task_waiting_for_quota[ task_index ] ? 1 : 0,
      snapshot_cgroup->cgroup.cpu_quota_available
    );
  }

  cgroup = _Cgroup_Get( runtime->cgroup_id[ group_index ], &lock_context );
  rtems_test_assert( cgroup != NULL );
  _ISR_lock_ISR_enable( &lock_context );
  core_cg = &cgroup->cgroup;
  cpu_quota_available = core_cg->cpu_quota_available;

  printf(
    "\033[33m[Ticks:%" PRIu64 "] %s task %" PRIu32 " finished in CG%" PRIu32 ", cgroup quota available=%" PRIu64 " ticks\033[0m\n",
    current_ticks(),
    plan->name,
    task_index,
    group_index + 1,
    cpu_quota_available
  );

  runtime->task_completed[ task_index ] = true;
  rtems_event_send( Init_task_id, RTEMS_EVENT_0 << task_index );
  rtems_task_exit();
}

static void init_case(cpu_quota_case *tc, uint32_t case_index)
{
  uint32_t ticks_per_second = _Watchdog_Ticks_per_second;

  memset( tc, 0, sizeof( *tc ) );
  tc->case_index = case_index;

  switch ( case_index ) {
    case 1:
      tc->name = "CGTEST5";
      tc->task_count = 6;
      tc->cgroup_count = 2;
      tc->configs[ 0 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second * 2,
        .cpu_period = ticks_per_second * 5,
        .memory_limit = 16 * 1024 * 1024,
        .blkio_limit = 0
      };
      tc->configs[ 1 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second * 2,
        .cpu_period = ticks_per_second * 5,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->task_to_group[ 0 ] = 1;
      tc->task_to_group[ 1 ] = 1;
      tc->task_to_group[ 2 ] = 1;
      tc->task_to_group[ 3 ] = 0;
      tc->task_to_group[ 4 ] = 0;
      tc->task_to_group[ 5 ] = 0;
      for ( uint32_t i = 0; i < tc->task_count; ++i ) {
        tc->task_busy_multiplier[ i ] = i;
      }
      break;
    case 2:
      tc->name = "CGTEST5-2";
      tc->task_count = 4;
      tc->cgroup_count = 1;
      tc->use_start_barrier = true;
      tc->configs[ 0 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second,
        .cpu_period = ticks_per_second * 3,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->task_busy_multiplier[ 0 ] = 1;
      tc->task_busy_multiplier[ 1 ] = 2;
      tc->task_busy_multiplier[ 2 ] = 3;
      tc->task_busy_multiplier[ 3 ] = 4;
      break;
    case 3:
      tc->name = "CGTEST5-3";
      tc->task_count = 6;
      tc->cgroup_count = 3;
      tc->configs[ 0 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second,
        .cpu_period = ticks_per_second * 3,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->configs[ 1 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second * 2,
        .cpu_period = ticks_per_second * 4,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->configs[ 2 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second * 3,
        .cpu_period = ticks_per_second * 6,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->task_to_group[ 0 ] = 0;
      tc->task_to_group[ 1 ] = 0;
      tc->task_to_group[ 2 ] = 1;
      tc->task_to_group[ 3 ] = 1;
      tc->task_to_group[ 4 ] = 2;
      tc->task_to_group[ 5 ] = 2;
      tc->task_busy_multiplier[ 0 ] = 1;
      tc->task_busy_multiplier[ 1 ] = 1;
      tc->task_busy_multiplier[ 2 ] = 2;
      tc->task_busy_multiplier[ 3 ] = 2;
      tc->task_busy_multiplier[ 4 ] = 3;
      tc->task_busy_multiplier[ 5 ] = 3;
      break;
    case 4:
      tc->name = "CGTEST5-4";
      tc->task_count = 8;
      tc->cgroup_count = 4;
      tc->use_start_barrier = true;
      tc->configs[ 0 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second,
        .cpu_period = ticks_per_second * 4,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->configs[ 1 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second * 2,
        .cpu_period = ticks_per_second * 5,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->configs[ 2 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second,
        .cpu_period = ticks_per_second * 2,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->configs[ 3 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second * 3,
        .cpu_period = ticks_per_second * 4,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->task_to_group[ 0 ] = 0;
      tc->task_to_group[ 1 ] = 0;
      tc->task_to_group[ 2 ] = 1;
      tc->task_to_group[ 3 ] = 1;
      tc->task_to_group[ 4 ] = 2;
      tc->task_to_group[ 5 ] = 2;
      tc->task_to_group[ 6 ] = 3;
      tc->task_to_group[ 7 ] = 3;
      tc->task_busy_multiplier[ 0 ] = 1;
      tc->task_busy_multiplier[ 1 ] = 1;
      tc->task_busy_multiplier[ 2 ] = 2;
      tc->task_busy_multiplier[ 3 ] = 2;
      tc->task_busy_multiplier[ 4 ] = 3;
      tc->task_busy_multiplier[ 5 ] = 3;
      tc->task_busy_multiplier[ 6 ] = 4;
      tc->task_busy_multiplier[ 7 ] = 4;
      break;
    case 5:
      tc->name = "CGTEST5-5";
      tc->task_count = 5;
      tc->cgroup_count = 2;
      tc->configs[ 0 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second,
        .cpu_period = ticks_per_second * 5,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->configs[ 1 ] = (CORE_cgroup_config){
        .cpu_shares = 0,
        .cpu_quota = ticks_per_second * 2,
        .cpu_period = ticks_per_second * 3,
        .memory_limit = 0,
        .blkio_limit = 0
      };
      tc->task_to_group[ 0 ] = 0;
      tc->task_to_group[ 1 ] = 0;
      tc->task_to_group[ 2 ] = 0;
      tc->task_to_group[ 3 ] = 1;
      tc->task_to_group[ 4 ] = 1;
      tc->task_busy_multiplier[ 0 ] = 1;
      tc->task_busy_multiplier[ 1 ] = 2;
      tc->task_busy_multiplier[ 2 ] = 3;
      tc->task_busy_multiplier[ 3 ] = 2;
      tc->task_busy_multiplier[ 4 ] = 3;
      break;
    default:
      rtems_test_assert( false );
      break;
  }
}

static void run_case( uint32_t case_index )
{
  cpu_quota_case *plan;
  rtems_status_code status;
  rtems_event_set received;
  uint64_t timeout_ticks;
  uint64_t cooldown_ticks;
  bool case_timed_out;
  cpu_quota_case_runtime runtime;

  memset( &runtime, 0, sizeof( runtime ) );
  init_case( &runtime.plan, case_index );
  plan = &runtime.plan;

  for ( uint32_t i = 0; i < 8; ++i ) {
    runtime.task_completed[ i ] = false;
    runtime.task_waiting_for_quota[ i ] = false;
    runtime.task_ctx[ i ].runtime = &runtime;
    runtime.task_ctx[ i ].task_index = i;
  }

  printf( "[CASE %" PRIu32 "] %s: start\n", case_index, plan->name );
  case_timed_out = false;

  for ( uint32_t i = 0; i < plan->cgroup_count; ++i ) {
    status = rtems_cgroup_create(
      rtems_build_name( 'C', 'A', '0' + case_index, '1' + i ),
      &runtime.cgroup_id[ i ],
      &plan->configs[ i ]
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    printf(
      "[Ticks:%" PRIu64 "] %s config CG%" PRIu32 ": quota=%" PRIu64 " period=%" PRIu64 "\n",
      current_ticks(),
      plan->name,
      i + 1,
      plan->configs[ i ].cpu_quota,
      plan->configs[ i ].cpu_period
    );
  }

  for ( uint32_t i = 0; i < plan->task_count; ++i ) {
    uint32_t group_index = plan->task_to_group[ i ];
    ISR_lock_Context lock_context;
    Cgroup_Control *cgroup;
    CORE_cgroup_Control *core_cg;

    status = rtems_task_create(
      rtems_build_name( 'A', 'L', '0' + case_index, '0' + i ),
      20 - i,
      RTEMS_MINIMUM_STACK_SIZE,
      RTEMS_DEFAULT_MODES,
      RTEMS_DEFAULT_ATTRIBUTES,
      &runtime.task_id[ i ]
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    status = rtems_cgroup_add_task( runtime.cgroup_id[ group_index ], runtime.task_id[ i ] );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    cgroup = _Cgroup_Get( runtime.cgroup_id[ group_index ], &lock_context );
    rtems_test_assert( cgroup != NULL );
    _ISR_lock_ISR_enable( &lock_context );
    core_cg = &cgroup->cgroup;

    printf(
      "[Ticks:%" PRIu64 "] %s assign task %" PRIu32 " -> CG%" PRIu32 " (busy=%" PRIu32 "s), quota=%" PRIu64 "\n",
      current_ticks(),
      plan->name,
      i,
      group_index + 1,
      plan->task_busy_multiplier[ i ],
      core_cg->cpu_quota_available
    );

    status = rtems_task_start(
      runtime.task_id[ i ],
      task_entry,
      (rtems_task_argument) &runtime.task_ctx[ i ]
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );
  }

  status = rtems_task_create(
    rtems_build_name( 'M', 'A', 'L', '0' + case_index ),
    8,
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &runtime.monitor_task_id
  );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  status = rtems_task_start(
    runtime.monitor_task_id,
    monitor_task,
    (rtems_task_argument) &runtime
  );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  release_case_start_barrier( &runtime );

  timeout_ticks = _Watchdog_Ticks_per_second * 120;
  status = rtems_event_receive(
    full_completion_mask( plan ),
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    timeout_ticks,
    &received
  );

  if ( status == RTEMS_TIMEOUT ) {
    case_timed_out = true;
    Any_case_timeout = true;
    printf(
      "[Ticks:%" PRIu64 "] %s timed out waiting for completion events\n",
      current_ticks(),
      plan->name
    );

    for ( uint32_t i = 0; i < plan->task_count; ++i ) {
      ISR_lock_Context lock_context;
      Thread_Control *thread;
      States_Control state = STATES_READY;

      thread = _Thread_Get( runtime.task_id[ i ], &lock_context );
      if ( thread != NULL ) {
        state = thread->current_state;
      }
      _ISR_lock_ISR_enable( &lock_context );

      printf(
        "[Ticks:%" PRIu64 "] %s timeout task %" PRIu32 ": completed=%d waiting_quota=%d state=0x%08" PRIx32 "\n",
        current_ticks(),
        plan->name,
        i,
        runtime.task_completed[ i ] ? 1 : 0,
        runtime.task_waiting_for_quota[ i ] ? 1 : 0,
        (uint32_t) state
      );

      if ( !runtime.task_completed[ i ] && runtime.task_id[ i ] != 0 ) {
        rtems_task_delete( runtime.task_id[ i ] );
        runtime.task_id[ i ] = 0;
      }
    }
  }

  if ( status != RTEMS_SUCCESSFUL && status != RTEMS_TIMEOUT ) {
    rtems_test_assert( false );
  }

  status = rtems_task_delete( runtime.monitor_task_id );
  rtems_test_assert( status == RTEMS_SUCCESSFUL || status == RTEMS_INVALID_ID );
  runtime.monitor_task_id = 0;

  for ( uint32_t i = 0; i < plan->task_count; ++i ) {
    if ( runtime.task_id[ i ] != 0 ) {
      status = rtems_task_delete( runtime.task_id[ i ] );
      rtems_test_assert( status == RTEMS_SUCCESSFUL || status == RTEMS_INVALID_ID );
      runtime.task_id[ i ] = 0;
    }
  }

  for ( uint32_t i = 0; i < plan->cgroup_count; ++i ) {
    status = rtems_cgroup_delete( runtime.cgroup_id[ i ] );
    if ( case_timed_out ) {
      rtems_test_assert( status == RTEMS_SUCCESSFUL || status == RTEMS_INVALID_ID );
    } else {
      rtems_test_assert( status == RTEMS_SUCCESSFUL );
    }
  }

  if ( case_timed_out ) {
    printf( "[CASE %" PRIu32 "] %s: timeout/forced cleanup\n", case_index, plan->name );
  } else {
    printf( "[CASE %" PRIu32 "] %s: done\n", case_index, plan->name );
  }

  cooldown_ticks = case_cooldown_ticks( plan );
  printf(
    "[Ticks:%" PRIu64 "] %s cooldown: waiting %" PRIu64 " ticks before next case\n",
    current_ticks(),
    plan->name,
    cooldown_ticks
  );
  rtems_task_wake_after( cooldown_ticks );
}

rtems_task Init( rtems_task_argument ignored )
{
  (void) ignored;

  rtems_print_printer_fprintf_putc( &rtems_test_printer );
  TEST_BEGIN();

  test_cpu = _Per_CPU_Get_by_index( 0 );
  Init_task_id = rtems_task_self();
  Any_case_timeout = false;

  printf( "Preparing merged CGTEST5 CPU quota execution...\n" );

  run_case( 1 );
  run_case( 2 );
  run_case( 3 );
  run_case( 4 );
  run_case( 5 );

  if ( Any_case_timeout ) {
    printf( "Merged CGTEST5 completed with timeout cleanup in at least one case.\n" );
  }

  printf( "All merged CGTEST5 cases completed.\n" );

  TEST_END();
  rtems_test_exit( 0 );
}
