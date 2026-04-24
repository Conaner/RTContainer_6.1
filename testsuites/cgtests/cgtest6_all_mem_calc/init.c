#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CONFIGURE_INIT
#include "system.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <rtems/test.h>
#include <rtems/score/watchdogimpl.h>
#include <rtems/score/corecgroup.h>
#include <rtems/score/isrlock.h>
#include <rtems/score/thread.h>
#include <rtems/score/threadimpl.h>

#include <rtems/rtems/cgroupimpl.h>
#include <rtems/rtems/event.h>
#include <rtems/rtems/tasks.h>

const char rtems_test_name[] = "CGTEST6 ALL MEM CALC";

#define MAX_TASKS 8
#define MAX_CGROUPS 4
#define SHARED_BUFFER_SIZE ( 2 * 1024 * 1024 )
#define SHRINK_EXPECT_IGNORE (-1)

typedef struct {
  const char *name;
  uint32_t task_count;
  uint32_t cgroup_count;
  uint32_t cpu_quota_factor;
  uint32_t cpu_period_factor;
  uint64_t memory_limit[ MAX_CGROUPS ];
  uint32_t task_to_group[ MAX_TASKS ];
  uint32_t shared_owner_task[ MAX_CGROUPS ];
  size_t task_alloc_size[ MAX_TASKS ];
  bool assert_task_alloc_success;
  int expected_shrink[ MAX_CGROUPS ];
} mem_calc_case;

typedef struct {
  const char *name;
  rtems_id id;
  void *shared_buffer;
  bool shrink_called;
} memcg_context;

static rtems_id Init_task_id;
static mem_calc_case Active_case;
static memcg_context Groups[ MAX_CGROUPS ];
static void *Task_buffer[ MAX_TASKS ];

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

static rtems_event_set completion_mask( uint32_t task_count )
{
  rtems_event_set mask = 0;

  for ( uint32_t i = 0; i < task_count; ++i ) {
    mask |= RTEMS_EVENT_0 << i;
  }

  return mask;
}

static void group_shrinker( void *arg, uintptr_t target )
{
  memcg_context *ctx = arg;

  ctx->shrink_called = true;
  printf(
    "\033[33m>>> [%s Shrink Callback] Triggered! Need %lu bytes <<<\033[0m\n",
    ctx->name,
    (unsigned long) target
  );

  if ( ctx->shared_buffer != NULL ) {
    free( ctx->shared_buffer );
    ctx->shared_buffer = NULL;
    printf( "\033[33m[%s Shrink Callback] Freed shared 2MB buffer\033[0m\n", ctx->name );
  } else {
    printf( "\033[33m[%s Shrink Callback] No memory to free\033[0m\n", ctx->name );
  }
}

static rtems_task worker_task( rtems_task_argument arg )
{
  uint32_t task_index = (uint32_t) arg;
  uint32_t group_index = Active_case.task_to_group[ task_index ];
  memcg_context *ctx = &Groups[ group_index ];
  const Thread_Control *self = _Thread_Get_executing();
  CORE_cgroup_Control *cg =
    ( self != NULL && self->is_added_to_cgroup ) ? self->cgroup : NULL;
  unsigned long quota_now = cg != NULL ? (unsigned long) cg->mem_quota_available : 0UL;

  printf( "[Task %lu] started in %s, quota=%lu\n", arg, ctx->name, quota_now );

  if ( task_index == Active_case.shared_owner_task[ group_index ] ) {
    ctx->shared_buffer = malloc( SHARED_BUFFER_SIZE );
    printf(
      ctx->shared_buffer != NULL
        ? "\033[32m[%s] shared 2MB allocation succeeded\033[0m\n"
        : "\033[31m[%s] shared 2MB allocation failed\033[0m\n",
      ctx->name
    );
    if ( Active_case.assert_task_alloc_success ) {
      rtems_test_assert( ctx->shared_buffer != NULL );
    }
  }

  Task_buffer[ task_index ] = malloc( Active_case.task_alloc_size[ task_index ] );
  if ( Task_buffer[ task_index ] != NULL ) {
    printf(
      "\033[32m[Task %lu] %s %zuMB allocation succeeded\033[0m\n",
      arg,
      ctx->name,
      Active_case.task_alloc_size[ task_index ] >> 20
    );
  } else {
    printf(
      "\033[31m[Task %lu] %s %zuMB allocation failed\033[0m\n",
      arg,
      ctx->name,
      Active_case.task_alloc_size[ task_index ] >> 20
    );
  }

  if ( Active_case.assert_task_alloc_success ) {
    rtems_test_assert( Task_buffer[ task_index ] != NULL );
  }

  rtems_event_send( Init_task_id, RTEMS_EVENT_0 << task_index );
  rtems_task_exit();
}

