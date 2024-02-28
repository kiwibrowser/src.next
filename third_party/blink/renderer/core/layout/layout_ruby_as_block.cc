// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/layout_ruby_as_block.h"

#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/layout/layout_ruby.h"
#include "third_party/blink/renderer/core/layout/layout_ruby_column.h"
#include "third_party/blink/renderer/core/layout/ruby_container.h"

namespace blink {

LayoutRubyAsBlock::LayoutRubyAsBlock(Element* element)
    : LayoutNGBlockFlow(element),
      ruby_container_(RuntimeEnabledFeatures::RubySimplePairingEnabled()
                          ? MakeGarbageCollected<RubyContainer>(*this)
                          : nullptr) {
  UseCounter::Count(GetDocument(), WebFeature::kRenderRuby);
}

LayoutRubyAsBlock::~LayoutRubyAsBlock() = default;

void LayoutRubyAsBlock::Trace(Visitor* visitor) const {
  visitor->Trace(ruby_container_);
  LayoutNGBlockFlow::Trace(visitor);
}

void LayoutRubyAsBlock::AddChild(LayoutObject* child,
                                 LayoutObject* before_child) {
  NOT_DESTROYED();

  if (RuntimeEnabledFeatures::BlockRubyWrappingInlineRubyEnabled()) {
    LayoutObject* inline_ruby = FirstChild();
    if (!inline_ruby) {
      inline_ruby = MakeGarbageCollected<LayoutRubyAsInline>(nullptr);
      inline_ruby->SetDocumentForAnonymous(&GetDocument());
      ComputedStyleBuilder new_style_builder =
          GetDocument()
              .GetStyleResolver()
              .CreateAnonymousStyleBuilderWithDisplay(StyleRef(),
                                                      EDisplay::kRuby);
      inline_ruby->SetStyle(new_style_builder.TakeStyle());
      LayoutNGBlockFlow::AddChild(inline_ruby);
    }
    inline_ruby->AddChild(child, before_child);
    return;
  }

  // If the child is a ruby column, just add it normally.
  if (child->IsRubyColumn()) {
    LayoutNGBlockFlow::AddChild(child, before_child);
    return;
  }

  if (RuntimeEnabledFeatures::RubySimplePairingEnabled()) {
    ruby_container_->AddChild(child, before_child);
    return;
  }

  if (before_child) {
    // Insert the child into a column.
    LayoutObject* column = before_child;
    while (column && !column->IsRubyColumn()) {
      column = column->Parent();
    }
    if (column) {
      if (before_child == column) {
        before_child = To<LayoutRubyColumn>(before_child)->FirstChild();
      }
      DCHECK(!before_child || before_child->IsDescendantOf(column));
      column->AddChild(child, before_child);
      return;
    }
    NOTREACHED();  // before_child should always have a column as parent!
                   // Emergency fallback: fall through and just append.
  }

  // If the new child would be appended, try to add the child to the previous
  // column if possible, or create a new column otherwise.
  // (The LayoutRubyColumn object will handle the details)
  auto* last_column = LayoutRubyAsInline::LastRubyColumn(*this);
  if (!last_column || last_column->HasRubyText()) {
    last_column = &LayoutRubyColumn::Create(this, *this);
    LayoutNGBlockFlow::AddChild(last_column, before_child);
    last_column->EnsureRubyBase();
  }
  last_column->AddChild(child);
}

void LayoutRubyAsBlock::RemoveChild(LayoutObject* child) {
  NOT_DESTROYED();
  if (RuntimeEnabledFeatures::BlockRubyWrappingInlineRubyEnabled()) {
    if (child->Parent() == this) {
      DCHECK(DynamicTo<LayoutRubyAsInline>(child));
      LayoutNGBlockFlow::RemoveChild(child);
      return;
    }
    NOTREACHED() << child;
    return;
  }

  // If the child's parent is *this (must be a ruby column), just use the normal
  // remove method.
  if (child->Parent() == this) {
    DCHECK(child->IsRubyColumn());
    LayoutNGBlockFlow::RemoveChild(child);
    return;
  }

  if (RuntimeEnabledFeatures::RubySimplePairingEnabled()) {
    NOTREACHED();
    return;
  }

  // Otherwise find the containing column and remove it from there.
  auto* column = LayoutRubyAsInline::FindRubyColumnParent(child);
  DCHECK(column);
  column->RemoveChild(child);
}

void LayoutRubyAsBlock::DidRemoveChildFromColumn(LayoutObject& child) {
  DCHECK(!RuntimeEnabledFeatures::BlockRubyWrappingInlineRubyEnabled());
  ruby_container_->DidRemoveChildFromColumn(child);
}

void LayoutRubyAsBlock::StyleDidChange(StyleDifference diff,
                                       const ComputedStyle* old_style) {
  NOT_DESTROYED();
  LayoutNGBlockFlow::StyleDidChange(diff, old_style);
  PropagateStyleToAnonymousChildren();
  if (RuntimeEnabledFeatures::BlockRubyWrappingInlineRubyEnabled()) {
    // Because LayoutInline::AnonymousHasStylePropagationOverride() returns
    // true, PropagateStyleToAnonymousChildren() doesn't update the style of
    // the LayoutRuby child.
    if (auto* inline_ruby = FirstChild()) {
      ComputedStyleBuilder new_style_builder =
          GetDocument()
              .GetStyleResolver()
              .CreateAnonymousStyleBuilderWithDisplay(
                  StyleRef(), inline_ruby->StyleRef().Display());
      UpdateAnonymousChildStyle(inline_ruby, new_style_builder);
      inline_ruby->SetStyle(new_style_builder.TakeStyle());
    }
  }
}

void LayoutRubyAsBlock::RemoveLeftoverAnonymousBlock(LayoutBlock*) {
  NOT_DESTROYED();
  NOTREACHED();
}

}  // namespace blink
