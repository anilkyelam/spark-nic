/*
 * An example RDMA client side code. 
 * Author: Animesh Trivedi 
 *         atrivedi@apache.org
 */

#include "rdma_common.h"
#include <sys/time.h>
#include <assert.h>


/* These are basic RDMA resources */
/* These are RDMA connection related resources */
static struct ibv_context **devices;
static struct rdma_event_channel *cm_event_channel = NULL;
static struct rdma_cm_id *cm_client_qp_id[MAX_QPS];
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *client_cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp[MAX_QPS];
static struct ibv_device_attr dev_attr;
/* These are memory buffers related resources */
static struct ibv_mr *client_qp_src_mr[MAX_QPS];
static struct ibv_mr *client_qp_dst_mr[MAX_QPS];
static struct ibv_mr *client_qp_metadata_mr[MAX_QPS];
static struct ibv_mr *server_qp_metadata_mr[MAX_QPS];
static struct rdma_buffer_attr client_qp_metadata_attr[MAX_QPS], server_qp_metadata_attr[MAX_QPS];
static struct ibv_send_wr client_send_wr, *bad_client_send_wr = NULL;
static struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = NULL;
static struct ibv_sge client_send_sge, server_recv_sge;
/* Source and Destination buffers, where RDMA operations source and sink */
static char *src = NULL, *dst = NULL; 

static int GLOBAL_GAP_INTEGER=0;
static int GLOBAL_KEYS=1;

static int finished_running_xput=0;
#define MAX_THREADS 32
#define MULTI_CQ

typedef struct {
    double err_code;
    double xput_ops;
    double xput_bps;
    double cq_poll_time_percent;        // in cpu cycles
    double cq_poll_count;
    double cq_empty_count;
} result_t;

result_t thread_results[MAX_THREADS];
static struct ibv_cq *client_cq_threads[MAX_THREADS];
static struct ibv_comp_channel *io_completion_channel_threads[MAX_THREADS];


/* Rdtsc blocks for time measurements */
unsigned cycles_low, cycles_high, cycles_low1, cycles_high1;
static __inline__ unsigned long long rdtsc(void)
{
   __asm__ __volatile__ ("RDTSC\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
            "%rax", "rbx", "rcx", "rdx");
}

static __inline__ unsigned long long rdtsc1(void)
{
   __asm__ __volatile__ ("RDTSC\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t": "=r" (cycles_high1), "=r" (cycles_low1)::
            "%rax", "rbx", "rcx", "rdx");
}

unsigned xcycles_low, xcycles_high, xcycles_low1, xcycles_high1;
static __inline__ unsigned long long xrdtsc(void)
{
   __asm__ __volatile__ ("RDTSC\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t": "=r" (xcycles_high), "=r" (xcycles_low)::
            "%rax", "rbx", "rcx", "rdx");
}

static __inline__ unsigned long long xrdtsc1(void)
{
   __asm__ __volatile__ ("RDTSC\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t": "=r" (xcycles_high1), "=r" (xcycles_low1)::
            "%rax", "rbx", "rcx", "rdx");
}


/* This is our testing function */
static int check_src_dst() 
{
    return memcmp((void*) src, (void*) dst, strlen(src));
}


/* A fast but good enough pseudo-random number generator. Good enough for what? */
/* Courtesy of https://stackoverflow.com/questions/1640258/need-a-fast-random-generator-for-c */
unsigned long rand_xorshf96(void) {          //period 2^96-1
    static unsigned long x=123456789, y=362436069, z=521288629;
    unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;
    return z;
}

uint32_t thread_contexts[MAX_THREADS];
/* This function prepares client side shared resources for all connections */
static int client_setup_shared_resources()
{
    int ret = -1;
    /* Get RDMA devices */
    devices = rdma_get_devices(&ret);
    if (ret == 0) {
        rdma_error("No RDMA devices found\n");
        return -ENODEV;
    }
    printf("%d devices found, using the first one: %s\n", ret, devices[0]->device->name);
    for(int i = 0; i < ret; i++)    debug("Device %d: %s\n", i+1, devices[i]->device->name);

    /* Create shared resources for all conections per device i.e., cq, pd, etc */
    /* Protection Domain (PD) is similar to a "process abstraction" 
     * in the operating system. All resources are tied to a particular PD. 
     * And accessing recourses across PD will result in a protection fault.
     */
    pd = ibv_alloc_pd(devices[0]);
    if (!pd) {
        rdma_error("Failed to alloc pd, errno: %d \n", -errno);
        return -errno;
    }
    debug("pd allocated at %p \n", pd);

    for(int i=0;i<MAX_THREADS;i++){
        printf("allocing completion channel %d\n",i);
        /* Now we need a completion channel, were the I/O completion 
        * notifications are sent. Remember, this is different from connection 
        * management (CM) event notifications. 
        * A completion channel is also tied to an RDMA device
        */
        io_completion_channel_threads[i] = ibv_create_comp_channel(devices[0]);
        printf("io completion channel @ %p\n",io_completion_channel_threads[i]);
        if (!io_completion_channel_threads[i]) {
            rdma_error("Failed to create IO completion event channel %d, errno: %d\n",
                    i,-errno);
            return -errno;
        }
        debug("completion event channel created at : %p \n", io_completion_channel_threads[i]);
    }

    ret = ibv_query_device(devices[0], &dev_attr);    
    if (ret) {
        rdma_error("Failed to get device info, errno: %d\n", -errno);
        return -errno;
    }
    printf("got device info. max qpe: %d, sge: %d, cqe: %d, max rd/at qp depth/outstanding: %d/%d max mr size: %lu\n", 
        dev_attr.max_qp_wr, dev_attr.max_sge, dev_attr.max_cqe, dev_attr.max_qp_init_rd_atom, dev_attr.max_qp_rd_atom, 
        dev_attr.max_mr_size);

    
    /*  Open a channel used to report asynchronous communication event */
    cm_event_channel = rdma_create_event_channel();
    if (!cm_event_channel) {
        rdma_error("Creating cm event channel failed, errno: %d \n", -errno);
        return -errno;
    }
    debug("RDMA CM event channel is created at : %p \n", cm_event_channel);

    printf("making thread completion queues as well\n");
    for(int i=0;i<MAX_THREADS;i++){
        thread_contexts[i]=i;
        client_cq_threads[i] = ibv_create_cq(devices[0] /* which device*/, 
            CQ_CAPACITY             /* maximum device capacity*/, 
            &thread_contexts[i]                    /* user context, not used here */,
            //io_completion_channel   /* which IO completion channel */, 
            io_completion_channel_threads[i]   /* which IO completion channel */, 
            //io_completion_channel_threads[i]   /* which IO completion channel */, 
            i                       /* signaling vector, not used here*/);
        if (!client_cq_threads[i]) {
            rdma_error("Failed to create CQ, errno: %d \n", -errno);
            return -errno;
        }
        printf("CQ created at %p with %d elements \n", client_cq_threads[i], client_cq_threads[i]->cqe);
        ret = ibv_req_notify_cq(client_cq_threads[i], 0);
        if (ret) {
            rdma_error("Failed to request notifications, errno: %d\n", -errno);
            return -errno;
        }

    }

    return ret;
}

