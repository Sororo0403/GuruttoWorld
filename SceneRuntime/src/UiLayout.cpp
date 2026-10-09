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
    if(x<clip[0] || y<clip[1] || x>clip[2] || y>clip[3]) return false;
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
void ApplyRectTrack(RectTransformComponent& rect,const AnimationTrack& track,const std::array<float,4>& value,float& opacity) {
    if(track.property=="uiPosition") rect.position={value[0],value[1]};
    else if(track.property=="uiSize") rect.size={std::max(0.0f,value[0]),std::max(0.0f,value[1])};
    else if(track.property=="uiRotation") rect.rotation=value[0];
    else if(track.property=="opacity") opacity=std::clamp(value[0],0.0f,1.0f);
}
RectTransformComponent AnimatedRect(const ScenePlacement& node,const UiState& state,float& opacity) {
    auto rect=*node.rectTransform;
    if(!node.animation || !node.animation->enabled) return rect;
    for(const auto& track:node.animation->tracks) {
        if(track.clock=="motionTime" && state.Value(track.clock,-1)<track.delay) continue;
        const auto value=Animation::Sample(track,state.values);
        if(value) ApplyRectTrack(rect,track,*value,opacity);
    }
    return rect;
}
RectTransformComponent ArrangedRect(const SceneLayout& layout,const ScenePlacement& parent,const ScenePlacement& node,const UiRect& bounds,RectTransformComponent rect) {
    if(!parent.layoutGroup || !parent.layoutGroup->enabled) return rect;
    const auto& group=*parent.layoutGroup;
    std::vector<const ScenePlacement*> children;
    for(const auto& child:layout.objects) if(child.parentId==parent.id && child.rectTransform && child.rectTransform->enabled) children.push_back(&child);
    const auto found=std::find(children.begin(),children.end(),&node);
    if(found==children.end()) return rect;
    const auto index=static_cast<size_t>(found-children.begin());
    const std::array<float,2> available{std::max(0.0f,bounds.size[0]/bounds.scale-group.padding[0]-group.padding[2]),std::max(0.0f,bounds.size[1]/bounds.scale-group.padding[1]-group.padding[3])};
    rect.anchorMin=rect.anchorMax=rect.pivot={0,0}; rect.position={group.padding[0],group.padding[1]};
    if(group.direction=="grid") {
        rect.size=group.cellSize;
        rect.position[0]+=static_cast<float>(index%group.columns)*(group.cellSize[0]+group.spacing[0]);
        rect.position[1]+=static_cast<float>(index/group.columns)*(group.cellSize[1]+group.spacing[1]);
    } else {
        const size_t axis=group.direction=="horizontal"?0:1;
        const bool expand=axis==0?group.expandWidth:group.expandHeight;
        if(expand) rect.size[axis]=std::max(0.0f,(available[axis]-group.spacing[axis]*static_cast<float>(children.size()-1))/static_cast<float>(children.size()));
        if(axis==0 && group.expandHeight) rect.size[1]=available[1];
        if(axis==1 && group.expandWidth) rect.size[0]=available[0];
        for(size_t i=0;i<index;++i) rect.position[axis]+=(expand?rect.size[axis]:children[i]->rectTransform->size[axis])+group.spacing[axis];
    }
    return rect;
}
void ClipChildren(UiRect& rect) {
    const float halfX=rect.size[0]*.5f,halfY=rect.size[1]*.5f;
    const float extentX=std::abs(std::cos(rect.rotation))*halfX+std::abs(std::sin(rect.rotation))*halfY;
    const float extentY=std::abs(std::sin(rect.rotation))*halfX+std::abs(std::cos(rect.rotation))*halfY;
    const float centerX=rect.position[0]+halfX,centerY=rect.position[1]+halfY;
    rect.clip={std::max(rect.clip[0],centerX-extentX),std::max(rect.clip[1],centerY-extentY),std::min(rect.clip[2],centerX+extentX),std::min(rect.clip[3],centerY+extentY)};
}
}
UiState SceneUi::Defaults(const SceneLayout& layout) {
    UiState state;
    for(const auto& p:layout.objects) {
        if(p.canvas && p.canvas->enabled) for(const auto& [key,value]:p.canvas->stateDefaults) state.values.try_emplace(key,value);
        if(p.slider) state.values.try_emplace(p.slider->binding.empty()?p.id:p.slider->binding,p.slider->value);
        if(p.toggle) state.values.try_emplace(p.toggle->binding.empty()?p.id:p.toggle->binding,p.toggle->value?1.0f:0.0f);
        if(p.inputField) state.strings.try_emplace(p.inputField->binding.empty()?p.id:p.inputField->binding,p.inputField->text);
        if(p.scrollView) state.scrollOffsets.try_emplace(p.id,p.scrollView->offset);
    }
    return state;
}
UiState SceneUi::SceneState(const SceneLayout& previousLayout,const SceneLayout& layout,const UiState& previous,bool reset) {
    auto next=reset?Defaults(layout):previous;
    if(!reset) {
        const auto defaults=Defaults(layout);
        next.values.insert(defaults.values.begin(),defaults.values.end()); next.strings.insert(defaults.strings.begin(),defaults.strings.end()); next.scrollOffsets.insert(defaults.scrollOffsets.begin(),defaults.scrollOffsets.end());
    } else {
        for(const auto& old:previousLayout.objects) {
            const auto* ancestor=&old; bool persistent=false; size_t remaining=previousLayout.objects.size()+1;
            while(ancestor && remaining-->0) {if(ancestor->persistent) {persistent=true; break;} ancestor=Find(previousLayout,ancestor->parentId);}
            const auto* current=Find(layout,old.id); if(!persistent || !current) continue;
            const auto number=[&](const std::string& oldBinding,const std::string& binding) {const auto found=previous.values.find(oldBinding.empty()?old.id:oldBinding); if(found!=previous.values.end()) next.values[binding.empty()?current->id:binding]=found->second;};
            if(old.slider && current->slider) number(old.slider->binding,current->slider->binding);
            if(old.toggle && current->toggle) number(old.toggle->binding,current->toggle->binding);
            if(old.inputField && current->inputField) {
                const auto found=previous.strings.find(old.inputField->binding.empty()?old.id:old.inputField->binding);
                if(found!=previous.strings.end()) next.strings[current->inputField->binding.empty()?current->id:current->inputField->binding]=found->second;
            }
            if(old.scrollView && current->scrollView) {const auto found=previous.scrollOffsets.find(old.id); if(found!=previous.scrollOffsets.end()) next.scrollOffsets[current->id]=found->second;}
        }
    }
    next.hovered.clear(); next.pressed.clear();
    const auto* focused=Find(layout,next.focused); if(!focused || !focused->inputField || !focused->inputField->enabled || !Resolve(layout,*focused,1,1,next).visible) next.focused.clear();
    return next;
}
UiRect SceneUi::Resolve(const SceneLayout& layout,const ScenePlacement& object,unsigned int width,unsigned int height,const UiState& state) {
    auto effective=Defaults(layout); for(const auto& [key,value]:state.values) effective.values[key]=value; effective.visibility=state.visibility;
    std::vector<const ScenePlacement*> chain; const auto* p=&object;
    while(p && chain.size()<=layout.objects.size()) {chain.push_back(p); p=Find(layout,p->parentId);}
    UiRect r; r.visible=false; bool hasCanvas=false; const ScenePlacement* parent=nullptr;
    for(auto i=chain.rbegin();i!=chain.rend();++i) {
        const auto& node=**i;
        if(node.canvas && hasCanvas) r.visible=r.visible && node.canvas->enabled;
        if(node.canvas && !hasCanvas) {
            hasCanvas=true;
            const auto& c=*node.canvas; r.scale=c.scaleWithScreen?std::min(width/c.referenceSize[0],height/c.referenceSize[1]):1;
            r.size=c.scaleWithScreen?std::array<float,2>{c.referenceSize[0]*r.scale,c.referenceSize[1]*r.scale}:std::array<float,2>{static_cast<float>(width),static_cast<float>(height)};
            r.position={(width-r.size[0])*0.5f,(height-r.size[1])*0.5f}; r.visible=c.enabled;
        }
        if(node.rectTransform) {
            float opacity=1;
            auto rect=AnimatedRect(node,effective,opacity);
            if(parent) rect=ArrangedRect(layout,*parent,node,r,rect);
            if(parent && parent->scrollView && parent->scrollView->enabled) {
                const auto found=state.scrollOffsets.find(parent->id);
                const auto offset=found==state.scrollOffsets.end()?parent->scrollView->offset:found->second;
                for(size_t axis=0;axis<2;++axis) rect.position[axis]-=offset[axis];
            }
            r=Child(r,rect,effective); r.opacity*=opacity;
        }
        const auto v=state.visibility.find(node.id); if(v!=state.visibility.end()) r.visible=r.visible && v->second;
        if(&node!=&object && ((node.mask && node.mask->enabled) || (node.scrollView && node.scrollView->enabled))) ClipChildren(r);
        parent=&node;
    }
    return r;
}
std::string SceneUi::Hit(const SceneLayout& layout,unsigned int width,unsigned int height,float x,float y,const UiState& state,bool buttonsOnly) {
    for(auto i=layout.objects.rbegin();i!=layout.objects.rend();++i) {
        if(!i->rectTransform || (buttonsOnly && !((i->button && i->button->enabled) || (i->inputField && i->inputField->enabled) || (i->slider && i->slider->enabled) || (i->toggle && i->toggle->enabled) || (i->scrollView && i->scrollView->enabled)))) continue;
        if(Resolve(layout,*i,width,height,state).Contains(x,y)) return i->id;
    }
    return {};
}
UiEvent SceneUi::Activate(const SceneLayout& layout,const std::string& object,UiState& state) {
    const auto* p=Find(layout,object); if(!p) return {};
    if(p->toggle && p->toggle->enabled) {
        const auto& c=*p->toggle; const auto key=c.binding.empty()?p->id:c.binding;
        const float value=state.Value(key,c.value?1.0f:0.0f)==0?1.0f:0.0f;
        state.values[key]=value; return {p->id,"valueChanged",key,{},c.changedEvent,value,{}};
    }
    if(p->inputField && p->inputField->enabled) {state.focused=p->id; return {};}
    if(!p->button || !p->button->enabled) return {};
    const auto& b=*p->button;
    if(b.action=="show") state.visibility[b.target]=true;
    if(b.action=="hide") state.visibility[b.target]=false;
    if(b.action=="toggle") {
        const auto i=state.visibility.find(b.target); state.visibility[b.target]=!(i==state.visibility.end() || i->second);
    }
    if(b.action=="setState") state.Assign(b.target);
    return {p->id,b.action,b.target,b.sound,b.event};
}
UiEvent SceneUi::Pointer(const SceneLayout& layout,unsigned int width,unsigned int height,float x,float y,bool down,bool pressed,bool released,float wheel,UiState& state) {
    state.hovered=Hit(layout,width,height,x,y,state);
    if(pressed) {
        state.pressed=state.hovered; state.dragPosition={x,y};
        const auto* p=Find(layout,state.pressed);
        if(!p || !p->inputField || !p->inputField->enabled) state.focused.clear();
        else state.focused=p->id;
        if(p && p->scrollView) state.dragScroll=state.scrollOffsets.contains(p->id)?state.scrollOffsets.at(p->id):p->scrollView->offset;
    }
    UiEvent event;
    const auto* p=Find(layout,state.pressed);
    if(p && p->slider && p->slider->enabled && down) {
        const auto& c=*p->slider; const auto rect=Resolve(layout,*p,width,height,state);
        const float localX=x-rect.position[0]-rect.size[0]*.5f,localY=y-rect.position[1]-rect.size[1]*.5f;
        const size_t axis=c.vertical?1:0;
        const float local=axis==0?localX*std::cos(rect.rotation)+localY*std::sin(rect.rotation):-localX*std::sin(rect.rotation)+localY*std::cos(rect.rotation);
        const float t=rect.size[axis]>0?std::clamp(local/rect.size[axis]+.5f,0.0f,1.0f):0;
        float value=c.minimum+(c.maximum-c.minimum)*t;
        if(c.wholeNumbers) value=std::round(value);
        value=std::clamp(value,c.minimum,c.maximum); const auto key=c.binding.empty()?p->id:c.binding;
        if(state.Value(key,c.value)!=value) {state.values[key]=value; event={p->id,"valueChanged",key,{},c.changedEvent,value,{}};}
    }
    const auto scroll=[&](const ScenePlacement& owner,bool dragging) {
        const auto& c=*owner.scrollView; const auto rect=Resolve(layout,owner,width,height,state);
        auto& offset=state.scrollOffsets.try_emplace(owner.id,c.offset).first->second;
        for(size_t axis=0;axis<2;++axis) {
            if(!(axis==0?c.horizontal:c.vertical)) {offset[axis]=0; continue;}
            const float maximum=std::max(0.0f,c.contentSize[axis]-rect.size[axis]/rect.scale);
            const float value=dragging?state.dragScroll[axis]+(state.dragPosition[axis]-(axis==0?x:y))/rect.scale:
                offset[axis]-((axis==1 || !c.vertical)?wheel*c.wheelSpeed:0);
            offset[axis]=std::clamp(value,0.0f,maximum);
        }
    };
    if(p && p->scrollView && p->scrollView->enabled && down) scroll(*p,true);
    if(wheel!=0) {
        const auto* hovered=Find(layout,Hit(layout,width,height,x,y,state,false));
        while(hovered) {
            if(hovered->scrollView && hovered->scrollView->enabled) {scroll(*hovered,false); break;}
            hovered=Find(layout,hovered->parentId);
        }
    }
    if(released) {
        if(!state.pressed.empty() && state.pressed==state.hovered) event=Activate(layout,state.pressed,state);
        state.pressed.clear();
    }
    return event;
}
std::string SceneUi::InputText(const ScenePlacement& p,const UiState& state) {
    if(!p.inputField) return p.text?p.text->text:std::string{};
    const auto& c=*p.inputField; const auto key=c.binding.empty()?p.id:c.binding;
    const auto found=state.strings.find(key); std::string text=found==state.strings.end()?c.text:found->second;
    if(text.empty()) return c.placeholder;
    if(c.password) {size_t count=0; for(const unsigned char character:text) if((character&0xc0)!=0x80) ++count; text.assign(count,'*');}
    if(state.focused==p.id && !c.readOnly) text+='|';
    return text;
}
UiEvent SceneUi::TextInput(const SceneLayout& layout,char32_t character,UiState& state) {
    const auto* p=Find(layout,state.focused); if(!p || !p->inputField || !p->inputField->enabled) return {};
    const auto& c=*p->inputField; const auto key=c.binding.empty()?p->id:c.binding;
    auto& text=state.strings.try_emplace(key,c.text).first->second;
    if(character==27) {state.focused.clear(); return {};}
    if(character=='\r' || character=='\n') {
        if(!c.multiline) return {p->id,"submit",key,{},c.submittedEvent,0,text};
        character='\n';
    }
    if(c.readOnly) return {};
    const auto before=text;
    if(character==8) {
        if(!text.empty()) {size_t index=text.size()-1; while(index>0 && (static_cast<unsigned char>(text[index])&0xc0)==0x80) --index; text.erase(index);}
    } else if((character>=32 || character=='\n') && character<=0x10ffff && !(character>=0xd800 && character<=0xdfff)) {
        size_t count=0; for(const unsigned char byte:text) if((byte&0xc0)!=0x80) ++count;
        if(count>=c.maxLength) return {};
        if(character<=0x7f) text+=static_cast<char>(character);
        else if(character<=0x7ff) {text+=static_cast<char>(0xc0|(character>>6)); text+=static_cast<char>(0x80|(character&0x3f));}
        else if(character<=0xffff) {text+=static_cast<char>(0xe0|(character>>12)); text+=static_cast<char>(0x80|((character>>6)&0x3f)); text+=static_cast<char>(0x80|(character&0x3f));}
        else {text+=static_cast<char>(0xf0|(character>>18)); text+=static_cast<char>(0x80|((character>>12)&0x3f)); text+=static_cast<char>(0x80|((character>>6)&0x3f)); text+=static_cast<char>(0x80|(character&0x3f));}
    }
    return text==before?UiEvent{}:UiEvent{p->id,"valueChanged",key,{},c.changedEvent,0,text};
}
}