static void cleanup_case_allocations( void )
{
  for ( uint32_t i = 0; i < MAX_TASKS; ++i ) {
    if ( Task_buffer[ i ] != NULL ) {
      free( Task_buffer[ i ] );
      Task_buffer[ i ] = NULL;
    }
  }

  for ( uint32_t i = 0; i < MAX_CGROUPS; ++i ) {
    if ( Groups[ i ].shared_buffer != NULL ) {
      free( Groups[ i ].shared_buffer );
      Groups[ i ].shared_buffer = NULL;
    }
  }
}

static void init_case( mem_calc_case *tc, uint32_t case_index )
{
  memset( tc, 0, sizeof( *tc ) );
  for ( uint32_t i = 0; i < MAX_CGROUPS; ++i ) {
    tc->expected_shrink[ i ] = SHRINK_EXPECT_IGNORE;
  }

  switch ( case_index ) {
    case 1:
      tc->name = "CGTEST6";
      tc->task_count = 6;
      tc->cgroup_count = 1;
      tc->cpu_quota_factor = 500;
      tc->cpu_period_factor = 500;
      tc->memory_limit[ 0 ] = 10 * 1024 * 1024;
      tc->shared_owner_task[ 0 ] = 0;
      tc->assert_task_alloc_success = false;
      tc->expected_shrink[ 0 ] = SHRINK_EXPECT_IGNORE;
      for ( uint32_t i = 0; i < tc->task_count; ++i ) {
        tc->task_to_group[ i ] = 0;
        tc->task_alloc_size[ i ] = i * 1024 * 1024;
      }
      break;
    case 2:
      tc->name = "CGTEST6-2";
      tc->task_count = 3;
      tc->cgroup_count = 1;
      tc->cpu_quota_factor = 60;
      tc->cpu_period_factor = 60;
      tc->memory_limit[ 0 ] = 8 * 1024 * 1024;
      tc->shared_owner_task[ 0 ] = 0;
      tc->task_alloc_size[ 0 ] = 3 * 1024 * 1024;
      tc->task_alloc_size[ 1 ] = 2 * 1024 * 1024;
      tc->task_alloc_size[ 2 ] = 1 * 1024 * 1024;
      tc->assert_task_alloc_success = true;
      tc->expected_shrink[ 0 ] = 0;
      break;
    case 3:
      tc->name = "CGTEST6-3";
      tc->task_count = 4;
      tc->cgroup_count = 2;
      tc->cpu_quota_factor = 60;
      tc->cpu_period_factor = 60;
      tc->memory_limit[ 0 ] = 6 * 1024 * 1024;
      tc->memory_limit[ 1 ] = 6 * 1024 * 1024;
      tc->task_to_group[ 0 ] = 0;
      tc->task_to_group[ 1 ] = 0;
      tc->task_to_group[ 2 ] = 1;
      tc->task_to_group[ 3 ] = 1;
      tc->shared_owner_task[ 0 ] = 0;
      tc->shared_owner_task[ 1 ] = 2;
      tc->task_alloc_size[ 0 ] = 3 * 1024 * 1024;
      tc->task_alloc_size[ 1 ] = 1 * 1024 * 1024;
      tc->task_alloc_size[ 2 ] = 4 * 1024 * 1024;
      tc->task_alloc_size[ 3 ] = 1 * 1024 * 1024;
      tc->assert_task_alloc_success = true;
      tc->expected_shrink[ 0 ] = 0;
      tc->expected_shrink[ 1 ] = 1;
      break;
    case 4:
      tc->name = "CGTEST6-4";
      tc->task_count = 6;
      tc->cgroup_count = 3;
      tc->cpu_quota_factor = 60;
      tc->cpu_period_factor = 60;
      tc->memory_limit[ 0 ] = 6 * 1024 * 1024;
      tc->memory_limit[ 1 ] = 5 * 1024 * 1024;
      tc->memory_limit[ 2 ] = 8 * 1024 * 1024;
      tc->task_to_group[ 0 ] = 0;
      tc->task_to_group[ 1 ] = 0;
      tc->task_to_group[ 2 ] = 1;
      tc->task_to_group[ 3 ] = 1;
      tc->task_to_group[ 4 ] = 2;
      tc->task_to_group[ 5 ] = 2;
      tc->shared_owner_task[ 0 ] = 0;
      tc->shared_owner_task[ 1 ] = 2;
      tc->shared_owner_task[ 2 ] = 4;
      tc->task_alloc_size[ 0 ] = 2 * 1024 * 1024;
      tc->task_alloc_size[ 1 ] = 1 * 1024 * 1024;
      tc->task_alloc_size[ 2 ] = 2 * 1024 * 1024;
      tc->task_alloc_size[ 3 ] = 2 * 1024 * 1024;
      tc->task_alloc_size[ 4 ] = 3 * 1024 * 1024;
      tc->task_alloc_size[ 5 ] = 2 * 1024 * 1024;
      tc->assert_task_alloc_success = true;
      tc->expected_shrink[ 0 ] = 0;
      tc->expected_shrink[ 1 ] = 1;
      tc->expected_shrink[ 2 ] = 0;
      break;
    case 5:
      tc->name = "CGTEST6-5";
      tc->task_count = 4;
      tc->cgroup_count = 2;
      tc->cpu_quota_factor = 60;
      tc->cpu_period_factor = 60;
      tc->memory_limit[ 0 ] = 5 * 1024 * 1024;
      tc->memory_limit[ 1 ] = 5 * 1024 * 1024;
      tc->task_to_group[ 0 ] = 0;
      tc->task_to_group[ 1 ] = 0;
      tc->task_to_group[ 2 ] = 1;
      tc->task_to_group[ 3 ] = 1;
      tc->shared_owner_task[ 0 ] = 0;
      tc->shared_owner_task[ 1 ] = 2;
      tc->task_alloc_size[ 0 ] = 2 * 1024 * 1024;
      tc->task_alloc_size[ 1 ] = 3 * 1024 * 1024;
      tc->task_alloc_size[ 2 ] = 3 * 1024 * 1024;
      tc->task_alloc_size[ 3 ] = 1 * 1024 * 1024;
      tc->assert_task_alloc_success = true;
      tc->expected_shrink[ 0 ] = 1;
      tc->expected_shrink[ 1 ] = 1;
      break;
    default:
      rtems_test_assert( false );
      break;
  }
}

