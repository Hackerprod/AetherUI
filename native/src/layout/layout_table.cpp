/* ── Table layout (phase 1: unspanned automatic columns) ────────────── */
#include "layout_internal.h"

#include <algorithm>
#include <vector>

struct table_row_s {
    aether_node_s* element = nullptr;
    std::vector<aether_node_s*> cells;
};

static void collect_table_rows(const StyleLookup* styles, aether_node_s* container,
                               std::vector<table_row_s>& rows)
{
    for (auto* child : container->children) {
        if (child->type != AETHER_NODE_ELEMENT) continue;
        const auto child_style = get_style(styles, child);
        if (child_style.display == AETHER_DISPLAY_NONE) continue;
        if (child_style.display == AETHER_DISPLAY_TABLE_ROW) {
            table_row_s row{};
            row.element = child;
            for (auto* candidate : child->children) {
                if (candidate->type != AETHER_NODE_ELEMENT) continue;
                const auto cell_style = get_style(styles, candidate);
                if (cell_style.display == AETHER_DISPLAY_TABLE_CELL) {
                    row.cells.push_back(candidate);
                }
            }
            if (!row.cells.empty()) rows.push_back(std::move(row));
            continue;
        }

        // Row groups are transparent in phase 1. This admits ordinary
        // table/thead/tbody/tfoot markup without claiming row-group sizing.
        if (child_style.display != AETHER_DISPLAY_TABLE_CELL) {
            collect_table_rows(styles, child, rows);
        }
    }
}

static std::vector<aether_table_column_contribution_s> measure_table_columns(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    const std::vector<table_row_s>& rows,
    float maximum,
    aether_layout_result_s* result)
{
    size_t column_count = 0;
    for (const auto& row : rows) column_count = std::max(column_count, row.cells.size());
    std::vector<aether_table_column_contribution_s> columns(column_count);
    for (const auto& row : rows) {
        for (size_t column = 0; column < row.cells.size(); ++column) {
            const float minimum = measure_child_natural_width(
                engine, styles, row.cells[column], maximum, result,
                aether_intrinsic_size_mode_e::min_content);
            const float preferred = measure_child_natural_width(
                engine, styles, row.cells[column], maximum, result,
                aether_intrinsic_size_mode_e::max_content);
            columns[column].min_content = std::max(columns[column].min_content, minimum);
            columns[column].max_content = std::max(columns[column].max_content, preferred);
        }
    }
    return columns;
}

float measure_intrinsic_table_width(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* table,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode)
{
    std::vector<table_row_s> rows;
    collect_table_rows(styles, table, rows);
    const auto columns = measure_table_columns(engine, styles, rows, maximum, result);
    float contribution = 0.f;
    for (const auto& column : columns) {
        contribution += mode == aether_intrinsic_size_mode_e::min_content
            ? column.min_content : column.max_content;
    }
    return contribution;
}

float layout_table_children(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* table,
    aether_rectf content,
    percentage_height_s containing_height,
    aether_layout_result_s* result)
{
    std::vector<table_row_s> rows;
    collect_table_rows(styles, table, rows);
    if (rows.empty()) return 0.f;

    const auto contributions = measure_table_columns(engine, styles, rows, content.width, result);
    const auto column_widths = aether_resolve_auto_table_columns(contributions, content.width);
    float cursor_y = content.y;

    for (const auto& row : rows) {
        const auto row_style = get_style(styles, row.element);
        auto* row_box = result->create_box();
        row_box->kind = LAYOUT_BOX_ELEMENT;
        row_box->element = row.element;
        row_box->style = row_style;
        register_box(result, row_box);

        struct cell_completion_s {
            element_completion_s completion;
            size_t flow_begin = 0, flow_end = 0;
            size_t tail_begin = 0, tail_end = 0;
        };
        std::vector<cell_completion_s> cells;
        cells.reserve(row.cells.size());
        const size_t row_flow_begin = result->paint_order.size();
        float cursor_x = content.x;
        float row_height = 0.f;
        for (size_t column = 0; column < row.cells.size(); ++column) {
            const float column_width = column_widths[column];
            cell_completion_s cell;
            cell.flow_begin = result->paint_order.size();
            auto* cell_box = layout_element(engine, styles, row.cells[column],
                                            {cursor_x, cursor_y, column_width, content.height}, containing_height,
                                            cursor_y, true,
                                            0.f, false, 0.f, forced_height_kind_e::none, true, result,
                                            nullptr, &cell.completion);
            cell.flow_end = result->paint_order.size();
            row_height = std::max(row_height, cell_box->margin_box.height);
            row_box->add_child(cell_box);
            cells.push_back(cell);
            cursor_x += column_width;
        }

        row_box->content_box = {content.x, cursor_y, content.width, row_height};
        row_box->padding_box = row_box->content_box;
        row_box->border_box = row_box->content_box;
        row_box->margin_box = row_box->content_box;
        const size_t tail_begin = result->paint_order.size();
        for (auto& cell : cells) {
            auto* box = cell.completion.box;
            auto allocated = box->border_box;
            const float decorations = aether_thicknessf_vertical(cell.completion.padding) +
                                      aether_thicknessf_vertical(cell.completion.border);
            allocated.height = std::max(decorations, row_height -
                aether_thicknessf_vertical(cell.completion.margin));
            cell.tail_begin = result->paint_order.size();
            layout_complete_element(engine, styles, cell.completion, allocated, result, true);
            cell.tail_end = result->paint_order.size();
        }
        if (result->records_boxes && result->paint_order.size() != tail_begin) {
            // Restore cell flow + completion registration chronology with one
            // linear scratch copy. Numeric boundaries survive vector growth.
            std::vector<aether_layout_box_s*> ordered;
            ordered.reserve(result->paint_order.size() - row_flow_begin);
            for (const auto& cell : cells) {
                ordered.insert(ordered.end(), result->paint_order.begin() + cell.flow_begin,
                               result->paint_order.begin() + cell.flow_end);
                ordered.insert(ordered.end(), result->paint_order.begin() + cell.tail_begin,
                               result->paint_order.begin() + cell.tail_end);
            }
            std::copy(ordered.begin(), ordered.end(), result->paint_order.begin() + row_flow_begin);
        }
        parent_box->add_child(row_box);
        cursor_y += row_height;
    }
    return std::max(0.f, cursor_y - content.y);
}
