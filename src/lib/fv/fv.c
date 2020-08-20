#include "log.h"
#include "sck.h"
#include "comm.h"
#include "lock.h"
#include "redo.h"
#include "fv_lsn.h"
#include "fv_rsvr.h"
#include "hash_alg.h"

static int fv_creat_rsvr(fv_cntx_t *ctx);
static int fv_rsvr_pool_destroy(fv_cntx_t *ctx);
static int fv_creat_lsvr(fv_cntx_t *ctx);
static int fv_creat_queue(fv_cntx_t *ctx);

/* CID比较回调 */
static int64_t fv_conn_cid_cmp_cb(const fv_socket_extra_t *extra1, const fv_socket_extra_t *extra2)
{
    return (int64_t)(extra1->cid - extra2->cid);
}

/* CID哈希回调 */
static uint64_t fv_conn_cid_hash_cb(const fv_socket_extra_t *extra)
{
    return extra->cid;
}

/******************************************************************************
 **函数名称: fv_init
 **功    能: 初始化全局信息
 **输入参数: 
 **     protocol: 协议
 **     conf: 配置路径
 **     log: 日志对象
 **输出参数: NONE
 **返    回: 全局对象
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
fv_cntx_t *fv_init(fv_protocol_t *protocol, fv_conf_t *conf, log_cycle_t *log)
{
    fv_cntx_t *ctx;

    /* > 创建全局对象 */
    ctx = (fv_cntx_t *)calloc(1, sizeof(fv_cntx_t));
    if (NULL == ctx) {
        log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
        return NULL;
    }

    ctx->log = log;
    ctx->conf = conf;
    ctx->protocol = protocol;

    do {
        /* > 设置进程打开文件数 */
        if (set_fd_limit(conf->connections.max)) {
            log_error(log, "errmsg:[%d] %s! max:%d",
                      errno, strerror(errno), conf->connections.max);
            break;
        }

        /* > 创建队列 */
        if (fv_creat_queue(ctx)) {
            log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
            break;
        }

        /* > 创建Agent线程池 */
        if (fv_creat_rsvr(ctx)) {
            log_error(log, "Initialize agent thread pool failed!");
            break;
        }

        /* > 创建Listen线程池 */
        if (fv_creat_lsvr(ctx)) {
            log_error(log, "Initialize agent thread pool failed!");
            break;
        }

        /* > 创建连接管理 */
        ctx->conn_cid_tab = (hash_tab_t *)hash_tab_creat(conf->rsvr_num,
            (hash_cb_t)fv_conn_cid_hash_cb, (cmp_cb_t)fv_conn_cid_cmp_cb, NULL);
        if (NULL == ctx->conn_cid_tab) {
            log_error(ctx->log, "Init sid list failed!");
            break;
        }

        return ctx;
    } while (0);

    free(ctx);
    return NULL;
}

/******************************************************************************
 **函数名称: fv_destroy
 **功    能: 销毁代理服务上下文
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: VOID
 **实现描述: 依次销毁侦听线程、接收线程、工作线程、日志对象等
 **注意事项: 按序销毁
 **作    者: # Qifeng.zou # 2014.11.17 #
 ******************************************************************************/
void fv_destroy(fv_cntx_t *ctx)
{
    fv_rsvr_pool_destroy(ctx);
}

/******************************************************************************
 **函数名称: fv_launch
 **功    能: 启动代理服务
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     设置线程回调
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
int fv_launch(fv_cntx_t *ctx)
{
    int idx;
    fv_conf_t *conf = ctx->conf;

    /* 1. 设置Agent线程回调 */
    for (idx=0; idx<conf->rsvr_num; ++idx) {
        thread_pool_add_worker(ctx->rsvr_pool, fv_rsvr_routine, ctx);
    }
    
    /* 2. 设置Listen线程回调 */
    for (idx=0; idx<conf->lsvr_num; ++idx) {
        thread_pool_add_worker(ctx->lsvr_pool, fv_lsvr_routine, ctx);
    }
 
    return FV_OK;
}

