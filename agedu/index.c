/*
 * index.c: Implementation of index.h.
 */

#include <assert.h>
#include <stdio.h>
#include <stddef.h>

#include "trie.h"
#include "index.h"
#include "malloc.h"

#define alignof(typ) ( offsetof(struct { char c; typ t; }, t) )

#define min(x,y) ((x)<(y) ? (x):(y))
#define max(x,y) ((x)>(y) ? (x):(y))

#define PADDING(x, mod) ( ((mod) - ((x) % (mod))) % (mod) )

struct avlnode {
    off_t children[2], element;
    int maxdepth;		       /* maximum depth of this subtree */
    unsigned long long totalsize;
};

static int index_navlnodes(int nodecount)
{
    int b, c, maxdepth, ret;

    /*
     * Model the tree growing at maximum imbalance. We do this by
     * determining the number of nodes in the most unbalanced
     * (i.e. smallest) tree of any given depth, and stopping when
     * that's larger than nodecount.
     */
    ret = 0;
    maxdepth = 1;
    b = 0;
    c = 1;
    while (b <= nodecount) {
	int tmp;

	/*
	 * A tree with at most b nodes can be at most depth
	 * maxdepth-1. A tree with more than b but at most c can
	 * be at most maxdepth. Therefore, for each tree size
	 * between b and c, we might need to adjust maxdepth+1
	 * nodes.
	 */
	tmp = min(c, nodecount);
	ret += (tmp - b) * maxdepth;

	tmp = 1 + b + c;
	b = c;
	c = tmp;
	maxdepth++;
    }

    return ret;
}

off_t index_compute_size(off_t currentsize, int nodecount)
{
    currentsize += PADDING(currentsize, alignof(off_t));
    currentsize += nodecount + sizeof(off_t);
    currentsize += PADDING(currentsize, alignof(struct avlnode));
    currentsize += index_navlnodes(nodecount) * sizeof(struct avlnode);

    return currentsize;
}

/* ----------------------------------------------------------------------
 * Functions to build the index.
 */

struct indexbuild {
    void *t;
    int n, nnodes, maxnodes;
    struct avlnode *nodes;
    off_t *roots;
    struct avlnode *currroot;
    struct avlnode *firstmutable;
};

#define ELEMENT(t,offset) \
    ((offset) ? (struct trie_file *)((char *)(t) + (offset)) : NULL)
#define NODE(t,offset) \
    ((offset) ? (struct avlnode *)((char *)(t) + (offset)) : NULL)
#define OFFSET(t,node) \
    ((node) ? (off_t)((const char *)node - (const char *)t) : 0)
#define MAXDEPTH(node) ((node) ? (node)->maxdepth : 0)

indexbuild *indexbuild_new(void *t, off_t startoff, int nodecount)
{
    indexbuild *ib = snew(indexbuild);

    ib->t = t;
    startoff += PADDING(startoff, alignof(off_t));
    ib->roots = (off_t *)((char *)t + startoff);
    trie_set_index_offset(t, startoff);
    startoff += nodecount * sizeof(off_t);
    startoff += PADDING(startoff, alignof(struct avlnode));
    ib->nodes = (struct avlnode *)((char *)t + startoff);
    ib->maxnodes = index_navlnodes(nodecount);
    ib->nnodes = ib->n = 0;
    ib->currroot = NULL;
    ib->firstmutable = ib->nodes + ib->nnodes;

    return ib;
}

/*
 * Return a mutable node, which is n or a copy of n if n is
 * non-NULL.
 */
static struct avlnode *avl_makemutable(indexbuild *ib, struct avlnode *n)
{
    struct avlnode *newnode;

    if (n && n >= ib->firstmutable)
	return n;		       /* already mutable */

    assert(ib->nnodes < ib->maxnodes);
    newnode = ib->nodes + ib->nnodes++;
    if (n)
	*newnode = *n;		       /* structure copy */
    return newnode;
}

/*
 * Fix the annotations in a tree node.
 */
static void avl_fix(indexbuild *ib, struct avlnode *n)
{
    /*
     * Make sure the max depth field is right.
     */
    n->maxdepth = 1 + max(MAXDEPTH(NODE(ib->t, n->children[0])),
			  MAXDEPTH(NODE(ib->t, n->children[1])));

    n->totalsize =
	(ELEMENT(ib->t, n->element)->size +
	 (n->children[0] ? NODE(ib->t, n->children[0])->totalsize : 0) +
	 (n->children[1] ? NODE(ib->t, n->children[1])->totalsize : 0));
}

