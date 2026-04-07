/*
 * PID容器切换时间测试
 * 目标: 验证容器切换时间为us级
 * 
 * 测试方法:
 * 1. 创建一个子PID容器
 * 2. 任务A在根容器和子容器之间来回切换,并测量每次切换的延迟
 * 3. 任务B保持在子容器中,防止子容器因rc=0被自动释放
 * 4. 多次采样统计平均值、最小值、最大值
 */

#include <stdio.h>
#include <rtems.h>
#include <rtems/score/container.h>
#include <rtems/score/pidContainer.h>
#include <tmacros.h>
#include <rtems/score/threadimpl.h>
#include <machine/_timecounter.h>

const char rtems_test_name[] = "PID CONTAINER SWITCH LATENCY TEST";

#define TEST_ITERATIONS 1000

// 全局数据
static rtems_id task_a_id;
static rtems_id task_b_id;

static PidContainer *root_pid = NULL;
static PidContainer *sub_container = NULL;

static uint64_t latency_sum = 0;
static uint64_t latency_min = 0;
static uint64_t latency_max = 0;
static int iterations = 0;
static volatile int test_done = 0;

// 获取高精度时间戳(纳秒)
static inline uint64_t get_timestamp_ns(void)
{
    struct bintime bt;
    _Timecounter_Binuptime(&bt);
    return ((uint64_t)bt.sec * 1000000000ULL) + 
           ((uint64_t)bt.frac >> 32) * 1000000000ULL / 0x100000000ULL;
}

// 任务A: 在根容器和子容器之间来回切换并测量延迟
static rtems_task task_a(rtems_task_argument arg)
{
    int i;
    Thread_Control *self = _Thread_Get_executing();
    
    printf("任务A启动\n");
    printf("任务A开始容器切换测试\n");
    
    for (i = 0; i < TEST_ITERATIONS; i++) {
        uint64_t start_time = get_timestamp_ns();
        rtems_pid_container_move_task(root_pid, sub_container, self);
        uint64_t mid_time = get_timestamp_ns();
        rtems_pid_container_move_task(sub_container, root_pid, self);
        uint64_t end_time = get_timestamp_ns();
        
        uint64_t latency = ((mid_time - start_time) + (end_time - mid_time)) / 2;
        latency_sum += latency;
        
        if (i == 0 || latency > latency_max)
            latency_max = latency;
        if (i == 0 || latency < latency_min)
            latency_min = latency;
        
        iterations++;
        
        if ((i + 1) % 200 == 0)
            printf("任务A: 已完成 %d 次切换\n", i + 1);
    }
    
    test_done = 1;
    printf("任务A完成测试\n");
    rtems_task_suspend(RTEMS_SELF);
}

// 任务B: 保持在子容器中,防止子容器因rc=0被自动释放
static rtems_task task_b(rtems_task_argument arg)
{
    Thread_Control *self = _Thread_Get_executing();
    
    printf("任务B启动,移动到子容器\n");
    rtems_pid_container_move_task(root_pid, sub_container, self);
    printf("任务B已在子容器中(rc=%d)\n", rtems_pid_container_get_rc(sub_container));
    
    while (!test_done)
        rtems_task_wake_after(100);
    rtems_task_suspend(RTEMS_SELF);
}

static rtems_task Init(rtems_task_argument ignored)
{
    rtems_status_code sc;
    
    rtems_print_printer_fprintf_putc(&rtems_test_printer);
    TEST_BEGIN();

#ifdef RTEMSCFG_PID_CONTAINER
    Container *root_container = rtems_container_get_root();
    root_pid = root_container->pidContainer;
    
    printf("========================================\n");
    printf("PID容器切换时间测试\n");
    printf("测试次数: %d\n", TEST_ITERATIONS);
    printf("说明: 任务A在根容器和子容器间切换,任务B保持在子容器\n");
    printf("========================================\n\n");
    
    // 创建一个子容器
    sub_container = rtems_pid_container_create();
    printf("创建子容器(ID=%d)\n", rtems_pid_container_get_id(sub_container));
    
    // 创建任务
    sc = rtems_task_create(rtems_build_name('T','S','K','A'), 10,
        RTEMS_MINIMUM_STACK_SIZE * 2, RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES, &task_a_id);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    
    sc = rtems_task_create(rtems_build_name('T','S','K','B'), 10,
        RTEMS_MINIMUM_STACK_SIZE * 2, RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES, &task_b_id);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    printf("任务已创建\n\n");
    
    // 启动任务B
    sc = rtems_task_start(task_b_id, task_b, 0);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    rtems_task_wake_after(10);
    
    // 启动任务A
    sc = rtems_task_start(task_a_id, task_a, 0);
    rtems_test_assert(sc == RTEMS_SUCCESSFUL);
    
    printf("任务A已启动,保持在根容器,开始切换测试\n\n");
    
    // 等待测试完成
    while (!test_done)
        rtems_task_wake_after(100);
    rtems_task_wake_after(50);
    
    // 打印结果
    printf("\n========================================\n");
    printf("测试结果\n");
    printf("========================================\n");
    
    if (iterations > 0) {
        printf("完成切换次数: %d\n", iterations);
        printf("平均切换时间: %.2f us\n", latency_sum / (double)iterations / 1000.0);
        printf("最小切换时间: %.2f us\n", latency_min / 1000.0);
        printf("最大切换时间: %.2f us\n", latency_max / 1000.0);
    } else {
        printf("测试未完成\n");
    }
    printf("========================================\n");
    
#else
    printf("此测试需要PID容器支持\n");
#endif
    
    TEST_END();
    rtems_test_exit(0);
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS 10

#define CONFIGURE_INITIAL_EXTENSIONS RTEMS_TEST_INITIAL_EXTENSION

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT
#include <rtems/confdefs.h>