/* This function prepares client side connection for a QP */
static int client_prepare_connection(struct sockaddr_in *s_addr, int qp_num, int port_num)
{
    int ret = -1;
    struct rdma_cm_event *cm_event = NULL;
    /* rdma_cm_id is the connection identifier (like socket) which is used 
    * to define an RDMA connection. 
    */
    ret = rdma_create_id(cm_event_channel, &cm_client_qp_id[qp_num], 
            devices[0],
            RDMA_PS_TCP);
    if (ret) {
        rdma_error("Creating cm id failed with errno: %d \n", -errno); 
        return -errno;
    }

    /* Resolve destination and optional source addresses from IP addresses  to
    * an RDMA address.  If successful, the specified rdma_cm_id will be bound
    * to a local device. */
    s_addr->sin_port = htons(port_num);
    ret = rdma_resolve_addr(cm_client_qp_id[qp_num], NULL, (struct sockaddr*) s_addr, 2000);
    if (ret) {
        rdma_error("Failed to resolve address, errno: %d \n", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ADDR_RESOLVED\n");
    ret  = process_rdma_cm_event(cm_event_channel, 
            RDMA_CM_EVENT_ADDR_RESOLVED,
            &cm_event);
    if (ret) {
        rdma_error("Failed to receive a valid event, ret = %d \n", ret);
        return ret;
    }
    /* we ack the event */
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the CM event, errno: %d\n", -errno);
        return -errno;
    }
    debug("RDMA address is resolved \n");
    /* Resolves an RDMA route to the destination address in order to 
    * establish a connection */
    ret = rdma_resolve_route(cm_client_qp_id[qp_num], 2000);
    if (ret) {
        rdma_error("Failed to resolve route, erno: %d \n", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ROUTE_RESOLVED\n");
    ret = process_rdma_cm_event(cm_event_channel, 
            RDMA_CM_EVENT_ROUTE_RESOLVED,
            &cm_event);
    if (ret) {
        rdma_error("Failed to receive a valid event, ret = %d \n", ret);
        return ret;
    }
    /* we ack the event */
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the CM event, errno: %d \n", -errno);
        return -errno;
    }
    printf("Trying to connect QP %d to server at : %s port: %d \n", qp_num, 
        inet_ntoa(s_addr->sin_addr), ntohs(s_addr->sin_port));

    /* TODO: Get capacity from device */
    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
    * Set capacity to device limits (since we use only one qp and one application) 
    * This only sets the limits; this will let us play around with the actual numbers */
    bzero(&qp_init_attr, sizeof qp_init_attr);
    qp_init_attr.cap.max_recv_sge = MAX_SGE;    /* Maximum SGE per receive posting;*/
    qp_init_attr.cap.max_recv_wr = MAX_WR;      /* Maximum receive posting capacity; */
    qp_init_attr.cap.max_send_sge = MAX_SGE;    /* Maximum SGE per send posting;*/
    qp_init_attr.cap.max_send_wr = MAX_WR;      /* Maximum send posting capacity; */
    qp_init_attr.cap.max_inline_data = 128;      /* Maximum amount of inline data */
    qp_init_attr.qp_type = IBV_QPT_RC;                  /* QP type, RC = Reliable connection */

    /* We use same completion queue, but one can use different queues */
    #ifdef MULTI_CQ
    qp_init_attr.recv_cq = client_cq_threads[qp_num]; /* Where should I notify for receive completion operations */
    qp_init_attr.send_cq = client_cq_threads[qp_num]; /* Where should I notify for send completion operations */
    #else
    qp_init_attr.recv_cq = client_cq; /* Where should I notify for receive completion operations */
    qp_init_attr.send_cq = client_cq; /* Where should I notify for send completion operations */
    #endif
    ret = rdma_create_qp(cm_client_qp_id[qp_num], pd, &qp_init_attr);
    if (ret) {
        rdma_error("Failed to create QP, errno: %d \n", -errno);
        return -errno;
    }
    client_qp[qp_num] = cm_client_qp_id[qp_num]->qp;
    printf("QP %d created at %p \n", qp_num, client_qp[qp_num]);
    return ret;
}

/* Pre-posts a receive buffer before calling rdma_connect () */
static int client_pre_post_recv_buffer(int qp_num)
{
    int ret = -1;
    server_qp_metadata_mr[qp_num] = rdma_buffer_register(pd,
            &server_qp_metadata_attr[qp_num],
            sizeof(server_qp_metadata_attr[qp_num]),
            (IBV_ACCESS_LOCAL_WRITE));
    if(!server_qp_metadata_mr[qp_num]){
        rdma_error("Failed to setup the server metadata mr , -ENOMEM\n");
        return -ENOMEM;
    }
    server_recv_sge.addr = (uint64_t) server_qp_metadata_mr[qp_num]->addr;
    server_recv_sge.length = (uint32_t) server_qp_metadata_mr[qp_num]->length;
    server_recv_sge.lkey = (uint32_t) server_qp_metadata_mr[qp_num]->lkey;
    /* now we link it to the request */
    bzero(&server_recv_wr, sizeof(server_recv_wr));
    server_recv_wr.sg_list = &server_recv_sge;
    server_recv_wr.num_sge = 1;
    ret = ibv_post_recv(client_qp[qp_num] /* which QP */,
            &server_recv_wr /* receive work request*/,
            &bad_server_recv_wr /* error WRs */);
    if (ret) {
        rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
        return ret;
    }
    debug("Receive buffer pre-posting is successful \n");
    return 0;
}

/* Connects a QP to an RDMA server QP */
static int client_connect_qp_to_server(int qp_num) 
{
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    bzero(&conn_param, sizeof(conn_param));
    conn_param.initiator_depth = MAX_RD_AT_IN_FLIGHT;
    conn_param.responder_resources = MAX_RD_AT_IN_FLIGHT;
    conn_param.retry_count = 3; // if fail, then how many times to retry
    ret = rdma_connect(cm_client_qp_id[qp_num], &conn_param);
    if (ret) {
        rdma_error("Failed to connect to remote host , errno: %d\n", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ESTABLISHED\n");
    ret = process_rdma_cm_event(cm_event_channel, 
            RDMA_CM_EVENT_ESTABLISHED,
            &cm_event);
    if (ret) {
        rdma_error("Failed to get cm event, ret = %d \n", ret);
           return ret;
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge cm event, errno: %d\n", 
                   -errno);
        return -errno;
    }
    printf("The client qp %d is connected successfully \n", qp_num);
    return 0;
}

/* Set up buffers to exchange data with the server, on a given QP. The client sends its, and then receives
 * from the server. The client-side metadata on the server is _not_ used because
 * this program is client driven. But it shown here how to do it for the illustration
 * purposes.
 * buffer: register as client buffer if provided.
 * buffer_size: if buffer is not provided, allocate buffers of this size on both client and server. 
 */
static int client_xchange_metadata_with_server(int qp_num, char* buffer, uint32_t buffer_size)
{
    struct ibv_wc wc[2];
    int ret = -1;
    
    client_qp_src_mr[qp_num] = 
        buffer == NULL ? 
            rdma_buffer_alloc(pd,
                buffer_size,
                MEMORY_PERMISSION
                ) :
            rdma_buffer_register(pd,
                buffer,
                strlen(buffer),
                MEMORY_PERMISSION);;
    if(!client_qp_src_mr[qp_num]){
        rdma_error("Failed to register the first buffer, ret = %d \n", ret);
        return ret;
    }

    /* we prepare metadata for the first buffer */
    client_qp_metadata_attr[qp_num].address = (uint64_t) client_qp_src_mr[qp_num]->addr; 
    client_qp_metadata_attr[qp_num].length = client_qp_src_mr[qp_num]->length; 
    client_qp_metadata_attr[qp_num].stag.local_stag = client_qp_src_mr[qp_num]->lkey;
    /* now we register the metadata memory */
    client_qp_metadata_mr[qp_num] = rdma_buffer_register(pd,
            &client_qp_metadata_attr[qp_num],
            sizeof(client_qp_metadata_attr[qp_num]),
            IBV_ACCESS_LOCAL_WRITE);
    if(!client_qp_metadata_mr[qp_num]) {
        rdma_error("Failed to register the client metadata buffer, ret = %d \n", ret);
        return ret;
    }

    /* now we fill up SGE */
    client_send_sge.addr = (uint64_t) client_qp_metadata_mr[qp_num]->addr;
    client_send_sge.length = (uint32_t) client_qp_metadata_mr[qp_num]->length;
    client_send_sge.lkey = client_qp_metadata_mr[qp_num]->lkey;
    /* now we link to the send work request */
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list = &client_send_sge;
    client_send_wr.num_sge = 1;
    client_send_wr.opcode = IBV_WR_SEND;
    client_send_wr.send_flags = IBV_SEND_SIGNALED;
    /* Now we post it */
    ret = ibv_post_send(client_qp[qp_num], 
               &client_send_wr,
           &bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to send client metadata, errno: %d \n", 
                -errno);
        return -errno;
    }
    /* at this point we are expecting 2 work completion. One for our 
     * send and one for recv that we will get from the server for 
     * its buffer information */
    ret = process_work_completion_events(io_completion_channel_threads[qp_num], 
            wc, 2);
    if(ret != 2) {
        rdma_error("We failed to get 2 work completions , ret = %d \n",
                ret);
        return ret;
    }
    debug("Server sent us buffer location and credentials for QP %d, showing \n", qp_num);
    show_rdma_buffer_attr(&server_qp_metadata_attr[qp_num]);
    return 0;
}

/* This function does the following for given QP:
 * 1) Prepare memory buffers for RDMA operations 
 * 1) RDMA write from src -> remote buffer 
 * 2) RDMA read from remote bufer -> dst
 */ 
static int client_remote_memory_ops(int qp_num) 
{
    struct ibv_wc wc;
    int ret = -1;
    struct timeval start, end;
    long ops_count = 0;
    double duration = 0.0;
    double throughput = 0.0;
    uint64_t start_cycles, end_cycles;

    client_qp_dst_mr[qp_num] = rdma_buffer_register(pd,
        dst,
        strlen(src), MEMORY_PERMISSION
        );
    if (!client_qp_dst_mr[qp_num]) {
        rdma_error("We failed to create the destination buffer, -ENOMEM\n");
        return -ENOMEM;
    }
    /* Step 1: is to copy the local buffer into the remote buffer. We will 
     * reuse the previous variables. */
    /* now we fill up SGE */
    client_send_sge.addr = (uint64_t) client_qp_src_mr[qp_num]->addr;
    client_send_sge.length = (uint32_t) client_qp_src_mr[qp_num]->length;
    client_send_sge.lkey = client_qp_src_mr[qp_num]->lkey;
    /* now we link to the send work request */
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list = &client_send_sge;
    client_send_wr.num_sge = 1;
    client_send_wr.opcode = IBV_WR_RDMA_WRITE;
    client_send_wr.send_flags = IBV_SEND_SIGNALED;
    /* we have to tell server side info for RDMA */
    client_send_wr.wr.rdma.rkey = server_qp_metadata_attr[qp_num].stag.remote_stag;
    client_send_wr.wr.rdma.remote_addr = server_qp_metadata_attr[qp_num].address;


    rdtsc();
    /* Now we post it */
    ret = ibv_post_send(client_qp[qp_num], 
               &client_send_wr,
           &bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to write client src buffer, errno: %d \n", 
                -errno);
        return -errno;
    }
    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(io_completion_channel_threads[qp_num], &wc, 1);
    if(ret != 1) {
        rdma_error("We failed to get 1 work completions , ret = %d \n", ret);
        return ret;
    }
    rdtsc1();

    debug("Client side WRITE is complete \n");
    start_cycles = ( ((int64_t)cycles_high << 32) | cycles_low );
    end_cycles = ( ((int64_t)cycles_high1 << 32) | cycles_low1 );
    printf("Client side WRITE took %lf mu-sec\n", (end_cycles - start_cycles) * 1e6 / CPU_FREQ);

    /* Now we prepare a READ using same variables but for destination */
    client_send_sge.addr = (uint64_t) client_qp_dst_mr[qp_num]->addr;
    client_send_sge.length = (uint32_t) client_qp_dst_mr[qp_num]->length;
    client_send_sge.lkey = client_qp_dst_mr[qp_num]->lkey;
    /* now we link to the send work request */
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list = &client_send_sge;
    client_send_wr.num_sge = 1;
    client_send_wr.opcode = IBV_WR_RDMA_READ;
    client_send_wr.send_flags = IBV_SEND_SIGNALED;
    /* we have to tell server side info for RDMA */
    client_send_wr.wr.rdma.rkey = server_qp_metadata_attr[qp_num].stag.remote_stag;
    client_send_wr.wr.rdma.remote_addr = server_qp_metadata_attr[qp_num].address;
    /* Now we post it */
    ret = ibv_post_send(client_qp[qp_num], &client_send_wr, &bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to read client dst buffer from the master, errno: %d \n", -errno);
        return -errno;
    }
    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(io_completion_channel_threads[qp_num], &wc, 1);
    if(ret != 1) {
        rdma_error("We failed to get 1 work completions , ret = %d \n", ret);
        return ret;
    }
    debug("Client side READ is complete \n");
    
    // This buffer is local to this method, won't need it anymore.
    rdma_buffer_deregister(client_qp_dst_mr[qp_num]);	
    return 0;
}



/* List of ops that are being instrumented */
enum rdma_measured_op { RDMA_READ_OP, RDMA_WRITE_OP, RDMA_CAS_OP, RDMA_FAA_OP};
enum mem_reg_mode { 
    MR_MODE_PRE_REGISTER,               // Use a pre-registered buffer and use it for all transactions
    MR_MODE_PRE_REGISTER_WITH_ROTATE,   // Use a set of pre-registered buffers (for the same piece of memory) but rotate their usage
    MR_MODE_REGISTER_IN_DATAPTH,        // Register buffers in datapath as necessary
};

struct xput_thread_args {
    int thread_id;
    int core;
    int num_concur;
    struct ibv_cq *cq_ptr;
    int msg_size;
    enum rdma_measured_op rdma_op;      // rdma op to use i.e., read or write
    uint64_t start_cycles;
    struct ibv_mr **mr_buffers;           /* Make sure to deregister these local MRs before exiting */
    int num_lbuffers;
};

#define HIST_SIZE 32
uint32_t qp_rec_global[MAX_THREADS][HIST_SIZE];

inline int get_buf_offset(int slot_num, int msg_size) {
    return slot_num * (msg_size + GLOBAL_GAP_INTEGER);
}

int get_buffer_size(int num_concur,int msg_size) {
    return num_concur *(get_buf_offset(1,msg_size) - get_buf_offset(0, msg_size));
}

void * xput_thread(void * args) {
    struct xput_thread_args * targs = (struct xput_thread_args *)args;
    //printf("Hello from xput going to core %d\n",targs->core);
    stick_this_thread_to_core(targs->core);

    int num_concur = targs->num_concur;
    struct ibv_cq *cq_ptr = targs->cq_ptr;
    int msg_size = targs->msg_size;
    enum rdma_measured_op rdma_op = targs->rdma_op;
    struct ibv_mr **mr_buffers=targs->mr_buffers;           /* Make sure to deregister these local MRs before exiting */
    int num_lbuffers = targs->num_lbuffers;

    uint64_t start_cycles, end_cycles;
    start_cycles=targs->start_cycles;


    struct ibv_wc* wc;
    int ret = -1, n, i;
    uint64_t xstart_cycles, xend_cycles;
    struct timeval      start, end;

    const uint64_t minimum_duration_secs = 20;  /* number of seconds to run the experiment for at least */
    const uint64_t check_watch_interval = 1e6;  /* Check time every million requests as checking on every request might be too costly */
    int stop_posting = 0;
    uint64_t poll_time = 0, poll_count = 0, idle_count = 0;
    uint64_t wr_posted = 0, wr_acked = 0;
    int qp_num = 0;
    union work_req_id wr_id;

    struct ibv_sge local_client_send_sge;
    local_client_send_sge.addr = (uint64_t) mr_buffers[targs->thread_id]->addr;  
    local_client_send_sge.length = (uint32_t) msg_size;           // Send only msg_size
    local_client_send_sge.lkey = mr_buffers[targs->thread_id]->lkey;

    struct ibv_send_wr local_client_send_wr, *local_bad_client_send_wr;

    #define BATCH_SIZE 1024
    struct ibv_send_wr local_client_send_wr_batch[BATCH_SIZE];
    //local_client_send_wr = client_send_wr;


    bzero(&local_client_send_wr_batch, sizeof(local_client_send_wr)*BATCH_SIZE);
    for (int i=0;i<BATCH_SIZE;i++) {
        local_client_send_wr_batch[i].sg_list = &local_client_send_sge;
        local_client_send_wr_batch[i].num_sge = 1;
        switch (rdma_op) {
            case RDMA_READ_OP:
                local_client_send_wr_batch[i].opcode = IBV_WR_RDMA_READ;
            break;
            case RDMA_WRITE_OP:
                local_client_send_wr_batch[i].opcode = IBV_WR_RDMA_WRITE;
                local_client_send_wr_batch[i].send_flags |= IBV_SEND_INLINE;          // NOTE: This tells the other NIC to send completion events
            break;
            case RDMA_CAS_OP:
                local_client_send_wr_batch[i].opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
            break;
            case RDMA_FAA_OP:
                local_client_send_wr_batch[i].opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
            break;
        }
        //client_send_wr.opcode = rdma_op == RDMA_READ_OP ? IBV_WR_RDMA_READ : IBV_WR_RDMA_WRITE;
        local_client_send_wr_batch[i].send_flags |= IBV_SEND_SIGNALED;          // NOTE: This tells the other NIC to send completion events
        //local_client_send_wr.send_flags = IBV_SEND_INLINE;          // NOTE: This tells the other NIC to send completion events
        /* we have to tell server side info for RDMA */
        local_client_send_wr_batch[i].wr.rdma.rkey = server_qp_metadata_attr[0].stag.remote_stag;
        local_client_send_wr_batch[i].wr.rdma.remote_addr = server_qp_metadata_attr[0].address;

        //From clover setting up atomics (userspace_one_cs)
        if (rdma_op == RDMA_CAS_OP) {
            local_client_send_wr_batch[i].wr.atomic.remote_addr = server_qp_metadata_attr[0].address;
            local_client_send_wr_batch[i].wr.atomic.rkey = server_qp_metadata_attr[0].stag.remote_stag;
            local_client_send_wr_batch[i].wr.atomic.compare_add = 0;
            local_client_send_wr_batch[i].wr.atomic.swap = 0;
        }

        if (rdma_op == RDMA_FAA_OP) {
            local_client_send_wr_batch[i].wr.atomic.remote_addr = server_qp_metadata_attr[0].address;
            local_client_send_wr_batch[i].wr.atomic.rkey = server_qp_metadata_attr[0].stag.remote_stag;
            local_client_send_wr_batch[i].wr.atomic.compare_add = 1ULL;
        }
    }

    result_t result;

    uint64_t local_server_address = server_qp_metadata_attr[0].address;

    int	buf_offset = 0, slot_num = 0;
    int buf_num = 0;

    char * buf_ptr;

    //printf("[Thread %d] io ref %d\n",targs->thread_id, cq_ptr->channel->refcnt);

    gettimeofday (&start, NULL);
    rdtsc();
    start_cycles = ( ((int64_t)cycles_high << 32) | cycles_low );
    uint32_t *qp_rec=qp_rec_global[targs->thread_id];

    bzero(qp_rec,HIST_SIZE*sizeof(uint32_t));

    // printf("\n");
    // for(int i=0;i<8;i++){
    //     printf("(tid %d) %d\t%d \tqp_hist_loc[%p]\n",targs->thread_id,i,qp_rec[i],qp_rec);
    // }

    wc = (struct ibv_wc *) calloc (num_concur, sizeof(struct ibv_wc));
    do {
        /* Poll the completion queue for the completion event for the earlier write */
        //xrdtsc();
        do {
            //printf("Thread %d, CQ %p\n",targs->thread_id,cq_ptr);
            n = ibv_poll_cq(cq_ptr, num_concur, wc);       // get upto num_concur entries
            //printf("exit poll\n");
            if (n < 0) {
                printf("Failed to poll cq for wc due to %d\n", ret);
                rdma_error("Failed to poll cq for wc due to %d \n", ret);
                exit(1);
            }
            poll_count++;
            if (n == 0) {
                idle_count++;
                if (finished_running_xput) {
                    //printf("IDLE BREAK FROM FINSIHED RUN %d\n",wr_posted++);
                    break;
                }
            }     
        } while (n < 1);
        /* For each completed request */
        //hist_size[n]++;
        for (i = 0; i < n; i++) {
            /* Check that it succeeded */
            /*
            if (wc[i].status != IBV_WC_SUCCESS) {
                rdma_error("Work completion (WC) has error status: %d, %s at index %ld\n",  
                    wc[i].status, ibv_wc_status_str(wc[i].status), wc[i].wr_id);
            }*/

            if (!finished_running_xput) {
                /* Issue another request that reads/writes from same locations as the completed one */


                wr_id.val =  wc[i].wr_id;
                qp_rec[wr_id.s.qp_num]++;
                qp_num = targs->thread_id;
                slot_num = wr_id.s.window_slot;

                //buf_offset = slot_num * msg_size;
                buf_offset = get_buf_offset(slot_num,msg_size);
                buf_ptr     = mr_buffers[buf_num]->addr + buf_offset;

                local_client_send_wr_batch[i].wr_id = wr_id.val;              /* User-assigned id to recognize this WR on completion */
                local_client_send_wr_batch[i].wr.rdma.remote_addr = local_server_address + buf_offset;
                
                if(rdma_op == RDMA_CAS_OP){
                    local_client_send_wr_batch[i].wr.atomic.remote_addr = local_server_address + buf_offset;
                    local_client_send_wr_batch[i].wr.atomic.compare_add = 0;
                    local_client_send_wr_batch[i].wr.atomic.swap = 0;
                } else if(rdma_op == RDMA_FAA_OP){
                    local_client_send_wr_batch[i].wr.atomic.remote_addr = local_server_address + buf_offset;
                    local_client_send_wr_batch[i].wr.atomic.compare_add = 1ULL;
                }
                buf_num     = targs->thread_id;

                // ret = ibv_post_send(client_qp[qp_num], 
                //         &local_client_send_wr_batch[i],
                //         &local_bad_client_send_wr);
                // if (ret) {
                //     rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
                //     exit(1);
                // }
                // wr_posted++;
            }
        }
        //configure the list
        if (!finished_running_xput) {
            for (i = 0; i < n-1; i++) {
                local_client_send_wr_batch[i].next=&local_client_send_wr_batch[i+1];
            }
            local_client_send_wr_batch[n-1].next=NULL;
            ret = ibv_post_send(client_qp[qp_num], 
                    &local_client_send_wr_batch[0],
                    &local_bad_client_send_wr);
            if (ret) {
                rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
                exit(1);
            }
            wr_posted+=n;
        }


        wr_acked += n;
    } while(!finished_running_xput);
    
    rdtsc1();
    end_cycles = ( ((int64_t)cycles_high1 << 32) | cycles_low1 );
    gettimeofday (&end, NULL);

    /* Calculate duration. See if RDTSC timer agrees with regular ctime.
     * ctime is more accurate on longer timescales as rdtsc depends on cpu frequency which is not stable
     * but rdtsc is low overhead so we can use that from (roughly) keeping track of time while we use ctime 
     * to calculate numbers */
    double duration_rdtsc = (end_cycles - start_cycles) / CPU_FREQ;
    double duration_ctime = (double)((end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) * 1.0e-6);
    //printf("Duration as measured by RDTSC: %.2lf secs, by ctime: %.2lf secs\n", duration_rdtsc, duration_ctime);

    double goodput_pps = wr_acked / duration_ctime;
    double goodput_bps = goodput_pps * msg_size * 8;
    //printf("Goodput = %.2lf Gbps. Sampled for %.2lf seconds\n", goodput_bps / 1e9, duration_ctime);
    
    /* Fill in result */
    result.xput_bps = goodput_bps / 1e9;
    result.xput_ops = goodput_pps;
    result.cq_poll_time_percent = poll_time * 100.0 / CPU_FREQ / duration_rdtsc;
    result.cq_poll_count = poll_count * 1.0 / wr_acked;     // TODO: Is dividing by wr the right way to interpret this number?
    result.cq_empty_count = idle_count * 1.0 / wr_acked;     // TODO: Is dividing by wr the right way to interpret this number?
    thread_results[targs->thread_id]=result;

}

/* Measures throughput for RDMA READ/WRITE ops for a specified message size and number of concurrent messages (i.e., requests in flight) */
/* Returns xput in ops/sec */
static result_t measure_xput(
    uint32_t msg_size,                  // payload size
    int num_concur,                     // number of requests in flight; use 1 for RTT measurements
    enum rdma_measured_op rdma_op,      // rdma op to use i.e., read or write
    enum mem_reg_mode mr_mode,          // mem reg mode; see comments for each "enum mem_reg_mode" 
    int num_lbuffers,                   // number of pre-registed buffers to rotate between; only valid for MR_MODE_PRE_REGISTER_WITH_ROTATE
    int num_qps                         // num of QPs to use
) {
    int ret = -1, n, i;
    struct ibv_wc* wc;
    uint64_t start_cycles, end_cycles;
    uint64_t xstart_cycles, xend_cycles;
    struct timeval      start, end;
    struct ibv_cq *cq_ptr = NULL;
    void *context = NULL;
    struct ibv_mr **mr_buffers = NULL;           /* Make sure to deregister these local MRs before exiting */
    struct ibv_mr *tmpbuffer = NULL;
    result_t result;
    union work_req_id wr_id;
    int qp_num = 0;

    
    /* Learned that the limit for number of outstanding requests for READ and ATOMIC ops 
     * is very less; while there is no such limit for WRITES. The connection parameters initiator_depth
     * and responder_resources determine such limit, which are again limited by hardware device 
     * attributes max_qp_rd_atom and max_qp_init_rd_atom. Why is there such a limit?  */
    /* In any case, for now, disallow request for concurrency more than the limit */
    if ((rdma_op == RDMA_READ_OP || rdma_op == RDMA_CAS_OP || rdma_op == RDMA_FAA_OP) && num_concur > (MAX_RD_AT_IN_FLIGHT * num_qps)) {
        rdma_error("Device cannot support more than %d outstnading READs (num_concur=%d, qps %d)\n", 
            MAX_RD_AT_IN_FLIGHT, num_concur,num_qps);
        result.err_code = -EOVERFLOW;
        return result;
    }

    /*
    num_lbuffers = (mr_mode == MR_MODE_PRE_REGISTER_WITH_ROTATE) ? num_lbuffers : 1;
    if (num_lbuffers <= 0) {
        rdma_error("Invalid number of client-side buffers provided: %d\n", num_lbuffers);
        result.err_code = -EINVAL;
        return result; 
    }*/

    /* Allocate client buffers to read/write from (use the same piece of underlying memory) */
    mr_buffers = (struct ibv_mr**) malloc(num_lbuffers * sizeof(struct ibv_mr*));
    //size_t buf_size = msg_size * num_concur;
    size_t buf_size = get_buffer_size(num_concur,msg_size);
    mr_buffers[0] = rdma_buffer_alloc(pd,
        buf_size, MEMORY_PERMISSION
            
            );
    if (!mr_buffers[0]) {
        rdma_error("We failed to create the destination buffer, -ENOMEM\n");
        result.err_code = -ENOMEM;
        return result;
    }
    debug("Buffer registered (%3d): addr = %p, length = %ld, handle = %d, lkey = %d, rkey = %d\n", 0,
        mr_buffers[0]->addr, mr_buffers[0]->length, mr_buffers[0]->handle, mr_buffers[0]->lkey, mr_buffers[0]->rkey);

    if (num_lbuffers > 1) {
        /* Register rest of the buffers using same piece of memory */
        for (i = 1; i < num_lbuffers; i++) {
            mr_buffers[i] = rdma_buffer_register(pd,
                mr_buffers[0]->addr,
                buf_size, MEMORY_PERMISSION
                    );

            if (!mr_buffers[i]) {
                rdma_error("Registering buffer %d failed\n", i);
                result.err_code = -ENOMEM;
                return result;
            }
            
            debug("Buffer registered (%3d): addr = %p, length = %ld, handle = %d, lkey = %d, rkey = %d\n", i,
                mr_buffers[i]->addr, mr_buffers[i]->length, mr_buffers[i]->handle, mr_buffers[i]->lkey, mr_buffers[i]->rkey);
        }
    }

    /* For sanity check, fill the client buffer with different alphabets in each message. 
     * We can verify this by reading the server buffer later. Only works for RDMA WRITEs though */
    for (i = 0; i < num_concur; i++)
        memset(mr_buffers[0]->addr + (i*msg_size), 'a' + i%26, msg_size);

    /* Prepare a template WR for RDMA ops */
    client_send_sge.addr = (uint64_t) mr_buffers[0]->addr;  
    client_send_sge.length = (uint32_t) msg_size;           // Send only msg_size
    client_send_sge.lkey = mr_buffers[0]->lkey;
    /* now we link to the send work request */
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list = &client_send_sge;
    client_send_wr.num_sge = 1;
    switch (rdma_op) {
        case RDMA_READ_OP:
            client_send_wr.opcode = IBV_WR_RDMA_READ;
        break;
        case RDMA_WRITE_OP:
            client_send_wr.opcode = IBV_WR_RDMA_WRITE;
            client_send_wr.send_flags |= IBV_SEND_INLINE;          // NOTE: This tells the other NIC to send completion events
        break;
        case RDMA_CAS_OP:
            client_send_wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
        break;
        case RDMA_FAA_OP:
            client_send_wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
            //client_send_wr.send_flags |= IBV_EXP_SEND_EXT_ATOMIC_INLINE;
        break;
    }
    //client_send_wr.opcode = rdma_op == RDMA_READ_OP ? IBV_WR_RDMA_READ : IBV_WR_RDMA_WRITE;
    client_send_wr.send_flags |= IBV_SEND_SIGNALED;          // NOTE: This tells the other NIC to send completion events
    /* we have to tell server side info for RDMA */
    client_send_wr.wr.rdma.rkey = server_qp_metadata_attr[0].stag.remote_stag;
    client_send_wr.wr.rdma.remote_addr = server_qp_metadata_attr[0].address;

    //From clover setting up atomics (userspace_one_cs)
    if (rdma_op == RDMA_CAS_OP) {
        client_send_wr.wr.atomic.remote_addr = server_qp_metadata_attr[0].address;
        client_send_wr.wr.atomic.rkey = server_qp_metadata_attr[0].stag.remote_stag;
        client_send_wr.wr.atomic.compare_add = 0;
        client_send_wr.wr.atomic.swap = 0;
    }
    if (rdma_op == RDMA_FAA_OP) {
        client_send_wr.wr.atomic.remote_addr = server_qp_metadata_attr[0].address;
        client_send_wr.wr.atomic.rkey = server_qp_metadata_attr[0].stag.remote_stag;
        client_send_wr.wr.atomic.compare_add = 1ULL;
    }


    gettimeofday (&start, NULL);
    rdtsc();
    start_cycles = ( ((int64_t)cycles_high << 32) | cycles_low );

    /* Post until number of max requests in flight is hit */
    char *buf_ptr = mr_buffers[0]->addr;
    int	buf_offset = 0, slot_num = 0;
    int buf_num = 0;
    uint64_t wr_posted = 0, wr_acked = 0;

    for (i = 0; i < num_concur; i++) {
        /* it is safe to reuse client_send_wr object after post_() returns */
        client_send_sge.lkey = mr_buffers[buf_num]->lkey;   /* Sets which MR to use to access the local address */   
        //wr_id.s.qp_num = qp_num;
        //wr_id.s.window_slot = slot_num;
        wr_id.s.qp_num=qp_num;
        wr_id.s.window_slot=slot_num;
        client_send_wr.wr_id = wr_id.val;                  /* User-assigned id to recognize this WR on completion */

        //client_send_wr.wr_id = wr_id.val;                  /* User-assigned id to recognize this WR on completion */
        client_send_sge.addr = (uint64_t) buf_ptr;          /* Sets which mem addr to read from/write to locally */  // FIXME
        client_send_wr.wr.rdma.remote_addr = server_qp_metadata_attr[0].address + buf_offset; 

        if (rdma_op == RDMA_CAS_OP) {
            client_send_wr.wr.atomic.remote_addr = server_qp_metadata_attr[0].address + buf_offset;
            client_send_wr.wr.atomic.compare_add = 0;
            client_send_wr.wr.atomic.swap = 0;
        }

        if (rdma_op == RDMA_FAA_OP) {
            client_send_wr.wr.atomic.remote_addr = server_qp_metadata_attr[0].address + buf_offset;
            client_send_wr.wr.atomic.compare_add = 1ULL;
        }

        //printf("i %d\n",i);
        ret = ibv_post_send(client_qp[qp_num], 
                &client_send_wr,
                &bad_client_send_wr);
        if (ret) {
            rdma_error("Failed to write client src buffer, errno: %d \n", -errno);
            result.err_code = -errno;
            return result;
        }
        	
        wr_posted++;

        #define PARALLEL_KEYS 1
        //slot_num    = (slot_num + 1) % num_concur;
        slot_num    = (slot_num + 1) % GLOBAL_KEYS;


        //buf_offset  = slot_num * msg_size * 1024;
        buf_offset = get_buf_offset(slot_num,msg_size);
        buf_num     = (buf_num + 1) % num_lbuffers;
	    buf_ptr     = mr_buffers[buf_num]->addr + buf_offset;     /* We can always use mr_buffers[0] as all buffers point to same memory */
        //printf("%p buf\n",buf_ptr);
        // buf_num     = rand_xorshf96() % num_lbuffers;
        qp_num      = (qp_num + 1) % num_qps;

    }

    pthread_t threadId[MAX_THREADS];
    int32_t total_threads = num_qps;

    struct ibv_cq *local_client_cq_threads[MAX_THREADS];

    // for (int i=0;i<total_threads;i++){
    //     printf("Global CQ %p\n",client_cq_threads[i]);
    // }


    for (int i=0;i<total_threads;i++){
        ret = ibv_get_cq_event(io_completion_channel_threads[i], &local_client_cq_threads[i], &context);
        //printf("Local CQ %p\n",local_client_cq_threads[i]);
        if (ret) {
            rdma_error("Failed to get next CQ event due to %d \n", -errno);
            result.err_code = -errno;
            return result;
        }
        ret = ibv_req_notify_cq(local_client_cq_threads[i], 0);
        if (ret) {
            rdma_error("Failed to request further notifications %d \n", -errno);
            result.err_code = -errno;
            return result;
        }
    }


    struct xput_thread_args targs[MAX_THREADS];
    for (int i=0;i<total_threads;i++){
        targs[i].thread_id=i;
        //targs[i].core=4+(2*i);
        targs[i].core=(2*i);
        targs[i].num_concur=num_concur;
        targs[i].cq_ptr=client_cq_threads[i];
        //targs[i].cq_ptr=cq_ptr;
        targs[i].msg_size = msg_size;
        targs[i].rdma_op = rdma_op;
        targs[i].num_lbuffers = num_lbuffers;
        targs[i].start_cycles = start_cycles;
        targs[i].mr_buffers = mr_buffers;
        finished_running_xput=0;
        pthread_create(&threadId[i], NULL, &xput_thread, (void*)&targs[i]);
        //Setupt next itt
    }

    const uint64_t minimum_duration_secs = 10;  /* number of seconds to run the experiment for at least */
    const uint64_t check_watch_interval = 1e6;  /* Check time every million requests as checking on every request might be too costly */
    int stop_posting = 0;
    uint64_t poll_time = 0, poll_count = 0, idle_count = 0;


    do {
        rdtsc1();
        end_cycles = ( ((int64_t)cycles_high1 << 32) | cycles_low1 );
        if ((end_cycles - start_cycles) >= minimum_duration_secs * CPU_FREQ) {
            //printf("Time is up, sending kill signal to threads");
            finished_running_xput = 1;
        }
        sleep(1);

    } while(!finished_running_xput);
    
    for (int i=0;i<total_threads;i++){
        pthread_join(threadId[i],NULL);
    }

    sleep(1);
    wc = (struct ibv_wc *) calloc (num_concur, sizeof(struct ibv_wc));
    uint32_t unfinished_requests=0;
    for (int i=0;i<total_threads;i++){
        do {
            //printf("Thread %d, CQ %p\n",targs->thread_id,cq_ptr);
            n = ibv_poll_cq(client_cq_threads[i], num_concur, wc);       // get upto num_concur entries
            //printf("exit poll\n");
            if (n < 0) {
                printf("Failed to poll cq for wc due to %d\n", ret);
                rdma_error("Failed to poll cq for wc due to %d \n", ret);
                exit(1);
            }
            unfinished_requests+=n;
            if (n == 0) {
                break;
            }     
        } while (n < 1);
    }



    /* Similar to connection management events, we need to acknowledge CQ events */
    //ibv_ack_cq_events(local_client_cq_threads[0], 1 /* we received one event notification. This is not number of WC elements */);
    for (int i=0;i<total_threads;i++){
        ibv_ack_cq_events(local_client_cq_threads[i], 64 /* we received one event notification. This is not number of WC elements */);
    }


    /* Deregister local MRs */
    for (i = 0; i < num_lbuffers; i++)
        rdma_buffer_deregister(mr_buffers[i]);

    result_t meta_result;
    meta_result.cq_empty_count=0;
    meta_result.cq_poll_count=0;
    meta_result.xput_bps=0;
    meta_result.xput_ops=0;
    for (i=0;i<total_threads;i++){
        meta_result.cq_empty_count+=thread_results[i].cq_empty_count;
        meta_result.cq_poll_count+=thread_results[i].cq_poll_count;
        meta_result.xput_bps+=thread_results[i].xput_bps;
        meta_result.xput_ops+=thread_results[i].xput_ops;
    }

    return meta_result;
}

/* Data transfer modes */
enum data_transfer_mode { 
    DTR_MODE_NO_GATHER,         // Assume that data is already gathered in a single big buffer
    DTR_MODE_CPU_GATHER,        // Assume that data is scattered but is gathered by CPU into a single big buffer before xmit
    DTR_MODE_NIC_GATHER,        // Assume that data is scattered but is gathered by NIC with a scatter-gather op during xmit
    DTR_MODE_PIECE_BY_PIECE,    // Assume that data is scattered but each piece is sent in a different rdma write op
};


/* This function disconnects the RDMA connection from the server and cleans up 
 * all the resources, for a given QP
 */
static int client_disconnect_and_clean(int qp_num)
{
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    /* active disconnect from the client side */
    ret = rdma_disconnect(cm_client_qp_id[qp_num]);
    if (ret) {
        rdma_error("Failed to disconnect, errno: %d \n", -errno);
        //continuing anyways
    }
    ret = process_rdma_cm_event(cm_event_channel, 
            RDMA_CM_EVENT_DISCONNECTED,
            &cm_event);
    if (ret) {
        rdma_error("Failed to get RDMA_CM_EVENT_DISCONNECTED event, ret = %d\n",
                ret);
        //continuing anyways 
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge cm event, errno: %d\n", 
                   -errno);
        //continuing anyways
    }
    /* Destroy QP */
    rdma_destroy_qp(cm_client_qp_id[qp_num]);

    /* Destroy client cm id */
    ret = rdma_destroy_id(cm_client_qp_id[qp_num]);
    if (ret) {
        rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
        // we continue anyways;
    }

    /* Destroy memory buffers */
    rdma_buffer_deregister(server_qp_metadata_mr[qp_num]);
    rdma_buffer_deregister(client_qp_metadata_mr[qp_num]);	
    rdma_buffer_deregister(client_qp_src_mr[qp_num]);	
    //printf("Client QP %d clean up is complete \n", qp_num);
    return 0;
}

