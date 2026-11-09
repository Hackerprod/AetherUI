// White-box authority checks. Included only by the actual-core runtime test TU.
namespace publication_receipt_test {
using namespace aether::style;
static size_t receipt_checks=0;
static void need(bool ok,const char* label) {
    ++receipt_checks;
    if(!ok){std::fprintf(stderr,"FAIL publication receipt: %s\n",label);std::exit(1);}
}
struct JournalSnapshot {
    uint64_t floor,last; aether::dom::JournalLoss loss; bool poison;
    std::vector<aether::dom::MutationRecord> records;
    explicit JournalSnapshot(const aether::dom::MutationJournal& j):floor(j.floor_epoch()),last(j.last_epoch()),
        loss(j.loss()),poison(j.monotonicity_lost()),records(j.begin(),j.end()){}
    bool matches(const aether::dom::MutationJournal& j)const {
        if(floor!=j.floor_epoch()||last!=j.last_epoch()||loss!=j.loss()||poison!=j.monotonicity_lost()||records.size()!=j.size())return false;
        for(size_t i=0;i<records.size();++i){const auto& a=records[i];const auto& b=j.begin()[i];
            if(a.subject!=b.subject||a.mutation_epoch!=b.mutation_epoch||a.structure_epoch!=b.structure_epoch||
               a.kind!=b.kind||a.old_state!=b.old_state||a.new_state!=b.new_state)return false;}
        return true;
    }
};
struct ReceiptFixture {
    aether_doc_t doc=nullptr; aether_style_runtime_t runtime=nullptr; aether_node_t node=nullptr;
    ReceiptFixture(){
        need(aether_doc_create(&doc)==AETHER_OK,"create document");
        need(aether_doc_parse_html(doc,"<html><body><div id='receipt'></div><div id='other'></div></body></html>")==AETHER_OK,"parse document");
        node=aether_doc_get_element_by_id(doc,"receipt");need(node!=nullptr,"find node");
        need(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&runtime)==AETHER_OK,"create runtime");
        int64_t sheet=0;
        need(aether_style_runtime_add_stylesheet(runtime,"#receipt{opacity:.25}#receipt:hover{opacity:.75}",2,nullptr,&sheet,nullptr)==AETHER_OK,"add sheet");
    }
    ~ReceiptFixture(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    aether_style_transaction_t prepare(){
        aether_style_transaction_t tx=nullptr;
        need(aether_style_runtime_prepare_revision(runtime,doc,nullptr,&tx)==AETHER_OK,"prepare full");return tx;
    }
    aether_style_revision_t commit(aether_style_transaction_t tx){
        aether_style_revision_t out=nullptr;
        need(aether_style_runtime_commit_revision(runtime,tx,&out)==AETHER_OK&&out,"commit full");return out;
    }
    void full(){auto tx=prepare();auto out=commit(tx);aether_style_transaction_destroy(tx);aether_style_revision_destroy(out);}
    void rejects(aether_style_transaction_t tx){
        auto revision=runtime->revision;const auto generation=runtime->generation;
        const auto tree=doc->identity->style_tree_epoch;JournalSnapshot journal(doc->mutation_journal);
        aether_style_revision_t out=nullptr;
        need(aether_style_runtime_commit_revision(runtime,tx,&out)==AETHER_ERROR_INVALID_STATE&&!out,"reject unauthorized publication");
        need(runtime->revision==revision&&runtime->generation==generation&&doc->identity->style_tree_epoch==tree,"rejection preserves publication");
        need(journal.matches(doc->mutation_journal),"rejection preserves every journal record");
    }
};
static FullPayloadRevisionInput input_from(const StyleRevisionRef& revision){
    FullPayloadRevisionInput in;in.contextual=revision->contextual();in.generation=revision->generation();
    in.document_identity=revision->document_identity();in.input_epoch=revision->input_epoch();
    in.structure_epoch=revision->structure_epoch();in.style_tree_epoch=revision->style_tree_epoch();
    in.environment=revision->environment();in.document=revision->document();in.document_lifetime=revision->document_lifetime();
    for(const auto entry:revision->entries())in.entries.push_back({entry.node(),entry.payload(),entry.impact()});
    return in;
}
static void generic_has_no_authority(){
    ReceiptFixture f;f.full();const auto historical=f.runtime->revision;
    aether_element_set_state(f.node,AETHER_STATE_HOVER);
    auto tx=f.prepare();const auto valid=tx->publication_receipt;
    need(valid&&valid->matches(valid,tx->revision)&&!valid->incremental(),"real exhaustive producer binds full receipt");
    const auto candidate=tx->revision;
    for(int variant=0;variant<3;++variant){
        auto in=input_from(candidate);
        if(variant==1){need(in.entries.size()>1,"omission fixture contains multiple live nodes");in.entries.pop_back();}
        if(variant==2){
            bool replaced=false;
            for(auto& entry:in.entries)if(entry.node==f.node){
                const auto* old=historical->directory().find(reinterpret_cast<uintptr_t>(f.node));
                need(old&&old->payload!=entry.payload,"stale payload differs from current computed result");
                entry.payload=old->payload;replaced=true;
            }
            need(replaced,"stale payload substituted under current epoch header");
        }
        JournalSnapshot journal(f.doc->mutation_journal);
        CertifiedRevisionDeltaRef certificate;
        need(certify_full_payload_revision(historical,in,certificate)==AETHER_OK&&certificate,"generic schema certificate accepted");
        StyleRevisionRef schema;
        need(freeze_certified_revision(certificate,schema)==AETHER_OK&&schema,"generic schema freeze remains compatible");
        StyleRevisionRef unchanged=candidate;auto receipt=valid;
        need(freeze_certified_revision(certificate,unchanged,receipt)==AETHER_ERROR_INVALID_STATE,"generic certificate cannot issue receipt");
        need(unchanged==candidate&&receipt==valid,"failed issuance preserves both outputs");
        need(journal.matches(f.doc->mutation_journal),"generic freeze never acknowledges inputs");
        // Even the genuine resolver receipt is not authority for this new schema candidate.
        tx->revision=schema;f.rejects(tx);tx->revision=candidate;
    }
    auto out=f.commit(tx);need(f.doc->mutation_journal.floor_epoch()==candidate->input_epoch()&&f.doc->mutation_journal.size()==0,"restored exact candidate acknowledges");
    aether_style_revision_destroy(out);aether_style_transaction_destroy(tx);
}
static void binding_and_status(){
    ReceiptFixture f;f.full();aether_element_set_state(f.node,AETHER_STATE_HOVER);
    auto tx=f.prepare();auto other=f.prepare();auto correct=tx->publication_receipt;
    need(other->revision!=tx->revision&&other->revision->generation()==tx->revision->generation()&&
         other->revision->document_identity()==tx->revision->document_identity(),"two preparations share document and generation but not candidate");
    tx->publication_receipt.reset();
    for(bool status:{false,true}){tx->state_result.incremental=status;f.rejects(tx);}
    // Same pointer with a different control block must fail the self-owner seal.
    CertifiedPublicationReceiptRef alias(correct.get(),[](const CertifiedPublicationReceipt*){});
    need(!alias->matches(alias,tx->revision),"receipt alias owner rejected");
    tx->publication_receipt=alias;f.rejects(tx);
    tx->publication_receipt=other->publication_receipt;
    need(!tx->publication_receipt->matches(tx->publication_receipt,tx->revision),"same-generation other candidate receipt rejected");
    f.rejects(tx);tx->publication_receipt=correct;
    // A copied public incremental bit must neither grant nor remove FULL authority.
    tx->state_result.incremental=true;
    aether_status_t wrong_thread=AETHER_OK;
    std::thread worker([&]{aether_style_revision_t out=nullptr;wrong_thread=aether_style_runtime_commit_revision(f.runtime,tx,&out);need(!out,"wrong thread has no result");});worker.join();
    need(wrong_thread==AETHER_ERROR_INVALID_STATE,"wrong thread rejected");
    auto out=f.commit(tx);
    need(f.doc->mutation_journal.floor_epoch()==out->value->input_epoch()&&f.doc->mutation_journal.size()==0,"full receipt authority independent of status bit");
    f.rejects(tx);f.rejects(other);
    aether_style_transaction_destroy(other);aether_style_transaction_destroy(tx);aether_style_revision_destroy(out);
}
static void receipt_lifetime(){
    ReceiptFixture f;auto tx=f.prepare();
    std::weak_ptr<const CertifiedPublicationReceipt> weak=tx->publication_receipt;
    auto snapshot=tx->revision;auto out=f.commit(tx);
    aether_style_map_t map=nullptr;need(aether_style_revision_create_map(out,&map)==AETHER_OK,"retain published map");
    auto handle=aether_style_map_get(map,f.node);need(handle!=nullptr,"retain published handle");
    need(!weak.expired(),"transaction retains receipt");aether_style_transaction_destroy(tx);
    need(weak.expired(),"revision runtime map and handle do not retain receipt");
    need(snapshot==out->value&&snapshot==f.runtime->revision,"published snapshot survives receipt destruction");
    aether_computed_style_handle_destroy(handle);aether_style_map_destroy(map);aether_style_revision_destroy(out);
    tx=f.prepare();weak=tx->publication_receipt;auto abandoned=tx->revision;JournalSnapshot journal(f.doc->mutation_journal);
    aether_style_transaction_destroy(tx);need(weak.expired()&&abandoned,"abandoned receipt reclaimed despite retained candidate");
    need(journal.matches(f.doc->mutation_journal),"abandonment leaves journal unchanged");
}
static void journal_three_way_guard(){
    for(bool backwards:{false,true})for(bool with_loss:{false,true}){
        ReceiptFixture f;f.full();
        if(with_loss)for(int i=0;i<130;++i)aether_element_set_state(f.node,i+1);
        else aether_element_set_state(f.node,AETHER_STATE_HOVER);
        auto tx=f.prepare();const auto input=f.doc->identity->mutation_epoch;
        const auto last=f.doc->mutation_journal.last_epoch();need(input==last&&input>1,"matching journal before white-box disagreement");
        // Append without mutating the document epoch. Forward mismatch stays
        // monotonic; reverse mismatch also exercises the permanent poison guard.
        f.doc->mutation_journal.append(f.node->identity,aether::dom::MutationKind::state,
            backwards?last-1:last+1,f.doc->identity->structure_epoch,f.node->state,f.node->state);
        need(f.doc->identity->mutation_epoch==input,"white-box append does not alter document epoch");
        JournalSnapshot journal(f.doc->mutation_journal);auto out=f.commit(tx);
        need(journal.matches(f.doc->mutation_journal),"full publication preserves floor loss poison and records on last/input mismatch");
        need(out->value->input_epoch()==input,"full still publishes current input revision");
        aether_style_transaction_destroy(tx);aether_style_revision_destroy(out);
    }
}
// Missing observation can leave last behind current without an epoch-gap
// append. Exercise the three-way ACK guard independently of poison rejection.
static void journal_lag_without_poison(){
    for(bool with_loss:{false,true}){
        ReceiptFixture f;f.full();
        const auto observed=f.doc->identity->mutation_epoch+1;
        f.doc->mutation_journal.append(f.node->identity,
            with_loss?aether::dom::MutationKind::unknown:aether::dom::MutationKind::state,
            observed,f.doc->identity->structure_epoch,f.node->state,f.node->state);
        f.doc->identity->mutation_epoch=observed;
        need(!f.doc->mutation_journal.monotonicity_lost(),"contiguous observed input has no poison");
        need(f.doc->mutation_journal.loss()==(with_loss?aether::dom::JournalLoss::unknown_mutation:aether::dom::JournalLoss::none),
             "lag fixture has precisely requested ordinary loss");
        // Simulate an input observation missing from the journal, rather than
        // appending a regressive epoch that would independently poison it.
        ++f.doc->identity->mutation_epoch;
        const auto input=f.doc->identity->mutation_epoch;
        need(f.doc->mutation_journal.last_epoch()<input&&!f.doc->mutation_journal.monotonicity_lost(),
             "journal genuinely lags current without monotonicity poison");
        JournalSnapshot journal(f.doc->mutation_journal);auto tx=f.prepare();
        need(tx->publication_receipt&&!tx->publication_receipt->incremental()&&
             tx->publication_receipt->matches(tx->publication_receipt,tx->revision),
             "lagged observation still permits authentic exhaustive full preparation");
        need(journal.matches(f.doc->mutation_journal),"full prepare preserves lagged journal");
        auto out=f.commit(tx);
        need(out->value->input_epoch()==input,"full publishes true current input despite journal lag");
        need(journal.matches(f.doc->mutation_journal)&&!f.doc->mutation_journal.monotonicity_lost(),
             "full cannot repair lag or clear ordinary loss even without poison");
        aether_style_transaction_destroy(tx);aether_style_revision_destroy(out);
    }
}
static void stale_receipt(){
    ReceiptFixture f;f.full();auto tx=f.prepare();
    aether_element_set_state(f.node,AETHER_STATE_HOVER);f.rejects(tx);
    aether_style_transaction_destroy(tx);
}

#if defined(AETHERUI_ENABLE_STATE_INCREMENTAL)
static aether_style_transaction_t prepare_current_state(ReceiptFixture& f){
    aether_style_revision_t base=nullptr;
    need(aether_style_runtime_retain_revision(f.runtime,&base)==AETHER_OK&&base,"retain genuine current state base");
    aether_style_transaction_t tx=nullptr;
    const auto status=aether_style_runtime_prepare_state_revision(f.runtime,f.doc,base,nullptr,0,nullptr,&tx);
    aether_style_revision_destroy(base);
    need(status==AETHER_OK&&tx,"prepare state from genuine current base");
    need(tx->state_result.incremental&&tx->publication_receipt&&tx->publication_receipt->incremental()&&
         tx->publication_receipt->matches(tx->publication_receipt,tx->revision),"state producer issues exact genuine incremental receipt");
    return tx;
}
static void incremental_receipt_authority(){
    ReceiptFixture f;f.full();
    // Both empty and local agendas have authority independent of public status.
    for(bool local:{false,true}){
        if(local)aether_element_set_state(f.node,AETHER_STATE_HOVER);
        JournalSnapshot before(f.doc->mutation_journal);
        auto tx=prepare_current_state(f);auto other=prepare_current_state(f);
        need(before.matches(f.doc->mutation_journal),"state preparation does not acknowledge");
        const auto candidate=tx->revision;const auto correct=tx->publication_receipt;
        tx->state_result.incremental=false;
        tx->publication_receipt.reset();f.rejects(tx);
        tx->publication_receipt=other->publication_receipt;f.rejects(tx);
        tx->publication_receipt=correct;tx->revision=other->revision;f.rejects(tx);tx->revision=candidate;
        auto out=f.commit(tx);
        need(f.doc->mutation_journal.floor_epoch()==candidate->input_epoch()&&f.doc->mutation_journal.size()==0,
             "incremental receipt acknowledges despite false public status");
        f.rejects(other);aether_style_transaction_destroy(other);
        aether_style_transaction_destroy(tx);aether_style_revision_destroy(out);
    }
    for(bool loss:{false,true}){
        ReceiptFixture changed;changed.full();aether_element_set_state(changed.node,AETHER_STATE_HOVER);
        auto tx=prepare_current_state(changed);const auto input=changed.doc->identity->mutation_epoch;
        // Only journal evidence changes after prepare; document and captured
        // style-tree epochs still pass their ordinary publication guards.
        changed.doc->mutation_journal.append(changed.node->identity,
            loss?aether::dom::MutationKind::unknown:aether::dom::MutationKind::state,
            input+1,changed.doc->identity->structure_epoch,changed.node->state,changed.node->state);
        need(changed.doc->identity->mutation_epoch==input&&!changed.doc->mutation_journal.monotonicity_lost(),
             "post-prepare journal mismatch leaves ordinary epochs and monotonicity intact");
        if(loss)need(changed.doc->mutation_journal.loss()==aether::dom::JournalLoss::unknown_mutation,"post-prepare unknown loss recorded");
        tx->state_result.incremental=false;changed.rejects(tx);aether_style_transaction_destroy(tx);
    }
}
static void shared_document_receipts(){
    for(bool fall_behind:{false,true}){
        ReceiptFixture a;
        aether_style_runtime_t b=nullptr;
        need(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&b)==AETHER_OK,"create second runtime");
        aether_style_transaction_t btx=nullptr;aether_style_revision_t bbase=nullptr;
        need(aether_style_runtime_prepare_revision(b,a.doc,nullptr,&btx)==AETHER_OK,"prepare B initial full");
        need(aether_style_runtime_commit_revision(b,btx,&bbase)==AETHER_OK,"publish B initial full");
        aether_style_transaction_destroy(btx);a.full();
        if(!fall_behind){
            need(aether_style_runtime_prepare_revision(b,a.doc,nullptr,&btx)==AETHER_OK,"prepare B before competing publication");
            const auto captured=btx->base_style_tree_epoch;const auto input=btx->input_epoch;
            a.full();
            need(a.doc->identity->mutation_epoch==input&&a.doc->identity->style_tree_epoch!=captured,
                 "A changes shared style tree at identical input epoch");
            // Retain a record without changing input, so B rejection must preserve
            // journal data as well as its existing publication.
            a.doc->mutation_journal.append(a.node->identity,aether::dom::MutationKind::state,
                input+1,a.doc->identity->structure_epoch,a.node->state,a.node->state);
            JournalSnapshot journal(a.doc->mutation_journal);const auto prior=b->revision;
            aether_style_revision_t out=nullptr;
            need(aether_style_runtime_commit_revision(b,btx,&out)==AETHER_ERROR_INVALID_STATE&&!out,
                 "B receipt cannot publish across A shared style-tree publication");
            need(b->revision==prior&&journal.matches(a.doc->mutation_journal),"stale B preserves revision and retained journal records");
            aether_style_transaction_destroy(btx);
        }else{
            aether_element_set_state(a.node,AETHER_STATE_HOVER);
            auto atx=prepare_current_state(a);auto aout=a.commit(atx);
            need(bbase->value->input_epoch()<a.doc->mutation_journal.floor_epoch(),"B committed base falls below A local ACK floor");
            JournalSnapshot journal(a.doc->mutation_journal);
            need(aether_style_runtime_prepare_state_revision(b,a.doc,bbase,nullptr,0,nullptr,&btx)==AETHER_OK,"prepare B below journal floor");
            need(!btx->state_result.incremental&&btx->publication_receipt&&!btx->publication_receipt->incremental(),
                 "B below floor receives only genuine FULL fallback receipt");
            need(journal.matches(a.doc->mutation_journal),"B fallback preparation cannot change journal");
            aether_style_revision_t bout=nullptr;
            need(aether_style_runtime_commit_revision(b,btx,&bout)==AETHER_OK,"B authentic full fallback publishes");
            need(a.doc->mutation_journal.floor_epoch()==aout->value->input_epoch(),"B full never lowers acknowledged floor");
            need(bbase->value->input_epoch()<bout->value->input_epoch(),"retained B snapshot keeps historical input epoch");
            aether_style_transaction_destroy(atx);aether_style_revision_destroy(aout);
            aether_style_transaction_destroy(btx);aether_style_revision_destroy(bout);
        }
        aether_style_revision_destroy(bbase);aether_style_runtime_destroy(b);
    }
}
#endif
} // namespace publication_receipt_test
static void publication_receipt_tests(){
    using namespace publication_receipt_test;
    generic_has_no_authority();binding_and_status();receipt_lifetime();journal_three_way_guard();journal_lag_without_poison();stale_receipt();
#if defined(AETHERUI_ENABLE_STATE_INCREMENTAL)
    incremental_receipt_authority();shared_document_receipts();
#endif
    std::printf("publication receipt authority: %zu checks passed\n",publication_receipt_test::receipt_checks);
}
