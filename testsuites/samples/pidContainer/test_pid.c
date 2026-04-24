/*
 * PID container task switch benchmark.
 *
 * Method:
 * 1. Create two PID containers.
 * 2. Move Task01 to container A and Task02 to container B.
 * 3. Let both tasks alternate via RTEMS_YIELD_PROCESSOR.
 * 4. Measure with the same approach used in rhtaskswitch/taskswitch.c.
 */

#include <rtems/btimer.h>
#include <rtems.h>
#include <rtems/counter.h>
#include <rtems/score/container.h>
#include <rtems/score/pidContainer.h>
#include <rtems/score/threadimpl.h>
#include <inttypes.h>
#include <stdio.h>
#include <tmacros.h>

#define BENCHMARKS 50000

const char rtems_test_name[] = "PID CONTAINER TASK SWITCH";

static rtems_task Task01( rtems_task_argument ignored );
static rtems_task Task02( rtems_task_argument ignored );
static rtems_task Init( rtems_task_argument ignored );

static rtems_id           Task_id[2];
static rtems_name         Task_name[2];
static uint32_t           loop_overhead;
static uint32_t           dir_overhead;
static unsigned long      count1;
static unsigned long      count2;
static rtems_status_code  status;

static PidContainer *root_pid;
static PidContainer *container_a;
static PidContainer *container_b;

static uint64_t ticks_to_nanoseconds( uint64_t ticks )
{
	return rtems_counter_ticks_to_nanoseconds( (rtems_counter_ticks) ticks );
}

#if defined(CONFIGURE_STACK_CHECKER_ENABLED) || defined(RTEMS_DEBUG)
#define PRINT_WARNING() \
	do { \
		puts( "\nTHE TIMES REPORTED BY THIS TEST INCLUDE DEBUG CODE!\n" ); \
	} while (0)
#else
#define PRINT_WARNING() do { } while (0)
#endif

static void put_switch_time(
	const char *message,
	uint32_t    total_time,
	uint32_t    iterations,
	uint32_t    loop_ov,
	uint32_t    dir_ov
)
{
	int64_t net_total;
	int64_t adjusted;
	uint32_t raw_avg;
	uint64_t adjusted_ns;
	uint64_t raw_avg_ns;
	uint64_t loop_ov_ns;
	uint64_t dir_ov_ns;

	rtems_test_assert( iterations != 0 );

	raw_avg = total_time / iterations;
	net_total = (int64_t) total_time - (int64_t) loop_ov;
	if ( net_total < 0 ) {
		net_total = 0;
	}

	adjusted = ( net_total / (int64_t) iterations ) - (int64_t) dir_ov;
	if ( adjusted < 0 ) {
		adjusted = 0;
	}

	adjusted_ns = ticks_to_nanoseconds( (uint64_t) adjusted );
	raw_avg_ns = ticks_to_nanoseconds( (uint64_t) raw_avg );
	loop_ov_ns = ticks_to_nanoseconds( (uint64_t) loop_ov );
	dir_ov_ns = ticks_to_nanoseconds( (uint64_t) dir_ov );

	printf(
		"%s value: %" PRId64 " ticks = %" PRIu64 ".%03" PRIu64 " us, raw_avg=%" PRIu32 " ticks = %" PRIu64 ".%03" PRIu64 " us, loop_ov=%" PRIu32 " ticks = %" PRIu64 ".%03" PRIu64 " us, dir_ov=%" PRIu32 " ticks = %" PRIu64 ".%03" PRIu64 " us\n",
		message,
		adjusted,
		adjusted_ns / 1000ULL,
		adjusted_ns % 1000ULL,
		raw_avg,
		raw_avg_ns / 1000ULL,
		raw_avg_ns % 1000ULL,
		loop_ov,
		loop_ov_ns / 1000ULL,
		loop_ov_ns % 1000ULL,
		dir_ov,
		dir_ov_ns / 1000ULL,
		dir_ov_ns % 1000ULL
	);
}