/******************************************************************************
 **函数名称: fv_creat_rsvr
 **功    能: 创建接收线程池
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
static int fv_creat_rsvr(fv_cntx_t *ctx)
{
    int idx, num;
    fv_rsvr_t *agent;
    const fv_conf_t *conf = ctx->conf;

    /* > 新建Agent对象 */
    agent = (fv_rsvr_t *)calloc(conf->rsvr_num, sizeof(fv_rsvr_t));
    if (NULL == agent) {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FV_ERR;
    }

    /* > 新建通信管道 */
    ctx->rsvr_cmd_fd = (pipe_t *)calloc(conf->rsvr_num, sizeof(pipe_t));
    if (NULL == ctx->rsvr_cmd_fd) {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FV_ERR;
    }

    for (idx=0; idx<conf->rsvr_num; idx+=1) {
        pipe_creat(&ctx->rsvr_cmd_fd[idx]);
    }

    /* > 创建Worker线程池 */
    ctx->rsvr_pool = thread_pool_init(conf->rsvr_num, NULL, agent);
    if (NULL == ctx->rsvr_pool) {
        log_error(ctx->log, "Initialize thread pool failed!");
        free(agent);
        return FV_ERR;
    }

    /* 3. 依次初始化Agent对象 */
    for (idx=0; idx<conf->rsvr_num; ++idx) {
        if (fv_rsvr_init(ctx, agent+idx, idx)) {
            log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
            break;
        }
    }

    if (idx == conf->rsvr_num) {
        return FV_OK; /* 成功 */
    }

    /* 4. 释放Agent对象 */
    num = idx;
    for (idx=0; idx<num; ++idx) {
        fv_rsvr_destroy(agent+idx);
    }

    FREE(agent);
    thread_pool_destroy(ctx->rsvr_pool);

    return FV_ERR;
}

/******************************************************************************
 **函数名称: fv_creat_lsvr
 **功    能: 创建帧听线程池
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015-06-30 15:06:58 #
 ******************************************************************************/
static int fv_creat_lsvr(fv_cntx_t *ctx)
{
    int idx;
    fv_lsvr_t *lsvr;
    fv_conf_t *conf = ctx->conf;

    /* > 侦听指定端口 */
    ctx->listen.lsn_sck_id = tcp_listen(conf->port);
    if (ctx->listen.lsn_sck_id < 0) {
        log_error(ctx->log, "errmsg:[%d] %s! port:%d",
                  errno, strerror(errno), conf->port);
        return FV_ERR;
    }

    spin_lock_init(&ctx->listen.accept_lock);

    /* > 创建LSN对象 */
    ctx->listen.lsvr = (fv_lsvr_t *)calloc(1, conf->lsvr_num*sizeof(fv_lsvr_t));
    if (NULL == ctx->listen.lsvr) {
        CLOSE(ctx->listen.lsn_sck_id);
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FV_ERR;
    }

    /* > 初始化侦听服务 */
    for (idx=0; idx<conf->lsvr_num; ++idx) {
        lsvr = ctx->listen.lsvr + idx;
        lsvr->log = ctx->log;
        lsvr->id = idx;
    }

    ctx->lsvr_pool = thread_pool_init(conf->lsvr_num, NULL, ctx->listen.lsvr);
    if (NULL == ctx->lsvr_pool) {
        CLOSE(ctx->listen.lsn_sck_id);
        FREE(ctx->listen.lsvr);
        log_error(ctx->log, "Initialize thread pool failed!");
        return FV_ERR;
    }

    return FV_OK;
}

/******************************************************************************
 **函数名称: fv_rsvr_pool_destroy
 **功    能: 销毁Agent线程池
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.15 #
 ******************************************************************************/
static int fv_rsvr_pool_destroy(fv_cntx_t *ctx)
{
    int idx;
    void *data;
    fv_rsvr_t *agent;
    const fv_conf_t *conf = ctx->conf;

    /* 1. 释放Agent对象 */
    for (idx=0; idx<conf->rsvr_num; ++idx) {
        agent = (fv_rsvr_t *)ctx->rsvr_pool->data + idx;

        fv_rsvr_destroy(agent);
    }

    /* 2. 释放线程池对象 */
    data = ctx->rsvr_pool->data;

    thread_pool_destroy(ctx->rsvr_pool);

    free(data);

    ctx->rsvr_pool = NULL;

    return FV_ERR;
}

