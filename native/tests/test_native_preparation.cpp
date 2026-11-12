#ifdef NDEBUG
#error "This test requires assertions"
#endif
#include <aether/rendering/preparation/aether_command_source.hpp>
#include <aether/rendering/preparation/preparation_metrics.hpp>
#include <cassert>
#include <variant>
using namespace aether::rendering::preparation;
struct aether_display_list_s { int valid{}; };
namespace {
int frees{};
std::uint32_t version() noexcept { return AETHER_ABI_VERSION_CURRENT; }
aether_capabilities_t capabilities() noexcept { return AETHER_CAPABILITY_DISPLAY_LIST; }
std::size_t size() noexcept { return sizeof(aether_paint_command_t); }
std::int32_t count(aether_display_list_t) noexcept { return 1; }
aether_status_t get(aether_display_list_t, std::int32_t, aether_paint_command_t* out) noexcept {
 *out={}; out->tag=AETHER_PAINT_FILL_ROUNDED_RECT;
 out->data.fill_rounded_rect.rect={0,0,2,3};
 out->data.fill_rounded_rect.color={1,0,0,1}; return AETHER_OK;
}
void release(aether_paint_command_t*) noexcept { ++frees; }
struct Sink : CommandSink {
 int accepted{};
 bool accept(const PreparedEvent& e) override {
  assert(std::holds_alternative<Fill>(e.command)); ++accepted; return true;
 }
};
}
int main() {
 static_assert(snapshot_supported_tags.size()==9);
 aether_display_list_s list;
 AetherApi api{version,capabilities,size,count,get,release};
 auto snapshot=snapshot_commands(api,&list);
 assert(std::holds_alternative<Snapshot>(snapshot) && frees==1);
 auto prepared=prepare(std::get<Snapshot>(snapshot));
 assert(std::holds_alternative<PreparedFrame>(prepared));
 Sink sink;
 assert(!replay(std::get<PreparedFrame>(prepared),sink) && sink.accepted==1);
 PreparationMetrics metrics(nullptr);
 assert(metrics.begin(Phase::snapshot_copy));
 assert(metrics.finish(Phase::snapshot_copy,PhaseState::succeeded));
 assert(metrics.snapshot_copy().timing_reason==TimingReason::clock_unavailable);
}
