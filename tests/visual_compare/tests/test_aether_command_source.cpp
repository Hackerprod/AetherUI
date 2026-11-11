#include "aether_visual_compare/aether_command_source.hpp"
#include <cassert>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

// A legitimate, live fake list. Its callbacks never invoke the engine.
struct aether_display_list_s {
 aether_paint_command_t prototype{};
 std::string text_buffer{"hello\0", 6};
 std::int32_t count{1};
 std::int32_t fail_at{-1};
};
namespace {
using namespace avc::preparation;
struct State {
 std::uint32_t version{AETHER_ABI_VERSION_CURRENT};
 aether_capabilities_t capabilities{AETHER_CAPABILITY_DISPLAY_LIST};
 std::size_t size{sizeof(aether_paint_command_t)};
 int counts{}, gets{}, frees{};
} state;
std::uint32_t version() noexcept { return state.version; }
aether_capabilities_t capabilities() noexcept { return state.capabilities; }
std::size_t command_size() noexcept { return state.size; }
std::int32_t count(aether_display_list_t list) noexcept { ++state.counts; return list->count; }
aether_status_t get(aether_display_list_t list, std::int32_t index,
                    aether_paint_command_t* output) noexcept {
 ++state.gets;
 if (index == list->fail_at) return AETHER_ERROR_INVALID_STATE;
 auto command = list->prototype;
 if (command.tag == AETHER_PAINT_DRAW_TEXT && command.data.draw_text.text) {
  auto* text = new (std::nothrow) char[list->text_buffer.size()];
  if (!text) return AETHER_ERROR_OUT_OF_MEMORY;
  std::memcpy(text, list->text_buffer.data(), list->text_buffer.size());
  command.data.draw_text.text = text;
 }
 *output = command;
 return AETHER_OK;
}
void free_command(aether_paint_command_t* command) noexcept {
 ++state.frees;
 if (command->tag == AETHER_PAINT_DRAW_TEXT) {
  delete[] command->data.draw_text.text;
  command->data.draw_text.text = nullptr;
 }
}
const AetherApi api{version, capabilities, command_size, count, get, free_command};
static_assert(std::is_convertible_v<decltype(&get), decltype(api.get_command)>);
static_assert(std::is_convertible_v<decltype(&free_command), decltype(api.free_command)>);
void reset() { state = State{}; }
void expect_error(const SnapshotResult& result, ErrorCode code) {
 assert(std::holds_alternative<Error>(result));
 assert(std::get<Error>(result).code == code);
}
aether_display_list_s text_list() {
 aether_display_list_s list;
 list.prototype.tag = AETHER_PAINT_DRAW_TEXT;
 auto& text = list.prototype.data.draw_text;
 // This live marker is only a signal to FakeApi, which allocates its own copy.
 static char marker[] = "text";
 text.text = marker;
 text.font_family[0] = 'f';
 text.font_family[1] = '\0';
 text.font_size = 12;
 text.font_weight = 400;
 text.text_align = AETHER_TEXT_ALIGN_START;
 text.wrap = AETHER_FALSE;
 return list;
}
struct CancelAtCall final : Cancellation {
 int calls{};
 int stop{};
 explicit CancelAtCall(int value) : stop(value) {}
 bool cancelled() noexcept override { return ++calls >= stop; }
};
void gates() {
 aether_display_list_s list;
 for (int missing = 0; missing < 6; ++missing) {
  reset();
  auto incomplete = api;
  switch (missing) {
  case 0: incomplete.abi_version = nullptr; break;
  case 1: incomplete.abi_capabilities = nullptr; break;
  case 2: incomplete.paint_command_size = nullptr; break;
  case 3: incomplete.get_count = nullptr; break;
  case 4: incomplete.get_command = nullptr; break;
  case 5: incomplete.free_command = nullptr; break;
  }
  expect_error(snapshot_commands(incomplete, &list), ErrorCode::INVALID_API);
  assert(state.counts == 0 && state.gets == 0 && state.frees == 0);
 }
 reset();
 expect_error(snapshot_commands(api, nullptr), ErrorCode::INVALID_LIST);
 assert(state.counts == 0 && state.gets == 0 && state.frees == 0);
 for (auto packed : {AETHER_ABI_VERSION_MAKE(1,34,0), AETHER_ABI_VERSION_MAKE(0,35,0),
                     AETHER_ABI_VERSION_MAKE(0,34,1)}) {
  reset(); state.version = packed;
  expect_error(snapshot_commands(api, &list), ErrorCode::ABI_VERSION);
  assert(state.counts == 0 && state.gets == 0 && state.frees == 0);
 }
 reset(); state.capabilities = 0;
 expect_error(snapshot_commands(api, &list), ErrorCode::ABI_CAPABILITY);
 assert(state.counts == 0 && state.gets == 0 && state.frees == 0);
 reset(); ++state.size;
 expect_error(snapshot_commands(api, &list), ErrorCode::ABI_SIZE);
 assert(state.counts == 0 && state.gets == 0 && state.frees == 0);
}
void tags_and_ownership() {
 for (const auto tag : snapshot_supported_tags) {
  reset(); auto list = text_list(); list.prototype.tag = tag;
  if (tag == AETHER_PAINT_PUSH_TRANSFORM) list.prototype.data.push_transform.matrix = identity;
  if (tag == AETHER_PAINT_BEGIN_PAINT_CHUNK || tag == AETHER_PAINT_END_PAINT_CHUNK)
   list.prototype.data.paint_chunk.style_node_id = 7;
  // Zero-initialize non-text payloads so no inactive text bytes become geometry.
  if (tag != AETHER_PAINT_DRAW_TEXT && tag != AETHER_PAINT_PUSH_TRANSFORM &&
      tag != AETHER_PAINT_BEGIN_PAINT_CHUNK && tag != AETHER_PAINT_END_PAINT_CHUNK) {
   list.prototype = {}; list.prototype.tag = tag;
  }
  const auto result = snapshot_commands(api, &list);
  assert(std::holds_alternative<Snapshot>(result));
  assert(command_tag(std::get<Snapshot>(result).commands.front()) == static_cast<int>(tag));
  assert(state.gets == 1 && state.frees == 1);
 }
 const aether_paint_command_tag_t unsupported[]{AETHER_PAINT_DRAW_BOX_SHADOW,
  AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT, AETHER_PAINT_DRAW_TEXT_GRADIENT,
  AETHER_PAINT_FILL_BORDER_SIDE, AETHER_PAINT_DRAW_TEXT_SHADOW,
  AETHER_PAINT_DRAW_BACKGROUND_IMAGE, AETHER_PAINT_APPLY_BACKDROP_FILTER,
  AETHER_PAINT_PUSH_SCROLL_OFFSET, AETHER_PAINT_POP_SCROLL_OFFSET,
  AETHER_PAINT_DRAW_SCROLLBAR_CHROME};
 for (auto tag : unsupported) {
  reset(); aether_display_list_s list; list.prototype.tag = tag;
  const auto result = snapshot_commands(api, &list);
  expect_error(result, ErrorCode::UNSUPPORTED_TAG);
  assert(std::get<Error>(result).tag == static_cast<int>(tag));
  assert(state.gets == 1 && state.frees == 1);
 }
 reset(); aether_display_list_s unknown;
 unknown.prototype.tag = static_cast<aether_paint_command_tag_t>(19);
 expect_error(snapshot_commands(api, &unknown), ErrorCode::UNKNOWN_TAG);
 assert(state.frees == 1);
 reset();
 auto owned = [] {
  auto list = text_list();
  auto result = snapshot_commands(api, &list);
  assert(state.frees == 1);
  list.text_buffer.assign(100, 'x');
  return result;
 }();
 const auto& text = std::get<Text>(std::get<Snapshot>(owned).commands[0]);
 assert(text.text == "hello" && text.font_family == "f");
}
void limits_and_payloads() {
 for (const auto n : {-1, 65537}) {
  reset(); aether_display_list_s list; list.count = n;
  expect_error(snapshot_commands(api, &list), ErrorCode::COMMAND_COUNT);
  assert(state.gets == 0 && state.frees == 0);
 }
 for (const auto n : {0, 65536}) {
  reset(); aether_display_list_s list; list.count = n;
  const auto result = snapshot_commands(api, &list);
  assert(std::get<Snapshot>(result).commands.size() == static_cast<std::size_t>(n));
  assert(state.gets == n && state.frees == n);
 }
 reset(); auto null_text = text_list(); null_text.prototype.data.draw_text.text = nullptr;
 expect_error(snapshot_commands(api, &null_text), ErrorCode::INVALID_PAYLOAD);
 assert(state.frees == 1);
 reset(); auto unterminated = text_list();
 unterminated.text_buffer.assign(max_text_bytes + 1, 'x'); // Entire scan is readable.
 expect_error(snapshot_commands(api, &unterminated), ErrorCode::LIMIT_EXCEEDED);
 assert(state.frees == 1);
 reset(); auto boundary = text_list();
 boundary.text_buffer.assign(max_text_bytes, 'x'); boundary.text_buffer.push_back('\0');
 assert(std::holds_alternative<Snapshot>(snapshot_commands(api, &boundary)));
 reset(); auto bad_family = text_list();
 std::memset(bad_family.prototype.data.draw_text.font_family, 'x', 64);
 expect_error(snapshot_commands(api, &bad_family), ErrorCode::INVALID_PAYLOAD);
 assert(state.frees == 1);
 reset(); auto family_boundary = text_list();
 std::memset(family_boundary.prototype.data.draw_text.font_family, 'x', 63);
 family_boundary.prototype.data.draw_text.font_family[63] = '\0';
 assert(std::holds_alternative<Snapshot>(snapshot_commands(api, &family_boundary)));
 reset(); auto invalid_utf8 = text_list(); invalid_utf8.text_buffer = std::string("\xc0\xaf\0", 3);
 expect_error(snapshot_commands(api, &invalid_utf8), ErrorCode::INVALID_UTF8);
 assert(state.frees == 1);
 reset(); auto invalid_family = text_list(); invalid_family.prototype.data.draw_text.font_family[0] = '\xc0';
 expect_error(snapshot_commands(api, &invalid_family), ErrorCode::INVALID_UTF8);
 reset(); auto empty = text_list(); empty.text_buffer = std::string(1, '\0');
 assert(std::holds_alternative<Snapshot>(snapshot_commands(api, &empty)));
 // These are adapter restrictions, not assertions that core output is corrupt.
 reset(); auto invisible = text_list(); invisible.prototype.data.draw_text.font_size = 0;
 expect_error(snapshot_commands(api, &invisible), ErrorCode::INVALID_PAYLOAD);
 assert(state.frees == 1);
 reset(); auto no_family = text_list(); no_family.prototype.data.draw_text.font_family[0] = '\0';
 expect_error(snapshot_commands(api, &no_family), ErrorCode::INVALID_PAYLOAD);
 reset(); auto budget = text_list();
 budget.text_buffer.assign(max_text_bytes - 1, 'x'); budget.text_buffer.push_back('\0');
 budget.count = 128; // (65,535 text + 1 family) * 128 == 8 MiB.
 assert(std::holds_alternative<Snapshot>(snapshot_commands(api, &budget)));
 assert(state.frees == 128);
 reset(); budget.count = 129;
 const auto over = snapshot_commands(api, &budget);
 expect_error(over, ErrorCode::LIMIT_EXCEEDED);
 assert(std::get<Error>(over).index == 128 && state.frees == 129);
}
void acquisition_and_cancellation() {
 reset(); aether_display_list_s list; list.count = 3; list.fail_at = 1;
 const auto failed = snapshot_commands(api, &list);
 expect_error(failed, ErrorCode::GET_COMMAND_FAILED);
 assert(std::get<Error>(failed).index == 1 && !std::get<Error>(failed).tag);
 assert(state.gets == 2 && state.frees == 1);
 reset(); list.fail_at = -1;
 CancelAtCall before(1);
 expect_error(snapshot_commands(api, &list, &before), ErrorCode::CANCELLED);
 assert(state.counts == 0 && state.gets == 0);
 reset(); CancelAtCall after_get(3);
 expect_error(snapshot_commands(api, &list, &after_get), ErrorCode::CANCELLED);
 assert(state.gets == 1 && state.frees == 1);
 reset(); CancelAtCall between(4);
 expect_error(snapshot_commands(api, &list, &between), ErrorCode::CANCELLED);
 assert(state.gets == 1 && state.frees == 1);
 reset(); list.count = 1; CancelAtCall final(4);
 expect_error(snapshot_commands(api, &list, &final), ErrorCode::CANCELLED);
 assert(state.gets == 1 && state.frees == 1);
 reset(); list.count = 0; CancelAtCall empty_final(2);
 expect_error(snapshot_commands(api, &list, &empty_final), ErrorCode::CANCELLED);
 assert(state.counts == 1 && state.gets == 0 && state.frees == 0);
}
} // namespace
int main() {
 gates(); tags_and_ownership(); limits_and_payloads(); acquisition_and_cancellation();
}
