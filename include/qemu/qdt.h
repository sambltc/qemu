/*
 * Functions for manipulating IEEE1275 (Open Firmware) style device
 * trees.
 *
 * Copyright David Gibson, Red Hat Inc. 2016
 *
 * This work is licensed unter the GNU GPL version 2 or (at your
 * option) any later version.
 */
#ifndef QEMU_QDT_H__
#define QEMU_QDT_H__

#include <string.h>
#include <stdint.h>
#include <glib.h>
#include "qemu/queue.h"

typedef struct QDTProperty QDTProperty;
typedef struct QDTNode QDTNode;

struct QDTProperty {
    gchar *name;
    QTAILQ_ENTRY(QDTProperty) list;
    gsize len;
    uint8_t val[];
};

struct QDTNode {
    gchar *name;
    QDTNode *parent;
    QTAILQ_HEAD(, QDTProperty) properties;
    QTAILQ_HEAD(, QDTNode) children;
    QTAILQ_ENTRY(QDTNode) sibling;
};

/*
 * Node functions
 */

QDTNode *qdt_new_node(const gchar *name);
QDTNode *qdt_get_node_relative(QDTNode *node, const gchar *path);
QDTNode *qdt_get_node(QDTNode *root, const gchar *path);
QDTNode *qdt_add_subnode(QDTNode *parent, const gchar *name);

static inline QDTNode *qdt_new_tree(void)
{
    return qdt_new_node("");
}

/*
 * Property functions
 */

const QDTProperty *qdt_getprop(const QDTNode *node, const gchar *name);
void qdt_delprop(QDTNode *node, const gchar *name);
const QDTProperty *qdt_setprop(QDTNode *node, const gchar *name,
                               gconstpointer val, gsize len);
const QDTProperty *qdt_setprop_cells_(QDTNode *node, const gchar *name,
                                      const uint32_t *val, gsize len);
const QDTProperty *qdt_setprop_u64s_(QDTNode *node, const gchar *name,
                                     const uint64_t *val, gsize len);
const QDTProperty *qdt_setprop_string(QDTNode *node, const gchar *name,
                                      const gchar *val);
void qdt_set_phandle(QDTNode *node, uint32_t phandle);

#define qdt_setprop_bytes(node, name, ...)                              \
    ({                                                                  \
        uint8_t vals[] = { __VA_ARGS__ };                               \
        qdt_setprop((node), (name), vals, sizeof(vals));                \
    })
#define qdt_setprop_cells(node, name, ...)                              \
    ({                                                                  \
        uint32_t vals[] = { __VA_ARGS__ };                              \
        qdt_setprop_cells_((node), (name), vals, ARRAY_SIZE(vals));     \
    })
#define qdt_setprop_u64s(node, name, ...)                               \
    ({                                                                  \
        uint64_t vals[] = { __VA_ARGS__ };                              \
        qdt_setprop_u64s_((node), (name), vals, ARRAY_SIZE(vals));      \
    })
static inline const QDTProperty *qdt_setprop_empty(QDTNode *node,
                                                   const gchar *name)
{
    return qdt_setprop_bytes(node, name);
}
static inline const QDTProperty *qdt_setprop_dup(QDTNode *node,
                                                 const gchar *name,
                                                 const QDTProperty *oldprop)
{
    return qdt_setprop(node, name, oldprop->val, oldprop->len);
}

/*
 * Whole tree functions
 */

void *qdt_flatten(QDTNode *root, gsize bufsize, Error **errp);

#endif /* QEMU_QDT_H__ */
