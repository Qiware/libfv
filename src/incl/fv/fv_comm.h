#if !defined(__FV_COMM_H__)
#define __FV_COMM_H__

#include "comm.h"
#include "list.h"
#include "mem_pool.h"

/* 错误码 */
typedef enum
{
    FV_OK = 0                  /* 正常 */
    , FV_SHOW_HELP             /* 显示帮助信息 */
    , FV_DONE                  /* 完成 */
    , FV_SCK_AGAIN             /* 出现EAGAIN提示 */
    , FV_SCK_CLOSE             /* 套接字关闭 */

    , FV_ERR = ~0x7FFFFFFF     /* 失败、错误 */
} fv_errno_e;

/* 处罚回调的原因 */
typedef enum
{
    FV_CALLBACK_SCK_CREAT      /* 创建SCK对象 */
    , FV_CALLBACK_SCK_CLOSED   /* 连接关闭 */
    , FV_CALLBACK_SCK_DESTROY  /* 销毁SCK对象 */

    , FV_CALLBACK_RECEIVE      /* 收到数据 */
    , FV_CALLBACK_WRITEABLE    /* 可写事件 */

    , FV_CALLBACK_ADD_POLL_FD  /* 将套接字加入事件监听 */
    , FV_CALLBACK_DEL_POLL_FD  /* 将套接字移除事件监听 */
    , FV_CALLBACK_CHANGE_MODE_POLL_FD  /* 改变监听的事件 */
    , FV_CALLBACK_LOCK_POLL    /* 对POLL加锁 */
    , FV_CALLBACK_UNLOCK_POLL  /* 对POLL解锁 */
} fv_callback_reason_e;

/* 新增套接字对象 */
typedef struct
{
    int fd;                 /* 套接字 */
    struct timeb ctm;       /* 创建时间 */
    uint64_t cid;           /* Connection ID */
} fv_add_sck_t;

/* KICK请求对象 */
typedef struct
{
    uint64_t cid;           /* 踢人请求 */
} fv_kick_req_t;

/* 超时连接链表 */
typedef struct
{
    time_t ctm;             /* 当前时间 */
    list_t *list;           /* 超时链表 */
} fv_conn_timeout_list_t;

#endif /*__FV_COMM_H__*/
