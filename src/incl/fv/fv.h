#if !defined(__FV_H__)
#define __FV_H__

#include "sck.h"
#include "slot.h"
#include "pipe.h"
#include "queue.h"
#include "fv_lsn.h"
#include "fv_comm.h"
#include "rb_tree.h"
#include "spinlock.h"
#include "avl_tree.h"
#include "hash_tab.h"
#include "thread_pool.h"

/* 宏定义 */
#define ACC_TMOUT_SCAN_SEC    (15)        /* 超时扫描间隔 */

/* 配置信息 */
typedef struct {
    int nid;                        /* 结点ID */

    char ipaddr[IP_ADDR_MAX_LEN];   /* 外网IP */
    int port;                       /* 侦听端口 */

    struct {
        int max;                    /* 最大并发数 */
        int timeout;                /* 连接超时时间 */
    } connections;

    int lsvr_num;                   /* 帧听线程数 */
    int rsvr_num;                   /* 接收线程数 */

    queue_conf_t connq;             /* 连接队列 */
    queue_conf_t recvq;             /* 接收队列 */
    queue_conf_t sendq;             /* 发送队列 */
} fv_conf_t;

typedef struct _acc_cntx_t fv_cntx_t;
typedef int (*fv_callback_t)(fv_cntx_t *ctx, socket_t *asi, int reason, void *user, void *in, int len, void *args);
typedef size_t (*fv_get_packet_body_size_cb_t)(void *head);

/* 帧听协议 */
typedef struct
{
    fv_callback_t callback;        /* 处理回调 */
    size_t per_packet_head_size;    /* 每个包的报头长度 */
    fv_get_packet_body_size_cb_t get_packet_body_size; /* 每个包的报体长度 */
    size_t per_session_data_size;   /* 每个会话的自定义数据大小 */
    void *args;                     /* 附加参数 */
} fv_protocol_t;

/* 发送项 */
typedef struct
{
    uint64_t cid;                   /* 连接ID */
    int len;                        /* 发送长度 */
    void *data;                     /* 发送内容 */
} fv_send_item_t;

/* 代理对象 */
typedef struct _acc_cntx_t {
    fv_conf_t *conf;               /* 配置信息 */
    log_cycle_t *log;               /* 日志对象 */
    avl_tree_t *reg;                /* 函数注册表 */
    fv_protocol_t *protocol;       /* 处理协议 */

    /* 侦听信息 */
    struct {
        int lsn_sck_id;             /* 侦听套接字 */
        spinlock_t accept_lock;     /* 侦听锁 */
        uint64_t cid;               /* Session ID */
        fv_lsvr_t *lsvr;           /* 侦听对象 */
    } listen;

    pipe_t *rsvr_cmd_fd;            /* 接收线程池通信FD */
    thread_pool_t *rsvr_pool;       /* 接收线程池 */
    thread_pool_t *lsvr_pool;       /* 帧听线程池 */

    hash_tab_t *conn_cid_tab;       /* CID集合(注:数组长度与Agent相等) */

    queue_t **connq;                /* 连接队列(注:数组长度与Agent相等) */
    ring_t **sendq;                 /* 发送队列(注:数组长度与Agent相等) */
    queue_t **kickq;                /* 踢人队列(注:数组长度与Agent相等) */
} fv_cntx_t;

#define ACC_GET_NODE_ID(ctx) ((ctx)->conf->nid)

/* 外部接口 */
fv_cntx_t *fv_init(fv_protocol_t *protocol, fv_conf_t *conf, log_cycle_t *log);
int fv_launch(fv_cntx_t *ctx);
void fv_destroy(fv_cntx_t *ctx);

int fv_async_send(fv_cntx_t *ctx, int type, uint64_t cid, void *data, int len);
int fv_async_kick(fv_cntx_t *ctx, uint64_t cid);
uint64_t fv_sck_get_cid(socket_t *sck);

#endif /*__FV_H__*/
