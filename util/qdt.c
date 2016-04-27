/*
 * Functions for manipulating IEEE1275 (Open Firmware) style device
 * trees.
 *
 * Copyright David Gibson, Red Hat Inc. 2016
 *
 * This work is licensed unter the GNU GPL version 2 or (at your
 * option) any later version.
 */

#include <libfdt.h>
#include <stdbool.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/qdt.h"
#include "qemu/error-report.h"

/*
 * Node functions
 */

QDTNode *qdt_new_node(const gchar *name)
{
    QDTNode *node = g_new0(QDTNode, 1);

    g_assert(!strchr(name, '/'));

    node->name = g_strdup(name);
    QTAILQ_INIT(&node->properties);
    QTAILQ_INIT(&node->children);

    return node;
}

static QDTNode *get_subnode(QDTNode *parent, const gchar *name, size_t namelen)
{
    QDTNode *child;

    g_assert(!memchr(name, '/', namelen));

    QTAILQ_FOREACH(child, &parent->children, sibling) {
        if ((strlen(child->name) == namelen)
            && (memcmp(child->name, name, namelen) == 0)) {
            return child;
        }
    }

    return NULL;
}

QDTNode *qdt_get_node_relative(QDTNode *node, const gchar *path)
{
    const gchar *slash;
    gsize seglen;

    do {
        slash = strchr(path, '/');
        seglen = slash ? slash - path : strlen(path);

        node = get_subnode(node, path, seglen);
        path += seglen + 1;
    } while (node && slash);

    return node;
}

QDTNode *qdt_get_node(QDTNode *root, const gchar *path)
{
    g_assert(!root->parent);
    g_assert(path[0] == '/');
    return qdt_get_node_relative(root, path + 1);
}

QDTNode *qdt_add_subnode(QDTNode *parent, const gchar *name)
{
    QDTNode *child = qdt_new_node(name);

    child->parent = parent;
    QTAILQ_INSERT_TAIL(&parent->children, child, sibling);
    return child;
}

/*
 * Property functions
 */

static QDTProperty *getprop_(const QDTNode *node, const gchar *name)
{
    QDTProperty *prop;

    QTAILQ_FOREACH(prop, &node->properties, list) {
        if (strcmp(prop->name, name) == 0) {
            return prop;
        }
    }
    return NULL;
}

const QDTProperty *qdt_getprop(const QDTNode *node, const gchar *name)
{
    return getprop_(node, name);
}

void qdt_delprop(QDTNode *node, const gchar *name)
{
    QDTProperty *prop = getprop_(node, name);

    if (prop) {
        QTAILQ_REMOVE(&node->properties, prop, list);
        g_free(prop->name);
        g_free(prop);
    }
}

const QDTProperty *qdt_setprop(QDTNode *node, const gchar *name,
                               gconstpointer val, gsize len)
{
    QDTProperty *prop;

    qdt_delprop(node, name);

    prop = g_malloc0(sizeof(*prop) + len);
    prop->name = g_strdup(name);
    prop->len = len;
    memcpy(prop->val, val, len);
    QTAILQ_INSERT_TAIL(&node->properties, prop, list);
    return prop;
}

const QDTProperty *qdt_setprop_string(QDTNode *node, const gchar *name,
                                      const gchar *val)
{
    return qdt_setprop(node, name, val, strlen(val) + 1);
}

const QDTProperty *qdt_setprop_cells_(QDTNode *node, const gchar *name,
                                      const uint32_t *val, gsize len)
{
    uint32_t swapval[len];
    gsize i;

    for (i = 0; i < len; i++) {
        swapval[i] = cpu_to_fdt32(val[i]);
    }
    return qdt_setprop(node, name, swapval, sizeof(swapval));
}

const QDTProperty *qdt_setprop_u64s_(QDTNode *node, const gchar *name,
                                     const uint64_t *val, gsize len)
{
    uint64_t swapval[len];
    gsize i;

    for (i = 0; i < len; i++) {
        swapval[i] = cpu_to_fdt64(val[i]);
    }
    return qdt_setprop(node, name, swapval, sizeof(swapval));
}

void qdt_set_phandle(QDTNode *node, uint32_t phandle)
{
    g_assert((phandle != 0) && (phandle != (uint32_t)-1));
    qdt_setprop_cells(node, "linux,phandle", phandle);
    qdt_setprop_cells(node, "phandle", phandle);
}

/*
 * Whole tree functions
 */

static void qdt_flatten_node(void *fdt, QDTNode *node, Error **errp)
{
    QDTProperty *prop;
    QDTNode *subnode;
    Error *local_err = NULL;
    int ret;

    ret = fdt_begin_node(fdt, node->name);
    if (ret < 0) {
        error_setg(errp, "Error flattening device tree: fdt_begin_node(): %s",
                   fdt_strerror(ret));
        return;
    }

    QTAILQ_FOREACH(prop, &node->properties, list) {
        ret = fdt_property(fdt, prop->name, prop->val, prop->len);
        if (ret < 0) {
            error_setg(errp, "Error flattening device tree: fdt_property(): %s",
                       fdt_strerror(ret));
            return;
        }
    }

    QTAILQ_FOREACH(subnode, &node->children, sibling) {
        qdt_flatten_node(fdt, subnode, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    ret = fdt_end_node(fdt);
    if (ret < 0) {
        error_setg(errp, "Error flattening device tree: fdt_end_node(): %s",
                   fdt_strerror(ret));
        return;
    }
}

void *qdt_flatten(QDTNode *root, gsize bufsize, Error **errp)
{
    void *fdt = g_malloc0(bufsize);
    Error *local_err = NULL;
    int ret;

    assert(!root->parent); /* Should be a root node */

    ret = fdt_create(fdt, bufsize);
    if (ret < 0) {
        error_setg(errp, "Error flattening device tree: fdt_create(): %s",
                   fdt_strerror(ret));
        goto fail;
    }

    ret = fdt_finish_reservemap(fdt);
    if (ret < 0) {
        error_setg(errp,
                   "Error flattening device tree: fdt_finish_reservemap(): %s",
                   fdt_strerror(ret));
        goto fail;
    }

    qdt_flatten_node(fdt, root, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto fail;
    }

    ret = fdt_finish(fdt);
    if (ret < 0) {
        error_setg(errp, "Error flattening device tree: fdt_finish(): %s",
                   fdt_strerror(ret));
        goto fail;
    }

    return fdt;

fail:
    g_free(fdt);
    return NULL;
}
