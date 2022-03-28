/*
 * Header file for the common RDMA routines used in the server/client example 
 * program. 
 *
 * Author: Animesh Trivedi 
 *          atrivedi@apache.org 
 *
 */

#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

//for thread pinning
#define _GNU_SOURCE
#include <sched.h> // cpu_set_t, CPU_SET
#include <dlfcn.h>
#include <errno.h> // EINVAL
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // sysconf


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include <netdb.h>
#include <netinet/in.h>	
#include <arpa/inet.h>
#include <sys/socket.h>

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>


/* Error Macro*/
#define rdma_error(msg, args...) do {\
	fprintf(stderr, "%s : %d : ERROR : "msg, __FILE__, __LINE__, ## args);\
}while(0);

// #define ACN_RDMA_DEBUG
#ifdef ACN_RDMA_DEBUG 
/* Debug Macro */
#define debug(msg, args...) do {\
    printf("DEBUG: "msg, ## args);\
}while(0);

#else 

#define debug(msg, args...) 

#endif /* ACN_RDMA_DEBUG */


/* The following numbers are specific to YAK machines and 
 * associated MLNX CX-5 RDMA NICs */

/* CPU freq on yak-00; may be different on other machines 
 * (TIP: Turn off frequency scaling) */
#define CPU_FREQ (2.5e9)

/* MAX work requests (limited by "max_qp_wr" dev attr ) */
#define MAX_WR (256)

/* Capacity of the completion queue (CQ) (limited by "max_cqe" dev attr) */
#define CQ_CAPACITY (2*MAX_WR)

/* MAX SGE capacity (limited by "max_sge" dev attr which is 30 for CX-5s) */
#define MAX_SGE (30)

/* MAX outstnading READ/ATOMIC ops 
 * (limited by "max_qp_init_rd_atom" and "max_qp_rd_atom" device attrs) */
#define MAX_RD_AT_IN_FLIGHT (16)
//#define MAX_RD_AT_IN_FLIGHT (64)

/* MAX memory registrations (limited by device resources, found empiirically for our NIC) */
#define MAX_MR (16000)

/* Default port where the RDMA server is listening */
#define DEFAULT_RDMA_PORT (20886)

/* Self-imposed limit of total queue pairs */
#define MAX_QPS 32

#define USE_DEVICE_MEMORY
#define DEVICE_MEMORY_KB 262144

/* Bit field for work request id (NOT Portable) */
union work_req_id {
    struct {
		unsigned qp_num 		: 6;		// max: 64
		unsigned window_slot 	: 10;		// max: 1024
		unsigned unused 		: 32;		
    } s;
    uint64_t val;
};


#define MEMORY_PERMISSION (IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_READ|IBV_ACCESS_REMOTE_ATOMIC|IBV_ACCESS_REMOTE_WRITE)

/* 
 * We use attribute so that compiler does not step in and try to pad the structure.
 * We use this structure to exchange information between the server and the client. 
 *
 * For details see: http://gcc.gnu.org/onlinedocs/gcc/Type-Attributes.html
 */
struct __attribute((packed)) rdma_buffer_attr {
  uint64_t address;
  uint32_t length;
  union stag {
	  /* if we send, we call it local stags */
	  uint32_t local_stag;
	  /* if we receive, we call it remote stag */
	  uint32_t remote_stag;
  }stag;
};

/* resolves a given destination name to sin_addr */
int get_addr(char *dst, struct sockaddr *addr);

/* prints RDMA buffer info structure */
void show_rdma_buffer_attr(struct rdma_buffer_attr *attr);

/* 
 * Processes an RDMA connection management (CM) event. 
 * @echannel: CM event channel where the event is expected. 
 * @expected_event: Expected event type 
 * @cm_event: where the event will be stored 
 */
int process_rdma_cm_event(struct rdma_event_channel *echannel, 
		enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event);

/* Allocates an RDMA buffer of size 'length' with permission permission. This 
 * function will also register the memory and returns a memory region (MR) 
 * identifier or NULL on error. 
 * @pd: Protection domain where the buffer should be allocated 
 * @length: Length of the buffer 
 * @permission: OR of IBV_ACCESS_* permissions as defined for the enum ibv_access_flags
 */
struct ibv_mr* rdma_buffer_alloc(struct ibv_pd *pd, 
		uint32_t length, 
		enum ibv_access_flags permission);


struct ibv_mr* rdma_buffer_alloc_dm(struct ibv_pd *pd, uint32_t size,
    enum ibv_access_flags permission);

struct ibv_mr *rdma_buffer_register_dm(struct ibv_pd *pd, 
		void *addr, uint32_t length, 
		enum ibv_access_flags permission);


/* Frees a previously allocated RDMA buffer. The buffer must be allocated by 
 * calling rdma_buffer_alloc();
 * @mr: RDMA memory region to free 
 */
void rdma_buffer_free(struct ibv_mr *mr);

/* This function registers a previously allocated memory. Returns a memory region 
 * (MR) identifier or NULL on error.
 * @pd: protection domain where to register memory 
 * @addr: Buffer address 
 * @length: Length of the buffer 
 * @permission: OR of IBV_ACCESS_* permissions as defined for the enum ibv_access_flags
 */
struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd, 
		void *addr, 
		uint32_t length, 
		enum ibv_access_flags permission);
/* Deregisters a previously register memory 
 * @mr: Memory region to deregister 
 */
void rdma_buffer_deregister(struct ibv_mr *mr);

/* Processes a work completion (WC) notification. 
 * @comp_channel: Completion channel where the notifications are expected to arrive 
 * @wc: Array where to hold the work completion elements 
 * @max_wc: Maximum number of expected work completion (WC) elements. wc must be 
 *          atleast this size.
 */
int process_work_completion_events(struct ibv_comp_channel *comp_channel, 
		struct ibv_wc *wc, 
		int max_wc);

/* prints some details from the cm id */
void show_rdma_cmid(struct rdma_cm_id *id);


int stick_this_thread_to_core(int core_id);


#endif /* RDMA_COMMON_H */
