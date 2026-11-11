#include "aether_visual_compare/paint_snapshot.hpp"
#include <array>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace avc::preparation;
namespace {
void check(bool value) { if (!value) std::abort(); }
const PreparedFrame& frame(const PrepareResult& result) {
 check(std::holds_alternative<PreparedFrame>(result));
 return std::get<PreparedFrame>(result);
}
void failed(const Snapshot& snapshot, ErrorCode code,
            std::optional<std::size_t> index = {}) {
 const auto result = prepare(snapshot);
 check(std::holds_alternative<Error>(result));
 check(std::get<Error>(result).code == code);
 if (index) check(std::get<Error>(result).index == index);
}
Text text() { return Text{"hello",{1,2,3,4},"family",12,400,AETHER_TEXT_ALIGN_START,{0,0,0,1},AETHER_FALSE}; }
void same(Matrix a, Matrix b) {
 check(a.m11 == b.m11 && a.m12 == b.m12 && a.m21 == b.m21 &&
       a.m22 == b.m22 && a.dx == b.dx && a.dy == b.dy);
}
void point(Matrix m, float x, float y, float expected_x, float expected_y) {
 check(x*m.m11+y*m.m21+m.dx == expected_x);
 check(x*m.m12+y*m.m22+m.dy == expected_y);
}
struct RecordingSink final : CommandSink {
 std::vector<PreparedEvent> events;
 bool accept(const PreparedEvent& event) override { events.push_back(event); return true; }
};
struct CancelAt final : Cancellation {
 std::size_t calls{};
 std::size_t target;
 explicit CancelAt(std::size_t n) : target(n) {}
 bool cancelled() noexcept override { return ++calls >= target; }
};
struct Flag final : Cancellation {
 bool value{};
 bool cancelled() noexcept override { return value; }
};
struct CancellingSink final : CommandSink {
 Flag& flag;
 std::size_t calls{};
 explicit CancellingSink(Flag& f) : flag(f) {}
 bool accept(const PreparedEvent&) override { ++calls; flag.value = true; return true; }
};
struct FailingSink final : CommandSink {
 enum class Mode { reject, allocation, other };
 Mode mode;
 std::size_t calls{};
 explicit FailingSink(Mode m) : mode(m) {}
 bool accept(const PreparedEvent&) override {
  if (++calls != 2) return true;
  if (mode == Mode::allocation) throw std::bad_alloc{};
  if (mode == Mode::other) throw std::runtime_error("synthetic sink failure");
  return false;
 }
};
void matrices_and_clips() {
 const std::array<Matrix,6> cases{{identity,{1,0,0,1,10,20},{2,0,0,3,0,0},
  {1,2,3,1,0,0},{-1,0,0,1,0,0},{0,0,0,0,0,0}}};
 const std::array<std::array<float,2>,6> points{{{1,1},{11,21},{2,3},{4,3},{-1,1},{0,0}}};
 for (std::size_t i=0; i<cases.size(); ++i) {
  const auto result = prepare(Snapshot{{PushTransform{cases[i]},Fill{},PopTransform{},Fill{}}});
  const auto& events = frame(result).events();
  same(events[1].matrix,cases[i]);
  point(events[1].matrix,1,1,points[i][0],points[i][1]);
  same(events[3].matrix,identity);
 }
 const Radii radii{1,2,3,4,5,6,7,8};
 const Matrix translate{1,0,0,1,10,20};
 const auto result = prepare(Snapshot{{BeginChunk{7},PushTransform{translate},
  PushClip{{-1,-2,10,20},radii},PushTransform{{2,0,0,3,0,0}},Fill{},
  PopTransform{},PopClip{},PopTransform{},EndChunk{7},Fill{}}});
 const auto& events = frame(result).events();
 check(events.size() == 10);
 same(events[2].matrix,translate);
 check(events[2].transform_depth == 1);
 point(events[4].matrix,1,1,12,23);
 check(events[4].transform_depth == 2);
 same(events[5].matrix,translate);
 same(events[9].matrix,identity);
 const auto& clip = std::get<PushClip>(events[2].command);
 check(clip.rect.x == -1 && clip.rect.y == -2 && clip.rect.width == 10 && clip.rect.height == 20);
 check(clip.radii.top_left_x == 1 && clip.radii.top_left_y == 2 &&
       clip.radii.top_right_x == 3 && clip.radii.top_right_y == 4 &&
       clip.radii.bottom_right_x == 5 && clip.radii.bottom_right_y == 6 &&
       clip.radii.bottom_left_x == 7 && clip.radii.bottom_left_y == 8);
 RecordingSink sink;
 check(!replay(frame(result),sink));
 check(sink.events.size() == events.size());
 for (std::size_t i=0; i<events.size(); ++i) {
  check(command_tag(sink.events[i].command) == command_tag(events[i].command));
  same(sink.events[i].matrix,events[i].matrix);
 }
}
void stacks() {
 failed(Snapshot{{PopClip{}}},ErrorCode::STACK_UNDERFLOW,0);
 failed(Snapshot{{PopTransform{}}},ErrorCode::STACK_UNDERFLOW,0);
 failed(Snapshot{{EndChunk{1}}},ErrorCode::STACK_UNDERFLOW,0);
 failed(Snapshot{{BeginChunk{1},EndChunk{2}}},ErrorCode::STACK_MISMATCH,1);
 failed(Snapshot{{BeginChunk{0}}},ErrorCode::INVALID_PAYLOAD,0);
 failed(Snapshot{{EndChunk{-1}}},ErrorCode::INVALID_PAYLOAD,0);
 failed(Snapshot{{PushClip{}}},ErrorCode::UNCLOSED_SCOPE);
 failed(Snapshot{{PushTransform{}}},ErrorCode::UNCLOSED_SCOPE);
 failed(Snapshot{{BeginChunk{1}}},ErrorCode::UNCLOSED_SCOPE);
 failed(Snapshot{{PushTransform{},PushClip{},PopTransform{},PopClip{}}},ErrorCode::CLIP_TRANSFORM_CROSSING,2);
 const auto crossed = prepare(Snapshot{{PushClip{},PushTransform{{1,0,0,1,2,3}},
  PopClip{},Fill{},PopTransform{},Fill{}}});
 point(frame(crossed).events()[3].matrix,1,1,3,4);
 same(frame(crossed).events()[5].matrix,identity);
 // Chunk scopes are independent; IDs may repeat without imposing global LIFO.
 const auto chunks = prepare(Snapshot{{BeginChunk{4},PushTransform{},EndChunk{4},
  BeginChunk{4},PopTransform{},EndChunk{4}}});
 check(frame(chunks).events().size() == 6);
 for (int kind=0; kind<3; ++kind) {
  Snapshot depth;
  for (std::size_t i=0; i<max_scope_depth; ++i) {
   if (kind == 0) depth.commands.emplace_back(PushTransform{});
   else if (kind == 1) depth.commands.emplace_back(PushClip{});
   else depth.commands.emplace_back(BeginChunk{1});
  }
  Snapshot excess = depth;
  excess.commands.emplace_back(PushClip{});
  failed(excess,ErrorCode::LIMIT_EXCEEDED,max_scope_depth);
  for (std::size_t i=0; i<max_scope_depth; ++i) {
   if (kind == 0) depth.commands.emplace_back(PopTransform{});
   else if (kind == 1) depth.commands.emplace_back(PopClip{});
   else depth.commands.emplace_back(EndChunk{1});
  }
  check(frame(prepare(depth)).events().size() == 2*max_scope_depth);
 }
 Snapshot mixed;
 for (std::size_t i=0; i<85; ++i) {
  mixed.commands.emplace_back(BeginChunk{1});
  mixed.commands.emplace_back(PushTransform{});
  mixed.commands.emplace_back(PushClip{});
 }
 mixed.commands.emplace_back(BeginChunk{2});
 auto excess = mixed;
 excess.commands.emplace_back(PushTransform{});
 failed(excess,ErrorCode::LIMIT_EXCEEDED,max_scope_depth);
 mixed.commands.emplace_back(EndChunk{2});
 for (std::size_t i=0; i<85; ++i) {
  mixed.commands.emplace_back(PopClip{});
  mixed.commands.emplace_back(PopTransform{});
  mixed.commands.emplace_back(EndChunk{1});
 }
 check(frame(prepare(mixed)).events().size() == 2*max_scope_depth);
 // Late error cannot produce a frame that could dispatch a partial prefix.
 RecordingSink sink;
 const auto late = prepare(Snapshot{{Fill{},text(),PopClip{}}});
 if (const auto* ready = std::get_if<PreparedFrame>(&late)) (void)replay(*ready,sink);
 check(std::holds_alternative<Error>(late));
 check(std::get<Error>(late).index == 2);
 check(sink.events.empty());
}
void fields_and_limits() {
 const float nan = std::numeric_limits<float>::quiet_NaN();
 const float inf = std::numeric_limits<float>::infinity();
 for (float v : {nan,inf,-inf}) {
  for (auto member : {&Rect::x,&Rect::y,&Rect::width,&Rect::height}) {
   Fill f; f.rect.*member = v; failed(Snapshot{{f}},ErrorCode::INVALID_PAYLOAD);
  }
  for (auto member : {&Matrix::m11,&Matrix::m12,&Matrix::m21,&Matrix::m22,&Matrix::dx,&Matrix::dy}) {
   PushTransform p; p.matrix.*member = v; failed(Snapshot{{p}},ErrorCode::INVALID_PAYLOAD);
  }
  auto t = text(); t.font_size = v; failed(Snapshot{{t}},ErrorCode::INVALID_PAYLOAD);
 }
 for (auto member : {&Rect::width,&Rect::height}) {
  Fill f; f.rect.*member = -1; failed(Snapshot{{f}},ErrorCode::INVALID_PAYLOAD);
 }
 for (auto member : {&Radii::top_left_x,&Radii::top_left_y,&Radii::top_right_x,&Radii::top_right_y,
                     &Radii::bottom_right_x,&Radii::bottom_right_y,&Radii::bottom_left_x,&Radii::bottom_left_y}) {
  for (float v : {-1.f,nan,inf}) {
   Fill f; f.radii.*member = v; failed(Snapshot{{f}},ErrorCode::INVALID_PAYLOAD);
  }
 }
 for (auto member : {&Color::r,&Color::g,&Color::b,&Color::a}) {
  for (float v : {-0.1f,1.1f,nan,inf}) {
   Fill f; f.color.*member = v; failed(Snapshot{{f}},ErrorCode::INVALID_PAYLOAD);
  }
 }
 for (float v : {-1.f,nan,inf}) {
  Stroke s; s.stroke_width = v; failed(Snapshot{{s}},ErrorCode::INVALID_PAYLOAD);
 }
 const auto degenerate = prepare(Snapshot{{Fill{{-3,-4,0,0},{},{1,1,1,1}},Stroke{}}});
 check(frame(degenerate).events().size() == 2);
 // Adapter policy, not proof of core corruption: invisible size-zero text and
 // an empty family are outside this preparation subset.
 for (float v : {0.f,-1.f}) {
  auto t = text(); t.font_size = v; failed(Snapshot{{t}},ErrorCode::INVALID_PAYLOAD);
 }
 auto t = text(); t.font_family.clear(); failed(Snapshot{{t}},ErrorCode::INVALID_PAYLOAD);
 for (std::int32_t weight : {0,1001}) {
  t = text(); t.font_weight = weight; failed(Snapshot{{t}},ErrorCode::INVALID_PAYLOAD);
 }
 for (std::int32_t weight : {1,1000}) {
  t = text(); t.font_weight = weight; check(frame(prepare(Snapshot{{t}})).events().size() == 1);
 }
 for (std::int32_t align : {AETHER_TEXT_ALIGN_START,AETHER_TEXT_ALIGN_CENTER,AETHER_TEXT_ALIGN_END,
                          AETHER_TEXT_ALIGN_LEFT,AETHER_TEXT_ALIGN_RIGHT,AETHER_TEXT_ALIGN_JUSTIFY}) {
  t = text(); t.text_align = align; check(frame(prepare(Snapshot{{t}})).events().size() == 1);
 }
 t = text(); t.text_align = 6; failed(Snapshot{{t}},ErrorCode::INVALID_PAYLOAD);
 t = text(); t.wrap = 2; failed(Snapshot{{t}},ErrorCode::INVALID_PAYLOAD);
 t = text(); t.wrap = AETHER_TRUE; check(frame(prepare(Snapshot{{t}})).events().size() == 1);
 t = text(); t.text.clear(); check(frame(prepare(Snapshot{{t}})).events().size() == 1);
 t = text(); t.text = std::string("a\0b",3); failed(Snapshot{{t}},ErrorCode::INVALID_PAYLOAD);
 t = text(); t.font_family = std::string("a\0b",3); failed(Snapshot{{t}},ErrorCode::INVALID_PAYLOAD);
 for (const std::string& malformed : {std::string("\xc0\x80",2),std::string("\xe2\x82",2),std::string("\xed\xa0\x80",3)}) {
  t = text(); t.text = malformed; failed(Snapshot{{t}},ErrorCode::INVALID_UTF8);
  t = text(); t.font_family = malformed; failed(Snapshot{{t}},ErrorCode::INVALID_UTF8);
 }
 t = text(); t.text = "\xe2\x82\xac"; t.font_family = "\xc3\xa9";
 check(frame(prepare(Snapshot{{t}})).events().size() == 1);
 t = text(); t.font_family.assign(63,'f'); check(frame(prepare(Snapshot{{t}})).events().size() == 1);
 t.font_family.push_back('f'); failed(Snapshot{{t}},ErrorCode::LIMIT_EXCEEDED);
 t = text(); t.text.assign(max_text_bytes,'x'); check(frame(prepare(Snapshot{{t}})).events().size() == 1);
 t.text.push_back('x'); failed(Snapshot{{t}},ErrorCode::LIMIT_EXCEEDED);
 Snapshot count;
 count.commands.resize(max_commands,Fill{});
 check(frame(prepare(count)).events().size() == max_commands);
 count.commands.emplace_back(Fill{}); failed(count,ErrorCode::COMMAND_COUNT);
 Snapshot budget;
 t = text(); t.text.assign(max_text_bytes-1,'x'); t.font_family = "f";
 for (std::size_t i=0; i<max_string_bytes/max_text_bytes; ++i) budget.commands.emplace_back(t);
 check(frame(prepare(budget)).events().size() == max_string_bytes/max_text_bytes);
 t.text.clear(); budget.commands.emplace_back(t); failed(budget,ErrorCode::LIMIT_EXCEEDED,budget.commands.size()-1);
 const float huge = std::numeric_limits<float>::max();
 failed(Snapshot{{PushTransform{{huge,0,0,1,0,0}},PushTransform{{2,0,0,1,0,0}}}},ErrorCode::NUMERIC_RANGE,1);
 failed(Snapshot{{PushTransform{{1,0,0,1,huge,0}},PushTransform{{1,0,0,1,huge,0}}}},ErrorCode::NUMERIC_RANGE,1);
 // A caller-owned snapshot is mutable, so the second preflight must revalidate.
 Snapshot mutable_snapshot{{text()}};
 check(frame(prepare(mutable_snapshot)).events().size() == 1);
 std::get<Text>(mutable_snapshot.commands[0]).font_size = 0;
 failed(mutable_snapshot,ErrorCode::INVALID_PAYLOAD,0);
}
void ownership_and_callbacks() {
 static_assert(!std::is_default_constructible_v<PreparedFrame>);
 static_assert(!std::is_copy_constructible_v<PreparedFrame>);
 static_assert(!std::is_move_assignable_v<PreparedFrame>);
 static_assert(std::is_same_v<decltype(std::declval<PreparedFrame&>().events()),const std::vector<PreparedEvent>&>);
 static_assert(std::is_trivially_copyable_v<Error>);
 const auto owned = [] {
  Snapshot source{{text(),Fill{},Stroke{}}};
  auto result = prepare(source);
  std::get<Text>(source.commands[0]).text = "changed";
  source.commands.clear();
  return result;
 }();
 check(std::get<Text>(frame(owned).events()[0].command).text == "hello");
 RecordingSink copied;
 check(!replay(frame(owned),copied));
 check(copied.events.size() == 3);
 for (auto mode : {FailingSink::Mode::reject,FailingSink::Mode::allocation,FailingSink::Mode::other}) {
  FailingSink sink(mode);
  const auto failure = replay(frame(owned),sink);
  check(failure.has_value());
  check(failure->code == (mode == FailingSink::Mode::allocation ? ErrorCode::OUT_OF_MEMORY : ErrorCode::SINK_FAILED));
  check(failure->index == 1 && sink.calls == 2);
 }
 // Synthetic callback exceptions exercise translation, not heap fault injection.
}
void cancellation() {
 const Snapshot empty;
 for (std::size_t call : {1u,2u}) {
  CancelAt cancellation(call);
  const auto result = prepare(empty,&cancellation);
  check(std::holds_alternative<Error>(result));
  check(std::get<Error>(result).code == ErrorCode::CANCELLED);
  check(cancellation.calls == call);
 }
 const Snapshot two{{Fill{},Fill{}}};
 for (std::size_t call : {1u,2u,3u,4u}) {
  CancelAt cancellation(call);
  const auto result = prepare(two,&cancellation);
  check(std::holds_alternative<Error>(result));
  check(std::get<Error>(result).code == ErrorCode::CANCELLED);
 }
 const auto ready_empty = prepare(empty);
 for (std::size_t call : {1u,2u}) {
  CancelAt cancellation(call); RecordingSink sink;
  const auto error = replay(frame(ready_empty),sink,&cancellation);
  check(error && error->code == ErrorCode::CANCELLED && sink.events.empty());
 }
 const auto ready = prepare(two);
 for (std::size_t call : {1u,2u,3u,4u}) {
  CancelAt cancellation(call); RecordingSink sink;
  const auto error = replay(frame(ready),sink,&cancellation);
  check(error && error->code == ErrorCode::CANCELLED);
  const std::size_t expected = call <= 2 ? 0 : call-2;
  check(sink.events.size() == expected);
 }
 Flag flag; CancellingSink sink(flag);
 const auto between = replay(frame(ready),sink,&flag);
 check(between && between->code == ErrorCode::CANCELLED && sink.calls == 1);
 const auto one = prepare(Snapshot{{Fill{}}});
 Flag final_flag; CancellingSink final_sink(final_flag);
 const auto last = replay(frame(one),final_sink,&final_flag);
 check(last && last->code == ErrorCode::CANCELLED && final_sink.calls == 1);
}
} // namespace
int main() {
 matrices_and_clips();
 stacks();
 fields_and_limits();
 ownership_and_callbacks();
 cancellation();
}
