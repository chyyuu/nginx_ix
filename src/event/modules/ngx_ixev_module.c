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

static void ngx_handler(struct ixev_ctx *ctx, unsigned int reason)
{
    ngx_connection_t *c = container_of(ctx, ngx_connection_t, ctx);
    printf("[ixev] %s not implemented, connection: %p\n", __func__, c);
}

static struct ixev_ctx *ngx_accept(struct ip_tuple *id)
{
    ngx_connection_t *c;

    c = ngx_get_connection(0, default_log);
    if (c == NULL) {
        printf("[ixev] ngx_get_connection failed\n");
        return NULL;
    }

    ixev_ctx_init(&c->ctx);
    ixev_set_handler(&c->ctx, IXEVIN, &ngx_handler);
    return &c->ctx;
}

static void ngx_release(struct ixev_ctx *ctx)
{
    ngx_connection_t *c = container_of(ctx, ngx_connection_t, ctx);
    ngx_free_connection(c);
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

    ixev_init_thread();

    ngx_io = ngx_os_io;
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
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_ERROR;
}


static ngx_int_t
ngx_ixev_del_connection(ngx_connection_t *c, ngx_uint_t flags)
{
    printf("[ixev] %s not implemented\n", __func__);
    return NGX_ERROR;
}


static ngx_int_t
ngx_ixev_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{
    ixev_wait();
    return NGX_ERROR;
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
