/*
 * Virtual Ethernet Interface Driver for RTEMS Network Containers
 * 
 * Copyright (c) 2025
 * 
 * This driver provides virtual ethernet interfaces that work in pairs,
 * similar to Linux veth devices. Packets sent to one interface in a pair
 * are received by the peer interface, enabling inter-container communication.
 */

#ifndef _NET_IF_VETH_H_
#define _NET_IF_VETH_H_

/* Define _KERNEL to access kernel networking structures */
#ifndef _KERNEL
#define _KERNEL
#endif

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/mbuf.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <rtems.h>
#include <rtems/score/isrlock.h>

/* Network interface flags (if not defined in system headers) */
#ifndef IFF_UP
#define IFF_UP          0x0001  /* Interface is up */
#endif
#ifndef IFF_BROADCAST
#define IFF_BROADCAST   0x0002  /* Broadcast address valid */
#endif
#ifndef IFF_DEBUG
#define IFF_DEBUG       0x0004  /* Turn on debugging */
#endif
#ifndef IFF_LOOPBACK
#define IFF_LOOPBACK    0x0008  /* Is a loopback net */
#endif
#ifndef IFF_POINTOPOINT
#define IFF_POINTOPOINT 0x0010  /* Interface is point-to-point link */
#endif
#ifndef IFF_NOTRAILERS
#define IFF_NOTRAILERS  0x0020  /* Avoid use of trailers */
#endif
#ifndef IFF_DRV_RUNNING
#define IFF_DRV_RUNNING 0x0040  /* Resources allocated */
#endif
#ifndef IFF_NOARP
#define IFF_NOARP       0x0080  /* No address resolution protocol */
#endif
#ifndef IFF_PROMISC
#define IFF_PROMISC     0x0100  /* Receive all packets */
#endif
#ifndef IFF_ALLMULTI
#define IFF_ALLMULTI    0x0200  /* Receive all multicast packets */
#endif
#ifndef IFF_DRV_OACTIVE
#define IFF_DRV_OACTIVE 0x0400  /* Transmission in progress */
#endif
#ifndef IFF_SIMPLEX
#define IFF_SIMPLEX     0x0800  /* Can't hear own transmissions */
#endif
#ifndef IFF_LINK0
#define IFF_LINK0       0x1000  /* Per link layer defined bit */
#endif
#ifndef IFF_LINK1
#define IFF_LINK1       0x2000  /* Per link layer defined bit */
#endif
#ifndef IFF_LINK2
#define IFF_LINK2       0x4000  /* Per link layer defined bit */
#endif
#ifndef IFF_MULTICAST
#define IFF_MULTICAST   0x8000  /* Supports multicast */
#endif

/* Compatibility macros */
#ifndef IFF_RUNNING
#define IFF_RUNNING     IFF_DRV_RUNNING
#endif
#ifndef IFF_OACTIVE
#define IFF_OACTIVE     IFF_DRV_OACTIVE
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum queue size for pending packets
 */
#define VETH_QUEUE_SIZE 64

/*
 * Packet queue structure using mbuf
 */
struct mbuf_queue_entry {
    struct mbuf *m;
    TAILQ_ENTRY(mbuf_queue_entry) entries;
};

struct mbuf_queue {
    TAILQ_HEAD(, mbuf_queue_entry) head;
    int count;
    int max_size;
#if ISR_LOCK_NEEDS_OBJECT
    ISR_lock_Control lock;  /* ISR spinlock for thread-safe access */
#else
    int lock;
#endif
};

/*
 * Virtual Ethernet softc structure
 * Each veth interface has one of these
 */
struct veth_softc {
    struct arpcom arpcom;               /* Ethernet common part */
    struct veth_softc *peer;            /* Peer veth interface */
    struct mbuf_queue rx_queue;         /* Receive queue for incoming packets */
    rtems_id rx_task_id;                /* Receiver task ID */
    void *container;                    /* Associated network container */
    
    /* Statistics */
    unsigned long tx_packets;
    unsigned long tx_bytes;
    unsigned long rx_packets;
    unsigned long rx_bytes;
    unsigned long tx_dropped;
    unsigned long rx_dropped;
    
    /* Control flags */
    int running;                        /* Interface is up and running */
};

/*
 * Mbuf queue operations
 */

/**
 * Initialize an mbuf queue
 * 
 * @param queue Pointer to the queue structure
 * @param max_size Maximum number of packets the queue can hold
 * @return 0 on success, -1 on error
 */
int mbuf_queue_init(struct mbuf_queue *queue, int max_size);

/**
 * Enqueue an mbuf packet
 * 
 * @param queue Pointer to the queue structure
 * @param m Mbuf to enqueue
 * @return 0 on success, -1 if queue is full
 */
int mbuf_queue_enqueue(struct mbuf_queue *queue, struct mbuf *m);

/**
 * Dequeue an mbuf packet
 * 
 * @param queue Pointer to the queue structure
 * @return Mbuf pointer, or NULL if queue is empty
 */
struct mbuf *mbuf_queue_dequeue(struct mbuf_queue *queue);

/**
 * Destroy an mbuf queue and free all pending packets
 * 
 * @param queue Pointer to the queue structure
 */
void mbuf_queue_destroy(struct mbuf_queue *queue);

/*
 * Veth interface operations
 */

/**
 * Create a new veth interface
 * 
 * @param name Interface name (e.g., "veth0")
 * @param container_group Pointer to the container's network group
 * @return Pointer to veth_softc structure, or NULL on error
 */
struct veth_softc *veth_create(const char *name, void *container_group);

/**
 * Set up a veth pair by connecting two veth interfaces
 * 
 * @param veth1 First veth interface
 * @param veth2 Second veth interface
 * @return 0 on success, -1 on error
 */
int veth_set_pair(struct veth_softc *veth1, struct veth_softc *veth2);

/**
 * Destroy a veth interface
 * 
 * @param sc Pointer to the veth_softc structure
 */
void veth_destroy(struct veth_softc *sc);

/**
 * Start the veth interface (bring it up)
 * 
 * @param ifp Pointer to the network interface
 */
void veth_start(struct ifnet *ifp);

/**
 * Initialize the veth interface
 * 
 * @param arg Pointer to the veth_softc structure
 */
void veth_init(void *arg);

/**
 * Handle ioctl commands for veth interface
 * 
 * @param ifp Pointer to the network interface
 * @param command Ioctl command
 * @param data Command data
 * @return 0 on success, error code on failure
 */
int veth_ioctl(struct ifnet *ifp, ioctl_command_t command, caddr_t data);

/**
 * Receiver task entry point
 * Process packets in the receive queue
 * 
 * @param arg Pointer to the veth_softc structure
 */
void veth_rx_task(rtems_task_argument arg);

/**
 * Generate a random MAC address for veth interface
 * 
 * @param mac Buffer to store the MAC address (6 bytes)
 */
void veth_generate_mac(unsigned char *mac);

#ifdef __cplusplus
}
#endif

#endif /* _NET_IF_VETH_H_ */
