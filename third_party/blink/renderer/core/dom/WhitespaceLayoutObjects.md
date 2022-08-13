# Whitespace LayoutObjects

Text nodes which only contain whitespaces sometimes have a layout object and
sometimes they don't. This document tries to explain why and how these layout
objects are created.

## Why

For layout purposes, whitespace nodes are sometimes significant, but not
always. In Blink, we try to create as few of them as possible to save memory,
and save CPU by having fewer layout objects to traverse.

### Inline flow

Whitespace typically matters in an inline flow context. Example:

    <span>A</span> </span>B</span>

If we didn't create a LayoutObject for the whitespace node between the two
spans, we would have rendered the markup above as "AB" as the span layout
objects would have been siblings in the layout tree.

### Block flow

Whitespace typically doesn't matter in a block flow context. Example:

    <div>A</div> <div>B</div>

In the example above, the whitespace node between the divs would not contribute
to layout/rendering. Hence, we can skip creating a LayoutText for it.

### Out-of-flow

Out-of-flow elements like absolutely positioned elements do not affect inline
or block in-flow layout. That means we can skip such elements when considering
the need for whitespace layout objects.

Example:

    <div><span style="position:absolute">A</span> </span>B</span></div>

In the example above, we don't need to create a whitespace layout object since
it will be the first in-flow child of the block, and will not contribute to the
layout/rendering.

Example:

    <span>A</span> <span style="position:absolute">Z</span> <span>B</span>

In the example above, we need to create a whitespace layout object to separate
the A and B in the rendering. However, we only need to create a layout object
for one of the whitespace nodes as whitespace collapse.

### Preformatted text and editing

Some values of the CSS white-space property will cause whitespace to not
collapse and affect layout and rendering also in block layout. In those cases
we always create layout objects for whitespace nodes.

Whitespace nodes are also significant in editing mode.

## How

We decide if we need to attach or re-attach the layout tree for a text node as
part of the layout tree rebuild. A layout tree rebuild starts at the
documentElement by calling Element::RebuildLayoutTree.

### RebuildLayoutTree

The Element::RebuildLayoutTree traversal happens in the flat tree order
traversing children from right to left (see RebuildChildrenLayoutTrees in the
ContainerNode class). We keep track of the last seen text node in an instance
of the WhitespaceAttacher class which is passed to the various traversal
methods.

Important methods:

    WhitespaceAttacher::*
    Element::RebuildLayoutTree
    ContainerNode::RebuildChildrenLayoutTrees

### Attaching a layout tree

Once we encounter a node which needs re-attachment during RebuildLayoutTree, we
do a Node::AttachLayoutTree for that sub-tree. AttachLayoutTree attaches nodes
in the flat tree order, but as opposed to RebuildLayoutTree, siblings are
attached from left to right. In order to decide if a whitespace Text node needs
a LayoutObject or not, we keep track of the previous in-flow layout tree
sibling in the AttachContext passed to AttachLayoutTree.

When a sub-tree has been (re-)attached, we know which is the last in-flow
LayoutObject of that subtree (normally the root, unless the root is
display:contents or display:none). This last in-flow layout object, stored in
the AttachContext, is passed into the WhitespaceAttacher to see if a sub-sequent
whitespace node needs to be re-attached due to a different display type for the
root of the (re-)attached subtree. See following sub-section.

Important methods:

    Text::AttachLayoutTree()
    Text::TextLayoutObjectIsNeeded()

#### Re-attaching whitespace layout objects

When the computed display value changes, the requirement for whitespace
siblings may change.

Example:

    <span style="position:absolute">A</span> <span>B</span>

Initially, we don't need a layout object for the whitespace above. If we change
the position of the first span to static, we need a layout object for the
whitespace. During layout tree rebuild, we keep track of the last text node
sibling in the WhitespaceAttacher. The text node is reset when we encounter a
node with an in-flow layout box. Remember that we traverse from right to left.
That means we have stored the whitespace node above in the WhitespaceAttacher
when we re-attach the left-most span. After re-attachment we re-attach the
stored text node based on passing the first span with its LayoutObject as
previous in-flow into WhitespaceAttacher::DidReattachElement.

DidReattachElement will re-attach whitespace siblings as necessary.

#### Known issues

    https://crbug.com/750758
