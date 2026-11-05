#include "../stereo_batch_plan.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include "allocation_failure_fixture.hpp"
using namespace lmuqv;
size_t lcs(const std::vector<StereoBatchDraw>& a,const std::vector<StereoBatchDraw>& b){
    std::vector<std::vector<size_t>> counts(a.size()+1,std::vector<size_t>(b.size()+1));
    for(size_t i=0;i<a.size();++i)for(size_t j=0;j<b.size();++j)
        counts[i+1][j+1]=a[i].key==b[j].key&&a[i].pairClass&&a[i].pairClass==b[j].pairClass?counts[i][j]+1:(std::max)(counts[i][j+1],counts[i+1][j]);
    return counts.back().back();
}
void verify(const std::vector<StereoBatchDraw>& a,const std::vector<StereoBatchDraw>& b){
    const auto plan=plan_stereo_batch(a,b);assert(plan.complete&&plan.paired==lcs(a,b));
    std::vector<uint64_t> left,right;size_t pairs=0;
    for(const auto action:plan.actions){
        if(action.kind!=StereoBatchActionKind::Right){assert(action.left<a.size());left.push_back(a[action.left].key);}
        if(action.kind!=StereoBatchActionKind::Left){assert(action.right<b.size());right.push_back(b[action.right].key);}
        if(action.kind==StereoBatchActionKind::Pair){assert(a[action.left].key==b[action.right].key&&a[action.left].pairClass&&a[action.left].pairClass==b[action.right].pairClass);++pairs;}
    }
    assert(left.size()==a.size()&&right.size()==b.size()&&pairs==plan.paired&&plan.actions.size()==a.size()+b.size()-pairs);
    for(size_t i=0;i<a.size();++i)assert(left[i]==a[i].key);
    for(size_t i=0;i<b.size();++i)assert(right[i]==b[i].key);
}
int main(){
    verify({},{});verify({{1,1}},{});verify({},{{1,1}});
    verify({{1,1},{2,1}},{{2,1},{1,1}});verify({{1,0},{2,1}},{{1,1},{2,2}});
    std::vector<StereoBatchDraw> a;for(unsigned i=0;i<7;++i)a.push_back({i,1});auto b=a;unsigned permutations=0;
    do{verify(a,b);++permutations;}while(std::next_permutation(b.begin(),b.end(),[](auto x,auto y){return x.key<y.key;}));
    assert(permutations==5040);
    std::mt19937 random(71);
    for(unsigned attempt=0;attempt<500;++attempt){a.clear();b.clear();for(unsigned i=0;i<9;++i){if(random()%2)a.push_back({i,random()%3});if(random()%2)b.push_back({i,random()%3});}
        std::shuffle(a.begin(),a.end(),random);std::shuffle(b.begin(),b.end(),random);verify(a,b);}
    auto refused=plan_stereo_batch({{1,1},{1,1}},{});assert(!refused.complete&&refused.actions.empty()&&refused.paired==0);
    refused=plan_stereo_batch({},{{1,1},{1,1}});assert(!refused.complete&&refused.actions.empty());
    a.assign(stereoBatchDrawLimit+1,{});assert(!plan_stereo_batch(a,{}).complete);
    a.clear();for(unsigned i=0;i<stereoBatchDrawLimit;++i)a.push_back({i,1});
    const auto maximum=plan_stereo_batch(a,a);assert(maximum.complete&&maximum.paired==a.size()&&maximum.actions.size()==a.size());
    a={{1,1},{2,1},{3,0}};b={{2,1},{1,1},{4,1}};
    allocation_refusals([&]{return plan_stereo_batch(a,b);},[](const auto& p,bool complete){assert(p.complete==complete);if(!complete)assert(p.actions.empty()&&p.paired==0);});
    std::cout<<"Stereo batch order: 5040 permutations, random per-eye membership/compatibility, optimal non-crossing pairing, exact eye order and bounded limits passed\n";
}
