#!/bin/sh
# Run after run.sh has produced core/*.o; manual only, no GitHub Actions.
set -eu
out=${1:?usage: tests/hit_order_cache/regressions.sh BUILD_DIRECTORY}
mkdir -p "$out/regressions"
for name in test_block_static_position_lifetime test_cyclic_intrinsic_edges_lifetime test_descendant_intrinsic_width_lifetime test_flex_column_basis_private test_flex_content_basis_lifetime test_flex_cross_auto_lifetime test_flex_definite_basis_lifetime test_flex_inflexible_intrinsic_lifetime test_flex_reverse_private test_flex_stretch_deferred_private test_flex_wrap_intrinsic_lifetime test_inline_hanging_oom test_inline_units_oom test_layout_checked_edges_atomic test_password_presentation_lifetime test_table_cell_final_private test_z_index test_layout_immutable_consumers test_contextual_transition_frame test_flex_static_position_contextual test_flex_text_lifetime test_h4_ownership_oom; do
 flags=
 if [ "$name" = test_layout_checked_edges_atomic ]; then
  flags="-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc"
 fi
 c++ -std=c++20 -O1 -Inative/include -Inative/src -Inative/src/rendering "native/tests/$name.cpp" "$out"/core/*.o -pthread $flags -o "$out/regressions/$name"
 "$out/regressions/$name" > "$out/regressions/$name.log" 2>&1
 printf 'PASS %s\n' "$name"
done
