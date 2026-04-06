#include <stdio.h>
#include <stdlib.h>
#include <rtems.h>
#include <rtems/score/container.h>
#include <rtems/score/mntContainer.h>
#include <rtems/score/threadimpl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <rtems/libio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

static int mount_imfs(const char *target)
{
    int result = mount(NULL, target, "imfs", RTEMS_FILESYSTEM_READ_WRITE, NULL);
    if (result != 0) {
        printf("  挂载失败 %s: errno=%d: %s\n", target, errno, strerror(errno));
    } else {
        printf("  挂载成功 %s\n", target);
    }
    return result;
}

static void create_file(const char *path, const char *content)
{
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, content, strlen(content));
        close(fd);
        printf("  创建文件成功: %s\n", path);
    } else {
        printf("  创建文件失败: %s (errno=%d: %s)\n", path, errno, strerror(errno));
    }
}

static void check_file_access(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        printf("  文件可访问: %s\n", path);
        char buffer[128];
        int n = read(fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            printf("  文件内容: %s\n", buffer);
        }
        close(fd);
    } else {
        printf("  文件不可访问: %s (errno=%d: %s)\n", path, errno, strerror(errno));
    }
}

static int setup_container_fs(const char *mount_point)
{
    mkdir(mount_point, 0777);

    printf("  尝试挂载IMFS到 %s\n", mount_point);
    int result = mount(NULL, mount_point, "imfs", RTEMS_FILESYSTEM_READ_WRITE, NULL);
    if (result == 0) {
        printf("  挂载成功 %s\n", mount_point);
        return 0;
    }

    printf("  挂载失败 %s: errno=%d: %s\n", mount_point, errno, strerror(errno));
    printf("  尝试使用chroot作为备选隔离方案...\n");

    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s/dev", mount_point);
    mkdir(tmp_path, 0777);
    snprintf(tmp_path, sizeof(tmp_path), "%s/tmp", mount_point);
    mkdir(tmp_path, 0777);

    result = chroot(mount_point);
    if (result != 0) {
        printf("  chroot失败: %s\n", strerror(errno));
        return -1;
    }
    chdir("/");
    printf("  成功使用chroot切换到%s\n", mount_point);
    return 0;
}

static rtems_task Init(rtems_task_argument ignored)
{
    printf("*** BEGIN MNT CONTAINER TEST ***\n");

#ifdef RTEMSCFG_MNT_CONTAINER
    Container    *globalContainer = rtems_container_get_root();
    MntContainer *rootMnt         = globalContainer->mntContainer;

    Thread_Control *self = (Thread_Control *)_Thread_Get_executing();
    Container *initContainer = NULL;
    if (self->container == NULL) {
        initContainer = (Container *)malloc(sizeof(Container));
        if (initContainer) {
            memset(initContainer, 0, sizeof(Container));
            initContainer->mntContainer = rootMnt;
            rootMnt->rc++;
            self->container = initContainer;
        }
    }

    printf("根容器地址: %p, 根MNT容器ID: %d\n",
           (void *)globalContainer, rtems_mnt_container_get_id(rootMnt));

    MntContainer *mntA = rtems_mnt_container_create();
    MntContainer *mntB = rtems_mnt_container_create();
    printf("创建容器A (ID=%d) 容器B (ID=%d)\n",
           rtems_mnt_container_get_id(mntA),
           rtems_mnt_container_get_id(mntB));

    int ret;

    printf("\n在根容器中创建挂载点目录...\n");
    ret = mkdir("/mntA", 0777);
    printf("mkdir /mntA: ret=%d errno=%d\n", ret, errno);
    ret = mkdir("/mntB", 0777);
    printf("mkdir /mntB: ret=%d errno=%d\n", ret, errno);

    printf("\n----- 切换到容器A (ID=%d) -----\n",
           rtems_mnt_container_get_id(mntA));
    rtems_mnt_container_move_task(rootMnt, mntA, self);

    ret = setup_container_fs("/mntA");
    create_file("/mntA/fileA.txt", "这是容器A中的文件内容");

    printf("验证容器A中文件可见性:\n");
    check_file_access("/mntA/fileA.txt");

    printf("\n----- 切换到容器B (ID=%d) -----\n",
           rtems_mnt_container_get_id(mntB));
    rtems_mnt_container_move_task(mntA, mntB, self);

    ret = setup_container_fs("/mntB");
    create_file("/mntB/fileB.txt", "这是容器B中的文件内容");

    printf("验证容器B中文件可见性:\n");
    check_file_access("/mntB/fileB.txt");

    printf("容器B中尝试访问容器A的文件 (期望: 不可访问):\n");
    check_file_access("/mntA/fileA.txt");

    printf("\n----- 切回容器A -----\n");
    rtems_mnt_container_move_task(mntB, mntA, self);

    printf("验证容器A中文件可见性:\n");
    check_file_access("/mntA/fileA.txt");

    printf("容器A中尝试访问容器B的文件 (期望: 不可访问):\n");
    check_file_access("/mntB/fileB.txt");

    printf("\n----- 切回根容器 -----\n");
    rtems_mnt_container_move_task(mntA, rootMnt, self);
    MntContainer *curMnt = (self->container) ? self->container->mntContainer : NULL;
    printf("当前MNT容器ID: %d (应为根容器ID=%d)\n",
           rtems_mnt_container_get_id(curMnt),
           rtems_mnt_container_get_id(rootMnt));

    printf("\n--- 所有MNT容器测试完成 ---\n");

#else
    printf("RTEMSCFG_MNT_CONTAINER not defined\n");
    printf("此测试需要MNT容器支持\n");
#endif

    printf("\n*** END MNT CONTAINER TEST ***\n");
    rtems_task_exit();
}

#define CONFIGURE_APPLICATION_DOES_NOT_NEED_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_FILESYSTEM_IMFS
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 20
#define CONFIGURE_MAXIMUM_TASKS            2
#define CONFIGURE_MAXIMUM_SEMAPHORES       4
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_ATTRIBUTES     RTEMS_FLOATING_POINT
#define CONFIGURE_INIT
#include <rtems/confdefs.h>