static void run_case( uint32_t case_index )
{
  rtems_status_code status;
  rtems_event_set received;
  uint32_t ticks_per_second = _Watchdog_Ticks_per_second;

  init_case( &Active_case, case_index );

  memset( Groups, 0, sizeof( Groups ) );
  memset( Task_buffer, 0, sizeof( Task_buffer ) );

  printf( "[CASE %" PRIu32 "] %s: start\n", case_index, Active_case.name );

  for ( uint32_t i = 0; i < Active_case.cgroup_count; ++i ) {
    CORE_cgroup_config config = {
      .cpu_shares = 1,
      .cpu_quota = ticks_per_second * Active_case.cpu_quota_factor,
      .cpu_period = ticks_per_second * Active_case.cpu_period_factor,
      .memory_limit = Active_case.memory_limit[ i ],
      .blkio_limit = 0
    };
    ISR_lock_Context lock_context;
    Cgroup_Control *the_cgroup;

    Groups[ i ].name = ( i == 0 ) ? "CG1" : ( i == 1 ) ? "CG2" : ( i == 2 ) ? "CG3" : "CGX";

    status = rtems_cgroup_create(
      rtems_build_name( 'A', '6', '0' + case_index, '1' + i ),
      &Groups[ i ].id,
      &config
    );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );

    printf(
      "Config: %s memory_limit=%" PRIu64 " bytes\n",
      Groups[ i ].name,
      config.memory_limit
    );

    the_cgroup = _Cgroup_Get( Groups[ i ].id, &lock_context );
    rtems_test_assert( the_cgroup != NULL );
    the_cgroup->cgroup.shrink_callback = group_shrinker;
    the_cgroup->cgroup.shrink_arg = &Groups[ i ];
    _ISR_lock_ISR_enable( &lock_context );
  }

  Init_task_id = rtems_task_self();

  for ( uint32_t i = 0; i < Active_case.task_count; ++i ) {
    uint32_t group_index = Active_case.task_to_group[ i ];

    status = rtems_task_create(
      rtems_build_name( 'A', '6', 'T', '0' + i ),
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

  printf( "Init waiting for %" PRIu32 " completion events\n", Active_case.task_count );
  status = rtems_event_receive(
    completion_mask( Active_case.task_count ),
    RTEMS_EVENT_ALL | RTEMS_WAIT,
    RTEMS_NO_TIMEOUT,
    &received
  );
  rtems_test_assert( status == RTEMS_SUCCESSFUL );

  for ( uint32_t i = 0; i < Active_case.cgroup_count; ++i ) {
    if ( Active_case.expected_shrink[ i ] == SHRINK_EXPECT_IGNORE ) {
      continue;
    }

    if ( Active_case.expected_shrink[ i ] == 1 ) {
      rtems_test_assert( Groups[ i ].shrink_called );
    } else {
      rtems_test_assert( !Groups[ i ].shrink_called );
    }
  }

  cleanup_case_allocations();

  for ( uint32_t i = 0; i < Active_case.cgroup_count; ++i ) {
    status = rtems_cgroup_delete( Groups[ i ].id );
    rtems_test_assert( status == RTEMS_SUCCESSFUL );
  }

  printf( "[CASE %" PRIu32 "] %s: done\n", case_index, Active_case.name );
}

rtems_task Init( rtems_task_argument ignored )
{
  (void) ignored;

  rtems_print_printer_fprintf_putc( &rtems_test_printer );
  TEST_BEGIN();

  run_case( 1 );
  run_case( 2 );
  run_case( 3 );
  run_case( 4 );
  run_case( 5 );

  printf( "All merged CGTEST6 cases completed.\n" );

  TEST_END();
  rtems_test_exit( 0 );
}
