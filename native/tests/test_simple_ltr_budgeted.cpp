// Implementation self-checks; independent multi-owner acceptance is separate.
#define main legacy_q2b_unused_main
#include "test_simple_ltr_run_independent.cpp"
#undef main
int main(){
 auto b=bind();tt::LineBox line{3072,4096};tt::RunWork old,work;tt::RunBudget budget;
 auto legacy=tt::build_simple_ltr_run(b,"AA",line,nullptr,&old);
 auto fresh=tt::build_simple_ltr_run_budgeted(b,"AA",line,budget,nullptr,&work);
 assert(std::holds_alternative<Run>(legacy)&&std::holds_alternative<Run>(fresh));
 assert(std::get<Run>(legacy)->canvas.rgba==std::get<Run>(fresh)->canvas.rgba);
 assert(old.cmap_reserved==0&&old.mask_reserved==0&&old.canvas_reserved==0&&old.composite_reserved==0);
 assert(work.cmap_reserved==34&&work.outline_reserved==2000000);
 assert(work.mask_reserved==work.retained_mask_bytes&&work.canvas_reserved==work.clear_pixels&&work.canvas_reserved==work.conversion_pixels&&work.composite_reserved==work.source_pixel_visits);
 assert(budget.cmap.remaining==4352-34&&budget.outline.remaining==256000000-2000000);
 unsigned cases=1;
 struct Cap{tt::AggregateBudget tt::RunBudget::*field;std::uint64_t cost;};
 for(const auto& cap:std::vector<Cap>{{&tt::RunBudget::cmap,work.cmap_reserved},{&tt::RunBudget::outline,work.outline_reserved},{&tt::RunBudget::raster,work.raster_reserved},{&tt::RunBudget::masks,work.mask_reserved},{&tt::RunBudget::canvas_pixels,work.canvas_reserved},{&tt::RunBudget::composite_visits,work.composite_reserved}}){
  assert(cap.cost>0);tt::RunBudget exact;(exact.*cap.field).remaining=cap.cost;tt::RunWork w;
  assert(std::holds_alternative<Run>(tt::build_simple_ltr_run_budgeted(b,"AA",line,exact,nullptr,&w)));assert((exact.*cap.field).remaining==0);
  auto second=tt::build_simple_ltr_run_budgeted(b,"AA",line,exact);assert(std::holds_alternative<E>(second)&&std::get<E>(second)==E::limit_exceeded);
  tt::RunBudget low;(low.*cap.field).remaining=cap.cost-1;auto bad=tt::build_simple_ltr_run_budgeted(b,"AA",line,low,nullptr,&w);assert(std::holds_alternative<E>(bad)&&std::get<E>(bad)==E::limit_exceeded);cases+=3;
 }
 // Previous reservations cannot be refunded by a later budget failure.
 tt::RunBudget low;low.composite_visits.remaining=0;tt::RunWork fail;
 auto bad=tt::build_simple_ltr_run_budgeted(b,"AA",line,low,nullptr,&fail);
 assert(std::holds_alternative<E>(bad)&&std::get<E>(bad)==E::limit_exceeded);
 assert(fail.canvas_reserved==work.canvas_reserved&&!fail.composition_started&&low.canvas_pixels.remaining==16ULL*1024*1024-work.canvas_reserved);++cases;
 tt::RunBudget probeBudget;Cancel probe(~0u);assert(std::holds_alternative<Run>(tt::build_simple_ltr_run_budgeted(b,"A",line,probeBudget,&probe)));
 unsigned postchargeCancel=0;for(unsigned i=1;i<=probe.calls;++i){tt::RunBudget q;Cancel cancel(i);tt::RunWork w;auto r=tt::build_simple_ltr_run_budgeted(b,"A",line,q,&cancel,&w);assert(std::holds_alternative<E>(r)&&std::get<E>(r)==E::cancelled);if(w.mask_reserved){assert(q.masks.remaining==64ULL*1024*1024-w.mask_reserved);++postchargeCancel;}}
 assert(postchargeCancel>0);
 unsigned postchargeOOM=0;for(unsigned i=0;i<256;++i){tt::RunBudget q;tt::RunWork w;fail_after=i;auto r=tt::build_simple_ltr_run_budgeted(b,"A",line,q,nullptr,&w);fail_after=-1;if(std::holds_alternative<E>(r)){assert(std::get<E>(r)==E::out_of_memory);if(w.mask_reserved){assert(q.masks.remaining==64ULL*1024*1024-w.mask_reserved);++postchargeOOM;}}}
 assert(postchargeOOM>0);
 std::cout<<"Q2e budget selfchecks="<<cases<<" cancellation_sites="<<probe.calls<<" postcharge_cancel="<<postchargeCancel<<" sampled_oom_sites=256 postcharge_oom="<<postchargeOOM<<"\n";
}