/* This function destroys all the shared resources
 */
static int client_clean()
{
    int ret = -1;

    for (int i=0;i<MAX_THREADS;i++) {
        int ret = -1;
        /* Destroy CQ */
        ret = ibv_destroy_cq(client_cq_threads[i]);
        if (ret) {
            rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
            // we continue anyways;
        }
    }

    for (int i=0;i<MAX_THREADS;i++) {
        /* Destroy completion channel */
        ret = ibv_destroy_comp_channel(io_completion_channel_threads[i]);
        if (ret) {
            rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
            // we continue anyways;
        }
    }
    /* We free the buffers */
    free(src);
    free(dst);
    /* Destroy protection domain */
    ret = ibv_dealloc_pd(pd);
    if (ret) {
        rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
        // we continue anyways;
    }
    rdma_destroy_event_channel(cm_event_channel);
    rdma_free_devices(devices);
    printf("Client resource clean up is complete \n");
    return 0;
}

void usage() {
    printf("Usage:\n");
    printf("rdma_client: [-a <server_addr>] [-p <server_port>] [-s/--simple]\n");
    printf("	--simple: runs a simple ping to test RDMA connection\n");
    printf("(default IP is 127.0.0.1 and port is %d)\n", DEFAULT_RDMA_PORT);
    exit(1);
}