static struct avlnode *avl_insert(indexbuild *ib, struct avlnode *n,
				  off_t node)
{
    struct trie_file *newfile;
    struct trie_file *oldfile;
    int subtree;
    struct avlnode *nn;
 
    /*
     * Recursion bottoming out: if the subtree we're inserting
     * into is null, just construct and return a fresh node.
     */
    if (!n) {
	n = avl_makemutable(ib, NULL);
	n->children[0] = n->children[1] = 0;
	n->element = node;
	avl_fix(ib, n);
	return n;
    }

    /*
     * Otherwise, we have to insert into an existing tree.
     */

    /*
     * Determine which subtree to insert this node into. Ties
     * aren't important, so we just break them any old way.
     */
    newfile = (struct trie_file *)((char *)ib->t + node);
    oldfile = (struct trie_file *)((char *)ib->t + n->element);
    if (newfile->atime > oldfile->atime)
	subtree = 1;
    else
	subtree = 0;

    /*
     * Construct a copy of the node we're looking at.
     */
    n = avl_makemutable(ib, n);

    /*
     * Recursively insert into the next subtree down.
     */
    nn = avl_insert(ib, NODE(ib->t, n->children[subtree]), node);
    n->children[subtree] = OFFSET(ib->t, nn);

    /*
     * Rebalance if necessary, to ensure that our node's children
     * differ in maximum depth by at most one. Of course, the
     * subtree we've just modified will be the deeper one if so.
     */
    if (MAXDEPTH(NODE(ib->t, n->children[subtree])) >
	MAXDEPTH(NODE(ib->t, n->children[1-subtree])) + 1) {
	struct avlnode *p, *q;

	/*
	 * There are two possible cases, one of which requires a
	 * single tree rotation and the other requires two. It all
	 * depends on which subtree of the next node down (here p)
	 * is the taller. (It turns out that they can't both be
	 * the same height: any tree which has just increased in
	 * depth must have one subtree strictly taller than the
	 * other.)
	 */
	p = NODE(ib->t, n->children[subtree]);
	assert(p >= ib->firstmutable);
	if (MAXDEPTH(NODE(ib->t, p->children[subtree])) >=
	    MAXDEPTH(NODE(ib->t, p->children[1-subtree]))) {
	    /*
	     *     n                       p
	     *    / \                     / \
	     *  [k]  p         ->        n  [k+1]
	     *      / \                 / \
	     *    [k] [k+1]           [k] [k]
	     */
	    n->children[subtree] = p->children[1-subtree];
	    p->children[1-subtree] = OFFSET(ib->t, n);
	    avl_fix(ib, n);
	    n = p;
	} else {
	    q = NODE(ib->t, p->children[1-subtree]);
	    assert(q >= ib->firstmutable);
	    p->children[1-subtree] = OFFSET(ib->t, q);
	    /*
	     *     n               n                 q
	     *    / \             / \              /   \
	     *  [k]  p    ==    [k]  p      ->    n     p
	     *      / \             / \          / \   / \
	     *  [k+1] [k]          q  [k]      [k]  \ /  [k]
	     *                    / \         [k-1,k] [k-1,k]
	     *              [k-1,k] [k-1,k]
	     */
	    n->children[subtree] = q->children[1-subtree];
	    p->children[1-subtree] = q->children[subtree];
	    q->children[1-subtree] = OFFSET(ib->t, n);
	    q->children[subtree] = OFFSET(ib->t, p);
	    avl_fix(ib, n);
	    avl_fix(ib, p);
	    n = q;
	}
    }

    /*
     * Fix up our maximum depth field.
     */
    avl_fix(ib, n);

    /*
     * Done.
     */
    return n;
}

void indexbuild_add(indexbuild *ib, const struct trie_file *tf)
{
    off_t node = OFFSET(ib->t, tf);
    ib->currroot = avl_insert(ib, ib->currroot, node);
    ib->roots[ib->n++] = OFFSET(ib->t, ib->currroot);
    ib->firstmutable = ib->nodes + ib->nnodes;
}

off_t indexbuild_realsize(indexbuild *ib)
{
    return OFFSET(ib->t, (ib->nodes + ib->nnodes));
}

void indexbuild_free(indexbuild *ib)
{
    sfree(ib);
}

unsigned long long index_query(const void *t, int n, unsigned long long at)
{
    const off_t *roots;
    const struct avlnode *node;
    unsigned long count;
    unsigned long long ret;

    roots = (const off_t *)((const char *)t + trie_get_index_offset(t));

    if (n < 1)
	return 0;
    count = trie_count(t);
    if (n > count)
	n = count;

    node = NODE(t, roots[n-1]);

    ret = 0;

    while (node) {
	const struct trie_file *tf = ELEMENT(t, node->element);
	const struct avlnode *left = NODE(t, node->children[0]);
	const struct avlnode *right = NODE(t, node->children[1]);

	if (at <= tf->atime) {
	    node = left;
	} else {
	    if (left)
		ret += left->totalsize;
	    ret += tf->size;
	    node = right;
	}
    }

    return ret;
}

unsigned long long index_order_stat(const void *t, double f)
{
    const off_t *roots;
    const struct avlnode *node;
    unsigned long count;
    unsigned long long size;

    roots = (const off_t *)((const char *)t + trie_get_index_offset(t));
    count = trie_count(t);
    node = NODE(t, roots[count-1]);

    size = node->totalsize * f;
    assert(size <= node->totalsize);

    while (1) {
	const struct trie_file *tf = ELEMENT(t, node->element);
	const struct avlnode *left = NODE(t, node->children[0]);
	const struct avlnode *right = NODE(t, node->children[1]);

	if (left && size < left->totalsize) {
	    node = left;
	} else if (!right ||
                   size < (left ? left->totalsize : 0) + tf->size) {
	    return tf->atime;
	} else {
	    if (left)
		size -= left->totalsize;
	    size -= tf->size;
	    node = right;
	}
    }
}