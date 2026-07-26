#pragma once
#include <aether/aether.h>
#include <vector>

struct aether_display_list_builder_s {
    aether_text_measurer_fn text_measurer = nullptr;
    void*                   text_measurer_data = nullptr;
};

struct aether_display_list_s {
    std::vector<aether_paint_command_t> commands;

    ~aether_display_list_s() {
        for (auto& cmd : commands) {
            aether_paint_command_free(&cmd);
        }
    }
};