static rtems_task Task02( rtems_task_argument ignored )
{
	Thread_Control *self;
	uint32_t        elapsed;

	(void) ignored;

	self = _Thread_Get_executing();
	rtems_pid_container_move_task( root_pid, container_b, self );

	benchmark_timer_initialize();

	for ( count1 = 0; count1 < BENCHMARKS - 1; ++count1 ) {
		rtems_task_wake_after( RTEMS_YIELD_PROCESSOR );
	}

	elapsed = benchmark_timer_read();
	put_switch_time(
		"PID container task switch",
		elapsed,
		( BENCHMARKS * 2 ) - 1,
		loop_overhead,
		dir_overhead
	);

	printf(
		"Container A(id=%d, rc=%d), Container B(id=%d, rc=%d)\n",
		rtems_pid_container_get_id( container_a ),
		rtems_pid_container_get_rc( container_a ),
		rtems_pid_container_get_id( container_b ),
		rtems_pid_container_get_rc( container_b )
	);

	TEST_END();
	rtems_test_exit( 0 );
}

static rtems_task Task01( rtems_task_argument ignored )
{
	Thread_Control *self;

	(void) ignored;

	self = _Thread_Get_executing();
	rtems_pid_container_move_task( root_pid, container_a, self );

	status = rtems_task_start( Task_id[1], Task02, 0 );
	directive_failed( status, "rtems_task_start of TA02" );

	/* Yield once so Task02 can move to container B and start timing. */
	rtems_task_wake_after( RTEMS_YIELD_PROCESSOR );

	for ( count2 = 0; count2 < BENCHMARKS; ++count2 ) {
		rtems_task_wake_after( RTEMS_YIELD_PROCESSOR );
	}

	/* Should never reach here. */
	rtems_test_assert( false );
}

static rtems_task Init( rtems_task_argument ignored )
{
	(void) ignored;

	PRINT_WARNING();
	TEST_BEGIN();

#ifdef RTEMSCFG_PID_CONTAINER
	Container *root_container;

	root_container = rtems_container_get_root();
	rtems_test_assert( root_container != NULL );

	root_pid = root_container->pidContainer;
	rtems_test_assert( root_pid != NULL );

	container_a = rtems_pid_container_create();
	container_b = rtems_pid_container_create();

	rtems_test_assert( container_a != NULL );
	rtems_test_assert( container_b != NULL );

	printf(
		"Created PID containers: A=%d, B=%d\n",
		rtems_pid_container_get_id( container_a ),
		rtems_pid_container_get_id( container_b )
	);

	Task_name[0] = rtems_build_name( 'T', 'A', '0', '1' );
	status = rtems_task_create(
		Task_name[0],
		30,
		RTEMS_MINIMUM_STACK_SIZE,
		RTEMS_DEFAULT_MODES,
		RTEMS_DEFAULT_ATTRIBUTES,
		&Task_id[0]
	);
	directive_failed( status, "rtems_task_create of TA01" );

	Task_name[1] = rtems_build_name( 'T', 'A', '0', '2' );
	status = rtems_task_create(
		Task_name[1],
		30,
		RTEMS_MINIMUM_STACK_SIZE,
		RTEMS_DEFAULT_MODES,
		RTEMS_DEFAULT_ATTRIBUTES,
		&Task_id[1]
	);
	directive_failed( status, "rtems_task_create of TA02" );

	/* Measure loop overhead with no directives. */
	benchmark_timer_initialize();
	for ( count1 = 0; count1 < BENCHMARKS - 1; ++count1 ) {
		/* empty */
	}
	for ( count2 = 0; count2 < BENCHMARKS; ++count2 ) {
		/* empty */
	}
	loop_overhead = benchmark_timer_read();

	/* Measure rtems_task_wake_after() overhead without a task switch. */
	benchmark_timer_initialize();
	for ( count1 = 0; count1 < BENCHMARKS; ++count1 ) {
		rtems_task_wake_after( RTEMS_YIELD_PROCESSOR );
	}
	dir_overhead = benchmark_timer_read() / BENCHMARKS;

	status = rtems_task_start( Task_id[0], Task01, 0 );
	directive_failed( status, "rtems_task_start of TA01" );
#else
	puts( "RTEMSCFG_PID_CONTAINER not enabled" );
	TEST_END();
	rtems_test_exit( 0 );
#endif

	rtems_task_exit();
}

/* configuration information */
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_TIMER_DRIVER
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_MAXIMUM_TASKS 4
#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION
#define CONFIGURE_INIT
#include <rtems/confdefs.h>