int main(int argc, char **argv) {
    struct sockaddr_in server_sockaddr;
    int ret, option;
    int base_port = DEFAULT_RDMA_PORT;
    bzero(&server_sockaddr, sizeof server_sockaddr);
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    /* buffers are NULL */
    char* dummy_text = "HELLO";
    src = dst = NULL; 
    int simple = 0, i;
    int rtt_flag = 0, xput_flag = 0, xputv2_flag = 0;
    int num_concur = 1;
    int num_mbuf = 1;
    int msg_size = 0;
    int write_to_file = 0;
    char outfile[200] ;
    int num_qps = 1;

    /* Parse Command Line Arguments */
    static const struct option options[] = {
        {.name = "simple", .has_arg = no_argument, .val = 's'},
        {.name = "rtt", .has_arg = no_argument, .val = 'r'},
        {.name = "xput", .has_arg = no_argument, .val = 'x'},
        {.name = "xputv2", .has_arg = no_argument, .val = 'y'},
        {.name = "concur", .has_arg = required_argument, .val = 'c'},
        {.name = "buffers", .has_arg = required_argument, .val = 'b'},
        {.name = "msgsize", .has_arg = required_argument, .val = 'm'},
        {.name = "out", .has_arg = required_argument, .val = 'o'},
        {.name = "qps", .has_arg = required_argument, .val = 'q'},
        {}
    };
    while ((option = getopt_long(argc, argv, "sa:p:rxyc:b:m:o:q:", options, NULL)) != -1) {
        switch (option) {
            case 's':
                /* run the basic example to test connection */
                simple = 1;
                src = calloc(strlen(dummy_text) , 1);
                if (!src) {
                    rdma_error("Failed to allocate memory : -ENOMEM\n");
                    return -ENOMEM;
                }
                /* Copy the passes arguments */
                strncpy(src, dummy_text, strlen(dummy_text));
                dst = calloc(strlen(dummy_text), 1);
                if (!dst) {
                    rdma_error("Failed to allocate destination memory, -ENOMEM\n");
                    free(src);
                    return -ENOMEM;
                }
                break;
            case 'a':
                /* remember, this overwrites the port info */
                ret = get_addr(optarg, (struct sockaddr*) &server_sockaddr);
                if (ret) {
                    rdma_error("Invalid IP \n");
                    return ret;
                }
                break;
            case 'p':
                /* passed port to listen on */
                base_port = strtol(optarg, NULL, 0); 
                break;
            case 'r':
                /* Run RTT measurement (for all msg sizes) */
                rtt_flag = 1;     
                break;
            case 'x':
                /* Run xput measurement */
                xput_flag = 1;
                break;
            case 'y':
                /* Run advanced xput measurements for different data transfer modes  */
                xputv2_flag = 1;
                break;
            case 'c':
                /* concurrency i.e., number of requests in flight */
                /* num_concur < 0: invalid; = 0: vary from 1 to 128; > 0: use the number. Default is 1 */
                num_concur = strtol(optarg, NULL, 0); 
                if (num_concur < 0 || num_concur > MAX_WR) {
                    rdma_error("Invalid num_concur. Should be between 0 and %d\n", MAX_WR);
                    return -1;
                }
                break;  
            case 'b':
                /* number of MR buffers to use */
                /* num_mbuf < 0: invalid; = 0: vary from 1 to 256; > 0: use the number. Default is 1 */
                num_mbuf = strtol(optarg, NULL, 0); 
                if (num_mbuf < 0 || num_mbuf > MAX_MR) {
                    rdma_error("Invalid num_mbuf. Should be between 1 and %d\n", MAX_MR);
                    return -1;
                }
                break; 
            case 'm':
                /* input payload size to use */
                /* msg_size < 0: invalid; = 0: vary from 64 to 4096; > 0: use the number. Default is 0 (vary) */
                msg_size = strtol(optarg, NULL, 0); 
                if (msg_size < 0 || msg_size > 4096) {
                    rdma_error("Invalid msg_size. Should be between 1 and %d\n", 4096);
                    return -1;
                }
                break;
            case 'q':
                /* input number of queue pairs */
                /* qp_num < 0: invalid; = 0: vary from 1 to MAX_QPS; > 0: use the number. Default is 1 qp */
                num_qps = strtol(optarg, NULL, 0); 
                if (num_qps < 0 || num_qps > MAX_QPS) {
                    rdma_error("Invalid qps. Should be between 1 and %d\n", MAX_QPS);
                    return -1;
                }
                break;  
            case 'o':
                /* output file to write to */
                write_to_file = 1;
                strcpy(outfile, optarg);
                break;
            default:
                usage();
                break;
            }
        }

    /* Setup shared resources */
    ret = client_setup_shared_resources();
    if (ret) { 
        rdma_error("Failed to setup shared RDMA resources , ret = %d \n", ret);
        return ret;
    }

    /* Connect the local QPs to the ones on server. 
     * NOTE: Make sure to connect all QPs before moving to 
     * other activities that involve communication with the server as the 
     * server runs on single core and waits for connect request for all 
     * QPs before moving forward. */
    for(i = 0; i < num_qps; i++) {
        /* Each QP will try to connect to port numbers starting from base port */
        ret = client_prepare_connection(&server_sockaddr, i, base_port + i);
        if (ret) { 
            rdma_error("Failed to setup client connection , ret = %d \n", ret);
            return ret;
        }

        ret = client_pre_post_recv_buffer(i); 
        if (ret) { 
            rdma_error("Failed to setup client connection , ret = %d \n", ret);
            return ret;
        }
        
        ret = client_connect_qp_to_server(i);
        if (ret) { 
            rdma_error("Failed to setup client connection , ret = %d \n", ret);
            return ret;
        }

        // Give server some time to prepare for the next QP
        usleep(100 * 1000);     // 100ms
    }

    /* Connection now set up and ready to play */
    if (simple) {
        /* If asked for a simple program, run the original example for all qps */
        for (i = 0; i < num_qps; i++) {
            ret = client_xchange_metadata_with_server(i, src, strlen(src));
            if (ret) {
                rdma_error("Failed to setup client connection , ret = %d \n", ret);
                return ret;
            }

            ret = client_remote_memory_ops(i);
            if (ret) {
                rdma_error("Failed to finish remote memory ops, ret = %d \n", ret);
                return ret;
            }
                
            if (check_src_dst()) {
                rdma_error("src and dst buffers do not match \n");
            } else {
                printf("...\nSUCCESS, source and destination buffers match \n");
            }
        }
    }
    else {   
        /* Set up a buffer with enough size on server, we don't need to do this for every qp since we only need 
         * one server-side buffer that we can reuse for all QPs but we do it anyway since server expects it */
        /* Once metadata is exchanged, server-side buffer metadata would be saved in server_qp_metadata_attr[*] */
            printf("excanging metadata with server\n");
        for (i = 0; i < num_qps; i++) {
            #ifdef USE_DEVICE_MEMORY
            ret = client_xchange_metadata_with_server(i, NULL, (DEVICE_MEMORY_KB / (num_qps*2)));      // Relies on the server running CX5 TODO query remote device for mapped memory
            #else
            ret = client_xchange_metadata_with_server(i, NULL, 1024*1024*64);      // 1 MB
            #endif
            if (ret) {
                rdma_error("Failed to setup client connection , ret = %d \n", ret);
                return ret;
            }
        }
            printf("done excanging metadata with server");
        
        int min_num_concur = num_concur == 0 ? 1 : num_concur;
        int max_num_concur = num_concur == 0 ? 256 : num_concur;        /* Empirically found that anything above this number does not matter for single core */

        int min_msg_size = msg_size == 0 ? 64 : msg_size;
        int max_msg_size = msg_size == 0 ? 4096 : msg_size;
        int msg_size_incr = 64;

        int min_num_mbuf = num_mbuf == 0 ? 1 : num_mbuf;
        // int max_num_mbuf = num_mbuf == 0 ? MAX_MR : num_mbuf;
        int max_num_mbuf = num_mbuf == 0 ? 1e6 : num_mbuf;

        /* Get roundtrip latencies for ops */
        if (rtt_flag) {
            // printf("RTT for %d B WRITES: %0.2lf mu-secs\n", 64, measure_rtt(64, RDMA_WRITE_OP));        // WRITE RTT
            // printf("RTT for %d B READS : %0.2lf mu-secs\n", 64, measure_rtt(64, RDMA_READ_OP));         // READ RTT

            /* We can also get them using the xput method with 1 request in flight; results agreed with above method. */
            // printf("RTT for %d B WRITES: %0.2lf mu-secs\n", 64, 1e6 / measure_xput(64, 1, RDMA_WRITE_OP) );     // WRITE RTT
            // printf("RTT for %d B READS : %0.2lf mu-secs\n", 64, 1e6 / measure_xput(64, 1, RDMA_READ_OP));       // READ RTT


            /* Generate RTT numbers for different msg sizes */
            FILE *fptr;
            if (write_to_file) {
                printf("Writing output to: %s\n", outfile);
                fptr = fopen(outfile, "w");
                fprintf(fptr, "msg size,write,read\n");
            }
            printf("=========== RTTs =============\n");
            printf("msg size,write,read\n");
            for (msg_size = min_msg_size; msg_size <= max_msg_size; msg_size += msg_size_incr) {
                double wrtt = 1e6 / measure_xput(msg_size, 1, RDMA_WRITE_OP, MR_MODE_PRE_REGISTER_WITH_ROTATE, num_mbuf, num_qps).xput_ops;
                double rrtt = 1e6 / measure_xput(msg_size, 1, RDMA_READ_OP, MR_MODE_PRE_REGISTER_WITH_ROTATE, num_mbuf, num_qps).xput_ops;
                printf("%d,%.2lf,%.2lf\n", msg_size, wrtt, rrtt);
                if (write_to_file) {
                    fprintf(fptr, "%d,%.2lf,%.2lf\n", msg_size, wrtt, rrtt);
                    fflush(fptr);
                }
            }
            if (write_to_file)  fclose(fptr);
        }

        /* Get xputs for ops */
        if (xput_flag) {
            uint32_t msg_size = 8;
            uint32_t num_concur = 1;
            // max_reqs_in_flight = 20;
            // printf("Goodput for %3d B READS (%3d QPE) : %0.2lf gbps\n", msg_size, max_reqs_in_flight, 
            //     measure_xput(msg_size, max_reqs_in_flight, RDMA_READ_OP) * msg_size * 8 / 1e9);       // READ Xput
            // printf("Goodput for %3d B WRITES (%3d QPE) : %0.2lf gbps\n", msg_size, max_reqs_in_flight, 
            //     measure_xput(msg_size, max_reqs_in_flight, RDMA_WRITE_OP) );       // WRITE Xput

            FILE *fptr;
            if (write_to_file) {
                printf("Writing output to: %s\n", outfile);
                fptr = fopen(outfile, "w");
                fprintf(fptr,"msg size,concur,write_ops,write,read_ops,read,cas_ops,cas,faa_ops,faa\n");
            }
            printf("=========== Xput =============\n");
            printf("msg size,concur,write_ops,write,read_ops,read,cas_ops,cas,faa_ops,faa,keys\n");
            num_mbuf=num_qps;
            //for (num_concur =128; num_concur <=1024; num_concur *= 2) {
            //for (num_concur = num_qps; num_concur <=num_qps*(MAX_RD_AT_IN_FLIGHT); num_concur*=2) {
            /*
            num_concur=num_qps*(MAX_RD_AT_IN_FLIGHT);
            double casput_ops = measure_xput(msg_size, num_concur, RDMA_CAS_OP, MR_MODE_PRE_REGISTER, num_mbuf, num_qps).xput_ops;
            double casput_gbps = casput_ops * msg_size * 8 / 1e9;
            printf("%d,%d,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%d\n", msg_size, num_concur, 0.0,0.0,0.0,0.0,casput_ops,casput_gbps,GLOBAL_KEYS);
            */



            num_concur=92;
            GLOBAL_GAP_INTEGER=8;
            //Set this value if we only want one key, it controls what happens in measure_xput.
            //GLOBAL_KEYS=1024;
            GLOBAL_KEYS=8192;
            //for (GLOBAL_GAP_INTEGER=0;GLOBAL_GAP_INTEGER<1024;GLOBAL_GAP_INTEGER+=8) {
            //for (GLOBAL_KEYS=1;GLOBAL_KEYS<num_concur;GLOBAL_KEYS++) {
            //for (GLOBAL_KEYS=1;GLOBAL_KEYS<16;GLOBAL_KEYS++) {
                //double rput_ops =  measure_xput(msg_size, num_concur, RDMA_READ_OP, MR_MODE_PRE_REGISTER, num_mbuf, num_qps).xput_ops;

            //This is the primer to set up the middle box
            //num_qps=2;
            for (num_qps=1;num_qps<24;num_qps++){
            //for (num_concur=1;num_concur<=16;num_concur*=2){
            //for (num_concur = num_qps; num_concur <=num_qps*(MAX_RD_AT_IN_FLIGHT); num_concur+=num_qps) {
                num_concur=372*num_qps;
                //double rput_ops =  measure_xput(msg_size, num_concur, RDMA_READ_OP, MR_MODE_PRE_REGISTER, num_mbuf, num_qps).xput_ops;
                double wput_ops = measure_xput(msg_size, num_concur, RDMA_WRITE_OP, MR_MODE_PRE_REGISTER, num_mbuf, num_qps).xput_ops; 
                //double casput_ops = measure_xput(msg_size, num_concur, RDMA_CAS_OP, MR_MODE_PRE_REGISTER, num_mbuf, num_qps).xput_ops;
                //double faa_ops = measure_xput(msg_size, num_concur, RDMA_FAA_OP, MR_MODE_PRE_REGISTER, num_mbuf, num_qps).xput_ops;
                double rput_ops =1;
                double casput_ops = 1;
                double faa_ops = 1;
                double wput_gbps = wput_ops * msg_size * 8 / 1e9;
                double rput_gbps = rput_ops * msg_size * 8 / 1e9;
                double casput_gbps = casput_ops * msg_size * 8 / 1e9;
                double faa_gbps = casput_ops * msg_size * 8 / 1e9;
                printf("%d,%d,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%d\n", msg_size, num_concur, wput_ops, wput_gbps, rput_ops, rput_gbps,casput_ops,casput_gbps,faa_ops,faa_gbps,GLOBAL_KEYS);
                if (write_to_file) {
                    fprintf(fptr,"%d,%d,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%d\n", msg_size, num_concur, wput_ops, wput_gbps, rput_ops, rput_gbps,casput_ops,casput_gbps,faa_ops,faa_gbps,GLOBAL_GAP_INTEGER);
                    fflush(fptr);
                
                }
            }
            if (write_to_file)  fclose(fptr);
        }

    }

    for (i = 0; i < num_qps; i++) {
        ret = client_disconnect_and_clean(i);
        if (ret)
            rdma_error("Failed to cleanly disconnect qp %d \n", i);
    }
    ret = client_clean();
    if (ret)
        rdma_error("Failed to clean client resources\n");
    return ret;
}

