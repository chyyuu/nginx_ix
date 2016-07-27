#include <assert.h>

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

#include <ixev.h>

typedef struct {
    ngx_uint_t  dummy;
} ngx_ixev_conf_t;


static ngx_int_t ngx_ixev_init(ngx_cycle_t *cycle, ngx_msec_t timer);
static void ngx_ixev_done(ngx_cycle_t *cycle);
static ngx_int_t ngx_ixev_add_event(ngx_event_t *ev, ngx_int_t event,
                                    ngx_uint_t flags);
static ngx_int_t ngx_ixev_del_event(ngx_event_t *ev, ngx_int_t event,
                                    ngx_uint_t flags);
static ngx_int_t ngx_ixev_add_connection(ngx_connection_t *c);
static ngx_int_t ngx_ixev_del_connection(ngx_connection_t *c,
                                         ngx_uint_t flags);
static ngx_int_t ngx_ixev_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
                                         ngx_uint_t flags);

static void *ngx_ixev_create_conf(ngx_cycle_t *cycle);
static char *ngx_ixev_init_conf(ngx_cycle_t *cycle, void *conf);

static ngx_str_t      ixev_name = ngx_string("ixev");

static ngx_command_t  ngx_ixev_commands[] = {

      ngx_null_command
};


ngx_event_module_t  ngx_ixev_module_ctx = {
    &ixev_name,
    ngx_ixev_create_conf,               /* create configuration */
    ngx_ixev_init_conf,                 /* init configuration */

    {
        ngx_ixev_add_event,             /* add an event */
        ngx_ixev_del_event,             /* delete an event */
        ngx_ixev_add_event,             /* enable an event */
        ngx_ixev_del_event,             /* disable an event */
        ngx_ixev_add_connection,        /* add an connection */
        ngx_ixev_del_connection,        /* delete an connection */
        NULL,                           /* trigger a notify */
        ngx_ixev_process_events,        /* process the events */
        ngx_ixev_init,                  /* init the events */
        ngx_ixev_done,                  /* done the events */
    }
};

ngx_module_t  ngx_ixev_module = {
    NGX_MODULE_V1,
    &ngx_ixev_module_ctx,                /* module context */
    ngx_ixev_commands,                   /* module directives */
    NGX_EVENT_MODULE,                    /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    NULL,                                /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};

static ssize_t ngx_ix_recv(ngx_connection_t *c, u_char *buf, size_t size);
static ssize_t ngx_ix_recv_chain(ngx_connection_t *c, ngx_chain_t *in,
                                 off_t limit);
static ssize_t ngx_udp_ix_recv(ngx_connection_t *c, u_char *buf, size_t size);
static ssize_t ngx_ix_send(ngx_connection_t *c, u_char *buf, size_t size);
static ssize_t ngx_udp_ix_send(ngx_connection_t *c, u_char *buf, size_t size);
static ngx_chain_t *ngx_ix_send_chain(ngx_connection_t *c, ngx_chain_t *in,
                                      off_t limit);

ngx_os_io_t ngx_ix_io = {
    ngx_ix_recv,
    ngx_ix_recv_chain,
    ngx_udp_ix_recv,
    ngx_ix_send,
    ngx_udp_ix_send,
    ngx_ix_send_chain,
    0
};

static void ngx_handler(struct ixev_ctx *ctx, unsigned int reason)
{
    ngx_connection_t   *c = container_of(ctx, ngx_connection_t, ctx);
    ngx_event_t        *rev, *wev;

    if (reason & IXEVIN) {
        rev = c->read;
        rev->ready = 1;
        if (rev->handler)
            rev->handler(rev);
    }

    if (reason & IXEVOUT) {
        wev = c->write;
        wev->ready = 1;
        if (wev->handler)
            wev->handler(wev);
    }
}

static void ngx_release(struct ixev_ctx *ctx)
{
    ngx_connection_t *c = container_of(ctx, ngx_connection_t, ctx);

    ngx_del_conn(c, 0);

    if (c->pool)
        ngx_destroy_pool(c->pool);

    ngx_free_connection(c);
}

extern ngx_cycle_t *ongoing_cycle;
static ngx_listening_t *listening_socket = NULL;

static struct ixev_ctx *ngx_accept(struct ip_tuple *id)
{
    ngx_connection_t *c;
    ngx_log_t *log;
    ngx_event_t *rev, *wev;
    ngx_listening_t *ls = listening_socket;
    struct sockaddr_in *sin;
    int socklen = sizeof(struct sockaddr);

    c = ngx_get_connection(0, default_log);
    if (c == NULL) {
        printf("[ixev] ngx_get_connection failed\n");
        return NULL;
    }

