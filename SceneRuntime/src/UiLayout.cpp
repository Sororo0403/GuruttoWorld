#include <SceneRuntime/SceneUi.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace SceneRuntime {
float UiState::Value(const std::string& key,float fallback) const {
    const auto i=values.find(key); return i==values.end()?fallback:i->second;
}
namespace {
std::map<std::string,float> Assignments(const std::string& expression) {
    std::map<std::string,float> result;
    if(expression.empty()) return result;
    if(expression.back()=='&') throw std::runtime_error("Empty state clause");
    std::istringstream input(expression); std::string clause;
    while(std::getline(input,clause,'&')) {
        const auto equal=clause.find('=');
        if(equal==std::string::npos || equal==0 || clause.find('=',equal+1)!=std::string::npos) throw std::runtime_error("Invalid state assignment");
        size_t used=0; const auto value=std::stof(clause.substr(equal+1),&used);
        if(used!=clause.size()-equal-1 || !std::isfinite(value) || std::abs(value)>100000) throw std::runtime_error("Invalid state value");
        result[clause.substr(0,equal)]=value;
    }
    return result;
}
}
bool UiState::Matches(const std::string& expression) const {
    try {for(const auto& [key,value]:Assignments(expression)) if(Value(key)!=value) return false; return true;}
    catch(...) {return false;}
}
bool UiState::Assign(const std::string& expression) {
    try {const auto assignments=Assignments(expression); for(const auto& [key,value]:assignments) values[key]=value; return true;}
    catch(...) {return false;}
}
bool UiRect::Contains(float x,float y) const {
    x-=position[0]+size[0]*0.5f; y-=position[1]+size[1]*0.5f;
    const float a=x*std::cos(rotation)+y*std::sin(rotation),b=-x*std::sin(rotation)+y*std::cos(rotation);
    return visible && opacity>0.001f && size[0]>0 && size[1]>0 && std::abs(a)<=size[0]*0.5f && std::abs(b)<=size[1]*0.5f;
}
namespace {
const ScenePlacement* Find(const SceneLayout& layout,const std::string& id) {
    const auto i=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& p){return p.id==id;});
    return i==layout.objects.end()?nullptr:&*i;
}
UiRect Child(const UiRect& parent,const RectTransformComponent& c,const UiState& state) {
    UiRect r=parent; r.rotation+=c.rotation;
    r.visible=r.visible && c.enabled && state.Matches(c.visibleWhen);
    const float progress=std::clamp((state.Value("intro",1)-c.introDelay)/(1-c.introDelay),0.0f,1.0f);
    const float ease=1-std::pow(1-progress,3.0f);
    const float offset=c.introOffset*(1-ease)+state.Value(c.offsetBinding);
    std::array<float,2> center{};
    for(size_t i=0;i<2;++i) {
        r.size[i]=parent.size[i]*(c.anchorMax[i]-c.anchorMin[i])+c.size[i]*r.scale;
        if(i==0 && !c.widthBinding.empty()) r.size[i]*=std::clamp(state.Value(c.widthBinding),0.0f,1.0f);
        center[i]=parent.size[i]*c.anchorMin[i]+(c.position[i]+(i==0?offset:0))*r.scale+
            r.size[i]*(0.5f-c.pivot[i])-parent.size[i]*0.5f;
    }
    r.position={parent.position[0]+parent.size[0]*0.5f+center[0]*std::cos(parent.rotation)-center[1]*std::sin(parent.rotation)-r.size[0]*0.5f,
        parent.position[1]+parent.size[1]*0.5f+center[0]*std::sin(parent.rotation)+center[1]*std::cos(parent.rotation)-r.size[1]*0.5f};
    r.opacity*=ease*state.Value(c.opacityBinding,1); return r;
}
}
UiState SceneUi::Defaults(const SceneLayout& layout) {
    UiState state; for(const auto& p:layout.objects) if(p.canvas && p.canvas->enabled) for(const auto& [key,value]:p.canvas->stateDefaults) state.values.try_emplace(key,value); return state;
}
UiRect SceneUi::Resolve(const SceneLayout& layout,const ScenePlacement& object,unsigned int width,unsigned int height,const UiState& state) {
    auto effective=Defaults(layout); for(const auto& [key,value]:state.values) effective.values[key]=value; effective.visibility=state.visibility;
    std::vector<const ScenePlacement*> chain; const auto* p=&object;
    while(p && chain.size()<=layout.objects.size()) {chain.push_back(p); p=Find(layout,p->parentId);}
    UiRect r; r.visible=false; bool hasCanvas=false;
    for(auto i=chain.rbegin();i!=chain.rend();++i) {
        const auto& node=**i;
        if(node.canvas && hasCanvas) r.visible=r.visible && node.canvas->enabled;
        if(node.canvas && !hasCanvas) {
            hasCanvas=true;
            const auto& c=*node.canvas; r.scale=std::min(width/c.referenceSize[0],height/c.referenceSize[1]);
            r.size={c.referenceSize[0]*r.scale,c.referenceSize[1]*r.scale};
            r.position={(width-r.size[0])*0.5f,(height-r.size[1])*0.5f}; r.visible=c.enabled;
        }
        if(node.rectTransform) r=Child(r,*node.rectTransform,effective);
        const auto v=state.visibility.find(node.id); if(v!=state.visibility.end()) r.visible=r.visible && v->second;
    }
    return r;
}
std::string SceneUi::Hit(const SceneLayout& layout,unsigned int width,unsigned int height,float x,float y,const UiState& state,bool buttonsOnly) {
    for(auto i=layout.objects.rbegin();i!=layout.objects.rend();++i) {
        if(!i->rectTransform || (buttonsOnly && (!i->button || !i->button->enabled))) continue;
        if(Resolve(layout,*i,width,height,state).Contains(x,y)) return i->id;
    }
    return {};
}
UiEvent SceneUi::Activate(const SceneLayout& layout,const std::string& object,UiState& state) {
    const auto* p=Find(layout,object); if(!p || !p->button || !p->button->enabled) return {};
    const auto& b=*p->button;
    if(b.action=="show") state.visibility[b.target]=true;
    if(b.action=="hide") state.visibility[b.target]=false;
    if(b.action=="toggle") {
        const auto i=state.visibility.find(b.target); state.visibility[b.target]=!(i==state.visibility.end() || i->second);
    }
    if(b.action=="setState") state.Assign(b.target);
    return {p->id,b.action,b.target,b.sound,b.event};
}
}
