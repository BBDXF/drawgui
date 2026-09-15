#include "drawgui/theme/theme_bindings.h"

#include <cstddef>
#include <string>

#include "drawgui/layout/layout_tree.h"

namespace dg {
namespace {

[[nodiscard]] PropWrite reject(const LayoutTree& tree, NodeId node, PropStatus status,
                               const std::string& message) {
  return PropWrite{status, message + "\n    at " + tree.path_of(node)};
}

// Resolves `token_id`'s current value under `variant` and writes it through
// dg::set_prop() - the one place a resolved token becomes a concrete
// PropValue, shared by bind_token() (the first write) and
// ThemeBindings::apply() (every later theme switch), so the two can never
// disagree about how a token becomes a value.
[[nodiscard]] PropWrite resolve_and_write(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                                          const Theme& theme, ThemeVariant variant,
                                          dg_token_id token_id) {
  const std::optional<TokenType> token_kind = token_type(token_id);
  if (!token_kind.has_value()) {
    return reject(tree, node, PropStatus::kUnknownId,
                  "theme: no token has id " + std::to_string(token_id));
  }
  const std::optional<PropType> declared = prop_type(prop_id);
  if (!declared.has_value()) {
    return reject(tree, node, PropStatus::kUnknownId,
                  "property: no property has id " + std::to_string(prop_id));
  }

  if (*token_kind == TokenType::k_color) {
    if (*declared != PropType::k_color) {
      return reject(tree, node, PropStatus::kTypeMismatch,
                    "theme: a color token cannot bind a non-color property");
    }
    const std::optional<Color> value = theme.color_value(token_id, variant);
    if (!value.has_value()) {
      return reject(tree, node, PropStatus::kValueOutOfRange,
                    "theme: '" + std::string(token_name(token_id).value_or("?")) +
                        "' has no value for this variant in the loaded theme");
    }
    return set_prop(tree, node, prop_id, PropValue::color(*value));
  }

  // k_int: may bind a k_float or a k_length property - the two scalar
  // PropTypes that already round through to_pixels() into device pixels
  // (node_props.cpp), the same conversion an ordinary literal `gap`/`width`
  // write goes through.
  if (*declared != PropType::k_float && *declared != PropType::k_length) {
    return reject(tree, node, PropStatus::kTypeMismatch,
                  "theme: an int token cannot bind a color/enum/complex property");
  }
  const std::optional<int> value = theme.int_value(token_id);
  if (!value.has_value()) {
    return reject(tree, node, PropStatus::kValueOutOfRange,
                  "theme: '" + std::string(token_name(token_id).value_or("?")) +
                      "' has no value in the loaded theme");
  }
  const PropValue prop_value = *declared == PropType::k_length
                                   ? PropValue::length(static_cast<float>(*value))
                                   : PropValue::number(static_cast<float>(*value));
  return set_prop(tree, node, prop_id, prop_value);
}

}  // namespace

void ThemeBindings::bind(NodeId node, dg_prop_id prop_id, dg_token_id token_id) {
  const std::size_t index = node.value;
  if (by_node_.size() <= index) {
    by_node_.resize(index + 1);
  }
  std::vector<TokenBinding>& node_bindings = by_node_[index];
  for (TokenBinding& existing : node_bindings) {
    if (existing.prop_id == prop_id) {
      existing.token_id = token_id;
      return;
    }
  }
  node_bindings.push_back(TokenBinding{prop_id, token_id});
}

std::optional<dg_token_id> ThemeBindings::token_for(NodeId node, dg_prop_id prop_id) const {
  if (node.value >= by_node_.size()) {
    return std::nullopt;
  }
  for (const TokenBinding& binding : by_node_[node.value]) {
    if (binding.prop_id == prop_id) {
      return binding.token_id;
    }
  }
  return std::nullopt;
}

std::vector<TokenBinding> ThemeBindings::bindings_for(NodeId node) const {
  if (node.value >= by_node_.size()) {
    return {};
  }
  return by_node_[node.value];
}

std::size_t ThemeBindings::apply(LayoutTree& tree, const Theme& theme,
                                 ThemeVariant variant) const {
  std::size_t unresolved = 0;
  for (std::size_t index = 0; index < by_node_.size(); ++index) {
    const NodeId node{static_cast<std::uint32_t>(index)};
    for (const TokenBinding& binding : by_node_[index]) {
      const PropWrite result =
          resolve_and_write(tree, node, binding.prop_id, theme, variant, binding.token_id);
      if (!result.ok()) {
        ++unresolved;
      }
    }
  }
  return unresolved;
}

PropWrite bind_token(LayoutTree& tree, ThemeBindings& bindings, NodeId node, dg_prop_id prop_id,
                     const Theme& theme, ThemeVariant variant, dg_token_id token_id) {
  const PropWrite result = resolve_and_write(tree, node, prop_id, theme, variant, token_id);
  if (!result.ok()) {
    return result;
  }
  bindings.bind(node, prop_id, token_id);
  return result;
}

}  // namespace dg