/******************************************************************************
 **函数名称: fv_reg_def_hdl
 **功    能: 默认注册函数
 **输入参数:
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.12.20 #
 ******************************************************************************/
static int fv_reg_def_hdl(unsigned int type, char *buff, size_t len, void *args)
{
    static int total = 0;
    fv_cntx_t *ctx = (fv_cntx_t *)args;

    log_info(ctx->log, "total:%d", ++total);

    return FV_OK;
}

/******************************************************************************
 **函数名称: fv_creat_queue
 **功    能: 创建队列
 **输入参数:
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 此过程一旦失败, 程序必须退出运行. 因此, 在此申请的内存未被主动释放也不算内存泄露!
 **作    者: # Qifeng.zou # 2014.12.21 #
 ******************************************************************************/
static int fv_creat_queue(fv_cntx_t *ctx)
{
    int idx;
    const fv_conf_t *conf = ctx->conf;

    /* > 创建CONN队列(与Agent数一致) */
    ctx->connq = (queue_t **)calloc(conf->rsvr_num, sizeof(queue_t*));
    if (NULL == ctx->connq) {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FV_ERR;
    }

    for (idx=0; idx<conf->rsvr_num; ++idx) {
        ctx->connq[idx] = queue_creat(conf->connq.max, sizeof(fv_add_sck_t));
        if (NULL == ctx->connq[idx]) {
            log_error(ctx->log, "Create conn queue failed!");
            return FV_ERR;
        }
    }

    /* > 创建SEND队列(与Agent数一致) */
    ctx->sendq = (ring_t **)calloc(conf->rsvr_num, sizeof(ring_t *));
    if (NULL == ctx->sendq) {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FV_ERR;
    }

    for (idx=0; idx<conf->rsvr_num; ++idx) {
        ctx->sendq[idx] = ring_creat(conf->sendq.max);
        if (NULL == ctx->sendq[idx]) {
            log_error(ctx->log, "Create send queue failed!");
            return FV_ERR;
        }
    }

    /* > 创建KICK队列(与Agent数一致) */
    ctx->kickq = (queue_t **)calloc(conf->rsvr_num, sizeof(queue_t*));
    if (NULL == ctx->kickq) {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FV_ERR;
    }

    for (idx=0; idx<conf->rsvr_num; ++idx) {
        ctx->kickq[idx] = queue_creat(conf->connq.max, sizeof(fv_kick_req_t));
        if (NULL == ctx->kickq[idx]) {
            log_error(ctx->log, "Create kick queue failed!");
            return FV_ERR;
        }
    }

    return FV_OK;
}

/******************************************************************************
 **函数名称: fv_conn_cid_tab_add
 **功    能: 新增CID列表
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项: 
 **作    者: # Qifeng.zou # 2015-06-24 23:58:46 #
 ******************************************************************************/
int fv_conn_cid_tab_add(fv_cntx_t *ctx, fv_socket_extra_t *extra)
{
    return hash_tab_insert(ctx->conn_cid_tab, extra, WRLOCK);
}

/******************************************************************************
 **函数名称: fv_conn_cid_tab_del
 **功    能: 删除CID列表
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项: TODO: XXXX
 **作    者: # Qifeng.zou # 2015-06-24 23:58:46 #
 ******************************************************************************/
fv_socket_extra_t *fv_conn_cid_tab_del(fv_cntx_t *ctx, uint64_t cid)
{
    fv_socket_extra_t key;

    key.cid = cid;

    return hash_tab_delete(ctx->conn_cid_tab, &key, WRLOCK);
}

/******************************************************************************
 **函数名称: fv_get_rid_by_cid
 **功    能: 通过cid查找rsvr的序列号
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: RSVR的序列号
 **实现描述:
 **注意事项: 
 **作    者: # Qifeng.zou # 2015-06-24 23:58:46 #
 ******************************************************************************/
int fv_get_rid_by_cid(fv_cntx_t *ctx, uint64_t cid)
{
    int rid;
    fv_socket_extra_t *extra, key;

    key.cid = cid;

    extra = hash_tab_query(ctx->conn_cid_tab, &key, RDLOCK);
    if (NULL == extra) {
        return -1;
    }

    rid = extra->rid;

    hash_tab_unlock(ctx->conn_cid_tab, &key, RDLOCK);

    return rid;
}
