#include <machine/rtems-bsd-kernel-space.h>

/*
 * RTEMS versions of hostname functions
 * FIXME: Not thread-safe
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <rtems/rtems_bsdnet.h>
#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/kernel.h>

#ifdef RTEMSCFG_UTS_CONTAINER
#include <rtems/score/utsContainer.h>
#include <rtems/score/container.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/percpu.h>
#endif

#ifndef RTEMSCFG_UTS_CONTAINER
static char *rtems_hostname;
#endif

int
gethostname (char *name, size_t namelen)
{
#ifdef RTEMSCFG_UTS_CONTAINER
	Thread_Control *executing = _Thread_Get_executing();
	UtsContainer *uts;
	char *cp;

	if (executing && executing->container && executing->container->utsContainer) {
		uts = executing->container->utsContainer;
		cp = uts->name;
	} else {
		/* 如果无法获取容器,使用根容器 */
		Container *root = rtems_container_get_root();
		if (root && root->utsContainer) {
			cp = root->utsContainer->name;
		} else {
			cp = "";
		}
	}
#else
	char *cp = rtems_hostname;

	if (cp == NULL)
		cp = "";
#endif
	strncpy (name, cp, namelen);
	return 0;
}

int
sethostname (const char *name, size_t namelen)
{
#ifdef RTEMSCFG_UTS_CONTAINER
	Thread_Control *executing;
	UtsContainer *uts;

	if (namelen >= MAXHOSTNAMELEN) {
		errno = EINVAL;
		return -1;
	}

	executing = _Thread_Get_executing();
	if (executing && executing->container && executing->container->utsContainer) {
		uts = executing->container->utsContainer;
	} else {
		/* 如果无法获取容器,使用根容器 */
		Container *root = rtems_container_get_root();
		if (root && root->utsContainer) {
			uts = root->utsContainer;
		} else {
			errno = EINVAL;
			return -1;
		}
	}

	/* 直接修改容器中的主机名 */
	strncpy (uts->name, name, namelen);
	if (namelen < sizeof(uts->name)) {
		uts->name[namelen] = '\0';
	} else {
		uts->name[sizeof(uts->name) - 1] = '\0';
	}
	return 0;
#else
	char *old, *new;

	if (namelen >= MAXHOSTNAMELEN) {
		errno = EINVAL;
		return -1;
	}
	new = malloc (namelen + 1, M_HTABLE, M_NOWAIT);
	if (new == NULL) {
		errno = ENOMEM;
		return -1;
	}
	strncpy (new, name, namelen);
	new[namelen] = '\0';
	old = rtems_hostname;
	rtems_hostname = new;
	if (old)
		free (old, M_HTABLE);
	return 0;
#endif
}