    ixev_ctx_init(&c->ctx);

    /* from ngx_event_accept() */

    c->type = SOCK_STREAM;

    c->pool = ngx_create_pool(64 * sizeof(void *), default_log);
    if (c->pool == NULL)
        goto fail;

    c->sockaddr = ngx_palloc(c->pool, socklen);
    if (c->sockaddr == NULL)
        goto fail;
    sin = (struct sockaddr_in *)c->sockaddr;
    sin->sin_family = AF_INET;
    // XXX: check if ip and port are properly initialized
    sin->sin_port = id->src_port;
    sin->sin_addr.s_addr = id->src_ip;

    log = ngx_palloc(c->pool, sizeof(ngx_log_t));
    if (log == NULL)
        goto fail;
    *log = ls->log;

    c->recv = ngx_recv;
    c->send = ngx_send;
    c->recv_chain = ngx_recv_chain;
    c->send_chain = ngx_send_chain;

    c->log = log;
    c->pool->log = log;

    c->socklen = socklen;
    c->listening = ls;
    c->local_sockaddr = ls->sockaddr;
    c->local_socklen = ls->socklen;

    rev = c->read;
    wev = c->write;
    wev->ready = 1;

    rev->log = log;
    wev->log = log;

    c->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);

    if (ls->addr_ntop) {
        c->addr_text.data = ngx_pnalloc(c->pool, ls->addr_text_max_len);
        if (c->addr_text.data == NULL)
            goto fail;

        c->addr_text.len = ngx_sock_ntop(c->sockaddr, c->socklen,
                                         c->addr_text.data,
                                         ls->addr_text_max_len, 0);
        if (c->addr_text.len == 0)
            goto fail;
    }

    if (ngx_add_conn(c) == NGX_ERROR)
        goto fail;

    log->data = NULL;
    log->handler = NULL;

    ls->handler(c);

    return &c->ctx;

fail:
    ngx_release(&c->ctx);
    return NULL;
}

struct ixev_conn_ops nginx_ops = {
    .accept        = &ngx_accept,
    .release       = &ngx_release,
};

static ngx_int_t
ngx_ixev_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{
    if (ixev_init(&nginx_ops)) {
        printf("[ixev] init failed\n");
        return NGX_ERROR;
    }

    if (ongoing_cycle->listening.nelts != 1) {
        printf("[ixev] %lu listening sockets are not supported\n", ongoing_cycle->listening.nelts);
        return NGX_ERROR;
    }
    listening_socket = ongoing_cycle->listening.elts;

    ixev_init_thread();

    ngx_io = ngx_ix_io;
    ngx_event_actions = ngx_ixev_module_ctx.actions;

    return NGX_OK;
}


static void
ngx_ixev_done(ngx_cycle_t *cycle)
{
    printf("[ixev] %s not implemented\n", __func__);
}


static ngx_int_t
ngx_ixev_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_ERROR;
}


static ngx_int_t
ngx_ixev_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_ERROR;
}


static ngx_int_t
ngx_ixev_add_connection(ngx_connection_t *c)
{
    ixev_set_handler(&c->ctx, IXEVIN | IXEVOUT, &ngx_handler);

    c->read->active = 1;
    c->write->active = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_ixev_del_connection(ngx_connection_t *c, ngx_uint_t flags)
{
    ixev_set_handler(&c->ctx, 0, NULL);

    c->read->active = 0;
    c->write->active = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_ixev_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{
    ixev_wait();
    return NGX_OK;
}


static void *
ngx_ixev_create_conf(ngx_cycle_t *cycle)
{
    ngx_ixev_conf_t  *epcf;

    epcf = ngx_palloc(cycle->pool, sizeof(ngx_ixev_conf_t));
    if (epcf == NULL) {
        return NULL;
    }

    return epcf;
}


static char *
ngx_ixev_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_ixev_conf_t *epcf = conf;

    ngx_conf_init_uint_value(epcf->dummy, 0);

    return NGX_CONF_OK;
}

static ssize_t
ngx_ix_recv(ngx_connection_t *c, u_char *buf, size_t size)
{
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_ERROR;
}

static ssize_t
ngx_ix_recv_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit)
{
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_ERROR;
}

static ssize_t
ngx_udp_ix_recv(ngx_connection_t *c, u_char *buf, size_t size)
{
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_ERROR;
}

static ssize_t
ngx_ix_send(ngx_connection_t *c, u_char *buf, size_t size)
{
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_ERROR;
}

static ssize_t
ngx_udp_ix_send(ngx_connection_t *c, u_char *buf, size_t size)
{
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_ERROR;
}

static ngx_chain_t *
ngx_ix_send_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit)
{
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_CHAIN_ERROR;
}
